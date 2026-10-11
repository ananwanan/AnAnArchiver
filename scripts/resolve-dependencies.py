#!/usr/bin/env python3
"""Resolve stable upstream sources once, then initialize or replay that snapshot.

Uses Python's standard library and Git. GITHUB_TOKEN or GH_TOKEN authenticates
public GitHub metadata requests; credentials never enter the manifest.
"""
from __future__ import annotations

import argparse
import base64
import configparser
import datetime as dt
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

CORE = ("libarchive", "zlib", "bzip2", "xz", "zstd", "lz4", "expat", "mbedtls")
QT_MODULES = ("qtbase", "qtshadertools", "qtdeclarative", "qtsvg")
UPSTREAMS = {
    "libarchive": "ananwanan/libarchive", "zlib": "madler/zlib",
    "bzip2": "libarchive/bzip2", "xz": "tukaani-project/xz",
    "zstd": "facebook/zstd", "lz4": "lz4/lz4", "expat": "libexpat/libexpat",
    "mbedtls": "Mbed-TLS/mbedtls", "qt": "qt/qt5",
    "vulkan-headers": "KhronosGroup/Vulkan-Headers",
}
TAG_PATTERNS = {
    "bzip2": r"(?:bzip2-|v)?(\d+)\.(\d+)\.(\d+)",
    "expat": r"(?:R_|v?)(\d+)[_.](\d+)[_.](\d+)",
    "mbedtls": r"(?:mbedtls-|v)?(\d+)\.(\d+)\.(\d+)",
    "vulkan-headers": r"(?:v|vulkan-sdk-)?(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?",
}
SHA = re.compile(r"[0-9a-f]{40}\Z")


class ResolutionError(RuntimeError):
    pass


def git(*args: str, cwd: Path | None = None, check: bool = True) -> str:
    result = subprocess.run(["git", *args], cwd=cwd, text=True, encoding="utf-8",
                            errors="replace", capture_output=True,
                            env={**os.environ, "GIT_TERMINAL_PROMPT": "0", "GIT_OPTIONAL_LOCKS": "0"})
    if check and result.returncode:
        raise ResolutionError(f"Git failed: {' '.join(args)}\n{result.stderr.strip()}")
    return result.stdout.strip() if result.returncode == 0 else ""


def stable_version(name: str, tag: str) -> tuple[int, ...] | None:
    pattern = TAG_PATTERNS.get(name, r"v?(\d+)\.(\d+)\.(\d+)")
    match = re.fullmatch(pattern, tag)
    if not match:
        return None
    version = tuple(int(value) for value in match.groups() if value is not None)
    return None if name == "qt" and version[0] != 6 else version


def choose_release(name: str, releases: list[dict]) -> dict | None:
    validate_release_rows(releases)
    candidates = [(stable_version(name, row.get("tag_name", "")), row)
                  for row in releases if row.get("published_at") and not row.get("draft") and not row.get("prerelease")]
    candidates = [(version, row) for version, row in candidates if version is not None]
    return max(candidates, key=lambda item: item[0])[1] if candidates else None


def validate_release_rows(rows) -> None:
    if not isinstance(rows, list) or any(
            not isinstance(row, dict) or not isinstance(row.get("tag_name"), str)
            or not isinstance(row.get("draft", False), bool)
            or not isinstance(row.get("prerelease", False), bool)
            or row.get("published_at") is not None and not isinstance(row["published_at"], str)
            for row in rows):
        raise ResolutionError("Invalid GitHub release response")


def safe_relative_path(path: str) -> bool:
    value = PurePosixPath(path)
    return bool(path) and not value.is_absolute() and all(part not in ("..", ".") for part in value.parts) \
        and "\\" not in path and ":" not in path and path == value.as_posix()


