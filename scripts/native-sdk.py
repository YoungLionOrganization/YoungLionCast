#!/usr/bin/env python3
"""Build pinned, capture-only OBS in an isolated SDK directory."""
import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import tarfile
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
LOCK = json.loads((REPO / "cmake/native-lock.json").read_text())


def run(*args, cwd=None):
    subprocess.run([str(arg) for arg in args], cwd=cwd, check=True)


def download(url, output, checksum):
    if not output.exists():
        with urllib.request.urlopen(url, timeout=120) as response, output.open("wb") as stream:
            shutil.copyfileobj(response, stream)
    with output.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != checksum:
        raise RuntimeError(f"SHA-256 mismatch for {output.name}; remove and retry")


def unpack(root, archive, folder, url, checksum):
    download(url, root / archive, checksum)
    source = root / folder
    if not source.exists():
        with tarfile.open(root / archive) as bundle:
            bundle.extractall(root, filter="data")
    return source


def patch_obs(source):
    plugins = source / "plugins/CMakeLists.txt"
    content = plugins.read_text()
    if "macro(check_obs_browser)" in content:
        plugins.write_text(content.split("macro(check_obs_browser)", 1)[0] + """\
add_core_module(image-source)
add_core_module(obs-filters)
add_core_module(obs-outputs)
add_core_module(obs-x264)
add_core_module(text-freetype2)
add_core_module(linux-capture PLATFORMS LINUX)
add_core_module(linux-pipewire PLATFORMS LINUX)
add_core_module(win-capture PLATFORMS WINDOWS)
add_core_module(mac-capture PLATFORMS MACOS)
set_obs_core_modules()
""")

    content = plugins.read_text()
    if "add_core_module(obs-outputs)" not in content:
        plugins.write_text(content.replace("add_core_module(obs-filters)\n", "add_core_module(obs-filters)\nadd_core_module(obs-outputs)\nadd_core_module(obs-x264)\n"))

    outputs = source / "plugins/obs-outputs/obs-outputs.c"
    content = outputs.read_text()
    changes = """	// YoungLion's first transport test carries video only.
	struct obs_output_info cast_rtmp_video = rtmp_output_info;
	cast_rtmp_video.id = "cast_rtmp_video";
	cast_rtmp_video.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED | OBS_OUTPUT_SERVICE;
	obs_register_output(&cast_rtmp_video);"""
    if 'cast_rtmp_video.id' not in content:
        old = "\tobs_register_output(&rtmp_output_info);"
        if content.count(old) != 1:
            raise RuntimeError("Unexpected OBS outputs source")
        outputs.write_text(content.replace(old, changes))

    flv = source / "plugins/obs-outputs/flv-mux.c"
    content = flv.read_text()
    if "audio ? 20 : 8" not in content:
        content = content.replace("audio_t *audio = obs_encoder_audio(aencoder);",
                                  "audio_t *audio = aencoder ? obs_encoder_audio(aencoder) : NULL;")
        content = content.replace("AMF_EncodeInt32(enc, end, 20);",
                                  "AMF_EncodeInt32(enc, end, audio ? 20 : 8);")
        start = content.index('\tenc_num_val(&enc, end, "audiocodecid"')
        end = content.index('\n\tdstr_printf(&encoder_name', start)
        fields = content[start:end].rstrip().splitlines()
        content = content[:start] + "\tif (audio) {\n" + "\n".join("\t" + line if line else "" for line in fields) + "\n\t}\n" + content[end:]
        flv.write_text(content)

    rtmp = source / "plugins/obs-outputs/rtmp-stream.c"
    content = rtmp.read_text()
    if "YoungLion queue ceiling" not in content:
        old = "\tif (!disconnected(stream)) {\n\t\tadded_packet ="
        if content.count(old) != 1:
            raise RuntimeError("Unexpected OBS RTMP packet queue")
        content = content.replace(old, """	// YoungLion queue ceiling also applies when the socket stops draining entirely.
	if (!disconnected(stream) && stream->packets.size >= 120 * sizeof(new_packet)) {
		stream->dropped_frames++;
	} else if (!disconnected(stream)) {
		added_packet =""")
        old = "\tRTMP_EnableWrite(&stream->rtmp);"
        if content.count(old) != 1:
            raise RuntimeError("Unexpected OBS RTMP connect setup")
        rtmp.write_text(content.replace(old, "\tstream->rtmp.Link.receiveTimeout = 4;\n\tstream->rtmp.Link.sendTimeout = 4;\n" + old))

    xcomposite = source / "plugins/linux-capture/xcomposite-input.c"
    content = xcomposite.read_text()
    if "// then try to find a match by name & class" in content:
        begin = content.index("\t// then try to find a match by name & class")
        end = content.index("\ncleanup:", begin)
        xcomposite.write_text(content[:begin] + "\tUNUSED_PARAMETER(disp);\n\tret = 0;\n" + content[end:])

    # OBS normally uses an adjacent OBS.app when installing on macOS. Install
    # SDK frameworks and capture bundles even with the upstream frontend disabled.
    root = source / "CMakeLists.txt"
    content = root.read_text()
    if "# YoungLion SDK install" in content:
        root.write_text(content.replace("foreach(item image-source obs-filters text-freetype2 mac-capture)",
                                       "foreach(item image-source obs-filters obs-outputs obs-x264 text-freetype2 mac-capture)"))
    else:
        root.write_text(content + """
# YoungLion SDK install
if(OS_MACOS)
  foreach(item libobs libobs-opengl libobs-metal)
    if(TARGET ${item})
      install(TARGETS ${item} FRAMEWORK DESTINATION Frameworks LIBRARY DESTINATION lib)
    endif()
  endforeach()
  foreach(item image-source obs-filters obs-outputs obs-x264 text-freetype2 mac-capture)
    install(TARGETS ${item} BUNDLE DESTINATION PlugIns LIBRARY DESTINATION PlugIns)
  endforeach()
endif()
""")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--linux-qt", action="store_true", help="also install locked Linux Qt archives (requires py7zr)")
    args = parser.parse_args()
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    system = platform.system()
    if args.linux_qt:
        if system != "Linux" or platform.machine() != "x86_64":
            parser.error("--linux-qt requires Linux x86_64")
        import py7zr
        for entry in LOCK["linuxQtArchives"]:
            archive = root / entry["name"]
            download(entry["url"], archive, entry["sha256"])
            with py7zr.SevenZipFile(archive) as files:
                files.extractall(root / "qt")

    obs = LOCK["obs"]
    source = unpack(root, "obs-source.tar.gz", f"obs-studio-{obs['commit']}",
                    f"https://codeload.github.com/obsproject/obs-studio/tar.gz/{obs['commit']}", obs["sha256"])
    patch_obs(source)
    build_options = []
    if system == "Linux":
        ff = LOCK["ffmpeg"]
        name = f"ffmpeg-{ff['version']}"
        ffmpeg = unpack(root, f"{name}.tar.xz", name,
                        f"https://ffmpeg.org/releases/{name}.tar.xz", ff["sha256"])
        run("./configure", f"--prefix={root / 'media'}", "--enable-shared", "--disable-static",
            "--disable-programs", "--disable-doc", "--disable-debug", "--disable-autodetect",
            "--disable-x86asm", "--disable-everything", "--enable-avcodec", "--enable-avformat",
            "--enable-avutil", "--enable-swscale", "--enable-swresample", "--enable-zlib",
            "--enable-decoder=png,mjpeg,bmp,gif,webp", "--enable-demuxer=image2,gif,png_pipe,jpeg_pipe,webp_pipe",
            "--enable-parser=mjpeg,png,webp", "--enable-protocol=file,data",
            cwd=ffmpeg)
        run("make", f"-j{args.jobs}", cwd=ffmpeg)
        run("make", "install", cwd=ffmpeg)
        build_options += ["-G", "Ninja", f"-DCMAKE_PREFIX_PATH={root / 'media'}", "-DENABLE_WAYLAND=ON"]
    elif system == "Darwin":
        build_options += ["--preset", "macos", "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.4"]
    elif system == "Windows":
        build_options += ["--preset", "windows-x64", "-DENABLE_COMPAT_UPDATES=OFF"]
    else:
        parser.error(f"Unsupported build platform: {system}")
    build = root / "obs-build"
    prefix = root / "obs"
    run("cmake", "-S", source, "-B", build, *build_options, "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_INSTALL_PREFIX={prefix}", f"-DOBS_VERSION_OVERRIDE={obs['version']}",
        "-DENABLE_FRONTEND=OFF", "-DENABLE_SCRIPTING=OFF", "-DENABLE_HEVC=ON",
        "-DENABLE_PULSEAUDIO=OFF", "-DENABLE_SPEEXDSP=OFF", "-DENABLE_RNNOISE=OFF")
    run("cmake", "--build", build, "--config", "Release", "--parallel", args.jobs)
    run("cmake", "--install", build, "--config", "Release")
    if system in {"Darwin", "Windows"}:
        run("cmake", "--install", build, "--config", "Release", "--component", "Development")


if __name__ == "__main__":
    main()
