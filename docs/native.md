# YoungLion Cast desktop editor

The Qt 6.12.0 Widgets app uses C++20 and libobs 33.0.1. Docking and layout restoration use
`QMainWindow` and `QDockWidget`. Themes live in `desktop/styles`; `desktop/translations` contains
the Turkish Qt catalog. The GPU preview uses the active scene, and the editor releases sources in
inactive scenes. The preview draws directly to the native display surface.

For release downloads, scene editing and the two-receiver demo, see the
[quick start](quick-start.md).

## Build from source

Install Python 3.12+, CMake 3.28+, the Qt **6.12.0** desktop SDK with Network Authorization, and a
native toolchain. Linux requires Ninja and the development packages listed in the desktop CI
workflow. Windows uses Visual Studio 18 2026 and the 10.0.26100 SDK. macOS uses current Xcode with
deployment target 14.4. Use the SDK for the host architecture.

```sh
python scripts/native-sdk.py --root build-sdk
python scripts/native-release.py --sdk build-sdk --qt /path/to/6.12.0/sdk --build build-native --output dist
```

On Linux x64, `python -m pip install py7zr==1.1.0` and the SDK command's `--linux-qt` option install
the checked official archives in `build-sdk/qt`. Then use `--qt build-sdk/qt` for the release
command. The build pins FFmpeg and OBS archives by SHA-256 and builds only the modules needed for
the editor. The Linux bundle expects compatible system graphics, desktop libraries, and an X11
session or a Wayland session with a ScreenCast portal. Run `dist/YoungLionCast` on Linux,
`dist/bin/younglion-cast.exe` on Windows, or `dist/younglion-cast.app` on macOS.

Linux X11 display capture uses XShm and window capture uses XComposite with an EWMH window manager.
Wayland requires the user to choose the target in the system portal. macOS requires Screen Recording
permission for ScreenCaptureKit. Windows uses OBS window and display capture, and game capture for
supported games and graphics APIs. The game source requires a selected window. Application-window
capture uses the window source on each platform. Capture sources in opened projects wait for a fresh
selection. The editor also works offline for scene and project editing.

Use
`xvfb-run -a env CAST_TEST_GPU=1 QT_QPA_PLATFORM=xcb ctest --test-dir build-native -R '^capture$' --output-on-failure`
with the Qt and OBS runtime paths set for the test binaries. This exercises libobs XShm and
composition through Mesa in a virtual X11 session. Sign, package, and exercise the capture matrix on
physical desktops before shipping. Windows game capture, macOS ScreenCaptureKit, and Wayland portal
behavior need separate hardware and desktop tests. The publishing preview supports local RTMP.

## Try local RTMP

Follow the [two-receiver walkthrough](quick-start.md#send-video-to-two-local-receivers) to try
streaming without platform credentials.

Run `CAST_TEST_RTMP=1` alongside `CAST_TEST_GPU=1` under Xvfb to test two real FFmpeg listeners,
lose one connection and check the remaining FLV file with `ffprobe`.

## Connect YoungLion Account

1. Enable the Supabase OAuth 2.1 server. Implement its authorization/consent screen in the YoungLion
   Account frontend. Register a **public** OAuth client with the exact callback
   `http://127.0.0.1:49173/oauth/callback`, using Authorization Code + S256. Add the client ID to
   `CAST_OAUTH_CLIENT_IDS`. See
   [Supabase OAuth flows](https://supabase.com/docs/guides/auth/oauth-server/oauth-flows).
2. Apply the migrations and deploy the existing Edge functions on a staging project. Configure Kick
   and Twitch publishing credentials and the first-party cookie route in [setup.md](setup.md).
3. Copy `desktop/account.example.json` to the app config folder as `account.json`, or set
   `CAST_ACCOUNT_CONFIG` to its absolute path. Set only public IDs, the publishable key, and HTTPS
   URLs. On Linux, the keychain backend requires Secret Service or KWallet. Windows and macOS use
   Credential Manager and Keychain respectively.
4. Integrate the handoff in YoungLion Account. On the URL configured as `accountWebUrl`, read
   `native_request` and `native_ticket` and clear the URL query before loading third-party
   resources. Keep the handoff in memory or in short-lived first-party session state through
   sign-in. After the user signs in and explicitly chooses to continue, call
   `CastAccountClient.authorizeNative(requestId, ticket)` using the browser session and
   `credentials: "include"`, then navigate to `authorization_url`. Use
   `Referrer-Policy: no-referrer` and avoid recording these query values. The provider callback
   tells the user to return to Cast, where they confirm the returned channel. Reconnect uses the
   same broker.

The account frontend, OAuth application registration, and live deployment require the YoungLion
Account project and provider credentials. The C++ client and database broker are included here, and
the local tests do not require production tokens. Review the upstream notice before distributing a
build. See [third_party/NOTICE.md](../third_party/NOTICE.md).
