# AGENTS.md

## Repo at a glance

Turborepo monorepo. Primary language: TypeScript for services/apps, C++23 for shard-engine & orchestrator core. No CI, no OpenCode config, no releases configured.

---

## Package management

## Package management

- **Package manager is npm@11.12.1** (pinned via `packageManager` field in root `package.json`). Use `npm`, not yarn/pnpm, for all installs and scripts.
- npm **workspaces** cover `packages/*`, `services/*`, `apps/*`, `tools/*`.
- No lockfile-guard commands in CI; run `npm install` at the root for first-time setup.

---

## Running things

### Full monorepo

| Command          | What it does                                                                                                    |
| ---------------- | --------------------------------------------------------------------------------------------------------------- |
| `npm run build`  | Runs `turbo build` across all packages in dependency order                                                      |
| `npm run lint`   | Runs `turbo lint`                                                                                               |
| `npm run format` | Runs `prettier --write .` at the root (Prettier config: 2-space tabs, semicolons, 100-col, trailing commas ES5) |
| `npm run clean`  | `turbo clean`                                                                                                   |

Per-package: any workspace also exposes its own `build` / `lint` / `format` scripts (flat keys, not nested `scripts: { build: … }` in workspace `package.json`).

### Individual workspace (run from workspace dir)

```bash
cd packages/<name> && npm run build   # tsc
cd services/<name> && npm run dev      # tsx src/index.ts
```

### Developer servers

| Service                       | Port                                               | Entry                                                    |
| ----------------------------- | -------------------------------------------------- | -------------------------------------------------------- |
| Orchestrator (C++ server.cpp) | 8080 inside container → host 3000 (docker-compose) | `services/orchestrator/`                                 |
| Orchestrator (TS stub)        | 3000                                               | `services/orchestrator/src/index.ts` — health-check only |
| Postgres                      | 5432                                               | `cloudmine:cloudmine`                                    |
| Customer dashboard            | 3002                                               | `apps/customer-dashboard/`                               |
| Load-tester                   | standalone                                         | `tools/load-tester/`                                     |

```bash
docker-compose up orchestrator postgres  # starts orchestrator + DB
```

Testnet has its own compose file: `cd tools/testnet && ./run_testnet.sh`

---

## Build systems — two distinct chains

This repo mixes two build systems. **Do not assume `tsc` builds everything.**

### TypeScript packages/services

All packages under `packages/*` and `services/*` (except shard-engine) use:

- `tsc` for build
- `tsx` for dev
- `tsconfig.base.json` is extended by each package's `tsconfig.json`

### Shard engine — C++/CMake (`packages/shard-engine/`)

Three targets, not managed by turbo:

```bash
cd packages/shard-engine

# Native tests (requires OpenSSL, Catch2 fetched by CTest/FetchContent)
npm run build:native && cd build-native && ctest --output-on-failure

# Native library (no WASM)
npm run build:native

# WASM (requires Emscripten toolchain, OpenSSL)
npm run build          # reads emscripten-toolchain.cmake
```

- Native tests use Catch2 v3.5.4 (downloaded at cmake configure time).
- WASM build exports: `_shard_split`, `_shard_get_manifest_json`, `_shard_reconstruct`, `_shard_free`, `_shard_free_string`.
- WASM requires Emscripten with `-s USE_OPENSSL=1`.

### Orchestrator C++ (`services/orchestrator/` server.cpp)

The C++ binary is **built and tested separately from TypeScript**:

```bash
cd services/orchestrator
cmake -B build-native && cmake --build build-native
cd build-native && ctest --output-on-failure
```

`services/orchestrator/src/index.ts` is a thin TS stub (only `/health` route). The real API server code lives in `server.cpp`. The C++ server listens on **port 8080** by default; docker-compose maps **container port 3000 → host 3000**.

---

## Workspace ownership (what lives where)

