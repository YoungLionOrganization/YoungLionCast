import assert from "node:assert/strict";
import { createHandler } from "../supabase/functions/_shared/handlers.ts";
import { digest, TokenVault } from "../supabase/functions/_shared/crypto.ts";
import { Provider } from "../supabase/functions/_shared/providers.ts";
import type { Store } from "../supabase/functions/_shared/store.ts";

Deno.test("failed initial persistence retains or independently revokes both Kick tokens", async () => {
  const vault = new TokenVault("test", { test: btoa("a".repeat(32)) });
  const state = "s".repeat(43), id = crypto.randomUUID(), owner = crypto.randomUUID();
  const revoked: string[] = [];
  let durable = false;
  const tx = {
    id,
    owner_id: owner,
    provider: "kick",
    verifier: await vault.seal({ verifier: "verifier" }, `state:${owner}:kick:${id}`),
  };
  const store: Store = {
    rpc<T>(action: string) {
      if (action === "consume") return Promise.resolve(tx as T);
      if (action === "save_grant") {
        return Promise.reject(
          new Error("database temporarily unavailable"),
        );
      }
      if (action === "orphan") {
        durable = true;
        return Promise.resolve(true as T);
      }
      if (action === "fail_transaction") return Promise.resolve(true as T);
      throw new Error(`unexpected ${action}`);
    },
  };
  const provider = new Provider("kick", {
    clientId: "client",
    clientSecret: "secret",
    redirectUri: "https://cast.example/callback",
  }, (url) => {
    if (String(url).includes("/revoke")) {
      revoked.push(String(url));
      return Promise.resolve(Response.json({ error: "down" }, { status: 503 }));
    }
    return Promise.resolve(
      Response.json({
        access_token: "access",
        refresh_token: "refresh",
        expires_in: 3600,
        scope: "user:read channel:read streamkey:read",
      }),
    );
  });
  const handler = createHandler({
    enabled: true,
    origins: [],
    completeUrl: "https://account.example",
    store,
    vault,
    authenticate: () => Promise.reject(),
    provider: () => provider,
  });
  const cookie = `__Host-ylcast_${(await digest(state)).slice(0, 32)}=${"b".repeat(43)}`;
  await handler(
    new Request(
      `https://cast.example/functions/v1/platform-connections/v1/oauth/callback/kick?state=${state}&code=code`,
      { headers: { Cookie: cookie } },
    ),
  );
  assert.ok(
    durable ||
      (revoked.some((x) => x.includes("token=access")) &&
        revoked.some((x) => x.includes("token=refresh"))),
    "the refresh token must never be silently dropped",
  );
});
