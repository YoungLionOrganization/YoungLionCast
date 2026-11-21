# Setup and deployment

## Prerequisites

- Node 24+, pinned Deno from `npm ci`, Docker for integration tests.
- Supabase CLI 3.2.0 (`npx supabase@3.2.0 --help`) or compatible CLI 2.83+.
- A staging Supabase project with the existing YoungLion Account schema, including
  `yl_profiles.user_id` and `account_status`. The backend allows publishing connections only for
  `active` profiles, and checks the session still exists in Auth.
- Separate Twitch/Kick developer applications for **login** and **publishing**, and separate
  applications for staging and production. A provider account has one YoungLion owner.
- An Account frontend page that uses `clients/account.ts`. No page is assumed to exist merely
  because its URL appears in `.env.example`.

No hosted changes are executed by `npm ci`, verification, migrations on disk or the dry-run script.

## 1. Local verification

```sh
npm ci
npm run verify
npm run test:integration
```

The integration runner creates and removes its own Docker network/containers. It uses PostgreSQL and
PostgREST images pinned by digest. Third-party OAuth and Auth verification are controlled test
boundaries; Postgres, RLS, service-role RPCs, PostgREST and the HTTP client/router are real. Never
point the test environment at a hosted project. Tests reject nonlocal databases and any database
name other than `cast_test`.

On hosts with enterprise system CAs, set `DENO_TLS_CA_STORE=system`; do not disable TLS
verification.

## 2. Apply the migration to staging

Inspect CLI help for your installed version, then link the **staging** project and apply the
checked-in migration using the CLI's documented `db push` command. CLI 3 requires an explicit hosted
target. Do not apply the fixture `tests/fixtures/auth.sql` to Supabase.

The migration adds `yl_cast_connections`, `yl_cast_private`, a service-only RPC and an identity
ownership trigger. It backfills Twitch/Kick login ownership and fails atomically if identities
already have conflicting owners. Resolve conflicts explicitly; never delete identities to force a
successful migration. Existing Account tables are neither replaced nor recreated.

## 3. Configure function secrets

Copy `.env.example` to an ignored `.env` and populate it outside chat/logs. Generate independent
random secrets with an OS CSPRNG: 32 random bytes encoded as base64 for each `CAST_TOKEN_KEYS`
entry, and at least 32 random bytes for `CAST_MAINTENANCE_SECRET`. Keys never belong in frontend
bundles.

Functions accept the explicitly configured `CAST_SUPABASE_*` keys or hosted
`SUPABASE_PUBLISHABLE_KEYS` / `SUPABASE_SECRET_KEYS` JSON maps. Use modern keys, not a server key in
a browser. Upload secrets with the documented `supabase secrets set --env-file .env` command.

`CAST_COMPLETE_URL` must point to the real authenticated connection-confirmation page and its origin
must appear in `CAST_ALLOWED_ORIGINS`. OAuth callbacks never accept arbitrary redirect URLs. Set
`CAST_PUBLIC_API_URL` to a first-party HTTPS API origin, such as a reverse proxy under the Account
origin or a same-site custom API domain. It must route `/functions/v1/platform-connections/*` to
this function and preserve Cookie/Set-Cookie headers. Use this public origin when constructing
`CastAccountClient`, and configure the publishing-provider callbacks on the same origin.
`deploy/Caddyfile.example` provides a narrowly routed reverse-proxy example; provision its DNS/TLS
and replace the example domain before use. It does not proxy media or other Supabase APIs.

This is a deployment prerequisite: `POST /oauth/start` sets an HttpOnly, Secure, SameSite=Lax,
host-only cookie with a per-transaction browser proof. The authenticated fetch includes credentials;
the callback requires that proof and the originating live YoungLion session. The proof is never
included in the provider URL. A shared authorization URL therefore cannot bind another browser's
account. A cross-site `*.supabase.co` API may have its cookies blocked by browsers; do not bypass
the check or weaken SameSite to work around a missing first-party route.

Native sign-in uses a public OAuth client and the system browser. Follow [native setup](native.md)
for the registered loopback, `CAST_OAUTH_CLIENT_IDS`, and the Account-web handoff.

Keep `CAST_ENABLED=false` until deployment, provider configuration and the confirmation page are
ready. The switch disables new API work, including maintenance; drain cleanup jobs before a planned
shutdown. For an incident requiring immediate isolation, disable first and resume maintenance once
the incident is contained.

## 4. Deploy the functions

Deploy `platform-connections`, `kick-userinfo` and `oauth-maintenance` to staging using the CLI's
documented functions deployment commands. The committed `verify_jwt=false` settings are deliberate:
callbacks validate single-use state plus browser proof and a live originating session, Kick UserInfo
validates a Kick bearer, maintenance validates its own secret, and protected connection routes
verify the YoungLion token with Auth plus the current database session and profile state. Never
expose the RPC to `anon` or `authenticated`.

## 5. Login providers

