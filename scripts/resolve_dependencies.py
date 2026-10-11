#!/usr/bin/env python3
"""Resolve current stable upstream sources, or restore/verify one build's snapshot.

The parent repository's gitlinks record previous resolutions, not version policy.
This script never resets a dirty checkout and never silently falls back to them.
Only Python's standard library and Git are required.
"""
from __future__ import annotations

import argparse
import configparser
import datetime as dt
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request


CORE_DEPENDENCIES = ("libarchive", "zlib", "bzip2", "xz", "zstd", "lz4", "expat", "mbedtls")
QT_DEPENDENCIES = ("qt", "vulkan-headers")
QT_MODULES = ("qtbase", "qtshadertools", "qtdeclarative", "qtsvg")
TAG_PATTERNS = {
    "bzip2": r"(?:bzip2-|v)?(\d+\.\d+\.\d+)",
    "expat": r"(?:R_|v)?(\d+[._]\d+[._]\d+)",
    "mbedtls": r"(?:mbedtls-|v)?(\d+\.\d+\.\d+)",
    "qt": r"v(6\.\d+\.\d+)",
    "vulkan-headers": r"v(\d+\.\d+\.\d+)",
}
REVISION = re.compile(r"[0-9a-f]{40}")
SELECTIONS = {"release", "stable-tag", "default-branch", "parent-gitlink"}


class ResolutionError(RuntimeError):
    pass


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def git(*arguments: str, cwd: Path | None = None, check: bool = True) -> str:
    result = subprocess.run(["git", *map(str, arguments)], cwd=cwd, text=True,
                            encoding="utf-8", errors="replace", capture_output=True,
                            env={**os.environ, "GIT_TERMINAL_PROMPT": "0"})
    if check and result.returncode:
        raise ResolutionError(f"Git failed ({' '.join(arguments)}): {result.stderr.strip()}")
    return result.stdout.strip() if result.returncode == 0 else ""


