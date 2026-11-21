#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the canonical release metadata before building or publishing."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def metadata():
    data = json.loads((ROOT / "release/release.json").read_text())
    version = data["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version) or data["tag"] != f"v{version}":
        raise ValueError("Version must be a three-part version matching the tag")
    if data["title"] != f"YoungLion Cast {data['tag']}" or data["target_branch"] != "main":
        raise ValueError("Unexpected release title or target branch")
    notes = f"release/notes/{data['tag']}.md"
    if data["release_notes_path"] != notes or not (ROOT / notes).read_text().strip():
        raise ValueError("Versioned notes are required")
    if data["update_track"] not in {"stable", "preview"} or data["prerelease"] is not (
        data["update_track"] == "preview"
    ):
        raise ValueError("Release track and prerelease flag disagree")
    cmake = (ROOT / "CMakeLists.txt").read_text()
    match = re.search(r"^project\(YoungLionCast VERSION (\S+)", cmake, re.MULTILINE)
    package = json.loads((ROOT / "package.json").read_text())
    npm_lock = json.loads((ROOT / "package-lock.json").read_text())
    if not match or {match[1], package["version"], npm_lock["version"], npm_lock["packages"][""]["version"]} != {version}:
        raise ValueError("Version differs between CMake, npm and release metadata")
    return data


if __name__ == "__main__":
    for key, value in metadata().items():
        print(f"{key}={str(value).lower() if isinstance(value, bool) else value}")
