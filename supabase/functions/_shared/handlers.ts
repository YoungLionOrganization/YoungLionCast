import { AppError, boundedJson, errorResponse, json, safeHeaders } from "./http.ts";
import { type Authenticator, bearer, type Principal } from "./auth.ts";
import { challenge, digest, randomToken, TokenVault } from "./crypto.ts";
import { type Grant, type Platform, platform, Provider } from "./providers.ts";
import {
  type Connection,
  type Credential,
  credentialContext,
  rate,
  type Store,
  type Transaction,
} from "./store.ts";

export interface Options {
  enabled: boolean;
  origins: string[];
  completeUrl: string;
  store: Store;
  vault: TokenVault;
  authenticate: Authenticator;
  provider: (name: Platform, kind?: "login" | "publishing") => Provider;
}
const uuidPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
function uuid(value: unknown) {
  if (typeof value !== "string" || !uuidPattern.test(value)) throw new AppError("invalid_id");
  return value;
}
function escape(value: string) {
  return value.replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;").replace(
    />/g,
    "&gt;",
  );
}
async function body(request: Request, allowed: string[]) {
  if (!request.headers.get("Content-Type")?.startsWith("application/json")) {
    throw new AppError("invalid_body");
  }
  let data: Record<string, unknown>;
  try {
    data = await boundedJson(new Response(request.body), 8192);
  } catch {
    throw new AppError("invalid_body");
  }
  if (Object.keys(data).some((k) => !allowed.includes(k))) throw new AppError("invalid_body");
  return data;
}
async function principal(
  options: Options,
  request: Request,
  loginRegistration = false,
): Promise<Principal> {
  let user: Principal;
  try {
    user = await options.authenticate(request);
  } catch (e) {
    throw e instanceof AppError ? e : new AppError("unauthorized", 401);
  }
  await options.store.rpc(loginRegistration ? "admit_session" : "admit", user.id, {
    session_id: user.sessionId,
  });
  return user;
}

export function createHandler(options: Options) {
  return async (request: Request): Promise<Response> => {
    const origin = request.headers.get("Origin");
    const headers: Record<string, string> = origin && options.origins.includes(origin)
      ? { "Access-Control-Allow-Origin": origin, "Vary": "Origin" }
      : {};
    try {
      if (!options.enabled) throw new AppError("feature_disabled", 503);
      if (origin && !options.origins.includes(origin)) {
        throw new AppError("origin_not_allowed", 403);
      }
      if (request.method === "OPTIONS") {
        return new Response(null, {
          status: 204,
          headers: {
            ...headers,
            "Access-Control-Allow-Methods": "GET, POST, DELETE, OPTIONS",
            "Access-Control-Allow-Headers": "authorization, apikey, content-type",
          },
        });
      }
      const url = new URL(request.url);
      const path = url.pathname.replace(
        /^\/functions\/v1\/platform-connections|^\/platform-connections/,
        "",
      );
      const callback = /^\/v1\/oauth\/callback\/(kick|twitch)$/.exec(path);
      if (callback && request.method === "GET") {
        return await callbackRequest(options, url, platform(callback[1]));
      }
      const user = await principal(
        options,
        request,
        path === "/v1/login/twitch" && request.method === "POST",
      );
      await rate(options.store, `user:${user.id}`, 60);
      if (path === "/v1/oauth/start" && request.method === "POST") {
        const data = await body(request, ["provider"]);
        return json(await start(options, user.id, platform(data.provider)), 200, headers);
      }
      if (path === "/v1/oauth/pending" && request.method === "GET") {
        return json(
          await options.store.rpc("pending", user.id, {
            transaction_id: uuid(url.searchParams.get("transaction_id")),
          }),
          200,
          headers,
        );
      }
      if (path === "/v1/oauth/confirm" && request.method === "POST") {
        const data = await body(request, ["transaction_id"]);
        return json(
          await options.store.rpc("confirm", user.id, {
            transaction_id: uuid(data.transaction_id),
          }),
          200,
          headers,
        );
      }
      if (path === "/v1/connections" && request.method === "GET") {
        const after = url.searchParams.get("after");
        const rows = await options.store.rpc<Connection[]>(
          "list",
          user.id,
          after ? { after: uuid(after) } : {},
        );
        return json(
          { connections: rows, next_cursor: rows.length === 50 ? rows.at(-1)!.id : null },
          200,
          headers,
        );
      }
      const target = /^\/v1\/connections\/([^/]+)(\/reconnect)?$/.exec(path);
      if (target) {
        const id = uuid(target[1]);
        if (target[2] && request.method === "POST") {
          const connection = await options.store.rpc<Connection>("get", user.id, {
            connection_id: id,
          });
          return json(await start(options, user.id, connection.provider, id), 200, headers);
        }
        if (!target[2] && request.method === "DELETE") {
          await options.store.rpc("disconnect", user.id, { connection_id: id });
          return json({ status: "disconnected", revocation: "queued" }, 200, headers);
        }
      }
      if (path === "/v1/login/twitch" && request.method === "POST") {
        const data = await body(request, ["access_token", "refresh_token"]);
        const identity = user.identities.find((i) => i.provider === "twitch");
        if (
          !identity || typeof data.access_token !== "string" || data.access_token.length > 8192 ||
          !data.access_token.length ||
          (data.refresh_token !== undefined &&
            (typeof data.refresh_token !== "string" || data.refresh_token.length > 8192))
        ) throw new AppError("invalid_login_grant");
        const provider = options.provider("twitch", "login");
        const validation = await provider.validate(data.access_token, identity.provider_id);
        if (!validation) throw new AppError("provider_response", 502);
        const grant: Grant = {
          accessToken: data.access_token,
          refreshToken: String(data.refresh_token ?? ""),
          scopes: ["user:read:email"],
          expiresAt: validation.expiresAt,
        };
        await options.store.rpc("login_register", user.id, {
          session_id: user.sessionId,
          provider_user_id: identity.provider_id,
          grant: await options.vault.seal(
            grant,
            credentialContext(user.id, "twitch", user.sessionId),
          ),
          expires_at: grant.expiresAt,
        });
        return json({ registered: true }, 200, headers);
      }
      throw new AppError("not_found", 404);
    } catch (error) {
      const response = errorResponse(error);
      for (const [key, value] of Object.entries(headers)) response.headers.set(key, value);
      return response;
    }
  };
}