def parse_gitmodules(content: str) -> list[dict[str, str]]:
    config = configparser.RawConfigParser(interpolation=None, strict=True)
    config.read_string(content)
    modules = []
    for section in config.sections():
        match = re.fullmatch(r'submodule "(.+)"', section)
        if not match:
            raise ResolutionError(f"Invalid .gitmodules section: {section}")
        path = config.get(section, "path")
        if not safe_relative_path(path):
            raise ResolutionError(f"Unsafe submodule path: {path}")
        modules.append({"name": match[1], "path": path, "url": config.get(section, "url")})
    if len({row["path"] for row in modules}) != len(modules):
        raise ResolutionError("Duplicate submodule paths")
    return modules


def canonical_url(url: str) -> str:
    return url.rstrip("/").removesuffix(".git")


def nested_url(parent_url: str, url: str) -> str:
    return urllib.parse.urljoin(parent_url.rstrip("/") + "/", url) if url.startswith(".") else url


class GitHub:
    def __init__(self) -> None:
        self.token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")

    def request(self, url: str, missing_ok: bool = False):
        if not url.startswith("https://api.github.com/"):
            raise ResolutionError("Unexpected GitHub metadata or pagination host")
        headers = {"User-Agent": "AnAnArchiver-dependency-resolver", "Accept": "application/vnd.github+json"}
        if self.token:
            headers["Authorization"] = "Bearer " + self.token
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60) as response:
                return json.load(response), response.headers.get("Link", "")
        except urllib.error.HTTPError as error:
            error.close()
            if missing_ok and error.code == 404:
                return None, ""
            hint = " Set GITHUB_TOKEN or GH_TOKEN for authenticated metadata access." if error.code in (403, 429) else ""
            raise ResolutionError(f"GitHub metadata request failed ({error.code}): {url}.{hint}") from error
        except (OSError, ValueError) as error:
            raise ResolutionError(f"GitHub metadata request failed: {url}: {error}") from error

    def repo(self, url: str) -> str:
        parsed = urllib.parse.urlparse(canonical_url(url))
        if parsed.scheme != "https" or parsed.netloc != "github.com":
            raise ResolutionError(f"Expected designated HTTPS GitHub upstream: {url}")
        return parsed.path.strip("/")

    def metadata(self, repo: str) -> dict:
        row = self.request(f"https://api.github.com/repos/{repo}")[0]
        if not isinstance(row, dict) or not isinstance(row.get("fork"), bool) or not isinstance(row.get("default_branch"), str):
            raise ResolutionError(f"Invalid GitHub repository metadata: {repo}")
        return row

    def releases(self, repo: str) -> list[dict]:
        result = []
        url = f"https://api.github.com/repos/{repo}/releases?per_page=100"
        while url:
            rows, links = self.request(url)
            validate_release_rows(rows)
            result.extend(rows)
            match = re.search(r'<([^>]+)>; rel="next"', links)
            url = match[1] if match else ""
        return result

    def content(self, repo: str, revision: str, path: str, missing_ok: bool = False) -> str | None:
        row = self.request(f"https://api.github.com/repos/{repo}/contents/{path}?ref={revision}", missing_ok)[0]
        if row is None:
            return None
        if not isinstance(row, dict) or row.get("encoding") != "base64" or not isinstance(row.get("content"), str):
            raise ResolutionError(f"Expected textual source metadata: {repo}/{path}@{revision}")
        try:
            return base64.b64decode("".join(row["content"].split()), validate=True).decode("utf-8")
        except ValueError as error:
            raise ResolutionError(f"Invalid encoded source metadata: {repo}/{path}@{revision}") from error

    def gitlinks(self, repo: str, revision: str, modules: list[dict]) -> dict[str, str]:
        # Avoid downloading a recursive Qt source tree merely to locate four gitlinks.
        tree = self.request(f"https://api.github.com/repos/{repo}/git/trees/{revision}")[0]
        if not isinstance(tree, dict) or not isinstance(tree.get("tree"), list) or tree.get("truncated"):
            raise ResolutionError(f"Invalid or incomplete GitHub source tree: {repo}@{revision}")
        if any(not isinstance(row, dict) or not isinstance(row.get("path"), str)
               or not isinstance(row.get("mode"), str) or not isinstance(row.get("sha"), str)
               or not SHA.fullmatch(row["sha"]) for row in tree["tree"]):
            raise ResolutionError(f"Invalid GitHub source tree entries: {repo}@{revision}")
        links = {row["path"]: row["sha"] for row in tree["tree"] if row["mode"] == "160000"}
        for module in modules:
            path = module["path"]
            if path not in links and "/" in path:
                row = self.request(f"https://api.github.com/repos/{repo}/contents/{path}?ref={revision}")[0]
                if not isinstance(row, dict) or not isinstance(row.get("sha"), str) or not SHA.fullmatch(row["sha"]):
                    raise ResolutionError(f"Invalid GitHub submodule metadata: {repo}/{path}@{revision}")
                if row.get("submodule_git_url"):
                    links[path] = row["sha"]
            if path not in links:
                raise ResolutionError(f"No gitlink for declared submodule {repo}/{path}@{revision}")
        return links


