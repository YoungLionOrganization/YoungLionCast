# Quick start

## Open the Linux preview

Download the Linux x64 `.tar.gz` and its `.sha256` file from the same
[GitHub release](https://github.com/YoungLionOrganization/YoungLionCast/releases). Run the commands
from your download directory:

```sh
sha256sum --check YoungLionCast-v0.3.0-linux-x64.tar.gz.sha256
tar -xzf YoungLionCast-v0.3.0-linux-x64.tar.gz
./YoungLionCast/YoungLionCast
```

Keep the launcher beside the `bin`, `lib`, `plugins` and `share` directories. The bundle requires
system desktop libraries, x264 and Mbed TLS 3. Use a desktop session with an X11 or Wayland capture
backend. The capture smoke test uses X11. If a capture permission is denied or a target disappears,
select the target again from source properties.

## Edit a scene

1. In **Sahneler**, click **+** to create a scene. In **Kaynaklar**, add a text, image, screen or
   window source. Capture choices depend on your desktop session.
2. Select a source and use **Dönüşüm** to adjust position, scale, rotation or crop. Drag the
   selected layer in the preview to move it.
3. Use **Dosya → Kaydet** to save a `.ylcast` project. Keep any linked images with the project.
   Capture targets need to be selected again after reopening a project.

## Send video to two local receivers

Open two terminals and start one FFmpeg listener in each:

```sh
# Terminal 1
ffmpeg -listen 1 -i rtmp://127.0.0.1:1935/live/cast -c:v copy -f flv first.flv
# Terminal 2
ffmpeg -listen 1 -i rtmp://127.0.0.1:1936/live/cast -c:v copy -f flv second.flv
```

Open **Yerel yayın** in Cast, paste these URLs on separate lines and click **Yerel yayını başlat**:

```text
rtmp://127.0.0.1:1935/live/cast
rtmp://127.0.0.1:1936/live/cast
```

The panel shows each receiver’s state, sent data and dropped frames. Stop one FFmpeg listener to
observe its reconnect status while the other keeps receiving video. Click **Yerel yayını durdur**
when finished. These outputs carry H.264 video over IPv4 loopback. The Twitch and Kick account tab
manages authorizations and requires [YoungLion Account setup](native.md#connect-younglion-account).
