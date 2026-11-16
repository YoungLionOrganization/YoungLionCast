import { spawnSync } from "node:child_process";
import process from "node:process";
import { randomBytes } from "node:crypto";
import { setTimeout } from "node:timers/promises";

const prefix = `younglion-cast-test-${process.pid}`;
const db = `${prefix}-db`;
const rest = `${prefix}-rest`;
const password = randomBytes(24).toString("hex");
const postgresImage =
  "postgres@sha256:a77aa70e641402a21411b92b721c58d10f23b50ae5fdd2333efd22d166e1e5f7";
const restImage =
  "postgrest/postgrest@sha256:0a46780309a604cdc8b56c776c6e5e15788ce58174d709e40459ab5a2d44d228";
const deno = process.platform === "win32" ? "node_modules/.bin/deno.cmd" : "node_modules/.bin/deno";
function docker(args) {
  const result = spawnSync("docker", args, { encoding: "utf8" });
  if (result.status !== 0) throw new Error(result.stderr || "Docker command failed");
  return result.stdout.trim();
}
function test(args, env) {
  const result = spawnSync(deno, ["test", "--allow-env", "--allow-net", "--allow-read", ...args], {
    stdio: "inherit",
    env: { ...process.env, ...env },
  });
  if (result.status !== 0) throw new Error("Integration tests failed");
}
async function ready(check) {
  for (let attempt = 0; attempt < 120; attempt++) {
    if (await check()) return;
    await setTimeout(250);
  }
  throw new Error("Test dependency did not become ready");
}
try {
  docker(["network", "create", prefix]);
  docker([
    "run",
    "--detach",
    "--name",
    db,
    "--network",
    prefix,
    "--publish",
    "127.0.0.1::5432",
    "--env",
    `POSTGRES_PASSWORD=${password}`,
    "--env",
    "POSTGRES_DB=cast_test",
    postgresImage,
  ]);
  await ready(() =>
    spawnSync("docker", ["exec", db, "pg_isready", "-U", "postgres", "-d", "cast_test"]).status ===
      0
  );
  const port = docker(["port", db, "5432/tcp"]).split(":").at(-1);
  const env = {
    CAST_TEST_DATABASE_URL: `postgres://postgres:${password}@127.0.0.1:${port}/cast_test`,
  };
  // Apply and exercise the schema before PostgREST builds its initial cache.
  test(["tests/database_test.ts"], env);
  docker([
    "run",
    "--detach",
    "--name",
    rest,
    "--network",
    prefix,
    "--publish",
    "127.0.0.1::3000",
    "--env",
    `PGRST_DB_URI=postgres://postgres:${password}@${db}/cast_test`,
    "--env",
    "PGRST_DB_ANON_ROLE=service_role",
    "--env",
    "PGRST_DB_SCHEMAS=public",
    restImage,
  ]);
  const restPort = docker(["port", rest, "3000/tcp"]).split(":").at(-1);
  const restUrl = `http://127.0.0.1:${restPort}`;
  await ready(async () => {
    try {
      const response = await fetch(restUrl);
      await response.body?.cancel();
      return response.ok;
    } catch {
      return false;
    }
  });
  test(["tests/e2e_test.ts"], { ...env, CAST_TEST_REST_URL: restUrl });
  console.log("Disposable PostgreSQL and PostgREST integration checks passed.");
} catch (error) {
  console.error(error instanceof Error ? error.message : "Integration runner failed");
  process.exitCode = 1;
} finally {
  for (const name of [rest, db]) spawnSync("docker", ["rm", "--force", name], { stdio: "ignore" });
  spawnSync("docker", ["network", "rm", prefix], { stdio: "ignore" });
}
