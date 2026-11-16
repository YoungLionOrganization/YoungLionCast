import assert from "node:assert/strict";
import { authenticateWithSupabase } from "../supabase/functions/_shared/auth.ts";

const claims = {
  sub: "owner",
  iss: "https://project.example/auth/v1",
  aud: "authenticated",
  role: "authenticated",
  exp: 4102444800,
  session_id: "session",
};
function request(data: Record<string, unknown> = claims) {
  return new Request("https://project.example/api", {
    headers: { Authorization: `Bearer header.${btoa(JSON.stringify(data))}.signature` },
  });
}
Deno.test("Supabase rejection cannot be bypassed with a plausible JWT payload", async () => {
  const auth = authenticateWithSupabase(
    "https://project.example",
    "publishable",
    [],
    () => Promise.resolve(new Response(null, { status: 401 })),
  );
  await assert.rejects(() => auth(request()), /unauthorized/);
});
Deno.test("validated user must match issuer, audience, subject and allowed OAuth client", async () => {
  const auth = authenticateWithSupabase(
    "https://project.example",
    "publishable",
    ["cast"],
    () => Promise.resolve(Response.json({ id: "owner", identities: [] })),
  );
  assert.equal((await auth(request())).id, "owner");
  for (
    const changed of [
      { sub: "other" },
      { iss: "https://evil.example/auth/v1" },
      { aud: "other" },
      { exp: 1 },
      { exp: undefined },
      { session_id: "" },
      { client_id: "other" },
    ]
  ) {
    await assert.rejects(
      () => auth(request({ ...claims, ...changed })),
      /unauthorized|client_not_allowed/,
    );
  }
});
