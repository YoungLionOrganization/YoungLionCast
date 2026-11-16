import { AppError, boundedJson, type Fetcher } from "./http.ts";

export interface Principal {
  id: string;
  sessionId: string;
  identities: { provider: string; provider_id: string }[];
}
export type Authenticator = (request: Request) => Promise<Principal>;

export function bearer(request: Request) {
  const match = /^Bearer ([^\s]{1,16384})$/i.exec(request.headers.get("Authorization") ?? "");
  if (!match) throw new AppError("unauthorized", 401);
  return match[1];
}

export function authenticateWithSupabase(
  url: string,
  publishableKey: string,
  clientIds: string[],
  fetcher: Fetcher = fetch,
): Authenticator {
  return async (request) => {
    const token = bearer(request);
    let response: Response;
    try {
      response = await fetcher(`${url}/auth/v1/user`, {
        headers: { apikey: publishableKey, Authorization: `Bearer ${token}` },
        redirect: "error",
        signal: AbortSignal.timeout(8000),
      });
    } catch {
      throw new AppError("auth_unavailable", 503);
    }
    if (!response.ok) {
      await response.body?.cancel();
      throw new AppError("unauthorized", 401);
    }
    const user = await boundedJson(response);
    let claims: Record<string, unknown>;
    try {
      // Claims are used only AFTER Auth has verified this exact token.
      const part = token.split(".")[1].replace(/-/g, "+").replace(/_/g, "/");
      claims = JSON.parse(atob(part.padEnd(Math.ceil(part.length / 4) * 4, "=")));
    } catch {
      throw new AppError("unauthorized", 401);
    }
    if (
      claims.sub !== user.id || claims.iss !== `${url}/auth/v1` || claims.aud !== "authenticated" ||
      claims.role !== "authenticated" || typeof claims.exp !== "number" ||
      !Number.isFinite(claims.exp) || claims.exp <= Date.now() / 1000 ||
      typeof claims.session_id !== "string" || !claims.session_id.length
    ) throw new AppError("unauthorized", 401);
    if (claims.client_id && !clientIds.includes(String(claims.client_id))) {
      throw new AppError("client_not_allowed", 403);
    }
    const identities = Array.isArray(user.identities)
      ? user.identities.filter((x) =>
        typeof x?.provider === "string" && typeof x?.provider_id === "string"
      ).map((x) => ({ provider: x.provider, provider_id: x.provider_id }))
      : [];
    return { id: String(user.id), sessionId: claims.session_id, identities };
  };
}
