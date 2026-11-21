# Releases

The version, tag, release notes and preview or stable track live in `release/release.json`. Run
`python scripts/release-metadata.py` to check them against CMake, npm and the lockfile.

Update the metadata and its matching versioned notes on `main` to start **Publish release**. The
same workflow can also be dispatched from `main`. It verifies the backend and calls the native build
matrix for that commit. The publisher only runs if those jobs succeed. It checks all four platform
archives, assembles `SHA256SUMS.txt` and attaches the matching source archive. A version that
already has a GitHub Release is skipped. Packages from preview versions are published as
prereleases. Regular CI builds remain available without creating a release.

The binaries are unsigned development bundles. Download the package for your OS and check it with
`sha256sum --check` and the matching `.sha256` file. The Linux bundle needs the system Qt platform
dependencies, X11 or Wayland libraries, x264 and Mbed TLS 3. See the
[desktop build guide](native.md) and [dependency notices](../third_party/NOTICE.md).

The tag's source archive contains the app, backend, build scripts and OBS patch logic. It points to
the pinned
[OBS source](https://github.com/obsproject/obs-studio/tree/65c320b46e1af5c7a555161aea6182cdf7a8c524),
[FFmpeg 8.0 source](https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz),
[Qt 6.12.0 component sources](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/)
and the [OBS dependencies](https://github.com/obsproject/obs-deps/tree/2026-10-23) used for Windows
and macOS. The version and SHA-256 for the OBS and FFmpeg archives are in
[`cmake/native-lock.json`](../cmake/native-lock.json).
