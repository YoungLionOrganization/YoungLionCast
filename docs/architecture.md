# Architecture and boundaries

The Account authority remains Supabase Auth. Social login identities and third-party publishing
permissions are distinct. An Auth identity trigger and publishing confirmation share an atomic
ownership registry keyed by `(provider, stable provider user ID)`; neither email nor display name
establishes publishing ownership. This registry is private and has no automatic transfer operation.

Publishing OAuth progresses through `started → exchanging → pending → confirmed`, or `failed`. The
start response sets an HttpOnly host-only browser proof outside the authorization URL. A callback
requires that proof, its matching hash and the originating live session before consuming state or
exchanging a provider code. The public API must be first-party/same-site for browser cookie support;
native consumers will open the Account-web linking flow. Credentials are retained as encrypted
pending work immediately after token exchange and before fetching a profile. Only authenticated
owner confirmation exposes connection metadata. Every ciphertext is bound to its immutable
credential UUID, owner and provider with AES-GCM additional authenticated data. Separate key IDs
permit rotation.

The service RPC is invoker-security and executable only by `service_role`. Helpers that read Auth
sessions or coordinate Auth identity creation have fixed empty search paths, live in an unexposed
schema and cannot be called by public users. Only metadata is exposed, read-only under owner RLS.

Credential maintenance uses database leases, conditional versions and consistent metadata-before-
credential lock ordering. Disconnect invalidates a lease immediately. A token rotated by an
already-running worker is checkpointed durably before subsequent provider validation or final
metadata writes; a temporary validation failure therefore retries with the new refresh token. It
cannot reactivate the connection after disconnect; its new token is queued separately for
revocation. Failed network cleanup retains encrypted work with backoff. No database lock is held
while calling Twitch/Kick. See setup for backlog monitoring and integration limitations.

The Qt application signs in through the external browser with public-client PKCE S256 and stores its
refresh token in the system keychain. A desktop request creates a one-time browser ticket and an
independent polling proof. The broker requires the browser's authenticated YoungLion owner to match
the request owner before issuing the existing browser proof. Desktop confirmation verifies both live
sessions and the polling proof. See [the native setup guide](native.md).

The editor uses libobs for capture and composition. Only the active scene owns live sources. Qt
Widgets manages the preview surface and editor docks. The GPU preview does not copy frames through
Qt's CPU paint path.

The first broadcast release will share an encoder per compatible profile and open a direct
connection per destination. The [transport decision](adr/0001-direct-multistream.md) explains the
trade-off. Media will not traverse Supabase. Performance comparisons with OBS require measurements
on matching hardware, sources, and output settings.