| Workspace                       | Language                 | Role                                                               |
| ------------------------------- | ------------------------ | ------------------------------------------------------------------ |
| `packages/shard-engine/`        | C++23 → WASM             | Reed-Solomon erasure coding + AES-256-GCM encryption               |
| `packages/proof-system/`        | TypeScript               | PoS + PoC challenge/response (stub — only re-exports shared-types) |
| `packages/shared-types/`        | TypeScript               | Shared type definitions                                            |
| `packages/token-ledger-client/` | TypeScript               | Client SDK for token ops (stub — only re-exports shared-types)     |
| `services/orchestrator/`        | TypeScript + C++         | Miner registry, shard routing, re-replication, HTTP API            |
| `services/token-ledger/`        | TypeScript               | Off-chain ledger (stub)                                            |
| `services/instance-runner/`     | TypeScript               | Deployment pipeline (stub)                                         |
| `services/edge-proxy/`          | TypeScript               | Public HTTP edge/CDN (stub)                                        |
| `apps/customer-dashboard/`      | Next.js 16 + Tailwind v4 | Customer-facing cloud console                                      |
| `apps/miner-app/`               | React Native 0.85        | Android-first miner UI                                             |
| `tools/load-tester/`            | TypeScript               | Stress testing (stub)                                              |
| `tools/testnet/`                | —                        | Empty — Phase 1 local simulation not yet built                     |
| `contracts/`                    | —                        | Empty — Phase 4 blockchain, not started                            |

---

## Customer dashboard — Next.js 16 gotcha

`apps/customer-dashboard/package.json` uses `next@^16.2.4` and `react@19.x`. Next.js 16 introduces breaking changes vs earlier versions (file conventions, route config, cache APIs). **Before writing any dashboard code, read `node_modules/next/dist/docs/` for the exact version shipped here.** The `apps/customer-dashboard/AGENTS.md` file exists as a reminder.

---

## Orchestrator API surface (from actual source code)

`services/orchestrator/src/server.cpp` implements this custom HTTP server (no framework):

```
GET  /miners           list all miner IDs
GET  /miners/online    list online miner IDs
GET  /miner/:id        get single miner
POST /miner/register   register a new miner
POST /heartbeat        heartbeat from a known miner
POST /check-offline    list currently offline miners
```

