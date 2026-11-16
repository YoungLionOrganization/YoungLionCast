# Connection API

Base: `/functions/v1/platform-connections/v1`. Protected routes require
`Authorization: Bearer
<YoungLion access token>` and the Supabase publishable `apikey`. The server
resolves ownership from the verified subject and rejects inactive profiles, removed sessions and
disallowed native OAuth clients. A client-supplied owner is never accepted.

| Method | Route                                | Input / output                                                                              |
| ------ | ------------------------------------ | ------------------------------------------------------------------------------------------- |
| POST   | `/oauth/start`                       | `{provider:"kick"                                                                           |
| GET    | `/oauth/callback/:provider`          | Provider callback with code/state; returns a safe completion link                           |
| GET    | `/oauth/pending?transaction_id=UUID` | Owner-only actual account identity awaiting confirmation                                    |
| POST   | `/oauth/confirm`                     | `{transaction_id}` → connection metadata                                                    |
| GET    | `/connections?after=UUID`            | Up to 50 connections, `next_cursor` or null                                                 |
| POST   | `/connections/:id/reconnect`         | New authorization URL; the returned account must match the target                           |
| DELETE | `/connections/:id`                   | Immediately disabled; provider revocation queued durably                                    |
| POST   | `/login/twitch`                      | `{access_token,refresh_token?}` from the _login_ app; validated against the linked identity |

The login-registration route permits a current session during onboarding solely to register its
provider grant; this does not activate its profile or authorize publishing APIs. Expiry is obtained
from Twitch, never trusted from a request field.

Connection metadata includes ID, owner, provider, stable provider user ID, display name, picture,
scopes, expiry, timestamps and `connected|reauth_required|disconnected`. `connected` means OAuth
authorization is present, not that the channel is live or stream-key retrieval has been verified.
Lists include disconnected records to support reconnect/history. No public route returns provider
credentials, ciphertext or stream keys.

OAuth state lasts ten minutes, is stored hashed and is consumed atomically. Pending credentials are
encrypted and revoked after abandonment. Confirmation requires the same YoungLion owner and never
merges identities. The ownership registry intentionally retains an account claim after disconnect;
transfers require a separate verified support flow, outside v1. Confirm is idempotent within the
transaction lifetime; duplicate callbacks are rejected.

Error shape: `{error:{code:string}}`. Codes include `unauthorized`, `session_revoked`,
`account_inactive`, `client_not_allowed`, `not_found`, `invalid_state`, `owner_conflict`,
`account_mismatch`, `insufficient_scope`, `authorization_denied`, `rate_limited`,
`provider_rate_limited`, `provider_unavailable`, `database_unavailable`, `feature_disabled`. Never
render these as HTML. Translate them in the consumer. Errors contain no provider response body,
token or OAuth authorization code. Callback query strings must be redacted in infrastructure access
logs.

`kick-userinfo` is a separate GET endpoint for Supabase Auth. It accepts only a Kick bearer, has no
user-selected URL or user ID input, and returns standardized identity claims. Its bearer is not a
YoungLion JWT.

`oauth-maintenance` accepts POST with `X-Cast-Maintenance`. The secret is server-only and stored in
Vault for Cron. The response contains only processed/succeeded counts. Do not invoke it from a
browser or give its secret to a native application.
