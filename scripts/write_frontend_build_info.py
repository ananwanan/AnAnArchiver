"""Embed verified build identities in the desktop About and license dialogs.

Consume the prepared build identity, not the mutable resolution manifest. CMake
runs dependency verification before this generator, so missing records must fail
the build instead of decorating an existing binary with guessed versions.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess


def metadata(identity: dict, config: dict, revision: str, dirty: bool) -> dict:
    components = {}
    for source in identity["dependencies"]:
        name = source["name"]
        if name in components or not isinstance(source.get("version"), str) \
                or not re.fullmatch(r"[0-9a-f]{40,64}", source.get("revision", "")):
            raise ValueError("Missing or duplicate dependency version/revision: " + name)
        components[name] = {"version": source["version"], "revision": source["revision"],
                            "selection": "bundled-source"}
    for library in config["external_libraries"]:
        name = "qt" if library["name"] == "Qt" else library["name"]
        if name in components or not isinstance(library.get("version"), str) or not library["version"]:
            raise ValueError("Missing or conflicting selected library version: " + name)
        components[name] = {"version": library["version"], "revision": "",
                            "selection": library["selection"]}
    if "qt" not in components or "libarchive" not in components:
        raise ValueError("Desktop build identity must include Qt and libarchive")
    if not re.fullmatch(r"[0-9a-f]{40,64}", revision):
        raise ValueError("Application Git revision is invalid")
    return {"components": components, "applicationRevision": revision, "applicationDirty": dirty}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    identity = json.loads((args.build_dir / "dependency-build.json").read_text(encoding="utf-8"))
    config = json.loads((args.build_dir / "build-trace-config.json").read_text(encoding="utf-8"))
    git = ["git", "-C", config["root"]]
    revision = subprocess.check_output(git + ["rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(git + ["status", "--porcelain", "--untracked-files=no"],
                                        text=True, encoding="utf-8").strip())
    value = metadata(identity, config, revision, dirty)
    # JSON escaping plus ASCII serialization keeps filenames, quotes and raw
    # literal delimiters from becoming C++ source code.
    payload = json.dumps(value, ensure_ascii=True, separators=(",", ":"))
    source = "#pragma once\n#define ANANARCHIVER_DEPENDENCY_INFO_JSON " + json.dumps(payload) + "\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != source:
        args.output.write_text(source, encoding="utf-8")


if __name__ == "__main__":
    main()
