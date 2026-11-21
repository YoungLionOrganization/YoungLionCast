#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Archive a staged native bundle while preserving Unix permissions and symlinks."""
import hashlib
import os
import re
import shutil
import tarfile
from pathlib import Path

version = os.environ["CAST_RELEASE_VERSION"]
platform = os.environ["CAST_RELEASE_PLATFORM"]
if not re.fullmatch(r"\d+\.\d+\.\d+", version) or platform not in {
    "linux-x64", "windows-x64", "macos-x64", "macos-arm64"
}:
    raise SystemExit("Invalid release version or platform")

stage = Path(os.environ.get("CAST_STAGE", "dist"))
if not stage.is_dir() or not any(stage.iterdir()):
    raise SystemExit("No native bundle was staged")
output = Path("release-assets")
output.mkdir(exist_ok=True)
stem = f"YoungLionCast-v{version}-{platform}"
if platform == "windows-x64":
    archive = Path(shutil.make_archive(str(output / stem), "zip", stage))
else:
    archive = output / f"{stem}.tar.gz"
    with tarfile.open(archive, "w:gz") as bundle:
        bundle.add(stage, arcname="YoungLionCast")
with archive.open("rb") as file:
    digest = hashlib.file_digest(file, "sha256").hexdigest()
archive.with_name(archive.name + ".sha256").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
