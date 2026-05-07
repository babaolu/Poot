# Poot Architecture

## Overview

Poot is a decentralized physical infrastructure network (DePIN) that bridges mobile miners with cloud customers.

## Components

### Shard Engine (packages/shard-engine/)
C++23 Reed-Solomon erasure coding with AES-256-GCM encryption, compiled to WASM.

### Orchestrator (services/orchestrator/)
Node.js + TypeScript + Fastify. Manages miner registry, shard routing, and re-replication.

### Miner App (apps/miner-app/)
React Native CLI (Android primary). Background service lending phone resources.

### Customer Dashboard (apps/customer-dashboard/)
Next.js + TailwindCSS. Developer-facing cloud console.

### Edge Proxy (services/edge-proxy/)
Public HTTP edge, CDN, domain routing.

### Token Ledger (services/token-ledger/)
Off-chain ledger (Phase 2), on-chain bridge (Phase 4).
