#!/usr/bin/env python3
"""Configure, test and stage a development bundle for the host OS."""
import argparse
import os
import platform
import shutil
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


def run(*args, env=None):
    subprocess.run([str(arg) for arg in args], check=True, env=env)


def stage_linux(qt, output):
    dest = output / "lib"
    dest.mkdir(exist_ok=True)
    for component in ("Core", "DBus", "Gui", "Network", "NetworkAuth", "Widgets",
                      "WaylandClient", "XcbQpa", "OpenGL"):
        library = qt / "lib" / f"libQt6{component}.so.6"
        if library.is_file():
            shutil.copy2(library, dest / library.name)
    for directory in (qt, qt / "lib"):
        for library in directory.glob("libicu*.so.[0-9][0-9]"):
            shutil.copy2(library, dest / library.name)
    for name in ("platforms", "tls", "imageformats", "wayland-graphics-integration-client",
                 "wayland-shell-integration"):
        plug = qt / "plugins" / name
        if plug.exists():
            for entry in plug.glob("*.so"):
                if name == "platforms" and entry.name not in {"libqxcb.so", "libqwayland.so", "libqoffscreen.so"}:
                    continue
                if name == "imageformats" and entry.name not in {"libqgif.so", "libqjpeg.so"}:
                    continue
                target = output / "plugins" / name / entry.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(entry, target)
    (output / "bin/qt.conf").write_text("[Paths]\nPrefix = ..\nPlugins = plugins\n")
    launcher = output / "YoungLionCast"
    launcher.write_text('''#!/bin/sh
set -eu
cast_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$cast_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$cast_dir/bin/younglion-cast" "$@"
''')
    launcher.chmod(0o755)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("sdk", "qt", "build", "output"):
        parser.add_argument(f"--{option}", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    sdk, qt, build, output = (p.resolve() for p in (args.sdk, args.qt, args.build, args.output))
    if output.exists() and any(output.iterdir()):
        parser.error("output directory must be empty")
    system = platform.system()
    env = os.environ.copy()
    extra = []
    deps = sorted(sdk.glob("obs-studio-*/.deps/obs-deps-2026-10-23-" +
                           ("universal" if system == "Darwin" else "x64")))
    if deps:
        extra.append(f"-DCAST_OBS_DEPS={deps[0]}")
    if system == "Windows":
        extra += ["-G", "Visual Studio 18 2026", "-A", "x64"]
        env["PATH"] = os.pathsep.join([str(qt / "bin"), str(sdk / "obs/bin/64bit"),
                                        *(str(d / "bin") for d in deps), env["PATH"]])
    else:
        extra += ["-G", "Ninja"]
    if system == "Linux":
        extra.append(f"-DCAST_FFMPEG_PREFIX={sdk / 'media'}")
        env["LD_LIBRARY_PATH"] = ":".join(str(p) for p in (
            qt / "lib", qt, sdk / "obs/lib", sdk / "media/lib")) + ":" + env.get("LD_LIBRARY_PATH", "")
    if system == "Darwin":
        extra.append("-DCMAKE_OSX_DEPLOYMENT_TARGET=14.4")
        env["DYLD_FRAMEWORK_PATH"] = str(sdk / "obs/Frameworks")
        env["DYLD_LIBRARY_PATH"] = os.pathsep.join(str(d / "lib") for d in deps)
    run("cmake", "-S", REPO, "-B", build, *extra, "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_PREFIX_PATH={qt}", f"-DCAST_OBS_PREFIX={sdk / 'obs'}", env=env)
    run("cmake", "--build", build, "--config", "Release", "--parallel", args.jobs, env=env)
    run("ctest", "--test-dir", build, "-C", "Release", "--output-on-failure", env=env)
    run("cmake", "--install", build, "--config", "Release", "--prefix", output, env=env)
    if system == "Linux":
        stage_linux(qt, output)
    elif system == "Windows":
        run(qt / "bin/windeployqt.exe", "--release", "--no-translations", output / "bin/younglion-cast.exe", env=env)
    else:
        run(qt / "bin/macdeployqt", output / "younglion-cast.app", "-always-overwrite", env=env)


if __name__ == "__main__":
    main()
