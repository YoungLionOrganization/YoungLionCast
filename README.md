# YoungLion Cast

An OBS-inspired scene editor built with Qt 6.12 Widgets, C++20 and libobs 33.0.1. Arrange scenes,
position and crop sources, add text or images, and save projects locally. The native preview
supports capture through the desktop platform's capture backend. The account panel manages multiple
Twitch and Kick publishing accounts under one YoungLion Account.

Build the editor and configure native sign-in with the [desktop guide](docs/native.md). See the
[license and dependency notices](third_party/NOTICE.md) before distributing a bundle.

The Supabase backend uses separate OAuth grants for login and publishing. Twitch login uses
Supabase's built-in provider. Kick login uses `custom:kick` and a normalized UserInfo adapter.
Publishing credentials are encrypted and managed server-side. Native linking requires the same
YoungLion owner in the system browser and on the desktop.

- `npm ci && npm run verify` runs backend checks and unit tests.
- `npm run test:integration` runs integration tests on disposable Postgres and PostgREST.
- See [setup](docs/setup.md) and the [API](docs/api.md) for Account-web configuration.

The current application delivers capture and scene editing. Live account verification requires a
deployed staging backend, an Account frontend and registered OAuth apps. Broadcast output is a
separate delivery, with shared encoding and direct connections described in the
[transport decision](docs/adr/0001-direct-multistream.md).
