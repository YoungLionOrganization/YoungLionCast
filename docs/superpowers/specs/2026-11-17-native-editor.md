# Approved native editor scope

Qt 6.12.0, C++20, Qt Widgets and libobs 33.0.1. OBS-inspired central preview and movable docks;
separate base/light/dark QSS, System theme following and Turkish Qt translations. GPL-3.0-or-later.
First delivery: real screen/window/platform-appropriate game capture, editable text/image layers,
scene/project persistence, native YoungLion login and multi-account management. No audio mixer,
recording, encoders, direct/relay streaming or YouTube in this delivery.

Build/test targets are Windows 11 x64, Linux x64 Wayland/X11 and macOS Intel/Apple Silicon. Hardware
capture QA on all three is distinct from headless tests; live OAuth requires registered
native/broker clients and deployed functions. Never claim those prerequisites have been verified
merely from mock/provider contract tests.

Use bounded validated inputs, actual GPU composition, RAII libobs ownership and explicit capture
capabilities/errors. Do not silently capture the full display when a selected window disappears.
Persist projects atomically, never credentials. Native tokens belong in OS keychain, no plaintext
fallback. Native linking reauthenticates the browser's YoungLion subject before delegating platform
OAuth, preserving the existing browser proof and owner confirmation.