def remote_refs(url: str) -> dict[str, str]:
    rows = git("ls-remote", url)
    return dict((ref, revision) for revision, ref in (line.split("\t", 1) for line in rows.splitlines()))


def select_source(name: str, url: str, api: GitHub) -> dict:
    repo = api.repo(url)
    metadata = api.metadata(repo)
    releases = api.releases(repo)
    release = choose_release(name, releases)
    refs = remote_refs(url)
    tag = None
    if release:
        if not isinstance(release.get("html_url"), str) or not release["html_url"]:
            raise ResolutionError(f"Invalid published release metadata: {repo}")
        tag, selection, metadata_source = release["tag_name"], "release", release["html_url"]
    elif name != "libarchive" and not metadata.get("fork"):
        preview_tags = {row["tag_name"] for row in releases
                        if row.get("draft") or row.get("prerelease") or not row.get("published_at")}
        tags = [(stable_version(name, ref.removeprefix("refs/tags/")), ref.removeprefix("refs/tags/"))
                for ref in refs if ref.startswith("refs/tags/") and not ref.endswith("^{}")
                and ref.removeprefix("refs/tags/") not in preview_tags]
        tags = [(version, tag) for version, tag in tags if version is not None]
        if tags:
            _, tag = max(tags)
            selection, metadata_source = "stable-tag", f"https://github.com/{repo}/tags"
    # Forks inherit upstream tags, so absence of independent stable Releases
    # means following the designated fork's own default branch.
    if tag is not None:
        ref = "refs/tags/" + tag
        revision = refs.get(ref + "^{}", refs.get(ref))
        version = ".".join(str(part) for part in stable_version(name, tag))
    else:
        ref = "refs/heads/" + metadata["default_branch"]
        revision = refs.get(ref)
        selection, metadata_source = "default-branch", f"https://api.github.com/repos/{repo}"
        version = "git-" + (revision[:12] if revision else "unknown")
    if not revision or not SHA.fullmatch(revision):
        raise ResolutionError(f"Selected upstream ref is missing: {url} {ref}")
    result = {"name": name, "path": "third_party/" + name, "url": url,
              "version": version, "ref": ref, "revision": revision,
              "selection": selection, "metadata_source": metadata_source}
    if name == "libarchive":
        header = api.content(repo, revision, "libarchive/archive.h")
        match = re.search(r'#define\s+ARCHIVE_VERSION_ONLY_STRING\s+"([^"]+)"', header or "")
        if not match:
            raise ResolutionError("Cannot determine selected libarchive source version")
        result["version"] = match[1]
        result["version_source"] = "libarchive/archive.h:ARCHIVE_VERSION_ONLY_STRING"
    return result


