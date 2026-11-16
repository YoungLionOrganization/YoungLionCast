export class AppError extends Error {
  constructor(public code: string, public status = 400) {
    super(code);
  }
}

export type Fetcher = (url: string | URL | Request, init?: RequestInit) => Promise<Response>;

export async function boundedValue(response: Response, limit = 65536): Promise<unknown> {
  if (!response.body) throw new AppError("provider_response", 502);
  const reader = response.body.getReader();
  const chunks: Uint8Array[] = [];
  let total = 0;
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      total += value.length;
      if (total > limit) throw new AppError("provider_response", 502);
      chunks.push(value);
    }
    const bytes = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
      bytes.set(chunk, offset);
      offset += chunk.length;
    }
    return JSON.parse(new TextDecoder().decode(bytes));
  } catch (e) {
    if (e instanceof AppError) throw e;
    throw new AppError("provider_response", 502);
  } finally {
    await reader.cancel().catch(() => {});
  }
}

export async function boundedJson(
  response: Response,
  limit = 65536,
): Promise<Record<string, unknown>> {
  const body = await boundedValue(response, limit);
  if (!body || typeof body !== "object" || Array.isArray(body)) {
    throw new AppError("provider_response", 502);
  }
  return body as Record<string, unknown>;
}

export async function requestJson(fetcher: Fetcher, url: string, init: RequestInit = {}) {
  let response: Response;
  try {
    response = await fetcher(url, {
      ...init,
      redirect: "error",
      signal: AbortSignal.timeout(8000),
    });
  } catch {
    throw new AppError("provider_unavailable", 503);
  }
  const body = await boundedJson(response);
  if (!response.ok) {
    if (body.error === "invalid_grant" || response.status === 401) {
      throw new AppError("reauth_required", 409);
    }
    throw new AppError(
      response.status === 429 ? "provider_rate_limited" : "provider_unavailable",
      503,
    );
  }
  return body;
}

export function text(value: unknown, max = 8192): string {
  if (typeof value !== "string" || !value.length || value.length > max) {
    throw new AppError("provider_response", 502);
  }
  return value;
}

export const safeHeaders = {
  "Cache-Control": "no-store",
  "Referrer-Policy": "no-referrer",
  "X-Content-Type-Options": "nosniff",
};

export function json(value: unknown, status = 200, headers: HeadersInit = {}) {
  return Response.json(value, { status, headers: { ...safeHeaders, ...headers } });
}

export function errorResponse(error: unknown) {
  const e = error instanceof AppError ? error : new AppError("internal_error", 500);
  return json({ error: { code: e.code } }, e.status);
}