**Twitch:** Dashboard → Authentication → Providers → Twitch. Add the _login_ client ID/secret, copy
the displayed callback into the Twitch application, and allowlist the Account frontend's post-login
redirect URL in Supabase. Keep the initial scope at `user:read:email`.

**Kick:** The login application uses `user:read`, S256 PKCE and the callback displayed by Supabase.
Create it in Dashboard → Providers → New Provider → OAuth2 using the values emitted by:

```sh
npx deno run --env-file=.env --allow-env scripts/configure-auth.ts
```

Default output redacts the secret and performs no write. Once reviewed, the same script with
`--allow-net --apply` creates `custom:kick` through the Auth admin API. If it already exists, update
it in the Dashboard. Never disable PKCE or supply the raw Kick users endpoint as UserInfo. The
adapter returns a string `sub`, `name`, optional `picture`, and no invented email/verification
claim.

Verify `custom:kick` is enabled in the Dashboard after creation. Allow users without a provider
email to follow the existing Account onboarding restrictions. Source-level integration with those
restrictions remains necessary before making the button available on the live Account site.

## 6. Publishing applications

Use **different client IDs** from login:

| Platform | Callback                                                                         | Permissions                             |
| -------- | -------------------------------------------------------------------------------- | --------------------------------------- |
| Twitch   | `CAST_PUBLIC_API_URL/functions/v1/platform-connections/v1/oauth/callback/twitch` | `channel:read:stream_key`               |
| Kick     | `CAST_PUBLIC_API_URL/functions/v1/platform-connections/v1/oauth/callback/kick`   | `user:read channel:read streamkey:read` |

Put those IDs/secrets in `CAST_PUBLISH_*`. Login permission is never inferred from a publishing
connection. For another account, authenticate that account in the system browser; Kick account
selection must not be assumed to support Twitch's `force_verify` parameter. Always show the actual
returned identity before confirmation.

The existence of `streamkey:read` does not prove the desired Kick stream-key operation works for the
registered application. Live-verify it before the media phase; no raw stream-key endpoint is exposed
by this delivery.

## 7. Scheduled validation and cleanup

Enable Supabase Cron, pg_net and Vault in staging. Put `cast_functions_url` (ending in
`/functions/v1`) and `cast_maintenance_secret` (matching the Edge secret) into Vault using the
Dashboard. Then run `supabase/operations/schedule.sql` using an administrative connection.

The job runs every minute, leases at most ten credentials and performs network work outside database
transactions. Active credentials are validated at 45-minute intervals; refresh begins near expiry.
Backoff is capped at one hour. Monitor the due backlog and `last_error`/`attempts` in
`yl_cast_private.yl_cast_credentials` through an administrative connection. At sustained load this
worker must be scaled **before** the oldest due validation approaches fifteen minutes; otherwise
Twitch's hourly validation requirement cannot be guaranteed. Also monitor `cron.job_run_details` and
`net._http_response` failures. Never expose credential rows through the Data API.

The Account frontend must call `registerTwitchLogin` immediately after Twitch sign-in and before
allowing a long-lived Cast session. Supabase does not persist or refresh provider tokens itself.
Missing registration cannot be repaired by this standalone backend without the Account login
integration. A rejected Twitch login grant blocks that session in Cast; the Account frontend must
also sign that session out through Supabase. No claim is made that this revokes sessions in other
YoungLion products that have not integrated this admission policy.

## 8. Rollout, rotation and recovery

- Verify both provider flows, cancellation, email-less Kick onboarding, two accounts per platform,
  reconnect and disconnect in staging; run Supabase security/performance advisors.
- Repeat with production-specific application IDs, add the UI behind a feature flag, then enable
  Cast. Do not repoint staging callbacks or secrets to production.
- Rotate vault encryption by adding a new key ID while retaining old decrypt keys. Credentials are
  reencrypted as they are maintained. Do not remove old keys while any envelope still uses them.
- A failed external token exchange or rotating refresh can have succeeded upstream even if its
  response was lost. Require reauthorization instead of claiming exactly-once behavior.
- On rollback, disable new entry points/UI and provider buttons, retain the additive schema and
  encryption keys, and drain outstanding revocations. Do not drop credential tables as rollback.
- Disconnect publishing grants before the existing Account deletion pipeline deletes the user; that
  pipeline's integration is required because cascading deletion otherwise loses revocation
  credentials. The existing Account source was not supplied.

## Primary references

- [Supabase Twitch](https://supabase.com/docs/guides/auth/social-sign-in/auth-twitch)
- [Custom providers](https://supabase.com/docs/guides/auth/custom-oauth-providers)
- [Provider token lifecycle](https://supabase.com/docs/guides/auth/social-sign-in)
- [Kick OAuth](https://docs.kick.com/getting-started/generating-tokens-oauth2-flow)
- [Twitch validation](https://dev.twitch.tv/docs/authentication/validate-tokens/)
