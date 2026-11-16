import assert from "node:assert/strict";
import { challenge, digest, randomToken, TokenVault } from "../supabase/functions/_shared/crypto.ts";

Deno.test("PKCE matches the RFC 7636 S256 vector", async () => {
  assert.equal(await challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
  assert.equal(await digest("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  assert.match(randomToken(), /^[A-Za-z0-9_-]{43}$/);
});

Deno.test("encrypted grants cannot be moved between owners, purposes or providers", async () => {
  const vault = new TokenVault("current", { current: btoa("a".repeat(32)) });
  const sealed = await vault.seal({ refreshToken: "top-secret" }, "owner:1:kick:grant");
  assert.equal(JSON.stringify(sealed).includes("top-secret"), false);
  assert.deepEqual(await vault.open(sealed, "owner:1:kick:grant"), { refreshToken: "top-secret" });
  for (const context of ["owner:2:kick:grant", "owner:1:twitch:grant", "owner:1:kick:state"]) {
    await assert.rejects(() => vault.open(sealed, context), /vault_invalid/);
  }
  await assert.rejects(() => vault.open({ ...sealed, ciphertext: sealed.ciphertext.slice(1) }, "owner:1:kick:grant"));
});

Deno.test("key rotation retains old decrypt keys but encrypts with the current key", async () => {
  const old = new TokenVault("old", { old: btoa("a".repeat(32)) });
  const sealed = await old.seal({ value: 1 }, "context");
  const rotated = new TokenVault("new", { old: btoa("a".repeat(32)), new: btoa("b".repeat(32)) });
  assert.deepEqual(await rotated.open(sealed, "context"), { value: 1 });
  assert.equal((await rotated.seal({}, "context")).kid, "new");
  assert.throws(() => new TokenVault("bad", { bad: btoa("short") }));
});
