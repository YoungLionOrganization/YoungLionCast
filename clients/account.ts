import { challenge, randomToken } from "../supabase/functions/_shared/crypto.ts";
import type { Connection } from "../supabase/functions/_shared/store.ts";
import type { Account, Platform } from "../supabase/functions/_shared/providers.ts";
import { boundedValue, type Fetcher } from "../supabase/functions/_shared/http.ts";

export class CastClientError extends Error {
  constructor(public code: string, public status: number) {
    super(code);
  }
}
export interface OAuthStart {
  transaction_id: string;
  authorization_url: string;
  expires_at: string;
}
export interface PendingAccount {
  transaction_id: string;
  provider: Platform;
  account: Account;
  expires_at: string;
}
export interface YoungLionSession {
  access_token: string;
  refresh_token: string;
  expires_in?: number;
  provider_token?: string;
  provider_refresh_token?: string;
  user: { id: string };
}
async function result<T>(response: Response): Promise<T> {
  const data = await boundedValue(response, 262144);
  if (!response.ok) {
    const error = data as { error?: { code?: string } };
    throw new CastClientError(error?.error?.code ?? "request_failed", response.status);
  }
  return data as T;
}

/** Uses only a publishable key. The caller owns session storage and refresh. */
export class CastAccountClient {
  constructor(
    private supabaseUrl: string,
    private publishableKey: string,
    private accessToken: () => Promise<string>,
    private fetcher: Fetcher = fetch,
  ) {}
  private async request<T>(path: string, method = "GET", data?: unknown): Promise<T> {
    const response = await this.fetcher(
      `${this.supabaseUrl.replace(/\/$/, "")}/functions/v1/platform-connections/v1${path}`,
      {
        method,
        credentials: "include",
        headers: {
          apikey: this.publishableKey,
          Authorization: `Bearer ${await this.accessToken()}`,
          ...(data ? { "Content-Type": "application/json" } : {}),
        },
        ...(data ? { body: JSON.stringify(data) } : {}),
        redirect: "error",
        signal: AbortSignal.timeout(15000),
      },
    );
    return result<T>(response);
  }
  start(provider: Platform) {
    return this.request<OAuthStart>("/oauth/start", "POST", { provider });
  }
  pending(transactionId: string) {
    return this.request<PendingAccount>(
      `/oauth/pending?${new URLSearchParams({ transaction_id: transactionId })}`,
    );
  }
  confirm(transactionId: string) {
    return this.request<Connection>("/oauth/confirm", "POST", { transaction_id: transactionId });
  }
  list(after?: string) {
    return this.request<{ connections: Connection[]; next_cursor: string | null }>(
      `/connections${after ? `?${new URLSearchParams({ after })}` : ""}`,
    );
  }
  reconnect(connectionId: string) {
    return this.request<OAuthStart>(
      `/connections/${encodeURIComponent(connectionId)}/reconnect`,
      "POST",
    );
  }
  disconnect(connectionId: string) {
    return this.request<{ status: "disconnected"; revocation: "queued" }>(
      `/connections/${encodeURIComponent(connectionId)}`,
      "DELETE",
    );
  }
  /** Browser must already be signed in as the native request's YoungLion owner. */
  authorizeNative(requestId: string, ticket: string) {
    return this.request<OAuthStart>("/native/authorize", "POST", {
      request_id: requestId,
      ticket,
    });
  }
  registerTwitchLogin(accessToken: string, refreshToken?: string) {
    return this.request<{ registered: true }>("/login/twitch", "POST", {
      access_token: accessToken,
      ...(refreshToken ? { refresh_token: refreshToken } : {}),
    });
  }
}

/** Account-web integration helper; native OIDC is implemented separately in the Qt client. */
export async function beginYoungLionLogin(
  supabaseUrl: string,
  provider: Platform,
  redirectTo: string,
) {
  const verifier = randomToken();
  const url = new URL(`${supabaseUrl.replace(/\/$/, "")}/auth/v1/authorize`);
  url.search = new URLSearchParams({
    provider: provider === "kick" ? "custom:kick" : "twitch",
    redirect_to: redirectTo,
    code_challenge: await challenge(verifier),
    code_challenge_method: "s256",
  }).toString();
  return { authorizationUrl: url.toString(), verifier };
}

/** Exchange the auth code with the matching saved verifier; never log the returned session. */
export async function finishYoungLionLogin(
  supabaseUrl: string,
  publishableKey: string,
  code: string,
  verifier: string,
  fetcher: Fetcher = fetch,
) {
  const response = await fetcher(
    `${supabaseUrl.replace(/\/$/, "")}/auth/v1/token?grant_type=pkce`,
    {
      method: "POST",
      headers: { apikey: publishableKey, "Content-Type": "application/json" },
      body: JSON.stringify({ auth_code: code, code_verifier: verifier }),
      redirect: "error",
      signal: AbortSignal.timeout(15000),
    },
  );
  return result<YoungLionSession>(response);
}