def resolve_coupled(parent: dict, api: GitHub, requested: tuple[str, ...] | None = None) -> list[dict]:
    repo = api.repo(parent["url"])
    text = api.content(repo, parent["revision"], ".gitmodules", missing_ok=True)
    modules = parse_gitmodules(text) if text else []
    if requested is not None:
        found = {module["path"] for module in modules}
        if set(requested) - found:
            raise ResolutionError(f"Missing coupled source modules for {parent['name']}: {set(requested) - found}")
        modules = [module for module in modules if module["path"] in requested]
    if not modules:
        return []
    links = api.gitlinks(repo, parent["revision"], modules)
    result = []
    for module in modules:
        child = {"name": parent["name"] + "/" + module["path"],
                 "path": parent["path"] + "/" + module["path"],
                 "url": nested_url(parent["url"], module["url"]),
                 "version": "git-" + links[module["path"]][:12],
                 "ref": links[module["path"]], "revision": links[module["path"]],
                 "selection": "coupled-gitlink", "coupled_to": parent["name"],
                 "metadata_source": f"https://github.com/{repo}/tree/{parent['revision']}/{module['path']}"}
        if module["path"] == "tf-psa-crypto":
            version_header = "include/tf-psa-crypto/build_info.h"
            header = api.content(api.repo(child["url"]), child["revision"], version_header)
            match = re.search(r'#define\s+TF_PSA_CRYPTO_VERSION_STRING\s+"([^"]+)"', header or "")
            if not match:
                raise ResolutionError("Cannot determine matched TF-PSA-Crypto source version")
            child["version"] = match[1]
            child["version_source"] = version_header + ":TF_PSA_CRYPTO_VERSION_STRING"
        result.append(child)
        result.extend(resolve_coupled(child, api))
    return result


def validate_manifest(manifest: dict, root: Path, require_qt: bool = False) -> list[dict]:
    if not isinstance(manifest, dict) or type(manifest.get("schema_version")) is not int or manifest["schema_version"] != 1 \
            or not isinstance(manifest.get("includes_qt"), bool) or not isinstance(manifest.get("resolved_at"), str):
        raise ResolutionError("Unsupported dependency manifest schema")
    try:
        value = manifest["resolved_at"]
        parsed = dt.datetime.fromisoformat(value[:-1] + "+00:00" if value.endswith("Z") else value)
        if parsed.utcoffset() != dt.timedelta(0):
            raise ValueError("UTC is required")
    except ValueError as error:
        raise ResolutionError("Manifest resolved_at must be a UTC ISO timestamp") from error
    if require_qt and not manifest["includes_qt"]:
        raise ResolutionError("Requested Qt sources are absent from replay manifest")
    rows = manifest.get("dependencies")
    if not isinstance(rows, list) or not rows:
        raise ResolutionError("Manifest contains no dependency source set")
    names, paths, by_name = set(), set(), {}
    root_modules = {module["path"]: module for module in parse_gitmodules((root / ".gitmodules").read_text(encoding="utf-8"))}
    expected = set(CORE) | ({"qt", "vulkan-headers"} if manifest["includes_qt"] else set())
    for row in rows:
        fields = ("name", "path", "url", "version", "ref", "revision", "selection", "metadata_source")
        if not isinstance(row, dict) or any(not isinstance(row.get(key), str) or not row[key] for key in fields):
            raise ResolutionError("Invalid dependency source record")
        if not safe_relative_path(row["path"]) or not SHA.fullmatch(row["revision"]):
            raise ResolutionError(f"Invalid source path or commit: {row['name']}")
        if row["selection"] not in {"release", "stable-tag", "default-branch", "coupled-gitlink"}:
            raise ResolutionError(f"Invalid source selection: {row['name']}")
        if (row["selection"] in {"release", "stable-tag"} and not row["ref"].startswith("refs/tags/")
                or row["selection"] == "default-branch" and not row["ref"].startswith("refs/heads/")
                or row["selection"] == "coupled-gitlink" and row["ref"] != row["revision"]):
            raise ResolutionError(f"Invalid selected source ref: {row['name']}")
        if not (root / row["path"]).resolve().is_relative_to(root.resolve()):
            raise ResolutionError(f"Dependency path escapes workspace: {row['path']}")
        if row["name"] in names or row["path"] in paths:
            raise ResolutionError("Duplicate dependency source record")
        parent_name = row.get("coupled_to")
        if parent_name is not None and (not isinstance(parent_name, str) or not parent_name):
            raise ResolutionError(f"Invalid coupled source parent: {row['name']}")
        if parent_name:
            parent = by_name.get(parent_name)
            if not parent or not row["path"].startswith(parent["path"] + "/") or row["selection"] != "coupled-gitlink":
                raise ResolutionError(f"Invalid coupled source order or parent: {row['name']}")
            if row["name"] != parent_name + "/" + row["path"][len(parent["path"]) + 1:]:
                raise ResolutionError(f"Invalid coupled source name: {row['name']}")
        else:
            if row["name"] not in expected or row["path"] != "third_party/" + row["name"]:
                raise ResolutionError(f"Unexpected top-level dependency: {row['name']}")
            module = root_modules.get(row["path"])
            if not module or canonical_url(module["url"]) != canonical_url(row["url"]):
                raise ResolutionError(f"Replay upstream differs from .gitmodules: {row['name']}")
            if canonical_url(row["url"]) != "https://github.com/" + UPSTREAMS[row["name"]]:
                raise ResolutionError(f"Dependency upstream differs from project policy: {row['name']}")
        names.add(row["name"])
        paths.add(row["path"])
        by_name[row["name"]] = row
    if {row["name"] for row in rows if not row.get("coupled_to")} != expected:
        raise ResolutionError("Manifest does not contain complete requested source set")
    if manifest["includes_qt"] and not {"qt/" + module for module in QT_MODULES}.issubset(names):
        raise ResolutionError("Manifest omits matching Qt modules")
    return rows


