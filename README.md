# YoungLion Cast

Twitch/Kick account infrastructure for a lightweight Qt/C++/Rust broadcasting application. This
first delivery implements OAuth and multiple publishing accounts, using Supabase Auth, Postgres and
Deno Edge Functions. Capture, encoding, the native UI, YouTube and media relay are subsequent
deliverables.

- Twitch sign-in uses Supabase's built-in provider.
- Kick sign-in uses `custom:kick` and a normalized UserInfo adapter.
- Publishing accounts use separate OAuth grants, explicit confirmation and atomic ownership.
- Credentials are AES-256-GCM encrypted in an unexposed schema. No provider tokens are returned by
  connection APIs.
- Scheduled work validates, refreshes and revokes credentials with durable leases.

## Start here

1. `npm ci`
2. `npm run verify` — unit checks; database tests are explicitly skipped unless configured.
3. `npm run test:integration` — Docker-based disposable Postgres/PostgREST integration tests.
4. Follow [setup](docs/setup.md), [API](docs/api.md) and [architecture](docs/architecture.md).

See the [verification record](docs/verification.md) for executed checks and integration limits.

The Account site's source and OAuth developer credentials were not supplied. The backend and
[typed integration client](clients/account.ts) are prepared; they are **not deployed or integrated
into the live Account website**. Browser linking requires a first-party API route with cookie
support, as documented in setup. Production OAuth sign-in, email-less onboarding and the platform
stream-key APIs have not been live-verified. Neither the native desktop nor video streaming is
implemented in this phase.
