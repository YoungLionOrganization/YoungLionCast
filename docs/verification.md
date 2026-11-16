# Verification record — 2026-11-17

## Executed locally

- Clean `npm ci`: passed. npm manages pinned development packages; Deno does not rewrite its
  npm-installed runtime during tests.
- `npm run verify`: type check, lint and format check passed; 29 unit tests passed. The two
  database-dependent tests are explicitly skipped here without their environment.
- `npm run test:integration`: both database and full HTTP/PostgREST scenarios passed against
  isolated PostgreSQL 17 and PostgREST 13 containers. The runner removed its containers.
- A shared authorization URL was rejected without the initiating browser proof; the matching browser
  then completed the same transaction. Cross-owner confirmation was denied.
- Two Kick and two Twitch accounts were connected under one owner; disconnect and credential cleanup
  were exercised with actual owner RLS and service-role permissions.

## Independent review and corrections

A fresh reviewer inspected the entire new repository. These findings were reproduced as failing
tests and corrected in the same fix pass:

1. Transferable OAuth state: callbacks now require an HttpOnly host-only browser proof, its hash and
   the originating live YoungLion session before code exchange.
2. Lost rotating credentials: new pairs are checkpointed before subsequent validation; fenced grants
   become revocation work and cannot reactivate disconnected accounts.
3. Terminal Twitch errors: invalid refresh requires reauthorization; already-invalid access-token
   revocation completes cleanup instead of retrying forever.
4. Failed initial persistence: queue the full pair separately; fallback attempts both Kick tokens
   independently even when one revocation fails.

The refresh-scope finding was corrected too: omitted unchanged scopes are retained; explicitly
reduced/empty scopes remain authoritative. No review minors were deferred. Regression tests and the
full suite verified the fixes; no second independent review is claimed.

## Decisions and limits

- A separate YoungLion Cast repository was created because Account source was absent; the user
  authorized choosing a name. Existing workspace projects were not modified.
- Browser binding requires a first-party/same-site API endpoint. A Caddy example is supplied;
  hosting/DNS/proxy provisioning was not performed. Omitting browser proof is not a fallback.
- Real developer apps, live OAuth/Edge execution and provider-specific revocation behavior were
  unavailable for verification. Provider HTTP and Supabase user verification are controlled
  fixtures; Postgres/PostgREST and the HTTP connection API are real.
- Account frontend registration, session sign-out and deletion-drain integration remain required
  before production. The backend cannot replace unprovided Account code.
- Native capture/encoding, desktop OIDC, stream-key retrieval, YouTube and direct/relay broadcasting
  are outside this first delivery. No OBS performance comparison was made.
- No hosted migration, provider setup, function deployment, DNS change or push was performed. The
  work stays on local branch `feat/platform-oauth`.
