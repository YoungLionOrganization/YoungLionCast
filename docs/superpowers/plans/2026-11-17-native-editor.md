# Native Editor Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans task by task.

**Goal:** Implement the approved native desktop editor and account integration. **Architecture:** Qt
Widgets models/controllers call a C++ libobs adapter and a Qt NetworkAuth account client. Supabase
Edge extends the existing secure platform OAuth flow with a native broker. **Tech Stack:** Qt
6.12.0, C++20, CMake, QtTest, libobs 33.0.1, QtKeychain, existing Deno backend. **Spec:**
../specs/2026-11-17-native-editor.md

## Global Constraints

- Qt 6.12 only; isolated SDK, no downgrade. C++ native UI/media, separate QSS.
- GPL-3.0-or-later; preserve upstream notices and lock immutable dependency revisions.
- Screens/windows/game support is capability based; no silent full-screen fallback.
- No tokens in project/settings/log files. Offline editing does not require login.
- Physical three-platform QA and live OAuth are release gates distinct from compiled code.

## Review Focus

- Untrusted/malformed projects must not mutate the current document partially.
- Source removal and preview teardown must not access freed GPU/source objects.
- Window loss must not switch to another captured window/display.
- Stale network callbacks and logout must not restore an old account.
- Native handoff URLs must not allow another browser/user to attach a victim account.

### Task 1: Build and document core

Files: CMakeLists.txt, desktop/CMakeLists.txt, desktop/src/document.*,
desktop/tests/document_test.cpp. Produces: SourceSpec/SceneSpec/SceneDocument JSON validation,
atomic load/save, undoable editing.

- [x] Acquire verified pinned Qt/libobs SDK/source; create isolated build and dependency lock.
- [x] Write document tests, watch fail, implement schema and transaction-safe model operations.
- [x] Run native document tests and commit.

### Task 2: Capture and composition

Files: desktop/src/media_engine._, desktop/src/preview._, desktop/tests/media_test.cpp. Consumes:
validated document snapshots. Produces: capability-driven sources and native GPU preview.

- [x] Write capability/lifecycle/source-transform tests and observe failure.
- [x] Implement RAII scene/source ownership, selective plugins, platform capture selection and GPU
      display.
- [x] Verify available real media paths; keep physical capture checks distinct; commit.

### Task 3: Native UI and themes

Files: desktop/src/main_window._, desktop/src/theme._, desktop/styles/_, desktop/translations/_.
Consumes: document and media interfaces. Produces: functional OBS-layout editor, no unsupported
controls.

- [x] Add QtTest edit/undo/layout/theme scenarios and observe missing behavior.
- [x] Implement docks, source dialogs, transforms, project files, keyboard, translations and
      external QSS.
- [x] Run offscreen UI tests, inspect screenshots, fix relevant layout findings, commit.

### Task 4: Account and native broker

Files: desktop/src/accounts._, desktop/src/credentials._, Edge native broker and migration, account
tests. Produces: public-client S256 login, OS vault, safe connection
listing/linking/reconnect/disconnect.

- [x] Exercise the native browser PKCE request and broker owner/session/transfer/cancel checks.
- [x] Implement Qt NetworkAuth + QtKeychain and extend protected server handoff contracts.
- [x] Run native loopback/API tests and existing backend unit/Postgres tests; commit.

### Task 5: Packaging, regression and review

Files: native CI, platform deployment helpers and docs/native.md.

- [x] Build install/deployment packages for available host, CI matrix definitions for all three.
- [x] Run full suites, inspect rendered UI and package contents; record live/hardware gaps honestly.
- [x] Fresh whole-branch review; fix Important/Critical findings with regressions; commit.
