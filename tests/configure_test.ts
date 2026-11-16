import assert from "node:assert/strict";
import { kickConfiguration, redacted } from "../scripts/configure-auth.ts";

Deno.test("Kick Auth configuration keeps S256, normalized UserInfo and optional email", () => {
  const values: Record<string, string> = {
    SUPABASE_URL: "https://project.supabase.co",
    CAST_LOGIN_KICK_CLIENT_ID: "login-client",
    CAST_LOGIN_KICK_CLIENT_SECRET: "server-secret",
  };
  const config = kickConfiguration((key) => values[key]);
  assert.equal(config.identifier, "custom:kick");
  assert.equal(config.userinfo_url, "https://project.supabase.co/functions/v1/kick-userinfo");
  assert.equal(config.pkce_enabled, true);
  assert.equal(config.email_optional, true);
  assert.equal(JSON.stringify(redacted(config)).includes("server-secret"), false);
  assert.throws(() => kickConfiguration(() => undefined));
});