def repository_present(path: Path) -> bool:
    return (path / ".git").exists()


def recorded_gitlink(parent: Path, relative: str) -> str:
    link = git("ls-tree", "HEAD", "--", relative, cwd=parent, check=False).split()
    return link[2] if len(link) >= 3 and link[0] == "160000" else ""


def protect_local_commit(path: Path, recorded: str, resolved: str = "") -> None:
    current = git("rev-parse", "--verify", "HEAD", cwd=path, check=False)
    if not current or current in (recorded, resolved):
        return
    # Only remote tracking refs prove a prior fetch/push; local tags can be unpublished.
    if not git("for-each-ref", "--contains", current, "--format=%(refname)", "refs/remotes", cwd=path):
        raise ResolutionError(f"Dependency has an unrecorded local commit; publish or preserve it before updating: {path} ({current})")


def preflight_checkout(path: Path, visited: set[Path], accepted: dict[Path, str] | None = None,
                       recorded: str = "") -> None:
    path = path.resolve()
    if path in visited:
        return
    visited.add(path)
    if not repository_present(path):
        if path.exists() and any(path.iterdir()):
            raise ResolutionError(f"Uninitialized dependency directory is not empty: {path}")
        return
    if Path(git("rev-parse", "--show-toplevel", cwd=path)).resolve() != path:
        raise ResolutionError(f"Dependency is not a separate checkout: {path}")
    if git("status", "--porcelain=v1", "--untracked-files=all", "--ignore-submodules=all", cwd=path):
        raise ResolutionError(f"Dependency contains tracked changes or untracked files; preserve them before updating: {path}")
    if accepted is not None:
        protect_local_commit(path, recorded, accepted.get(path, ""))
    modules_file = path / ".gitmodules"
    if modules_file.exists():
        for module in parse_gitmodules(modules_file.read_text(encoding="utf-8")):
            preflight_checkout(path / module["path"], visited, accepted, recorded_gitlink(path, module["path"]))


