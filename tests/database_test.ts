import assert from "node:assert/strict";
import postgres from "postgres";

const connection = Deno.env.get("CAST_TEST_DATABASE_URL");
export const ownerA = "00000000-0000-4000-8000-000000000001";
export const ownerB = "00000000-0000-4000-8000-000000000002";

Deno.test({
  name: "Postgres: atomic OAuth, ownership, RLS and fenced credential maintenance",
  ignore: !connection,
  fn: async () => {
    const sql = postgres(connection!, { max: 4, onnotice: () => {} });
    const testUrl = new URL(connection!);
    if (
      !["localhost", "127.0.0.1", "postgres"].includes(testUrl.hostname) ||
      testUrl.pathname !== "/cast_test"
    ) throw new Error("Only a local disposable cast_test database is allowed");
    try {
      const migrationConnection = await sql.reserve();
      try {
        await migrationConnection.unsafe(
          await Deno.readTextFile(new URL("./fixtures/auth.sql", import.meta.url)),
        );
        const migrations = [
          ...Deno.readDirSync(new URL("../supabase/migrations/", import.meta.url)),
        ].filter((x) => x.name.endsWith(".sql")).sort((a, b) => a.name.localeCompare(b.name));
        for (const file of migrations) {
          await migrationConnection.unsafe(
            await Deno.readTextFile(
              new URL(`../supabase/migrations/${file.name}`, import.meta.url),
            ),
          );
        }
      } finally {
        migrationConnection.release();
      }
      await sql`insert into auth.users(id) values (${ownerA}), (${ownerB}) on conflict do nothing`;
      const session = "00000000-0000-4000-8000-000000000003",
        sessionB = "00000000-0000-4000-8000-000000000004";
      await sql`insert into auth.sessions(id,user_id) values(${session},${ownerA}),(${sessionB},${ownerB}) on conflict do nothing`;
      await sql`insert into public.yl_profiles(user_id,account_status) values(${ownerA},'active'),(${ownerB},'active') on conflict(user_id) do update set account_status='active'`;
      const call = (
        action: string,
        owner: string | null,
        payload: Record<string, unknown> = {},
      ) => {
        return sql.begin(async (txsql) => {
          await txsql.unsafe("set local role service_role");
          const rows = await txsql`select public.yl_cast_rpc(${action}, ${owner}::uuid, ${
            sql.json(payload as postgres.JSONValue)
          }) as result`;
          return rows[0].result;
        });
      };
      const start = (owner = ownerA, provider = "kick", target?: string) =>
        call("start", owner, {
          provider,
          state_hash: crypto.randomUUID(),
          browser_hash: "browser",
          session_id: owner === ownerA ? session : sessionB,
          verifier: { opaque: true },
          target,
        });
      const complete = async (
        tx: { id: string; state_hash: string },
        id: string,
        owner = ownerA,
      ) => {
        await call("consume", null, {
          provider: "kick",
          state_hash: tx.state_hash,
          browser_hash: "browser",
        });
        await call("save_grant", owner, {
          transaction_id: tx.id,
          grant: { opaque: "ciphertext" },
          expires_at: new Date(Date.now() + 3600000).toISOString(),
          scopes: ["user:read", "channel:read", "streamkey:read"],
        });
        await call("ready", owner, {
          transaction_id: tx.id,
          account: { id, name: "Channel", picture: null },
        });
        return call("confirm", owner, { transaction_id: tx.id });
      };

      // Native tickets are browser-facing; the independent proof and native session poll.
      const browserSession = crypto.randomUUID();
      await sql`insert into auth.sessions(id,user_id) values(${browserSession},${ownerA})`;
      const native = await call("native_start", ownerA, {
        provider: "kick",
        proof_hash: "a".repeat(64),
        ticket_hash: "b".repeat(64),
        session_id: session,
      });
      await assert.rejects(() =>
        call("native_claim", ownerB, {
          request_id: native.request_id,
          ticket_hash: "b".repeat(64),
          session_id: sessionB,
        }), /not_found/);
      await assert.rejects(() =>
        call("native_claim", ownerA, {
          request_id: native.request_id,
          ticket_hash: "c".repeat(64),
          session_id: browserSession,
        }), /invalid_state/);
      await call("native_claim", ownerA, {
        request_id: native.request_id,
        ticket_hash: "b".repeat(64),
        session_id: browserSession,
      });
      const binding = {
        request_id: native.request_id,
        ticket_hash: "b".repeat(64),
        provider: "kick",
        state_hash: crypto.randomUUID(),
        browser_hash: "native-browser",
        session_id: browserSession,
        verifier: { opaque: true },
      };
      const nativeTx = await call("native_bind", ownerA, binding);
      await assert.rejects(() => call("native_bind", ownerA, binding), /invalid_state/);
      await assert.rejects(() =>
        call("native_status", ownerA, {
          request_id: native.request_id,
          proof_hash: "b".repeat(64),
          session_id: session,
        }), /invalid_state/);
      await assert.rejects(() =>
        call("native_status", ownerA, {
          request_id: native.request_id,
          proof_hash: "a".repeat(64),
          session_id: browserSession,
        }), /invalid_state/);
      assert.equal(
        (await call("native_status", ownerA, {
          request_id: native.request_id,
          proof_hash: "a".repeat(64),
          session_id: session,
        })).status,
        "waiting",
      );
      await call("native_cancel", ownerA, {
        request_id: native.request_id,
        proof_hash: "a".repeat(64),
        session_id: session,
      });
      assert.equal(
        (await call("native_status", ownerA, {
          request_id: native.request_id,
          proof_hash: "a".repeat(64),
          session_id: session,
        })).status,
        "cancelled",
      );
      await assert.rejects(
        () => call("confirm", ownerA, { transaction_id: nativeTx.id }),
        /invalid_state/,
      );

      const nativeToConfirm = await call("native_start", ownerA, {
        provider: "kick",
        proof_hash: "d".repeat(64),
        ticket_hash: "e".repeat(64),
        session_id: session,
      });
      const pendingNative = await call("native_bind", ownerA, {
        ...binding,
        request_id: nativeToConfirm.request_id,
        ticket_hash: "e".repeat(64),
        state_hash: crypto.randomUUID(),
      });
      const pollNative = {
        request_id: nativeToConfirm.request_id,
        proof_hash: "d".repeat(64),
        session_id: session,
      };
      const consumed = await call("consume", null, {
        provider: "kick",
        state_hash: pendingNative.state_hash,
        browser_hash: "native-browser",
      });
      assert.equal(consumed.native, true);
      await call("save_grant", ownerA, {
        transaction_id: pendingNative.id,
        grant: { opaque: "native" },
        expires_at: new Date(Date.now() + 3600000).toISOString(),
        scopes: ["user:read"],
      });
      await call("ready", ownerA, {
        transaction_id: pendingNative.id,
        account: { id: "native-channel", name: "Native", picture: null },
      });
      assert.equal((await call("native_status", ownerA, pollNative)).status, "pending");
      await assert.rejects(
        () => call("confirm", ownerA, { transaction_id: pendingNative.id }),
        /invalid_state/,
      );
      await sql`delete from auth.sessions where id=${browserSession}`;
      await assert.rejects(() => call("native_confirm", ownerA, pollNative), /session_revoked/);
      await sql`insert into auth.sessions(id,user_id) values(${browserSession},${ownerA})`;
      assert.equal((await call("native_confirm", ownerA, pollNative)).display_name, "Native");
      assert.equal((await call("native_status", ownerA, pollNative)).status, "confirmed");
      // Remove the fixture before the existing assertions on connection/cleanup counts.
      await sql`delete from public.yl_cast_connections where provider_user_id='native-channel'`;

      const tx = await start();
      await assert.rejects(
        () =>
          call("consume", null, {
            provider: "kick",
            state_hash: tx.state_hash,
            browser_hash: "another-browser",
          }),
        /invalid_state/,
      );
      assert.equal(await call("admit", ownerA, { session_id: session }), true);
      await assert.rejects(() => call("admit", ownerB, { session_id: session }), /session_revoked/);
      await sql`update public.yl_profiles set account_status='onboarding_required' where user_id=${ownerA}`;
      await assert.rejects(
        () => call("admit", ownerA, { session_id: session }),
        /account_inactive/,
      );
      await sql`update public.yl_profiles set account_status='active' where user_id=${ownerA}`;
      await assert.rejects(
        () => call("consume", null, { provider: "twitch", state_hash: tx.state_hash }),
        /invalid_state/,
      );
      await assert.rejects(() => call("confirm", ownerB, { transaction_id: tx.id }), /not_found/);
      const first = await complete(tx, "123");
      await assert.rejects(
        () => call("consume", null, { provider: "kick", state_hash: tx.state_hash }),
        /invalid_state/,
      );
      assert.equal((await call("list", ownerA)).length, 1);
      assert.equal((await call("list", ownerB)).length, 0);
      await assert.rejects(
        () => call("disconnect", ownerB, { connection_id: first.id }),
        /not_found/,
      );
      assert.equal((await complete(await start(), "124")).provider_user_id, "124");
      assert.equal((await call("list", ownerA)).length, 2);
      await assert.rejects(
        async () => complete(await start(ownerB), "123", ownerB),
        /owner_conflict/,
      );
      const reconnect = await complete(await start(), "123");
      assert.equal(reconnect.id, first.id);
      assert.equal((await call("list", ownerA)).length, 2);

      // Auth identities must use the same ownership registry as publishing connections.
      await assert.rejects(
        () =>
          sql`insert into auth.identities(user_id,provider,provider_id) values (${ownerB},'custom:kick','123')`,
        /owner_conflict/,
      );
      await sql`insert into auth.identities(user_id,provider,provider_id) values (${ownerA},'custom:kick','123')`;
      await assert.rejects(
        () =>
          sql`insert into auth.identities(user_id,provider,provider_id) values (${ownerB},'twitch','twitch-1'), (${ownerA},'twitch','twitch-1')`,
        /owner_conflict/,
      );

      await sql`update yl_cast_private.yl_cast_credentials set due_at=now()-interval '1 minute' where kind='publishing'`;
      const [leases1, leases2] = await Promise.all([call("lease", null), call("lease", null)]);
      const ids = [...leases1, ...leases2].map((x: { id: string }) => x.id);
      assert.equal(new Set(ids).size, ids.length);
      const leased = [...leases1, ...leases2].find((x: { connection_id: string }) =>
        x.connection_id === first.id
      );
      await call("disconnect", ownerA, { connection_id: first.id });
      const stale = await call("finish", null, {
        credential_id: leased.id,
        lease: leased.lease,
        version: leased.version,
        grant: { stale: true },
        expires_at: new Date(Date.now() + 7200000).toISOString(),
      });
      assert.equal(stale, false);
      assert.equal(
        await call("checkpoint", null, {
          credential_id: leased.id,
          lease: leased.lease,
          version: leased.version,
          grant: { stale: true },
          expires_at: new Date(Date.now() + 7200000).toISOString(),
        }),
        false,
      );
      assert.equal(
        (await call("list", ownerA)).find((x: { id: string }) => x.id === first.id).status,
        "disconnected",
      );

      await sql.begin(async (txsql) => {
        await txsql.unsafe("set local role authenticated");
        await txsql`select set_config('request.jwt.claim.sub', ${ownerB}, true)`;
        assert.equal((await txsql`select * from public.yl_cast_connections`).length, 0);
      });
      await assert.rejects(() =>
        sql.begin(async (txsql) => {
          await txsql.unsafe("set local role authenticated");
          await txsql`select public.yl_cast_rpc('list',${ownerA}::uuid,'{}')`;
        }), /permission denied/);
      await sql.begin(async (txsql) => {
        await txsql.unsafe("set local role authenticated");
        await txsql`select set_config('request.jwt.claim.sub', ${ownerA}, true)`;
        assert.equal((await txsql`select * from public.yl_cast_connections`).length, 2);
      });
      await assert.rejects(() =>
        sql.begin(async (txsql) => {
          await txsql.unsafe("set local role authenticated");
          await txsql`select * from yl_cast_private.yl_cast_credentials`;
        }), /permission denied/);
    } finally {
      await sql.end();
    }
  },
});