def modules(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.read(path, encoding="utf-8")
    result = {}
    for section in parser.sections():
        if section.startswith('submodule "'):
            module_path, source = parser.get(section, "path"), parser.get(section, "url")
            validate_path(module_path)
            result[module_path] = source
    return result


def validate_path(value: str) -> None:
    path = PurePosixPath(value)
    if not value or "\\" in value or ":" in value or path.is_absolute() or ".." in path.parts:
        raise ResolutionError(f"Unsafe dependency path: {value!r}")


def version_key(name: str, tag: str) -> tuple[int, ...] | None:
    pattern = TAG_PATTERNS.get(name, r"v?(\d+\.\d+\.\d+)")
    match = re.fullmatch(pattern, tag)
    return tuple(map(int, re.split(r"[._]", match[1]))) if match else None


def github_releases(source: str) -> list[dict]:
    """Read published releases; absence is different from a failed API request."""
    match = re.fullmatch(r"https://github\.com/([^/]+)/([^/]+?)(?:\.git)?/?", source)
    if not match:
        return []
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "AnAnArchiver-dependency-resolver"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    releases = []
    url = f"https://api.github.com/repos/{match[1]}/{match[2]}/releases?per_page=100"
    while url:
        request = urllib.request.Request(url, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                data = json.load(response)
                links = response.headers.get("Link", "")
        except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
            detail = f"HTTP {error.code}" if isinstance(error, urllib.error.HTTPError) else type(error).__name__
            if isinstance(error, urllib.error.HTTPError):
                error.close()
            raise ResolutionError(f"Cannot query releases for {source}: {detail}; no older-version fallback. "
                                  "For GitHub rate limits set GITHUB_TOKEN or GH_TOKEN.") from error
        if not isinstance(data, list) or any(
                not isinstance(release, dict) or not isinstance(release.get("tag_name"), str)
                or not isinstance(release.get("draft", False), bool)
                or not isinstance(release.get("prerelease", False), bool)
                or release.get("published_at") is not None and not isinstance(release["published_at"], str)
                for release in data):
            raise ResolutionError(f"Invalid release response for {source}")
        releases.extend(data)
        following = re.search(r'<([^>]+)>;\s*rel="next"', links)
        url = following[1] if following else ""
        if url and not url.startswith("https://api.github.com/"):
            raise ResolutionError("Unexpected GitHub pagination host")
    return releases


def remote_refs(source: str) -> tuple[dict[str, str], str, str]:
    output = git("ls-remote", "--symref", source, "HEAD", "refs/tags/*")
    tags, branch, head = {}, "", ""
    for line in output.splitlines():
        value, reference = line.split("\t", 1)
        if value.startswith("ref: ") and reference == "HEAD":
            branch = value[5:]
        elif reference == "HEAD":
            head = value
        elif reference.startswith("refs/tags/"):
            tag = reference[len("refs/tags/"):]
            if tag.endswith("^{}"):
                tags[tag[:-3]] = value
            else:
                tags.setdefault(tag, value)
    return tags, branch, head


def select_version(name: str, source: str, tags: dict[str, str], branch: str,
                   head: str, releases: list[dict], resolved_at: str) -> dict:
    stable = []
    for release in releases:
        tag = release.get("tag_name", "")
        version = version_key(name, tag)
        if version and release.get("published_at") and not release.get("draft") and not release.get("prerelease"):
            if tag not in tags:
                raise ResolutionError(f"Published release {tag} is missing from {source}'s Git refs")
            stable.append((version, tag))
    selection = "release"
    if stable:
        version, tag = max(stable)
        revision = tags[tag]
    elif name == "libarchive":
        # Fork tags can be inherited and are not independent releases.
        if not REVISION.fullmatch(head) or not branch:
            raise ResolutionError(f"Cannot identify {source}'s default branch")
        version, tag, revision, selection = None, None, head, "default-branch"
    else:
        if releases:
            raise ResolutionError(f"No recognized stable release for {name} at {source}")
        candidates = [(key, tag) for tag in tags if (key := version_key(name, tag))]
        if not candidates:
            raise ResolutionError(f"No stable release tag for {name} at {source}")
        version, tag = max(candidates)
        revision, selection = tags[tag], "stable-tag"
    if not REVISION.fullmatch(revision):
        raise ResolutionError(f"Invalid upstream revision for {name}")
    result = {"name": name, "path": f"third_party/{name}", "source": source,
              "version": ".".join(map(str, version)) if version else "default-branch",
              "tag": tag, "revision": revision, "selection": selection, "resolved_at": resolved_at}
    if selection == "default-branch":
        result["branch"] = branch.removeprefix("refs/heads/")
    return result


def required_names(with_qt: bool) -> tuple[str, ...]:
    return CORE_DEPENDENCIES + (QT_DEPENDENCIES if with_qt else ())


def load_manifest(path: Path) -> dict:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ResolutionError(f"Cannot read dependency manifest {path}: {error}") from error
    if (not isinstance(manifest, dict) or type(manifest.get("schema_version")) is not int
            or manifest["schema_version"] != 1 or manifest.get("strategy") != "latest-stable"):
        raise ResolutionError("Unsupported dependency manifest schema or strategy")
    entries = manifest.get("dependencies")
    if not isinstance(entries, list) or not entries or not manifest.get("resolved_at"):
        raise ResolutionError("Dependency manifest is incomplete")
    resolved_at = manifest["resolved_at"]
    try:
        if not isinstance(resolved_at, str):
            raise ValueError("a timestamp string is required")
        normalized_time = resolved_at[:-1] + "+00:00" if resolved_at.endswith("Z") else resolved_at
        parsed_time = dt.datetime.fromisoformat(normalized_time)
        if parsed_time.utcoffset() != dt.timedelta(0):
            raise ValueError("a UTC timestamp is required")
    except ValueError as error:
        raise ResolutionError("Dependency manifest resolved_at must be a UTC ISO timestamp") from error
    paths = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise ResolutionError("Invalid dependency manifest entry")
        for field in ("name", "path", "source", "version", "revision", "selection", "resolved_at"):
            if not isinstance(entry.get(field), str) or not entry[field]:
                raise ResolutionError(f"Dependency manifest entry lacks {field}")
        validate_path(entry["path"])
        if entry["path"] in paths or not REVISION.fullmatch(entry["revision"]):
            raise ResolutionError("Duplicate dependency path or invalid revision in manifest")
        if entry["selection"] not in SELECTIONS or entry.get("tag") is not None and not isinstance(entry["tag"], str):
            raise ResolutionError("Invalid dependency selection or tag in manifest")
        if entry["resolved_at"] != resolved_at:
            raise ResolutionError("Dependency manifest contains mixed resolution timestamps")
        paths.add(entry["path"])
    return manifest


def validate_top_sources(root: Path, manifest: dict, with_qt: bool) -> list[dict]:
    declared = modules(root / ".gitmodules")
    entries = {entry["path"]: entry for entry in manifest["dependencies"]}
    selected = []
    required = required_names(with_qt)
    for name in CORE_DEPENDENCIES + QT_DEPENDENCIES:
        path = f"third_party/{name}"
        entry = entries.get(path)
        if name not in required and not entry:
            continue
        if not entry or entry["name"] != name or entry.get("parent") or entry["source"] != declared.get(path):
            raise ResolutionError(f"Missing or mismatched upstream manifest entry: {name}")
        if name in required:
            selected.append(entry)
    return selected


def is_repository(path: Path) -> bool:
    return (path / ".git").exists()


def check_clean(path: Path) -> None:
    # Check each child recursively instead of treating a parent's changed gitlink as
    # a modified file. A previously interrupted update may have paired children pending.
    if git("status", "--porcelain", "--ignore-submodules=all", "--untracked-files=all", cwd=path):
        raise ResolutionError(f"Dependency checkout contains local changes: {path}. Commit or move them before updating.")


def gitlink(parent: Path, relative: str) -> str:
    line = git("ls-tree", "HEAD", "--", relative, cwd=parent)
    return line.split()[2] if line.startswith("160000 ") else ""


def check_published(path: Path, recorded: str, previous: str = "", resolved: str = "") -> None:
    current = git("rev-parse", "--verify", "HEAD", cwd=path, check=False)
    if not current:  # A failed initial fetch can leave an empty initialized repository.
        return
    if current in (recorded, previous, resolved):
        return
    # A locally created tag does not prove that a commit has been published.
    refs = git("for-each-ref", "--contains", current, "--format=%(refname)", "refs/remotes", cwd=path)
    if not refs:
        raise ResolutionError(f"Dependency has an unrecorded local commit: {path} ({current}). Publish or preserve it before updating.")


def preflight(root: Path, top: list[dict], previous: dict | None) -> None:
    previous_entries = {entry["path"]: entry for entry in (previous or {}).get("dependencies", [])}

    def visit(path: Path, parent: Path, relative: str, global_path: str, resolved: str = "") -> None:
        if not is_repository(path):
            if path.exists() and any(path.iterdir()):
                raise ResolutionError(f"Dependency directory is not an initialized checkout and is not empty: {path}")
            return
        check_clean(path)
        prior = previous_entries.get(global_path, {})
        check_published(path, gitlink(parent, relative), prior.get("revision", ""), resolved)
        for child in modules(path / ".gitmodules"):
            visit(path / child, path, child, f"{global_path}/{child}")

    for entry in top:
        visit(root / entry["path"], root, entry["path"], entry["path"], entry["revision"])


def checkout(root: Path, entry: dict, parent: Path, relative: str) -> None:
    path = root / entry["path"]
    if not is_repository(path):
        path.mkdir(parents=True, exist_ok=True)
        git("submodule", "init", "--", relative, cwd=parent)
        module_dir = Path(git("rev-parse", "--git-path", f"modules/{relative}", cwd=parent))
        if not module_dir.is_absolute():
            module_dir = parent / module_dir
        module_dir = module_dir.resolve()
        module_dir.parent.mkdir(parents=True, exist_ok=True)
        git("init", "--quiet", "--separate-git-dir", str(module_dir), str(path))
        git("remote", "add", "origin", entry["source"], cwd=path)
    source = git("remote", "get-url", "origin", cwd=path)
    if source.rstrip("/") != entry["source"].rstrip("/"):
        raise ResolutionError(f"Unexpected origin URL at {path}: expected {entry['source']}")
    if git("rev-parse", "HEAD", cwd=path, check=False) != entry["revision"]:
        print(f"Fetching {entry['name']}: {entry['revision']}", flush=True)
        git("fetch", "--depth", "1", "--no-tags", "origin", entry["revision"], cwd=path)
        fetched = git("rev-parse", "FETCH_HEAD^{commit}", cwd=path)
        if fetched != entry["revision"]:
            raise ResolutionError(f"Fetched revision differs from the resolved snapshot: {entry['name']}")
        git("-c", "core.autocrlf=false", "checkout", "--detach", entry["revision"], cwd=path)
    # Preserve proof of the fetched upstream revision even if a later dependency fails.
    # The next run can safely resume without treating this snapshot as an unpublished commit.
    git("update-ref", "refs/remotes/origin/ananarchiver-resolved", entry["revision"], cwd=path)


def relative_source(parent_source: str, child: str) -> str:
    if not child.startswith("."):
        return child
    if "://" in parent_source:
        return urllib.parse.urljoin(parent_source.rstrip("/") + "/", child)
    return str((Path(parent_source) / child).resolve())


def nested_entries(root: Path, entry: dict, resolved_at: str) -> list[dict]:
    path = root / entry["path"]
    declared = modules(path / ".gitmodules")
    selected = QT_MODULES if entry["name"] == "qt" else declared.keys()
    result = []
    for relative in selected:
        if relative not in declared:
            raise ResolutionError(f"Required child {relative} is absent from {entry['name']}'s .gitmodules")
        revision = gitlink(path, relative)
        if not REVISION.fullmatch(revision):
            raise ResolutionError(f"Missing parent gitlink: {entry['path']}/{relative}")
        result.append({"name": f"{entry['name']}/{relative}", "path": f"{entry['path']}/{relative}",
                       "source": relative_source(entry["source"], declared[relative]),
                       "version": entry["version"] if entry["name"] == "qt" else f"git-{revision[:12]}",
                       "parent_version": entry["version"], "tag": None, "revision": revision,
                       "selection": "parent-gitlink", "parent": entry["name"], "resolved_at": resolved_at})
    return result


def actual_default_version(root: Path, entry: dict) -> None:
    if entry["name"] == "libarchive" and entry["selection"] == "default-branch":
        text = (root / entry["path"] / "build/version").read_text(encoding="utf-8").strip()
        match = re.fullmatch(r"(\d)(\d{3})(\d{3})([a-z]*)", text)
        if not match:
            raise ResolutionError("Cannot read the selected libarchive source version")
        entry["version"] = f"{int(match[1])}.{int(match[2])}.{int(match[3])}{match[4]}"
    elif entry["name"].endswith("/tf-psa-crypto"):
        text = (root / entry["path"] / "include/tf-psa-crypto/build_info.h").read_text(encoding="utf-8")
        match = re.search(r'#define\s+TF_PSA_CRYPTO_VERSION_STRING\s+"([^"]+)"', text)
        if not match:
            raise ResolutionError("Cannot read the selected TF-PSA-Crypto source version")
        entry["version"] = match[1]


def verify_version(root: Path, entry: dict) -> None:
    expected = entry.copy()
    if entry["selection"] in {"release", "stable-tag"}:
        version = version_key(entry["name"], entry.get("tag") or "")
        if not version:
            raise ResolutionError(f"Invalid stable tag in manifest: {entry['name']}")
        expected["version"] = ".".join(map(str, version))
    elif entry["selection"] == "parent-gitlink":
        expected["version"] = entry.get("parent_version") if entry.get("parent") == "qt" else f"git-{entry['revision'][:12]}"
    actual_default_version(root, expected)
    if entry["version"] != expected["version"]:
        raise ResolutionError(f"Dependency version does not match the selected source: {entry['name']}")


def synchronize(root: Path, top: list[dict], resolved_at: str,
                restored: dict | None = None) -> list[dict]:
    expected = {entry["path"]: entry for entry in (restored or {}).get("dependencies", [])}
    result = []

    def visit(entry: dict, parent: Path, relative: str) -> None:
        checkout(root, entry, parent, relative)
        actual_default_version(root, entry)
        result.append(entry)
        for child in nested_entries(root, entry, resolved_at):
            if restored:
                recorded = expected.get(child["path"])
                if not recorded or any(recorded.get(key) != child[key] for key in ("name", "source", "revision", "parent", "parent_version", "selection", "tag")):
                    raise ResolutionError(f"Manifest does not match the parent release's child: {child['path']}")
                child = recorded.copy()
            visit(child, root / entry["path"], child["path"][len(entry["path"]) + 1:])

    for entry in top:
        visit(entry.copy(), root, entry["path"])
    return result


def verify(root: Path, manifest: dict, with_qt: bool) -> None:
    top = validate_top_sources(root, manifest, with_qt)
    expected = {entry["path"]: entry for entry in manifest["dependencies"]}
    visited = set()

    def visit(entry: dict) -> None:
        visited.add(entry["path"])
        path = root / entry["path"]
        if not is_repository(path) or git("rev-parse", "HEAD", cwd=path) != entry["revision"]:
            raise ResolutionError(f"Dependency revision does not match manifest: {entry['path']}")
        if git("remote", "get-url", "origin", cwd=path).rstrip("/") != entry["source"].rstrip("/"):
            raise ResolutionError(f"Dependency origin does not match manifest: {entry['path']}")
        check_clean(path)
        verify_version(root, entry)
        for child in nested_entries(root, entry, manifest["resolved_at"]):
            recorded = expected.get(child["path"])
            if not recorded or any(recorded.get(key) != child[key] for key in ("name", "source", "revision", "parent", "parent_version", "selection", "tag")):
                raise ResolutionError(f"Manifest omits or mismatches a release child: {child['path']}")
            visit(recorded)

    for entry in top:
        visit(entry)
    for path in expected.keys() - visited:
        if not with_qt and (path == "third_party/vulkan-headers" or path == "third_party/qt" or path.startswith("third_party/qt/")):
            continue  # A source-Qt snapshot can also be consumed by a Core/system-Qt build.
        raise ResolutionError(f"Manifest includes an unverified dependency outside the selected source graph: {path}")


def write_manifest(path: Path, manifest: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--with-qt", action="store_true")
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--restore", action="store_true", help="restore a previously resolved complete source snapshot")
    actions.add_argument("--verify", action="store_true", help="verify selected sources without network or mutations")
    arguments = parser.parse_args(argv)
    root = arguments.root.resolve()
    manifest_path = (arguments.manifest or root / "out/dependency-resolution.json").resolve()
    try:
        if arguments.restore or arguments.verify:
            manifest = load_manifest(manifest_path)
            top = validate_top_sources(root, manifest, arguments.with_qt)
            if arguments.restore:
                previous_path = root / "out/dependency-resolution.json"
                previous = load_manifest(previous_path) if previous_path.exists() else None
                preflight(root, top, previous)
                synchronize(root, top, manifest["resolved_at"], manifest)
            verify(root, manifest, arguments.with_qt)
            print(f"{'Restored and verified' if arguments.restore else 'Verified'} dependency snapshot: {manifest_path}")
            return 0
        declared = modules(root / ".gitmodules")
        resolved_at = timestamp()
        top = []
        previous = load_manifest(manifest_path) if manifest_path.exists() else None
        for name in required_names(arguments.with_qt):
            source = declared.get(f"third_party/{name}")
            if not source:
                raise ResolutionError(f"Missing declared upstream for {name}")
            releases = github_releases(source)
            tags, branch, head = remote_refs(source)
            entry = select_version(name, source, tags, branch, head, releases, resolved_at)
            top.append(entry)
            print(f"Resolved {name}: {entry['tag'] or entry.get('branch')} ({entry['revision']})", flush=True)
        preflight(root, top, previous)
        entries = synchronize(root, top, resolved_at)
        manifest = {"schema_version": 1, "strategy": "latest-stable", "resolved_at": resolved_at,
                    "dependencies": entries}
        verify(root, manifest, arguments.with_qt)
        write_manifest(manifest_path, manifest)
        print(f"Saved verified dependency snapshot: {manifest_path}")
        return 0
    except (ResolutionError, OSError, configparser.Error) as error:
        print(f"Dependency resolution failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
