# Third-party components

YoungLion Cast's original code is GPL-3.0-or-later. Source distributions include this notice.

- OBS Studio / libobs 33.0.1, commit 65c320b46e1af5c7a555161aea6182cdf7a8c524:
  GPL-2.0-or-later (combined distribution under GPL-3.0-or-later). See licenses/OBS-GPL-2.txt.
  https://github.com/obsproject/obs-studio
- Qt 6.12.0 Core/Gui/Widgets/Network/Test/DBus: LGPL-3.0/GPL-3.0/commercial options;
  Qt Network Authorization: GPL-3.0/commercial. This distribution uses the GPL-compatible option.
  Preserve Qt's license files and third-party notices from the SDK in binary distributions.
  https://www.qt.io/qt-licensing
- QtKeychain 0.15.0: BSD-3-Clause; source and COPYING in qtkeychain/. The vendored MSVC
  UTF-8 option is guarded for MinGW compatibility. Provenance: qtkeychain/YL_VENDOR.md.
- FFmpeg 8.0: LGPL-2.1-or-later with the minimal build in scripts/native-sdk.py;
  enabling optional GPL components changes the applicable distribution obligations.
  https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz
- x264 (GPL-2.0-or-later) and Mbed TLS 3 (Apache-2.0 OR GPL-2.0-or-later) power the local
  H.264/RTMP preview through the OBS modules. Linux builds use the corresponding distribution
  packages. Windows/macOS use [the OBS dependencies](https://github.com/obsproject/obs-deps/tree/2026-10-23).
- OBS transitive dependencies (FreeType, Fontconfig, Jansson, SIMDe, libcaption,
  PipeWire/Wayland/X11, platform SDKs) retain their upstream licenses. Keep dependency
  license directories when assembling installers. GPU drivers are supplied by the OS/user.

Release packages must accompany their exact corresponding source (this repository plus the pinned
OBS/FFmpeg sources and build scripts), GPL text and all applicable dependency license notices.
Unsigned development bundles are not signed/notarized release installers.