Port in `docker-compose.yml`: **3000** (maps to port **8080** in the container — the C++ server's default port).
`src/index.ts` (Fastify stub) also binds **3000** — both agree with the exposed host port, but route traffic to different daemons.
The docker-compose `command` has been updated to run `/app/build-native/orchestrator-server` (the C++ binary) instead of `npm run dev`.
A `Dockerfile` (services/orchestrator/Dockerfile) exists: gcc:13 base, installs cmake + git + libpq-dev + pkg-config, builds via CMake, exposes port 8080, CMD runs `build-native/orchestrator-server`.

### Orchestrator Postgres persistence

New files under `services/orchestrator/`:

| File                                      | Role                                                                                                                                             |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| `include/orchestrator/postgres_store.hpp` | RAII wrapper around a `PGconn*`, exposes `insert_miner`, `update_miner`, `load_miners`, `mark_offline_if_stale`                                  |
| `src/postgres_store.cpp`                  | Implementation using the libpq C API                                                                                                             |
| `CMakeLists.txt`                          | `find_package(PkgConfig)` + `pkg_check_modules(PQ REQUIRED libpq)`; links `orchestrator-lib` and `orchestrator-server` against `${PQ_LIBRARIES}` |

**Activation**: set `DATABASE_URL` env var (libpq keyword format, e.g. `host=postgres user=cloudmine password=cloudmine dbname=cloudmine`). If not set or the connection fails, the server falls back to in-memory storage and logs a warning — it never crashes.

**DB schema** (run via `psql` inside the postgres container — see comment at top of `postgres_store.cpp`):

```sql
CREATE TABLE IF NOT EXISTS miners (
  id                  TEXT PRIMARY KEY,
  wallet_address      TEXT NOT NULL DEFAULT '',
  country             TEXT NOT NULL DEFAULT '',
  region              TEXT NOT NULL DEFAULT '',
  latitude            DOUBLE PRECISION DEFAULT 0,
  longitude           DOUBLE PRECISION DEFAULT 0,
  storage_bytes_avail BIGINT NOT NULL DEFAULT 0,
  storage_bytes_used  BIGINT NOT NULL DEFAULT 0,
  compute_units_avail INTEGER NOT NULL DEFAULT 0,
  compute_units_used  INTEGER NOT NULL DEFAULT 0,
  status              TEXT NOT NULL DEFAULT 'offline',
  last_heartbeat      TIMESTAMPTZ NOT NULL DEFAULT now(),
  uptime_pct          INTEGER NOT NULL DEFAULT 0,
  reputation_score    INTEGER NOT NULL DEFAULT 100,
  is_charging         BOOLEAN NOT NULL DEFAULT false,
  battery_pct         INTEGER NOT NULL DEFAULT 0,
  cpu_pct             REAL NOT NULL DEFAULT 0,
  thermal_state       TEXT NOT NULL DEFAULT 'nominal',
  network_type        TEXT NOT NULL DEFAULT 'none'
);

CREATE TABLE IF NOT EXISTS shard_assignments (
  id                 SERIAL PRIMARY KEY,
  shard_id           TEXT NOT NULL,
  miner_id           TEXT NOT NULL REFERENCES miners(id) ON DELETE CASCADE,
  customer_id        TEXT NOT NULL,
  replication_factor INTEGER NOT NULL DEFAULT 3,
  assigned_at        TIMESTAMPTZ NOT NULL DEFAULT now(),
  verified           BOOLEAN NOT NULL DEFAULT false
);
```

On startup the server hydrates all rows from `miners` into the in-memory registry if the tables exist, then continues serving. Mutations (register, heartbeat) are persisted after the in-memory write succeeds.

---

## Style / formatting

- ESLint: flat config (`eslint.config.js`), Prettier enforced as ESLint error.
- Prettier: trailing commas (ES5), 2-space indent, semicolons, double quotes, 100-col, TS target ES2022.
- `eslint.config.js` ignores: `dist`, `build`, `.turbo`, `*.wasm`, `android/**`, `ios/**`.
- No `tsc --noEmit` step in the root scripts — typechecking is implicit during `turbo build`.

---

## What's empty / not yet started

- `contracts/` — no files (Phase 4 blockchain not started).
- `apps/miner-app/` — beyond basic React Native scaffolding; actual background service and resource monitoring not implemented.
- `docs/architecture.md`, `docs/tokenomics.md` — exist but no CI-enforced verification (docs are prose, not executable).

### testnet (tools/testnet/) — populated

Files in `tools/testnet/` (Phase 1D CI pipeline):

| File                 | Role                                                                                                              |
| -------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `docker-compose.yml` | Orchestrator + Postgres + 10 simulated miners (separate volumes per miner)                                        |
| `Dockerfile`         | Python 3.12 miner image, installs `requests`, exposes port 9100                                                   |
| `miner_server.py`    | HTTP blob store: `GET /health`, `POST /shard?shard_id=…`, `GET /shard?shard_id=…`                                 |
| `shard_engine.py`    | Pure-Python GF(2^8) Reed-Solomon match to C++ shard-engine (XOR cipher — test-only, not production-cryptographic) |
| `run_testnet.py`     | End-to-end Python driver: register → heartbeat → shard → distribute → kill 3 → reconstruct → verify SHA-256       |
| `run_testnet.sh`     | Shell wrapper: `docker-compose up` then `python3 run_testnet.py`                                                  |
| `requirements.txt`   | `requests`, `numpy`                                                                                               |

**Quick-start** (from `tools/testnet/`):

```bash
./run_testnet.sh            # starts stack + runs pipeline (takes ~5-10 min first time)
# or, if stack already running:
./run_testnet.sh skip-start
```

The runner uses direct host-volume reads, not HTTP, for shard distribution and collection — no relay needed. Each miner gets its own volume (`./test_data/miner-XXX`). The orchestrator C++ server is contacted over HTTP only for the register/heartbeat/miners handshake.
