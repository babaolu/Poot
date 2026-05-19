# Poot

A decentralized physical infrastructure network (DePIN) that bridges mobile miners with cloud customers through a demand-gated token economy.

## What is Poot?

Poot is a platform with two faces:

- **Miners** use a mobile app that looks like a crypto mining app. In reality, it lends the phone's residual CPU, RAM, and storage to a distributed network.
- **Customers** get a standard cloud hosting dashboard — deploy websites, run server instances, store files — at a fraction of AWS/GCP prices.

The bridge: tokens are only minted when real customer instances actively consume miner resources. No demand = no mining = no inflation.

## Core Principles

1. **Demand-gated minting** — Token emission is strictly proportional to compute and storage served to paying customers
2. **Residual-only resource usage** — The miner app never causes lag, overheating, or perceptible battery drain
3. **No single phone knows anything useful** — Customer data is sharded across multiple miners using erasure coding (Reed-Solomon) with encryption
4. **K-redundancy** — Every shard has at least 3 physical copies. Customer uptime survives individual miner churn
5. **Consumer cloud UX** — The customer side feels indistinguishable from DigitalOcean or Render

## Architecture

```
poot/
├── packages/
│   ├── shard-engine/          # C++23, compiled to WASM (Reed-Solomon + AES-256-GCM)
│   ├── proof-system/          # PoS + PoC challenge/response logic
│   ├── token-ledger-client/   # Client SDK for token operations
│   └── shared-types/          # TypeScript types shared across services
│
├── services/
│   ├── orchestrator/          # Miner registry, shard routing, re-replication (C++23, custom HTTP)
│   ├── token-ledger/          # Off-chain ledger (Phase 2), on-chain bridge (Phase 4)
│   ├── instance-runner/       # Customer deployment pipeline
│   └── edge-proxy/           # Public HTTP edge, CDN, domain routing
│
├── apps/
│   ├── miner-app/             # React Native (Android primary, iOS secondary)
│   └── customer-dashboard/   # Next.js + TailwindCSS customer console
│
├── contracts/                 # Phase 4 — smart contracts
│
├── tools/
│   ├── testnet/              # Local simulation environment
│   └── load-tester/         # Stress test shard distribution
│
└── docs/                     # Architecture, tokenomics, API reference
```

## Tech Stack

| Layer | Technology |
|---|---|
| Shard engine | C++23 → WASM (Emscripten) |
| Miner app | React Native (Android primary) |
| Orchestrator | C++23 (custom HTTP, libpq persistence) |
| Customer dashboard | Next.js + TailwindCSS |
| Database | PostgreSQL + TimescaleDB |
| Peer networking | libp2p |
| Container runtime | Wasmtime (WebAssembly) |
| Edge proxy | Caddy |
| Monorepo | Turborepo + npm workspaces |

## Phases

- **Phase 1** — Foundation: shard engine, miner node daemon, orchestrator, local testnet
- **Phase 2** — Proof system and token ledger: PoS, PoC, off-chain ledger
- **Phase 3** — Customer cloud layer: dashboard, instance runner, edge proxy
- **Phase 4** — Decentralization: blockchain layer, decentralized orchestrator
- **Phase 5** — Growth: geographic clustering, reputation, mobile-first markets

## Quick Start

```bash
# Install dependencies
npm install

# Build all TypeScript packages
npm run build

# Build the C++ orchestrator binary
cd services/orchestrator && cmake -B build-native && cmake --build build-native

# Run linter
npm run lint

# Start development (TypeScript stub — health-check only, port 3000)
cd services/orchestrator && npm run dev

# Start orchestrator C++ server (real API, port 8080 inside container → 3000 host)
cd services/orchestrator && ./build-native/orchestrator-server
```

## License

[To be determined]