def commit_present(path: Path, revision: str) -> bool:
    return bool(git("rev-parse", "--verify", revision + "^{commit}", cwd=path, check=False))


def source_modules(path: Path, revision: str) -> list[dict]:
    content = git("show", revision + ":.gitmodules", cwd=path, check=False)
    return parse_gitmodules(content) if content else []


def verify_coupling(row: dict, rows: list[dict], root: Path) -> None:
    parent = next(item for item in rows if item["name"] == row["coupled_to"])
    relative = row["path"][len(parent["path"]) + 1:]
    checkout = root / parent["path"]
    modules = {module["path"]: module for module in source_modules(checkout, parent["revision"])}
    module = modules.get(relative)
    link = git("ls-tree", parent["revision"], "--", relative, cwd=checkout).split()
    if not module or canonical_url(nested_url(parent["url"], module["url"])) != canonical_url(row["url"]) \
            or len(link) < 3 or link[0] != "160000" or link[2] != row["revision"]:
        raise ResolutionError(f"Coupled source does not match parent gitlink: {row['name']}")


def verify_coupled_set(row: dict, rows: list[dict], root: Path) -> None:
    modules = source_modules(root / row["path"], row["revision"])
    if row["name"] == "qt":
        modules = [module for module in modules if module["path"] in QT_MODULES]
    elif row["name"] != "mbedtls" and not row.get("coupled_to"):
        return
    expected = {row["path"] + "/" + module["path"] for module in modules}
    recorded = {child["path"] for child in rows if child.get("coupled_to") == row["name"]}
    if expected != recorded:
        raise ResolutionError(f"Manifest omits or invents coupled sources for {row['name']}")


def initialize_checkout(root: Path, row: dict, rows: list[dict]) -> Path:
    target = root / row["path"]
    if repository_present(target):
        return target
    if row.get("coupled_to"):
        parent = next(item for item in rows if item["name"] == row["coupled_to"])
        parent_dir = root / parent["path"]
        relative = row["path"][len(parent["path"]) + 1:]
    else:
        parent_dir, relative = root, row["path"]
    modules = {module["path"]: module for module in parse_gitmodules((parent_dir / ".gitmodules").read_text(encoding="utf-8"))}
    module = modules.get(relative)
    if not module:
        raise ResolutionError(f"Unregistered submodule source: {row['path']}")
    git("submodule", "init", "--", relative, cwd=parent_dir)
    module_dir = Path(git("rev-parse", "--git-path", "modules/" + module["name"], cwd=parent_dir))
    if not module_dir.is_absolute():
        module_dir = (parent_dir / module_dir).resolve()
    target.parent.mkdir(parents=True, exist_ok=True)
    module_dir.parent.mkdir(parents=True, exist_ok=True)
    git("init", "--separate-git-dir", str(module_dir), str(target))
    git("config", "core.worktree", str(target), cwd=target)
    if not git("remote", "get-url", "origin", cwd=target, check=False):
        git("remote", "add", "origin", row["url"], cwd=target)
    return target