async function start(options: Options, owner: string, name: Platform, target?: string) {
  const provider = options.provider(name);
  const id = crypto.randomUUID(), state = randomToken(), verifier = randomToken();
  const transaction = await options.store.rpc<Transaction>("start", owner, {
    id,
    provider: name,
    state_hash: await digest(state),
    verifier: await options.vault.seal({ verifier }, `state:${owner}:${name}:${id}`),
    ...(target ? { target } : {}),
  });
  return {
    transaction_id: transaction.id,
    expires_at: transaction.expires_at,
    authorization_url: provider.authorizationUrl(
      state,
      name === "kick" ? await challenge(verifier) : undefined,
    ),
  };
}

async function callbackRequest(options: Options, url: URL, name: Platform): Promise<Response> {
  const state = url.searchParams.get("state"), code = url.searchParams.get("code");
  if (
    !state || !/^[A-Za-z0-9_-]{43}$/.test(state) || (!code && !url.searchParams.has("error")) ||
    (code && code.length > 2048)
  ) throw new AppError("invalid_state");
  const tx = await options.store.rpc<Transaction>("consume", null, {
    provider: name,
    state_hash: await digest(state),
  });
  const provider = options.provider(name);
  let grant: Grant | undefined;
  let persisted = false;
  try {
    if (url.searchParams.has("error")) throw new AppError("authorization_denied");
    const { verifier } = await options.vault.open<{ verifier: string }>(
      tx.verifier,
      `state:${tx.owner_id}:${name}:${tx.id}`,
    );
    grant = await provider.exchange(code!, name === "kick" ? verifier : undefined);
    await options.store.rpc("save_grant", tx.owner_id, {
      transaction_id: tx.id,
      grant: await options.vault.seal(grant, credentialContext(tx.owner_id, name, tx.id)),
      expires_at: grant.expiresAt,
      scopes: grant.scopes,
    });
    persisted = true;
    const account = await provider.account(grant.accessToken);
    await provider.validate(grant.accessToken, account.id);
    if (!provider.hasPublishingScopes(grant)) throw new AppError("insufficient_scope", 409);
    await options.store.rpc("ready", tx.owner_id, { transaction_id: tx.id, account });
    const complete = new URL(options.completeUrl);
    complete.searchParams.set("transaction_id", tx.id);
    return new Response(
      `<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>YoungLion Cast</title><h1>Account authorization received</h1><p>Return to YoungLion and confirm the account before it is connected.</p><a href="${
        escape(complete.toString())
      }">Return to YoungLion</a></html>`,
      {
        headers: {
          ...safeHeaders,
          "Content-Type": "text/html; charset=utf-8",
          "Content-Security-Policy":
            "default-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'",
        },
      },
    );
  } catch (error) {
    await options.store.rpc("fail_transaction", tx.owner_id, { transaction_id: tx.id }).catch(
      () => {},
    );
    if (grant && !persisted) await provider.revoke(grant.accessToken).catch(() => {});
    throw error;
  }
}

