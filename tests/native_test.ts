import assert from "node:assert/strict";
import { createHandler } from "../supabase/functions/_shared/handlers.ts";
import { digest, TokenVault } from "../supabase/functions/_shared/crypto.ts";
import { Provider } from "../supabase/functions/_shared/providers.ts";
import type { Store } from "../supabase/functions/_shared/store.ts";
const owner = "00000000-0000-4000-8000-000000000001";
const session = "00000000-0000-4000-8000-000000000002";
const id = "00000000-0000-4000-8000-000000000003";
const base = "https://api.example/functions/v1/platform-connections/v1";
const options = {
  enabled: true,
  origins: ["https://account.example"],
  completeUrl: "https://account.example/connections",
  vault: new TokenVault("k", { k: btoa("a".repeat(32)) }),
  authenticate: () => Promise.resolve({ id: owner, sessionId: session, identities: [] }),
  provider: (name: "kick" | "twitch") =>
    new Provider(name, {
      clientId: "client",
      clientSecret: "secret",
      redirectUri: base + "/oauth/callback/" + name,
    }),
};
function post(path: string, data: unknown) {
  return new Request(base + path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data),
  });
}
Deno.test("native handoff separates browser ticket from private polling proof", async () => {
  let payload: Record<string, unknown> = {};
  const store: Store = {
    rpc<T>(action: string, _owner: string | null, data = {}) {
      if (action === "rate" || action === "admit") return Promise.resolve(true as T);
      assert.equal(action, "native_start");
      payload = data;
      return Promise.resolve({ request_id: id } as T);
    },
  };
  const r = await createHandler({ ...options, store })(
    post("/native/start", { provider: "kick", proof_challenge: "a".repeat(64) }),
  );
  assert.equal(r.status, 200);
  const json = await r.json();
  assert.match(json.ticket, /^[A-Za-z0-9_-]{43}$/);
  assert.equal(payload.ticket_hash, await digest(json.ticket));
  assert.equal(payload.proof_hash, "a".repeat(64));
  assert.equal(payload.session_id, session);
  assert.equal(r.headers.has("Set-Cookie"), false);
});
Deno.test("native browser authorize establishes existing browser-bound OAuth cookie", async () => {
  const calls: string[] = [];
  const store: Store = {
    rpc<T>(action: string, _owner: string | null) {
      calls.push(action);
      if (action === "native_claim") return Promise.resolve({ provider: "kick" } as T);
      if (action === "native_bind") return Promise.resolve({ id, expires_at: "2099-01-01" } as T);

      return Promise.resolve(true as T);
    },
  };
  const r = await createHandler({ ...options, store })(
    post("/native/authorize", { request_id: id, ticket: "b".repeat(43) }),
  );
  assert.equal(r.status, 200);
  assert.match(r.headers.get("Set-Cookie")!, /Secure; HttpOnly; SameSite=Lax/);
  assert.ok(calls.includes("native_bind"));
});
Deno.test("native status hashes proof and never accepts malformed handoffs", async () => {
  let seen = false;
  const store: Store = {
    rpc<T>(action: string, _owner: string | null, data: Record<string, unknown> = {}) {
      if (action === "native_status") {
        seen = true;
        assert.equal(data.proof_hash, awaitDigest);
        return Promise.resolve({ status: "waiting" } as T);
      }
      return Promise.resolve(true as T);
    },
  };
  const awaitDigest = await digest("p".repeat(43));
  const h = createHandler({ ...options, store });
  assert.equal(
    (await h(post("/native/status", { request_id: id, proof: "p".repeat(43) }))).status,
    200,
  );
  assert.ok(seen);
  assert.equal(
    (await h(post("/native/start", { provider: "kick", proof_challenge: "bad" }))).status,
    400,
  );
});
