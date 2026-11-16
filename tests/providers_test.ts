import assert from "node:assert/strict";
import { Provider } from "../supabase/functions/_shared/providers.ts";

const credentials = {
  clientId: "cast-client", clientSecret: "server-secret",
  redirectUri: "https://cast.example/callback/kick",
};

Deno.test("Kick authorization requires S256 and never exposes the client secret", () => {
  const provider = new Provider("kick", credentials);
  assert.throws(() => provider.authorizationUrl("state"));
  const url = new URL(provider.authorizationUrl("state", "challenge"));
  assert.equal(url.origin, "https://id.kick.com");
  assert.equal(url.searchParams.get("code_challenge_method"), "S256");
  assert.equal(url.searchParams.get("scope"), "user:read channel:read streamkey:read");
  assert.equal(url.searchParams.get("state"), "state");
  assert.equal(url.searchParams.has("client_secret"), false);
});

Deno.test("Kick maps the authenticated stable ID without trusting email or query IDs", async () => {
  const provider = new Provider("kick", credentials, (url, init) => {
    assert.equal(String(url), "https://api.kick.com/public/v1/users");
    assert.equal(new Headers(init?.headers).get("Authorization"), "Bearer access");
    return Promise.resolve(Response.json({
      data: [{ user_id: 123, name: "Renamed", email: "unverified@example.com", profile_picture: "https://cdn.example/avatar" }], message: "OK",
    }));
  });
  assert.deepEqual(await provider.account("access"), {
    id: "123", name: "Renamed", picture: "https://cdn.example/avatar",
  });
});

for (const data of [[], [{ user_id: 1 }, { user_id: 2 }], [{ user_id: 0 }], [{ user_id: 9007199254740992 }]]) {
  Deno.test(`Kick rejects ambiguous or invalid identities ${JSON.stringify(data)}`, async () => {
    const provider = new Provider("kick", credentials, () => Promise.resolve(Response.json({ data })));
    await assert.rejects(() => provider.account("access"), /provider_response/);
  });
}

Deno.test("authorization code exchange uses server credentials and PKCE verifier", async () => {
  const provider = new Provider("kick", credentials, (url, init) => {
    assert.equal(String(url), "https://id.kick.com/oauth/token");
    const body = new URLSearchParams(String(init?.body));
    assert.equal(body.get("client_secret"), "server-secret");
    assert.equal(body.get("code_verifier"), "verifier");
    assert.equal(body.get("grant_type"), "authorization_code");
    return Promise.resolve(Response.json({ access_token: "access", refresh_token: "refresh", expires_in: 7200, scope: "user:read channel:read streamkey:read", token_type: "bearer" }));
  });
  const grant = await provider.exchange("code", "verifier");
  assert.equal(grant.accessToken, "access");
  assert.equal(grant.refreshToken, "refresh");
  assert.deepEqual(grant.scopes, ["user:read", "channel:read", "streamkey:read"]);
});

Deno.test("Twitch validation rejects a token from another developer application", async () => {
  const provider = new Provider("twitch", credentials, () => Promise.resolve(Response.json({ client_id: "other-client", user_id: "123", scopes: [], expires_in: 100 })));
  await assert.rejects(() => provider.validate("access", "123"), /provider_identity/);
});

Deno.test("provider redirects and oversized bodies are rejected without following redirects", async () => {
  const redirect = new Provider("kick", credentials, (_url, init) => {
    assert.equal(init?.redirect, "error");
    return Promise.resolve(new Response(null, { status: 302, headers: { location: "https://evil.example" } }));
  });
  await assert.rejects(() => redirect.account("access"));
  const oversized = new Provider("kick", credentials, () => Promise.resolve(new Response("x".repeat(70000))));
  await assert.rejects(() => oversized.account("access"), /provider_response/);
});

Deno.test("provider failures return safe errors without leaking response secrets", async () => {
  const provider = new Provider("kick", credentials, () => Promise.resolve(Response.json({ error: "invalid_grant", secret: "never-print" }, { status: 400 })));
  await assert.rejects(() => provider.refresh("old"), (error: Error) => error.message === "reauth_required");
});
