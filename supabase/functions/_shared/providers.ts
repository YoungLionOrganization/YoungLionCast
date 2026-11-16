import { AppError, type Fetcher, requestJson, text } from "./http.ts";

export type Platform = "twitch" | "kick";
export interface Credentials {
  clientId: string;
  clientSecret: string;
  redirectUri: string;
}
export interface Account {
  id: string;
  name: string;
  picture: string | null;
}
export interface Grant {
  accessToken: string;
  refreshToken: string;
  expiresAt: string;
  scopes: string[];
}

const endpoints = {
  twitch: {
    auth: "https://id.twitch.tv/oauth2/authorize",
    token: "https://id.twitch.tv/oauth2/token",
    users: "https://api.twitch.tv/helix/users",
    revoke: "https://id.twitch.tv/oauth2/revoke",
    scopes: ["channel:read:stream_key"],
  },
  kick: {
    auth: "https://id.kick.com/oauth/authorize",
    token: "https://id.kick.com/oauth/token",
    users: "https://api.kick.com/public/v1/users",
    revoke: "https://id.kick.com/oauth/revoke",
    scopes: ["user:read", "channel:read", "streamkey:read"],
  },
};

export function platform(value: unknown): Platform {
  if (value !== "twitch" && value !== "kick") throw new AppError("invalid_provider");
  return value;
}

export class Provider {
  constructor(
    public name: Platform,
    private credentials: Credentials,
    private fetcher: Fetcher = fetch,
  ) {}
  authorizationUrl(state: string, codeChallenge?: string) {
    if (this.name === "kick" && !codeChallenge) throw new AppError("pkce_required");
    const url = new URL(endpoints[this.name].auth);
    const params: Record<string, string> = {
      client_id: this.credentials.clientId,
      redirect_uri: this.credentials.redirectUri,
      response_type: "code",
      state,
      scope: endpoints[this.name].scopes.join(" "),
    };
    if (this.name === "twitch") params.force_verify = "true";
    if (codeChallenge) {
      params.code_challenge = codeChallenge;
      params.code_challenge_method = "S256";
    }
    url.search = new URLSearchParams(params).toString();
    return url.toString();
  }
  private async token(params: Record<string, string>, previous?: string): Promise<Grant> {
    const data = await requestJson(this.fetcher, endpoints[this.name].token, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({
        ...params,
        client_id: this.credentials.clientId,
        client_secret: this.credentials.clientSecret,
      }),
    });
    const seconds = Number(data.expires_in);
    if (!Number.isFinite(seconds) || seconds <= 0 || seconds > 31536000) {
      throw new AppError("provider_response", 502);
    }
    const scopes = Array.isArray(data.scope)
      ? data.scope
      : typeof data.scope === "string"
      ? data.scope.split(/\s+/).filter(Boolean)
      : [];
    if (scopes.some((s) => typeof s !== "string" || s.length > 200)) {
      throw new AppError("provider_response", 502);
    }
    return {
      accessToken: text(data.access_token),
      refreshToken: text(data.refresh_token ?? previous),
      expiresAt: new Date(Date.now() + seconds * 1000).toISOString(),
      scopes: scopes as string[],
    };
  }
  exchange(code: string, verifier?: string) {
    return this.token({
      grant_type: "authorization_code",
      code,
      redirect_uri: this.credentials.redirectUri,
      ...(verifier ? { code_verifier: verifier } : {}),
    });
  }
  refresh(token: string) {
    return this.token({ grant_type: "refresh_token", refresh_token: token }, token);
  }
  async account(accessToken: string): Promise<Account> {
    const data = await requestJson(this.fetcher, endpoints[this.name].users, {
      headers: {
        Authorization: `Bearer ${accessToken}`,
        ...(this.name === "twitch" ? { "Client-Id": this.credentials.clientId } : {}),
      },
    });
    if (!Array.isArray(data.data) || data.data.length !== 1) {
      throw new AppError("provider_response", 502);
    }
    const user = data.data[0];
    if (!user || typeof user !== "object") throw new AppError("provider_response", 502);
    let id: string;
    if (this.name === "kick") {
      if (!Number.isSafeInteger(user.user_id) || user.user_id <= 0) {
        throw new AppError("provider_response", 502);
      }
      id = String(user.user_id);
    } else {
      id = text(user.id, 64);
      if (!/^[1-9][0-9]*$/.test(id)) throw new AppError("provider_response", 502);
    }
    const picture = user.profile_picture ?? user.profile_image_url;
    return {
      id,
      name: text(user.name ?? user.display_name, 200),
      picture:
        typeof picture === "string" && picture.startsWith("https://") && picture.length <= 2048
          ? picture
          : null,
    };
  }
  async validate(accessToken: string, expectedId: string) {
    if (this.name === "kick") {
      if ((await this.account(accessToken)).id !== expectedId) {
        throw new AppError("provider_identity", 409);
      }
      return;
    }
    const data = await requestJson(this.fetcher, "https://id.twitch.tv/oauth2/validate", {
      headers: { Authorization: `OAuth ${accessToken}` },
    });
    if (data.client_id !== this.credentials.clientId || data.user_id !== expectedId) {
      throw new AppError("provider_identity", 409);
    }
    const seconds = Number(data.expires_in);
    if (!Number.isFinite(seconds) || seconds <= 0 || seconds > 31536000) {
      throw new AppError("provider_response", 502);
    }
    return { expiresAt: new Date(Date.now() + seconds * 1000).toISOString() };
  }
  hasPublishingScopes(grant: Grant) {
    return endpoints[this.name].scopes.every((s) => grant.scopes.includes(s));
  }
  async revoke(token: string, hint: "access_token" | "refresh_token" = "access_token") {
    let url = endpoints[this.name].revoke;
    const body = new URLSearchParams();
    if (this.name === "kick") url += `?${new URLSearchParams({ token, token_hint_type: hint })}`;
    else {
      body.set("client_id", this.credentials.clientId);
      body.set("token", token);
    }
    let response: Response;
    try {
      response = await this.fetcher(url, {
        method: "POST",
        body,
        redirect: "error",
        signal: AbortSignal.timeout(8000),
      });
    } catch {
      throw new AppError("provider_unavailable", 503);
    }
    await response.body?.cancel();
    if (!response.ok && response.status !== 401) throw new AppError("provider_unavailable", 503);
  }
}
