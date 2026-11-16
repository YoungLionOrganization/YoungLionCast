import { boundedValue } from "../supabase/functions/_shared/http.ts";

type Env = (key: string) => string | undefined;
export function kickConfiguration(env: Env) {
  const url = env("SUPABASE_URL"),
    client = env("CAST_LOGIN_KICK_CLIENT_ID"),
    secret = env("CAST_LOGIN_KICK_CLIENT_SECRET");
  if (!url || !client || !secret) {
    throw new Error("Set SUPABASE_URL and the two CAST_LOGIN_KICK credentials");
  }
  const base = new URL(url);
  if (base.protocol !== "https:" && !["localhost", "127.0.0.1"].includes(base.hostname)) {
    throw new Error("HTTPS is required");
  }
  return {
    provider_type: "oauth2",
    identifier: "custom:kick",
    name: "Kick",
    client_id: client,
    client_secret: secret,
    authorization_url: "https://id.kick.com/oauth/authorize",
    token_url: "https://id.kick.com/oauth/token",
    userinfo_url: `${url.replace(/\/$/, "")}/functions/v1/kick-userinfo`,
    scopes: ["user:read"],
    pkce_enabled: true,
    email_optional: true,
  };
}
export function redacted(config: ReturnType<typeof kickConfiguration>) {
  return { ...config, client_secret: "[redacted]" };
}

if (import.meta.main) {
  try {
    const config = kickConfiguration((key) => Deno.env.get(key));
    if (!Deno.args.includes("--apply")) {
      console.log(JSON.stringify({ mode: "dry-run", provider: redacted(config) }, null, 2));
    } else {
      const secret = Deno.env.get("CAST_SUPABASE_SECRET_KEY");
      if (!secret) {
        throw new Error("Set CAST_SUPABASE_SECRET_KEY for server-side provider administration");
      }
      const response = await fetch(
        `${Deno.env.get("SUPABASE_URL")!.replace(/\/$/, "")}/auth/v1/admin/custom-providers`,
        {
          method: "POST",
          headers: { apikey: secret, "Content-Type": "application/json" },
          body: JSON.stringify(config),
          redirect: "error",
          signal: AbortSignal.timeout(15000),
        },
      );
      await boundedValue(response);
      if (!response.ok) {
        throw new Error(
          `Provider creation failed (HTTP ${response.status}); inspect Dashboard without printing secrets. Existing providers must be updated there.`,
        );
      }
      console.log(
        "custom:kick created. Copy its Dashboard callback URL into the Kick login application before enabling sign-in.",
      );
    }
  } catch (error) {
    console.error(error instanceof Error ? error.message : "Configuration failed");
    Deno.exit(1);
  }
}
