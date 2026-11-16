import assert from "node:assert/strict";
import {
  beginYoungLionLogin,
  CastAccountClient,
  finishYoungLionLogin,
} from "../clients/account.ts";

Deno.test("publishing client sends the current YoungLion bearer without any privileged key", async () => {
  const client = new CastAccountClient(
    "https://project.example",
    "publishable",
    () => Promise.resolve("user-token"),
    (_url, init) => {
      assert.equal(new Headers(init?.headers).get("authorization"), "Bearer user-token");
      assert.equal(new Headers(init?.headers).get("apikey"), "publishable");
      assert.deepEqual(JSON.parse(String(init?.body)), { provider: "kick" });
      return Promise.resolve(
        Response.json({
          transaction_id: "id",
          authorization_url: "https://id.kick.com/oauth/authorize",
          expires_at: "2099-01-01T00:00:00Z",
        }),
      );
    },
  );
  assert.equal((await client.start("kick")).transaction_id, "id");
});
Deno.test("YoungLion social login uses Supabase PKCE independently of publishing OAuth", async () => {
  const login = await beginYoungLionLogin(
    "https://project.example",
    "kick",
    "https://account.example/callback",
  );
  const url = new URL(login.authorizationUrl);
  assert.equal(url.pathname, "/auth/v1/authorize");
  assert.equal(url.searchParams.get("provider"), "custom:kick");
  assert.equal(url.searchParams.get("code_challenge_method"), "s256");
  assert.equal(url.searchParams.has("client_secret"), false);
  assert.equal(url.searchParams.get("code_challenge")?.includes("="), false);
  const result = await finishYoungLionLogin(
    "https://project.example",
    "publishable",
    "auth-code",
    login.verifier,
    (_url, init) => {
      assert.deepEqual(JSON.parse(String(init?.body)), {
        auth_code: "auth-code",
        code_verifier: login.verifier,
      });
      return Promise.resolve(
        Response.json({
          access_token: "younglion",
          refresh_token: "refresh",
          provider_token: "twitch-provider",
          user: { id: "owner" },
        }),
      );
    },
  );
  assert.equal(result.access_token, "younglion");
});