export function createKickUserInfoHandler(options: Options) {
  return async (request: Request) => {
    try {
      if (!options.enabled) throw new AppError("feature_disabled", 503);
      if (request.method !== "GET") throw new AppError("method_not_allowed", 405);
      if (new URL(request.url).search) throw new AppError("invalid_request");
      const token = bearer(request);
      await rate(options.store, "kick-userinfo:global", 300);
      await rate(options.store, `kick-userinfo:${await digest(token)}`, 15);
      const account = await options.provider("kick", "login").account(token);
      return json({
        sub: account.id,
        name: account.name,
        ...(account.picture ? { picture: account.picture } : {}),
      });
    } catch (error) {
      return errorResponse(error);
    }
  };
}

export function createMaintenanceHandler(options: Options & { maintenanceSecret: string }) {
  return async (request: Request) => {
    try {
      if (!options.enabled) throw new AppError("feature_disabled", 503);
      if (request.method !== "POST") throw new AppError("method_not_allowed", 405);
      const provided = request.headers.get("X-Cast-Maintenance") ?? "";
      if (!provided || await digest(provided) !== await digest(options.maintenanceSecret)) {
        throw new AppError("unauthorized", 401);
      }
      const credentials = await options.store.rpc<Credential[]>("lease", null);
      const results = await Promise.all(
        credentials.map((credential) => maintain(options, credential)),
      );
      return json({ processed: results.length, succeeded: results.filter(Boolean).length });
    } catch (error) {
      return errorResponse(error);
    }
  };
}

async function maintain(options: Options, credential: Credential) {
  const data = {
    credential_id: credential.id,
    lease: credential.lease,
    version: credential.version,
  };
  try {
    const provider = options.provider(credential.provider, credential.purpose);
    let grant = await options.vault.open<Grant>(
      credential.grant_data,
      credentialContext(credential.owner_id, credential.provider, credential.id),
    );
    if (credential.kind === "cleanup") {
      await provider.revoke(grant.accessToken);
      if (credential.provider === "kick" && grant.refreshToken) {
        await provider.revoke(grant.refreshToken, "refresh_token");
      }
      return options.store.rpc<boolean>("removed", null, data);
    }
    if (Date.parse(grant.expiresAt) <= Date.now() + 300000) {
      if (!grant.refreshToken) throw new AppError("reauth_required", 409);
      grant = await provider.refresh(grant.refreshToken);
    }
    await provider.validate(grant.accessToken, credential.provider_user_id);
    if (credential.kind === "publishing" && !provider.hasPublishingScopes(grant)) {
      throw new AppError("reauth_required", 409);
    }
    const applied = await options.store.rpc<boolean>("finish", null, {
      ...data,
      grant: await options.vault.seal(
        grant,
        credentialContext(credential.owner_id, credential.provider, credential.id),
      ),
      expires_at: grant.expiresAt,
    });
    if (!applied) {
      const id = crypto.randomUUID();
      await options.store.rpc("orphan", credential.owner_id, {
        id,
        provider: credential.provider,
        purpose: credential.purpose,
        grant: await options.vault.seal(
          grant,
          credentialContext(credential.owner_id, credential.provider, id),
        ),
        expires_at: grant.expiresAt,
      });
    }
    return applied;
  } catch (error) {
    const denied = error instanceof AppError &&
      ["reauth_required", "provider_identity"].includes(error.code);
    await options.store.rpc(denied ? "deny" : "retry", null, data);
    return false;
  }
}
