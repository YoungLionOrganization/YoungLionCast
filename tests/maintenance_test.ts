import assert from "node:assert/strict";
import { createMaintenanceHandler } from "../supabase/functions/_shared/handlers.ts";
import { TokenVault } from "../supabase/functions/_shared/crypto.ts";
import { Provider } from "../supabase/functions/_shared/providers.ts";
import { credentialContext, type Store } from "../supabase/functions/_shared/store.ts";

const vault = new TokenVault("test", { test: btoa("a".repeat(32)) });
Deno.test("a disconnect during token rotation queues the fresh orphan for revocation", async () => {
  const credential = {
    id: "id",
    owner_id: "owner",
    provider: "kick",
    provider_user_id: "123",
    purpose: "publishing",
    kind: "publishing",
    lease: "lease",
    version: 1,
    grant_data: await vault.seal({
      accessToken: "old",
      refreshToken: "old-refresh",
      expiresAt: new Date(0).toISOString(),
      scopes: ["user:read", "channel:read", "streamkey:read"],
    }, credentialContext("owner", "kick", "id")),
  };
  let orphan: Record<string, unknown> | undefined;
  const store: Store = {
    rpc<T>(action: string, _owner: string | null, data = {}) {
      if (action === "lease") return Promise.resolve([credential] as T);
      if (action === "finish") return Promise.resolve(false as T);
      if (action === "checkpoint") return Promise.resolve(false as T);
      if (action === "orphan") {
        orphan = data;
        return Promise.resolve(true as T);
      }
      throw new Error(`unexpected ${action}`);
    },
  };
  const provider = new Provider(
    "kick",
    { clientId: "client", clientSecret: "secret", redirectUri: "https://project.example" },
    (url) =>
      Promise.resolve(Response.json(
        String(url).includes("/oauth/token")
          ? {
            access_token: "fresh",
            refresh_token: "fresh-refresh",
            expires_in: 3600,
            scope: "user:read channel:read streamkey:read",
          }
          : { data: [{ user_id: 123, name: "Name" }] },
      )),
  );
  const handler = createMaintenanceHandler({
    store,
    vault,
    enabled: true,
    origins: [],
    completeUrl: "https://account.example",
    authenticate: () => Promise.reject(),
    provider: () => provider,
    maintenanceSecret: "maintenance-secret",
  });
  const response = await handler(
    new Request("https://project.example/maintenance", {
      method: "POST",
      headers: { "X-Cast-Maintenance": "maintenance-secret" },
    }),
  );
  assert.equal(response.status, 200);
  assert.ok(orphan, "fresh tokens must not be lost when the lease was fenced by disconnect");
  assert.equal(JSON.stringify(orphan).includes("fresh-refresh"), false);
});

Deno.test("rotated credentials are durably checkpointed before subsequent validation can fail", async () => {
  const credential = {
    id: "id",
    owner_id: "owner",
    provider: "kick",
    provider_user_id: "123",
    purpose: "publishing",
    kind: "publishing",
    lease: "lease",
    version: 1,
    grant_data: await vault.seal({
      accessToken: "old",
      refreshToken: "old-refresh",
      expiresAt: new Date(0).toISOString(),
      scopes: ["user:read", "channel:read", "streamkey:read"],
    }, credentialContext("owner", "kick", "id")),
  };
  let persisted: Record<string, unknown> | undefined;
  const store: Store = {
    rpc<T>(action: string, _owner: string | null, data = {}) {
      if (action === "lease") return Promise.resolve([credential] as T);
      if (action === "checkpoint") {
        persisted = data;
        return Promise.resolve(true as T);
      }
      if (action === "retry") return Promise.resolve(true as T);
      throw new Error(`unexpected ${action}`);
    },
  };
  const provider = new Provider(
    "kick",
    { clientId: "client", clientSecret: "secret", redirectUri: "https://project.example" },
    (url) =>
      Promise.resolve(
        String(url).includes("/oauth/token")
          ? Response.json({
            access_token: "fresh",
            refresh_token: "rotated-refresh",
            expires_in: 3600,
            scope: "user:read channel:read streamkey:read",
          })
          : Response.json({ error: "upstream_down" }, { status: 503 }),
      ),
  );
  const handler = createMaintenanceHandler({
    store,
    vault,
    enabled: true,
    origins: [],
    completeUrl: "https://account.example",
    authenticate: () => Promise.reject(),
    provider: () => provider,
    maintenanceSecret: "worker",
  });
  await handler(
    new Request("https://project.example/maintenance", {
      method: "POST",
      headers: { "X-Cast-Maintenance": "worker" },
    }),
  );
  assert.ok(persisted, "a rotating refresh must be saved before provider validation");
});

Deno.test("cleanup uses the original login application's credentials", async () => {
  const credential = {
    id: "id",
    owner_id: "owner",
    provider: "twitch",
    provider_user_id: "123",
    purpose: "login",
    kind: "cleanup",
    lease: "lease",
    version: 1,
    grant_data: await vault.seal({
      accessToken: "old",
      refreshToken: "",
      expiresAt: new Date(0).toISOString(),
      scopes: [],
    }, credentialContext("owner", "twitch", "id")),
  };
  const store: Store = {
    rpc<T>(action: string) {
      return Promise.resolve((action === "lease" ? [credential] : true) as T);
    },
  };
  let requested = "";
  const handler = createMaintenanceHandler({
    store,
    vault,
    enabled: true,
    origins: [],
    completeUrl: "https://account.example",
    authenticate: () => Promise.reject(),
    maintenanceSecret: "maintenance-secret",
    provider: (_name, kind) => {
      requested = kind ?? "";
      return new Provider("twitch", {
        clientId: kind === "login" ? "login-client" : "publishing-client",
        clientSecret: "secret",
        redirectUri: "https://project.example",
      }, (_url, init) => {
        assert.equal(new URLSearchParams(String(init?.body)).get("client_id"), "login-client");
        return Promise.resolve(new Response(null, { status: 200 }));
      });
    },
  });
  await handler(
    new Request("https://project.example/maintenance", {
      method: "POST",
      headers: { "X-Cast-Maintenance": "maintenance-secret" },
    }),
  );
  assert.equal(requested, "login");
});