def apply_manifest(manifest: dict, root: Path, offline: bool = False, require_qt: bool = False,
                   core_only: bool = False) -> None:
    rows = validate_manifest(manifest, root, require_qt)
    if core_only:
        rows = [row for row in rows if row["name"].split("/")[0] in CORE]
    visited: set[Path] = set()
    accepted = {(root / row["path"]).resolve(): row["revision"] for row in rows}
    # Check every existing target and its OLD nested checkouts before changing
    # any source. A gitlink HEAD mismatch is allowed; modified files are not.
    for row in rows:
        if row.get("coupled_to"):
            parent = next(item for item in rows if item["name"] == row["coupled_to"])
            parent_dir = root / parent["path"]
            relative = row["path"][len(parent["path"]) + 1:]
        else:
            parent_dir, relative = root, row["path"]
        preflight_checkout(root / row["path"], visited, accepted, recorded_gitlink(parent_dir, relative))
        path = root / row["path"]
        if repository_present(path):
            origin = git("remote", "get-url", "origin", cwd=path, check=False)
            if origin and canonical_url(origin) != canonical_url(row["url"]):
                raise ResolutionError(f"Dependency origin differs from resolved upstream: {row['name']}")
        if offline and (not repository_present(path) or not commit_present(path, row["revision"])):
            raise ResolutionError(f"Offline replay requires a locally available commit: {row['name']} {row['revision']}")
    if offline:
        for row in rows:
            verify_coupled_set(row, rows, root)
            if row.get("coupled_to"):
                verify_coupling(row, rows, root)
    for row in rows:
        if row.get("coupled_to"):
            verify_coupling(row, rows, root)
        fresh_checkout = not repository_present(root / row["path"])
        checkout = initialize_checkout(root, row, rows)
        if not commit_present(checkout, row["revision"]):
            print(f"Fetching {row['name']}: {row['revision']}", flush=True)
            git("fetch", "--no-tags", "--depth", "1", row["url"], row["revision"], cwd=checkout)
        verify_coupled_set(row, rows, root)
        if fresh_checkout:
            # A deinitialized submodule retains HEAD and its index in .git/modules.
            # Plain checkout of that same HEAD preserves missing worktree files.
            # The target was empty at preflight; populate missing files without
            # --force or resetting any existing checkout/user changes.
            git("read-tree", row["revision"], cwd=checkout)
            git("checkout-index", "--all", cwd=checkout)
        git("-c", "submodule.recurse=false", "checkout", "--detach", row["revision"], cwd=checkout)
        if git("rev-parse", "HEAD", cwd=checkout) != row["revision"]:
            raise ResolutionError(f"Source checkout revision differs from resolution: {row['name']}")
        # A later fetch may fail before the manifest is written. Preserve proof of
        # successful source retrieval so the next initialization can safely resume.
        git("update-ref", "refs/remotes/origin/ananarchiver-resolved", row["revision"], cwd=checkout)
        print(f"{row['name']}: {row['version']} {row['revision']}", flush=True)


def verify_source_version(row: dict, root: Path) -> None:
    path = root / row["path"]
    if row["name"] == "libarchive":
        header = (path / "libarchive/archive.h").read_text(encoding="utf-8")
        match = re.search(r'#define\s+ARCHIVE_VERSION_ONLY_STRING\s+"([^"]+)"', header)
        version = match[1] if match else None
    elif row["name"].endswith("/tf-psa-crypto"):
        header = (path / "include/tf-psa-crypto/build_info.h").read_text(encoding="utf-8")
        match = re.search(r'#define\s+TF_PSA_CRYPTO_VERSION_STRING\s+"([^"]+)"', header)
        version = match[1] if match else None
    elif row["selection"] == "coupled-gitlink" or row["selection"] == "default-branch":
        version = "git-" + row["revision"][:12]
    else:
        ref = row["ref"]
        components = stable_version(row["name"], ref.removeprefix("refs/tags/")) if ref.startswith("refs/tags/") else None
        version = ".".join(map(str, components)) if components else None
    if version is None or version != row["version"]:
        raise ResolutionError(f"Recorded dependency version differs from selected source: {row['name']}")


def verify_manifest(manifest: dict, root: Path, require_qt: bool = False, core_only: bool = False) -> None:
    """Read-only verification: never contacts upstream or changes HEAD/index/files."""
    rows = validate_manifest(manifest, root, require_qt)
    if core_only:
        rows = [row for row in rows if row["name"].split("/")[0] in CORE]
    visited: set[Path] = set()
    for row in rows:
        path = root / row["path"]
        if not repository_present(path) or git("rev-parse", "--verify", "HEAD", cwd=path, check=False) != row["revision"]:
            raise ResolutionError(f"Source checkout revision differs from manifest: {row['name']}")
        origin = git("remote", "get-url", "origin", cwd=path, check=False)
        if canonical_url(origin) != canonical_url(row["url"]):
            raise ResolutionError(f"Dependency origin differs from manifest: {row['name']}")
        preflight_checkout(path, visited)
        verify_coupled_set(row, rows, root)
        if row.get("coupled_to"):
            verify_coupling(row, rows, root)
        verify_source_version(row, root)


