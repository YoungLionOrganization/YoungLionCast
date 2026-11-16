# YoungLion Cast OAuth design

Approved scope: Twitch and Kick sign-in to YoungLion Account, separate multi-account publishing
connections, Supabase Auth/Postgres/Edge Functions, one YoungLion owner per provider identity.
Native Qt/C++/Rust capture/encoding and both direct/relay media modes are subsequent work. YouTube
and team sharing are excluded from this delivery.

Login uses Twitch's built-in provider and custom:kick with a bearer-authenticated UserInfo adapter.
Publishing OAuth uses separate developer applications, explicit account confirmation, one-use
10-minute state, and Kick S256 PKCE. Tokens are encrypted outside exposed schemas. Ownership is
atomic across login identities and publishing connections. Unverified emails never establish identity.

Refresh and revocation are durable and bounded; Twitch grants are validated initially and hourly.
No video travels through Edge Functions or Postgres. No unmeasured OBS performance claims.

Account frontend source and provider credentials are absent. Deliver a standalone backend and typed
integration client, plus reproducible configuration/deployment instructions. Do not replace the
existing Account website or change live Auth settings without the missing configuration.
