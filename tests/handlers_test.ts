import assert from "node:assert/strict";
import {
  createHandler,
  createKickUserInfoHandler,
  createMaintenanceHandler,
} from "../supabase/functions/_shared/handlers.ts";
import { Provider } from "../supabase/functions/_shared/providers.ts";
import { TokenVault } from "../supabase/functions/_shared/crypto.ts";
import type { Store } from "../supabase/functions/_shared/store.ts";

const vault = new TokenVault("test", { test: btoa("a".repeat(32)) });
const owner = "00000000-0000-4000-8000-000000000001";
const session = "00000000-0000-4000-8000-000000000003";
const base = "https://project.supabase.co/functions/v1/platform-connections/v1";
const options = {
  enabled: true,
  origins: ["https://account.example"],
  completeUrl: "https://account.example/connections",
  vault,
  authenticate: () => Promise.resolve({ id: owner, sessionId: session, identities: [] }),
  provider: (name: "kick" | "twitch", _kind?: "login" | "publishing") =>
    new Provider(name, {
      clientId: "client",
      clientSecret: "secret",
      redirectUri: `${base}/oauth/callback/${name}`,
    }),
};

Deno.test("protected routes fail closed and reject cross-origin mutations", async () => {
  const store: Store = {
    rpc() {
      throw new Error("must not access database");
    },
  };
  const handler = createHandler({
    ...options,
    store,
    authenticate: () => Promise.reject(new Error("bad jwt")),
  });
  assert.equal((await handler(new Request(`${base}/connections`))).status, 401);
  assert.equal(
    (await handler(
      new Request(`${base}/oauth/start`, {
        method: "POST",
        headers: { Origin: "https://evil.example", "Content-Type": "application/json" },
        body: '{"provider":"kick"}',
      }),
    )).status,
    403,
  );
  assert.equal(
    (await createHandler({ ...options, store, enabled: false })(new Request(`${base}/connections`)))
      .status,
    503,
  );
});

Deno.test("start returns state only in provider URL and binds owner and encrypted verifier", async () => {
  let seen: Record<string, unknown> = {};
  const store: Store = {
    rpc<T>(action: string, principal: string | null, data = {}) {
      if (action === "rate" || action === "admit") return Promise.resolve(true as T);
      assert.equal(action, "start");
      assert.equal(principal, owner);
      seen = data;
      return Promise.resolve({ id: "tx", expires_at: "2099-01-01T00:00:00Z" } as T);
    },
  };
  const response = await createHandler({ ...options, store })(
    new Request(`${base}/oauth/start`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: '{"provider":"kick"}',
    }),
  );
  assert.equal(response.status, 200);
  const body = await response.json();
  const url = new URL(body.authorization_url);
  assert.match(url.searchParams.get("state")!, /^[A-Za-z0-9_-]{43}$/);
  assert.notEqual(seen.state_hash, url.searchParams.get("state"));
  assert.equal(JSON.stringify(body).includes("verifier"), false);
  assert.equal(typeof seen.verifier, "object");
});

Deno.test("kick userinfo requires a Kick bearer and never accepts a caller chosen user ID", async () => {
  const store: Store = {
    rpc<T>() {
      return Promise.resolve(true as T);
    },
  };
  const handler = createKickUserInfoHandler({
    ...options,
    store,
    provider: (name) =>
      new Provider(
        name,
        { clientId: "", clientSecret: "", redirectUri: "" },
        () =>
          Promise.resolve(
            Response.json({
              data: [{ user_id: 123, name: "Caster", email: "unverified@example.com" }],
            }),
          ),
      ),
  });
  assert.equal((await handler(new Request("https://project.example/kick-userinfo"))).status, 401);
  const response = await handler(
    new Request("https://project.example/kick-userinfo", {
      headers: { Authorization: "Bearer kick-token" },
    }),
  );
  assert.deepEqual(await response.json(), { sub: "123", name: "Caster" });
  assert.equal(response.headers.get("cache-control"), "no-store");
  assert.equal(
    (await handler(
      new Request("https://project.example/kick-userinfo?user_id=9", {
        headers: { Authorization: "Bearer kick-token" },
      }),
    )).status,
    400,
  );
});

Deno.test("callback rejects invalid state before token exchange and returns no secret details", async () => {
  const store: Store = {
    rpc() {
      return Promise.reject(new Error("invalid_state"));
    },
  };
  const response = await createHandler({ ...options, store })(
    new Request(`${base}/oauth/callback/kick?state=bad&code=secret-code`),
  );
  assert.equal(response.status, 400);
  assert.equal((await response.text()).includes("secret-code"), false);
  assert.equal(response.headers.get("referrer-policy"), "no-referrer");
});

Deno.test("maintenance rejects public callers before acquiring any lease", async () => {
  const store: Store = {
    rpc() {
      throw new Error("must not touch credentials");
    },
  };
  const handler = createMaintenanceHandler({
    ...options,
    store,
    maintenanceSecret: "maintenance-secret",
  });
  assert.equal(
    (await handler(new Request("https://project.example/maintenance", { method: "POST" }))).status,
    401,
  );
});
