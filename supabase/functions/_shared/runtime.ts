import { authenticateWithSupabase } from "./auth.ts";
import { TokenVault } from "./crypto.ts";
import {
  createHandler,
  createKickUserInfoHandler,
  createMaintenanceHandler,
  type Options,
} from "./handlers.ts";
import { AppError, errorResponse } from "./http.ts";
import { Provider } from "./providers.ts";
import { HttpStore } from "./store.ts";

type Env = (key: string) => string | undefined;
function required(env: Env, name: string) {
  const value = env(name);
  if (!value) throw new AppError("configuration_required", 503);
  return value;
}
function key(env: Env, singular: string, plural: string) {
  if (env(singular)) return env(singular)!;
  try {
    const value = JSON.parse(env(plural) ?? "{}").default;
    if (typeof value === "string" && value) return value;
  } catch { /* Fail closed below. */ }
  throw new AppError("configuration_required", 503);
}
function secureUrl(value: string) {
  const url = new URL(value);
  if (
    url.username || url.password || url.hash || url.search ||
    (url.protocol !== "https:" &&
      !(url.protocol === "http:" && ["localhost", "127.0.0.1"].includes(url.hostname)))
  ) throw new AppError("configuration_required", 503);
  return url.toString().replace(/\/$/, "");
}
export function loadOptions(env: Env): Options & { maintenanceSecret: string } {
  const url = secureUrl(required(env, "SUPABASE_URL"));
  const completeUrl = secureUrl(required(env, "CAST_COMPLETE_URL"));
  const origins = required(env, "CAST_ALLOWED_ORIGINS").split(",").map((s) => secureUrl(s.trim()));
  if (!origins.includes(new URL(completeUrl).origin)) {
    throw new AppError("configuration_required", 503);
  }
  const store = new HttpStore(url, key(env, "CAST_SUPABASE_SECRET_KEY", "SUPABASE_SECRET_KEYS"));
  const vault = new TokenVault(
    required(env, "CAST_TOKEN_KEY_ID"),
    JSON.parse(required(env, "CAST_TOKEN_KEYS")),
  );
  const publicKey = key(env, "CAST_SUPABASE_PUBLISHABLE_KEY", "SUPABASE_PUBLISHABLE_KEYS");
  return {
    enabled: env("CAST_ENABLED") === "true",
    store,
    vault,
    origins,
    completeUrl,
    maintenanceSecret: required(env, "CAST_MAINTENANCE_SECRET"),
    authenticate: authenticateWithSupabase(
      url,
      publicKey,
      (env("CAST_OAUTH_CLIENT_IDS") ?? "").split(",").filter(Boolean),
    ),
    provider: (name, kind = "publishing") => {
      if (name === "kick" && kind === "login") {
        return new Provider(name, { clientId: "", clientSecret: "", redirectUri: "" });
      }
      const prefix = `CAST_${kind === "login" ? "LOGIN" : "PUBLISH"}_${name.toUpperCase()}`;
      return new Provider(name, {
        clientId: required(env, `${prefix}_CLIENT_ID`),
        clientSecret: required(env, `${prefix}_CLIENT_SECRET`),
        redirectUri: `${url}/functions/v1/platform-connections/v1/oauth/callback/${name}`,
      });
    },
  };
}

export function runtimeHandler(
  kind: "connections" | "kick" | "maintenance",
  env: Env = (key) => Deno.env.get(key),
) {
  let handler: ((request: Request) => Promise<Response>) | undefined;
  return async (request: Request) => {
    try {
      if (env("CAST_ENABLED") !== "true") throw new AppError("feature_disabled", 503);
      if (!handler) {
        const options = loadOptions(env);
        handler = kind === "kick"
          ? createKickUserInfoHandler(options)
          : kind === "maintenance"
          ? createMaintenanceHandler(options)
          : createHandler(options);
      }
      return await handler(request);
    } catch (error) {
      return errorResponse(error);
    }
  };
}
