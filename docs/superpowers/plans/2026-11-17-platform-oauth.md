# YoungLion Cast OAuth Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement task by task.

**Goal:** Deliver runnable, tested Twitch/Kick account infrastructure.

**Architecture:** Supabase Edge Functions verify YoungLion sessions and execute short OAuth
transactions. Private Postgres state provides atomic ownership, token leases, and durable cleanup;
only safe connection metadata is exposed under owner RLS.

**Tech Stack:** Deno TypeScript, Web Crypto, Supabase Auth/PostgREST, PostgreSQL 17.

**Spec:** ../specs/2026-11-17-platform-oauth-design.md

## Global Constraints

- Separate sign-in and publishing OAuth applications; one owner per stable platform identity.
- No plaintext secrets in public tables, clients, logs, or committed environment files.
- No video engine, team sharing, or YouTube in v1.
- Provider credentials and the existing Account source are unavailable; live integration is not
  claimed.

## Review Focus

- Cross-provider and cross-owner callback substitution must fail.
- A revoked grant cannot be resurrected by a concurrent refresh.
- Abandoned confirmations must not leave retained third-party grants indefinitely.
- Invalid provider bodies must never choose an arbitrary user or fabricate verified email.
- Browser redirects must not disclose tokens or allow arbitrary destinations.

### Task 1: Provider and cryptographic boundaries

Files: functions/_shared/{providers,crypto,http}.ts, tests/providers_test.ts, tests/crypto_test.ts.
Interfaces: provider authorization/code exchange/current user/refresh/revoke; context-bound
encrypted JSON.

- [ ] Write failing protocol, malformed-body, PKCE, and ciphertext-isolation tests; observe failure.
- [ ] Implement fixed-host bounded HTTP, exact provider adapters, AES-GCM keyring and PKCE helpers.
- [ ] Run provider and crypto tests, type-check, commit.

### Task 2: Durable account and OAuth state

Files: supabase/migrations/*_platform_oauth.sql, functions/_shared/store.ts, tests/database_test.ts.
Interfaces: service-only RPCs consumed by HTTP handlers; owner-readable metadata under RLS.

- [ ] Write real PostgreSQL tests for owner isolation, state replay, identity collision, refresh
      leases, token cleanup, and concurrent disconnect; observe failure.
- [ ] Add migration and typed repository; no network I/O inside SQL transactions.
- [ ] Run tests against disposable PostgreSQL, commit.

### Task 3: Edge API and scheduled maintenance

Files: functions/{platform-connections,kick-userinfo,oauth-maintenance}/index.ts and shared
handlers. Interfaces: OAuth start/callback/confirm, list, reconnect, disconnect, login-token
registration.

- [ ] Write route/auth/real-store integration tests; observe failure.
- [ ] Implement account status/session verification, rate limits, feature flags, provider callbacks,
      encrypted pending grants, scheduled validation/cleanup with durable leases.
- [ ] Run complete tests, check, lint; commit.

### Task 4: Client, configuration, and operations

Files: clients/account.ts, scripts/configure-auth.ts, docs/{setup,api,architecture}.md, CI.
Interfaces: explicit login vs publishing methods; dry-run-first Auth setup.

- [ ] Test client request/confirmation and configuration validation before implementation.
- [ ] Add setup scripts, environment example, local test runner, deployment/rollback guidance and
      CI.
- [ ] Run full verification, request fresh whole-project review, fix important findings, commit.