def atomic_write_manifest(path: Path, manifest: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n", dir=path.parent,
                                         prefix=path.name + ".", suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            json.dump(manifest, output, indent=2, ensure_ascii=False)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def resolve(root: Path, include_qt: bool, api: GitHub) -> dict:
    modules = {module["path"]: module for module in parse_gitmodules((root / ".gitmodules").read_text(encoding="utf-8"))}
    rows = []
    for name in CORE + (("qt", "vulkan-headers") if include_qt else ()):
        module = modules.get("third_party/" + name)
        if not module or canonical_url(module["url"]) != "https://github.com/" + UPSTREAMS[name]:
            raise ResolutionError(f"Dependency upstream differs from project policy: {name}")
        row = select_source(name, module["url"], api)
        rows.append(row)
        if name == "qt":
            rows.extend(resolve_coupled(row, api, QT_MODULES))
        elif name == "mbedtls":
            rows.extend(resolve_coupled(row, api))
    result = {"schema_version": 1, "resolved_at": dt.datetime.now(dt.timezone.utc).isoformat(),
              "includes_qt": include_qt, "dependencies": rows}
    validate_manifest(result, root)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--qt", action="store_true", help="Include matching Qt source modules")
    parser.add_argument("--core-only", action="store_true", help="Replay only archive sources while preserving the full manifest")
    parser.add_argument("--manifest", type=Path, help="Output record (default: ROOT/out/dependencies/resolved-dependencies.json)")
    parser.add_argument("--replay-manifest", type=Path, help="Use an exact previously resolved source set")
    parser.add_argument("--offline", action="store_true", help="Replay locally available commits without network access")
    parser.add_argument("--resolve-only", action="store_true", help="Write resolution without changing dependency checkouts")
    parser.add_argument("--verify", action="store_true", help="Verify replay sources without network, checkout, or writing manifests")
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve()
        if not repository_present(root) or Path(git("rev-parse", "--show-toplevel", cwd=root)).resolve() != root:
            raise ResolutionError(f"Dependency root must be the project Git checkout: {root}")
        if args.qt and args.core_only:
            raise ResolutionError("--qt cannot be combined with --core-only")
        if args.core_only and not args.replay_manifest:
            raise ResolutionError("--core-only requires --replay-manifest")
        if args.offline and not args.replay_manifest:
            raise ResolutionError("--offline requires --replay-manifest; latest selection requires network access")
        if args.verify and (not args.replay_manifest or args.resolve_only):
            raise ResolutionError("--verify requires --replay-manifest and cannot be combined with --resolve-only")
        if args.replay_manifest:
            manifest = json.loads(args.replay_manifest.read_text(encoding="utf-8"))
            validate_manifest(manifest, root, args.qt)
        else:
            manifest = resolve(root, args.qt, GitHub())
        if args.verify:
            verify_manifest(manifest, root, args.qt, args.core_only)
            print(f"Verified dependency manifest: {args.replay_manifest.resolve()}")
            return 0
        if not args.resolve_only:
            apply_manifest(manifest, root, args.offline, args.qt, args.core_only)
            verify_manifest(manifest, root, args.qt, args.core_only)
        output = args.manifest or root / "out/dependencies/resolved-dependencies.json"
        atomic_write_manifest(output.resolve(), manifest)
        print(f"Dependency manifest: {output.resolve()}")
        return 0
    except (ResolutionError, OSError, ValueError, configparser.Error) as error:
        print(f"Dependency resolution failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
