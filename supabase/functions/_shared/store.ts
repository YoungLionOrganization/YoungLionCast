import { AppError, boundedValue, type Fetcher } from "./http.ts";
import type { Envelope } from "./crypto.ts";
import type { Account, Platform } from "./providers.ts";

export interface Store {
  rpc<T = unknown>(
    action: string,
    owner: string | null,
    data?: Record<string, unknown>,
  ): Promise<T>;
}
export interface Transaction {
  id: string;
  owner_id: string;
  provider: Platform;
  verifier: Envelope;
  expires_at: string;
  account?: Account;
}
export interface Credential {
  id: string;
  owner_id: string;
  provider: Platform;
  provider_user_id: string;
  kind: "pending" | "publishing" | "login" | "cleanup";
  purpose: "login" | "publishing";
  connection_id: string | null;
  grant_data: Envelope;
  lease: string;
  version: number;
}
export interface Connection {
  id: string;
  owner_id: string;
  provider: Platform;
  provider_user_id: string;
  display_name: string;
  picture: string | null;
  scopes: string[];
  expires_at: string;
  status: string;
}

const errors: Record<string, number> = {
  invalid_state: 400,
  not_found: 404,
  owner_conflict: 409,
  account_mismatch: 409,
  account_inactive: 403,
  account_unavailable: 503,
  session_revoked: 401,
  rate_limited: 429,
};
export function databaseError(value: unknown): AppError {
  const message = value && typeof value === "object" && "message" in value
    ? String(value.message)
    : "";
  for (const [code, status] of Object.entries(errors)) {
    if (message === code) return new AppError(code, status);
  }
  return new AppError("database_unavailable", 503);
}

export class HttpStore implements Store {
  constructor(private url: string, private secret: string, private fetcher: Fetcher = fetch) {}
  async rpc<T>(
    action: string,
    owner: string | null,
    data: Record<string, unknown> = {},
  ): Promise<T> {
    let response: Response;
    try {
      response = await this.fetcher(`${this.url}/rest/v1/rpc/yl_cast_rpc`, {
        method: "POST",
        headers: { apikey: this.secret, "Content-Type": "application/json" },
        body: JSON.stringify({ p_action: action, p_owner: owner, p_data: data }),
        redirect: "error",
        signal: AbortSignal.timeout(8000),
      });
    } catch {
      throw new AppError("database_unavailable", 503);
    }
    const result = await boundedValue(response, 262144);
    if (!response.ok) throw databaseError(result);
    return result as T;
  }
}

export async function rate(store: Store, bucket: string, limit: number) {
  if (!await store.rpc<boolean>("rate", null, { bucket, limit })) {
    throw new AppError("rate_limited", 429);
  }
}
export function credentialContext(owner: string, provider: Platform, id: string) {
  return `credential:${owner}:${provider}:${id}`;
}
