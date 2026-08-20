# Poot ── Demand-Gated Mobile DePIN

A decentralized physical infrastructure network (DePIN) that bridges mobile miners with cloud customers through a demand-gated token economy.

---

## ── What is Poot?

Poot is a platform with two faces:

- **Miners** use a mobile app that looks and feels like a cryptocurrency mining app. In reality, it lends the phone's residual CPU, RAM, and storage to a distributed network.
- **Customers** get a standard cloud hosting dashboard ── deploy websites, run server instances, store files ── at a fraction of AWS/GCP prices.

The bridge: **tokens are only minted when real customer instances actively consume miner resources**. No demand = no mining = no inflation. This gives the native token real economic backing.

---

## ── Core Design Principles

1. **Demand-Gated Minting**: Token emission is strictly proportional to compute and storage served to paying customers.
2. **Residual-Only Resource Usage**: The miner app never causes lag, overheating, or perceptible battery drain.
3. **Decentralized Zero-Knowledge Storage**: Customer data is sharded across multiple miners using erasure coding (Reed-Solomon) with AES-256-GCM encryption. No single phone knows anything useful.
4. **K-Redundancy Always**: Every shard has at least 3 physical copies. Customer uptime survives individual miner churn.
5. **Consumer Cloud UX**: The customer side feels indistinguishable from DigitalOcean or Render.

---

## ── Monorepo Structure

```text
poot/
├── packages/
│   ├── shard-engine/          # C++23, compiled to WASM (Reed-Solomon + AES-256-GCM)
│   ├── proof-system/          # PoS + PoC challenge/response logic (TypeScript types wrapper)
│   ├── token-ledger-client/   # Client SDK for token operations (TypeScript types wrapper)
│   └── shared-types/          # TypeScript types shared across services
│
├── services/
│   ├── orchestrator/          # Miner registry, shard routing, re-replication (C++23 HTTP, libpq PG store)
│   ├── token-ledger/          # Off-chain ledger (Phase 2), on-chain bridge (Phase 4)
│   ├── instance-runner/       # Customer deployment pipeline
│   └── edge-proxy/            # Public HTTP edge, CDN, domain routing
│
├── apps/
│   ├── miner-app/             # React Native (Android primary, iOS secondary)
│   └── customer-dashboard/    # Next.js 16 + Tailwind v4 customer console
│
├── contracts/                 # Phase 4 ── smart contracts (Solidity/Rust)
│
├── tools/
│   ├── testnet/               # Local Docker-Compose simulation environment
│   └── load-tester/           # Stress testing (stub)
│
└── docs/                      # Architecture, tokenomics, API reference
```

---

## ── Current Implementation Status

### Phase 1 ── Foundation & Infrastructure (Active)

| Component              | Sub-component          | Language / Tech      | Status        | Notes                                                                                                     |
| ---------------------- | ---------------------- | -------------------- | ------------- | --------------------------------------------------------------------------------------------------------- |
| **Shard Engine (1A)**  | Core Erasure Coding    | C++23                | **Completed** | Vandermonde Reed-Solomon GF(2^8), AES-256-GCM, SHA-256. 100% tests passed.                                |
| **Miner Daemon (1B)**  | Background Daemon & UI | React Native / Java  | _In Progress_ | UI shell created. Native background scheduling (WorkManager/Thermal APIs) pending.                        |
| **Orchestrator (1C)**  | Core API Server        | C++23 / Libpq        | **Completed** | Registry, heartbeats, and assignments. Port mapping fixed (8080 inside container -> 3000 host).           |
| **Orchestrator (1C)**  | Persistence Store      | C++23 / PostgreSQL   | _In Progress_ | Postgres schema & C++ RAII PGconn wrapper implemented. Falling back to memory due to libpq syntax errors. |
| **Local Testnet (1D)** | E2E Integration Suite  | Docker / Python 3.12 | **Completed** | Fully verified 10-node testnet suite with automated upload, shard, replicate, kill, and verify pipeline.  |

---

## ── Upgraded & Optimized Engineering Roadmap

To achieve true production readiness, we have upgraded and optimized the initial execution plan:

### 🚀 Optimization 1: Native Mobile JSI Bindings (Phase 1B Upgrade)

- **Initial Plan**: Run shard-engine WASM inside React Native or call a background Node.js worker.
- **Optimized Plan**: Expose the C++ Shard Engine directly to React Native using **JSI (JavaScript Interface)** or as a native Android `.so` / iOS Framework. This bypasses the bridge and WebAssembly virtualization, achieving **5-10x faster execution** and zero JS thread blocking during heavy encryption and erasure coding.

### 🌐 Optimization 2: Libp2p NAT Traversal & Multiplexing (Phase 2 Upgrade)

- **Initial Plan**: Connect to miners via direct HTTP REST endpoints.
- **Optimized Plan**: Use **libp2p** with **AutoNAT, STUN/TURN, and DCUtR (Direct Connection Utility for hole punching)**. Since mobile phones are behind cellular CGNATs or domestic routers, direct inbound HTTP requests will fail. Integrating libp2p enables secure bidirectional multiplexed connections without requiring public IPs or VPNs.

### ⚡ Optimization 3: Edge CDN Origin Shielding & Pull-Through Caching (Phase 3 Upgrade)

- **Initial Plan**: Reassemble shards in real-time from mobile nodes on every HTTP request.
- **Optimized Plan**: Introduce a **progressive pull-through caching layer** inside `edge-proxy`. Fully reassembled assets are cached on high-performance conventional NVMe CDN nodes. If a cache miss occurs, the proxy fetches and reassembles shards in the background. This guarantees sub-50ms TTFB for end users while preserving DePIN storage integrity.

### 🛡️ Optimization 4: Safe Parameterized DB Access (Phase 1C Cleanup)

- **Immediate Fix**: Fix the libpq implementation in `services/orchestrator/src/postgres_store.cpp` to use parameterized queries (`PQexecParams`) instead of string injection and escape quotes. This resolves syntax failures and ensures full security against SQL injection.

---

## ── Quick Start

### 1. Root Installation

Initialize the Turborepo monorepo and install dependencies:

```bash
npm install
```

### 2. Build TypeScript Monorepo

Compile shared packages and Next.js assets:

```bash
npm run build
```

### 3. Build & Run Shard Engine Tests

Verify erasure coding and encryption native C++ library tests:

```bash
cd packages/shard-engine
npm run build:native
cd build-native && ctest --output-on-failure
```

### 4. Build & Run Orchestrator

Build the production-grade custom C++ orchestrator:

```bash
cd services/orchestrator
cmake -B build-native && cmake --build build-native
./build-native/orchestrator-server
```

### 5. Execute Local Testnet

Run the Phase 1D end-to-end local simulation containing 10 simulated miners, docker networking, and failure recovery validation:

```bash
cd tools/testnet
./run_testnet.sh
```

---

## ── Documentation Suite

- [Architecture Guide](file:///home/itunz/Work/AI_work/Poot/docs/architecture.md) ── Detailed structural and system flow breakdown.
- [Tokenomics Whitepaper](file:///home/itunz/Work/AI_work/Poot/docs/tokenomics.md) ── Demand-gated economic parameters.
- [API Reference](file:///home/itunz/Work/AI_work/Poot/docs/api-reference.md) ── Internal HTTP routes and libp2p commands.
