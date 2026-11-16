import assert from "node:assert/strict";
import postgres from "postgres";
import { createHandler, createMaintenanceHandler } from "../supabase/functions/_shared/handlers.ts";
import { TokenVault } from "../supabase/functions/_shared/crypto.ts";
import { HttpStore } from "../supabase/functions/_shared/store.ts";
import { Provider } from "../supabase/functions/_shared/providers.ts";
import { CastAccountClient } from "../clients/account.ts";
import { AppError } from "../supabase/functions/_shared/http.ts";

const database = Deno.env.get("CAST_TEST_DATABASE_URL");
const rest = Deno.env.get("CAST_TEST_REST_URL");
Deno.test({
  name:
    "HTTP + real PostgREST/Postgres: two accounts per platform, confirmation, ownership and cleanup",
  ignore: !database || !rest,
  fn: async () => {
    const dbUrl = new URL(database!);
    if (
      !["localhost", "127.0.0.1", "postgres"].includes(dbUrl.hostname) ||
      dbUrl.pathname !== "/cast_test"
    ) throw new Error("Disposable local database required");
    const sql = postgres(database!, { max: 2 });
    const owner = "00000000-0000-4000-8000-000000000010",
      other = "00000000-0000-4000-8000-000000000011",
      session = "00000000-0000-4000-8000-000000000012",
      otherSession = "00000000-0000-4000-8000-000000000013";
    await sql`insert into auth.users(id) values(${owner}),(${other}) on conflict do nothing`;
    await sql`insert into public.yl_profiles(user_id,account_status) values(${owner},'active'),(${other},'active') on conflict(user_id) do update set account_status='active'`;
    await sql`insert into auth.sessions(id,user_id) values(${session},${owner}),(${otherSession},${other}) on conflict do nothing`;
    // Dedicated test users only; leave all other fixtures untouched.
    await sql`delete from public.yl_cast_connections where owner_id in (${owner},${other})`;
    await sql`delete from yl_cast_private.yl_cast_owners where owner_id in (${owner},${other})`;
    await sql`delete from yl_cast_private.yl_cast_transactions where owner_id in (${owner},${other})`;
    await sql`delete from yl_cast_private.yl_cast_credentials where owner_id in (${owner},${other})`;
    const vault = new TokenVault("test", { test: btoa("a".repeat(32)) });
    // Bare PostgREST has no /rest/v1 gateway prefix; this is the only test transport adaptation.
    const store = new HttpStore(
      "http://127.0.0.1:55440",
      "test-only",
      (url, init) => fetch(String(url).replace("http://127.0.0.1:55440/rest/v1", rest!), init),
    );
    const revoked: string[] = [];
    const options = {
      store,
      vault,
      enabled: true,
      origins: ["https://account.example"],
      completeUrl: "https://account.example/connections",
      authenticate: (req: Request) => {
        const token = req.headers.get("Authorization");
        if (token !== `Bearer ${owner}` && token !== `Bearer ${other}`) {
          throw new AppError("unauthorized", 401);
        }
        const id = token.slice(7);
        return Promise.resolve({
          id,
          sessionId: id === owner ? session : otherSession,
          identities: [],
        });
      },
      provider: (name: "kick" | "twitch") =>
        new Provider(name, {
          clientId: "cast-client",
          clientSecret: "server-only",
          redirectUri:
            `https://project.example/functions/v1/platform-connections/v1/oauth/callback/${name}`,
        }, (input, init) => {
          const url = String(input);
          const auth = new Headers(init?.headers).get("Authorization") ?? "";
          if (url.includes("/revoke")) {
            revoked.push(url);
            return Promise.resolve(new Response(null, { status: 200 }));
          }
          if (url.includes("/token")) {
            const code = new URLSearchParams(String(init?.body)).get("code");
            return Promise.resolve(
              Response.json({
                access_token: `access-${code}`,
                refresh_token: `refresh-${code}`,
                expires_in: 3600,
                scope: name === "kick"
                  ? "user:read channel:read streamkey:read"
                  : ["channel:read:stream_key"],
              }),
            );
          }
          const id = auth.match(/access-(\d+)/)?.[1] ?? "0";
          if (url.endsWith("/validate")) {
            return Promise.resolve(
              Response.json({
                client_id: "cast-client",
                user_id: id,
                scopes: ["channel:read:stream_key"],
                expires_in: 3600,
              }),
            );
          }
          return Promise.resolve(Response.json({
            data: [
              name === "kick"
                ? { user_id: Number(id), name: `Kick ${id}` }
                : { id, display_name: `Twitch ${id}` },
            ],
          }));
        }),
    };
    const handler = createHandler(options);
    const server = Deno.serve({ hostname: "127.0.0.1", port: 0, onListen: () => {} }, handler);
    const url = `http://127.0.0.1:${server.addr.port}`;
    let browserCookie = "";
    const client = new CastAccountClient(
      url,
      "publishable",
      () => Promise.resolve(owner),
      async (input, init) => {
        const headers = new Headers(init?.headers);
        if (browserCookie) headers.set("Cookie", browserCookie);
        const response = await fetch(input, { ...init, headers });
        const cookie = response.headers.get("Set-Cookie");
        if (cookie) browserCookie = cookie.split(";")[0];
        return response;
      },
    );
    const clientOther = new CastAccountClient(url, "publishable", () => Promise.resolve(other));
    try {
      const ids: string[] = [];
      for (
        const [provider, code] of [["kick", "101"], ["kick", "102"], ["twitch", "201"], [
          "twitch",
          "202",
        ]] as const
      ) {
        const start = await client.start(provider);
        const state = new URL(start.authorization_url).searchParams.get("state")!;
        const callback =
          `${url}/functions/v1/platform-connections/v1/oauth/callback/${provider}?${new URLSearchParams(
            { state, code },
          )}`;
        const victimResponse = await fetch(callback);
        assert.equal(
          victimResponse.status,
          400,
          "a shared provider URL must not bind a different browser's account",
        );
        await victimResponse.body?.cancel();
        const response = await fetch(callback, { headers: { Cookie: browserCookie } });
        assert.equal(response.status, 200, await response.clone().text());
        assert.equal((await response.text()).includes(`access-${code}`), false);
        const pending = await client.pending(start.transaction_id);
        assert.equal(pending.account.id, code);
        await assert.rejects(() => clientOther.confirm(start.transaction_id), /not_found/);
        const connected = await client.confirm(start.transaction_id);
        ids.push(connected.id);
        const replay = await fetch(callback);
        assert.equal(replay.status, 400);
        await replay.body?.cancel();
      }
      assert.equal((await client.list()).connections.length, 4);
      assert.equal((await clientOther.list()).connections.length, 0);
      await assert.rejects(() => clientOther.disconnect(ids[0]), /not_found/);
      const raw =
        await sql`select grant_data from yl_cast_private.yl_cast_credentials where owner_id=${owner}`;
      assert.equal(JSON.stringify(raw).includes("refresh-101"), false);
      await client.disconnect(ids[0]);
      assert.equal(
        (await client.list()).connections.find((c) => c.id === ids[0])?.status,
        "disconnected",
      );
      const maintenance = createMaintenanceHandler({
        ...options,
        maintenanceSecret: "worker-secret",
      });
      const response = await maintenance(
        new Request("https://project.example/maintenance", {
          method: "POST",
          headers: { "X-Cast-Maintenance": "worker-secret" },
        }),
      );
      assert.equal(response.status, 200);
      await response.body?.cancel();
      assert.ok(revoked.length >= 1);
      assert.equal(
        (await sql`select id from yl_cast_private.yl_cast_credentials where connection_id=${
          ids[0]
        }`).length,
        0,
      );
    } finally {
      await server.shutdown();
      await sql.end();
    }
  },
});
