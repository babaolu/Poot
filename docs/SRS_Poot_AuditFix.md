# Software Requirements Specification (SRS)

## CloudMine — Demand-Gated Mobile Compute & Storage Network

**Document version:** 1.0
**Status:** Draft for engineering handoff
**Prepared for:** Solo-founder build, executed via AI coding agents
**Conforms loosely to:** IEEE 830 SRS structure, adapted for an AI-agent-executed build process

---

## Revision Note

This SRS supersedes the informal build prompt used to produce the current `Poot` codebase. It incorporates findings from a direct code audit of that codebase (build verified, tests executed, and source read line-by-line). Every requirement below that corrects a defect found in that audit is explicitly marked **[AUDIT-FIX]** so the build agent understands it is not optional or stylistic — it is a correction to prior work.

---

## 1. Introduction

### 1.1 Purpose

This document specifies the functional and non-functional requirements for CloudMine, a decentralized physical infrastructure network (DePIN) that converts idle smartphone compute, storage, and bandwidth into a consumer-facing cloud hosting platform, settled through a demand-gated native token.

This SRS is written to be handed directly to an AI coding agent as an authoritative reference. Where a requirement is ambiguous, the agent should resolve it in favor of the stated design principles in Section 2.3, document the decision in `/docs/decisions.md`, and continue — not halt and ask, except where explicitly marked as a required checkpoint.

### 1.2 Scope

The system consists of:

- A mobile application ("miner app") that lends a phone's residual compute, storage, and bandwidth to the network in exchange for tokens.
- A backend orchestration layer that manages miner registration, shard placement, replication, and proof verification.
- A cryptographic shard engine that splits, encrypts, distributes, and reconstructs customer data and workloads across many miners.
- A token ledger and economic engine that mints tokens strictly in proportion to verified resource delivery to paying customers.
- A customer-facing cloud dashboard and API for deploying static sites, server instances, and object storage.
- An edge proxy layer that terminates public traffic and bridges it to the miner network.
- (Later phase) A blockchain layer that decentralizes token issuance, staking, and slashing.

Out of scope for this document: legal entity formation, token securities classification, and marketing collateral. These are referenced where they constrain engineering decisions (e.g., Section 9) but not specified in detail.

### 1.3 Intended Audience

- The AI coding agent(s) executing the build.
- The founder, in reviewing and prioritizing agent output.
- Any future engineer or auditor reviewing the system for correctness or security.

### 1.4 Definitions, Acronyms, Abbreviations

| Term              | Meaning                                                                                          |
| ----------------- | ------------------------------------------------------------------------------------------------ |
| Miner             | A mobile device (primarily Android) running the miner app and contributing resources             |
| Shard             | An encrypted fragment of a larger data object or workload, produced by erasure coding            |
| DGM               | Demand-Gated Minting — tokens minted only in proportion to verified, customer-consumed resources |
| PoS               | Proof of Storage — cryptographic challenge proving a miner still holds a shard                   |
| PoC               | Proof of Compute — verification that a miner executed a workload correctly                       |
| Residual capacity | The compute/storage/bandwidth a phone can lend without degrading its owner's normal use          |
| Orchestrator      | The service responsible for miner registry, shard assignment, and replication                    |
| Edge proxy        | Conventional (non-phone) infrastructure that terminates public HTTP(S) traffic                   |
| TEE               | Trusted Execution Environment — hardware-isolated secure compute region on modern phones         |
| K-of-N            | Erasure coding scheme where any K of N total shards reconstruct the original data                |
| CID               | Content identifier — a hash-derived address for a piece of content, à la IPFS                    |

### 1.5 References

- Acurast (smartphone TEE compute network) — architectural reference for confidential mobile compute.
- Filecoin whitepaper — reference for Proof-of-Storage and Proof-of-Spacetime design.
- Akash Network SDL — reference for deployment manifest design.
- RFC 5116 (AEAD interface), NIST SP 800-38D (AES-GCM) — cryptographic requirements in Section 6.
- libp2p specification — peer discovery and NAT traversal reference.

---

## 2. Overall Description

### 2.1 Product Perspective

CloudMine is a new, independent system. It is not an extension of an existing product. It draws architectural lessons from Acurast, Filecoin, and Akash but is not compatible with or dependent on any of them.

The product has two distinct user-facing surfaces that must never leak implementation details into each other:

1. **Miner surface** (mobile app): presented as a mining/passive-income app. Users should never need to understand sharding, erasure coding, or orchestration to use it.
2. **Customer surface** (web dashboard + API): presented as a conventional cloud hosting product. Customers should never need to know their data is stored on mobile phones.

### 2.2 Product Functions (Summary)

- Register and monitor miner devices; measure and report residual capacity.
- Accept customer deployment requests (static sites, containerized apps, object storage).
- Shard, encrypt, distribute, and replicate customer data across miner devices.
- Continuously verify miners are honestly storing/computing what they claim.
- Reassemble and serve customer data/workloads to end users via an edge proxy with acceptable latency.
- Mint tokens strictly in proportion to verified delivery, and settle payments between customers and miners.
- Provide dashboards for both miners (earnings, uptime, contributed resources) and customers (deployments, billing, logs).

### 2.3 Design Principles (Non-negotiable)

These principles override any specific requirement below if a conflict is discovered during implementation. The agent must not silently violate these; if a requirement below appears to conflict with a principle, the principle wins and the conflict must be logged in `/docs/decisions.md`.

1. **DP-1 — Demand-gated minting.** No active, paying customer resource consumption ⇒ no token minting. This must be enforced structurally (i.e., there must be no code path that mints tokens without a linked, verified customer consumption event), not just by policy or convention.
2. **DP-2 — Residual-only resource use.** The miner app must never cause a measurable, perceptible degradation of the host device's normal operation. This is measured, not assumed (see Section 5.2).
3. **DP-3 — No single miner holds meaningful data.** Every enforcement point for this (API boundaries, storage write paths) must make it structurally impossible to write an unsharded, unencrypted customer object to a single miner.
4. **DP-4 — Redundancy survives churn.** The system must tolerate the loss of any single miner, and any two simultaneously, without customer-visible data loss, for the default K-of-N configuration in Section 4.3.
5. **DP-5 — Consumer-grade cloud UX.** A developer using the customer dashboard should be able to deploy a static site without ever learning the word "shard," "miner," or "erasure coding."
6. **DP-6 — Build for a solo founder's velocity.** Where two implementations are functionally equivalent, prefer the one requiring less custom code, fewer hand-built subsystems, and faster iteration — even if the alternative is marginally more performant. **[AUDIT-FIX]** This principle was violated in the prior build (a hand-rolled C++ HTTP server was written instead of using a mature framework). It is restated here explicitly because it was violated once already.

### 2.4 User Classes and Characteristics

| User class                  | Description                                                                         | Technical sophistication                                           |
| --------------------------- | ----------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| Miner                       | Owns an Android (primarily) or iOS phone, installs the app for passive token income | Low — must not require any technical understanding                 |
| Customer (developer)        | Deploys websites/apps/storage, pays for hosting                                     | Moderate to high — comfortable with a cloud dashboard, CLI, or API |
| Platform operator (founder) | Operates the orchestrator, edge proxy, and token ledger                             | High                                                               |

### 2.5 Operating Environment

- Miner app: Android 10+ primary target; iOS as a constrained secondary target (see Section 5.5 for platform-specific constraints).
- Orchestrator, token ledger, instance runner, edge proxy: Linux server environment (containerized), initially single-region, designed for later multi-region deployment.
- Customer dashboard: modern evergreen browsers (Chrome, Safari, Firefox, Edge — last 2 versions).

### 2.6 Assumptions and Dependencies

- Initial deployment targets Nigeria/West Africa; Android device density and mobile data costs in this market inform default configuration (e.g., WiFi-preferred large transfers).
- The system assumes a bootstrapping period during which miner rewards may be subsidized ahead of real customer revenue (see Section 8.4). This is a business assumption with a direct technical requirement: the token ledger must support a distinctly-tagged "subsidy" mint path, separate from and clearly distinguished from demand-gated mints, so DP-1 is never violated or obscured.

---

## 3. System Architecture Overview

### 3.1 Component Diagram (textual)

```
[Miner Phones] <--heartbeat/shard I/O--> [Orchestrator] <--deployment mfst--> [Instance Runner]
      |                                        |                                     |
      |                                   [Token Ledger]                       [Shard Engine]
      |                                        |                                     |
      +------------------ proof challenges -----+                                    |
                                                                                       |
[Customers] --HTTPS--> [Edge Proxy] <----shard fetch/reassembly----------------------+
      |
[Customer Dashboard] --API--> [Instance Runner] / [Token Ledger] (billing)
```

### 3.2 Monorepo Layout (required structure)

```
cloudmine/
├── packages/
│   ├── shard-engine/          # Core erasure coding + encryption
│   ├── proof-system/          # PoS + PoC challenge/response
│   ├── token-ledger-client/   # Client SDK
│   └── shared-types/
├── services/
│   ├── orchestrator/          # Miner registry, shard routing, re-replication
│   ├── token-ledger/          # Off-chain ledger (Phase 2), on-chain bridge (Phase 4)
│   ├── instance-runner/       # Customer deployment pipeline
│   └── edge-proxy/            # Public HTTP edge, CDN, domain routing
├── apps/
│   ├── miner-app/
│   └── customer-dashboard/
├── contracts/                 # Phase 4
├── tools/
│   ├── testnet/
│   └── load-tester/
└── docs/
```

### 3.3 Technology Requirements

| Layer                                 | Required technology                                                                                                      | Rationale / constraint                                                                                                                                                                                                                            |
| ------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Shard engine                          | Rust (preferred) or C++23, compiled to WASM for portability                                                              | Must expose a stable C ABI usable from both native mobile bindings and WASM                                                                                                                                                                       |
| Orchestrator                          | Node.js + TypeScript + Fastify, **or** an async C++/Rust framework with equivalent concurrency (e.g., Drogon, actix-web) | **[AUDIT-FIX]** A single-threaded, blocking, hand-rolled socket server is explicitly prohibited. The orchestrator MUST handle concurrent connections without one request blocking another. See Section 5.1 for the specific defect this corrects. |
| Customer dashboard                    | Next.js + TailwindCSS                                                                                                    | —                                                                                                                                                                                                                                                 |
| Miner app                             | React Native, Android primary                                                                                            | Native modules permitted for performance-critical paths (JSI bindings to the shard engine)                                                                                                                                                        |
| Database                              | PostgreSQL                                                                                                               | Must use parameterized queries exclusively — see Section 6.4                                                                                                                                                                                      |
| Peer networking                       | libp2p                                                                                                                   | For miner discovery and NAT traversal in Phase 2+                                                                                                                                                                                                 |
| Container/execution runtime on miners | WebAssembly (Wasmtime) preferred; minimal sandboxed container as fallback                                                | Full Docker is prohibited on-device — too heavy for phones                                                                                                                                                                                        |
| Edge proxy                            | Caddy or Nginx + custom middleware                                                                                       | Automatic HTTPS required                                                                                                                                                                                                                          |

---

## 4. Functional Requirements

Requirements are numbered `FR-<subsystem>-<n>`. Each has a priority: **Must** (blocking for phase completion), **Should** (expected but not blocking), **Could** (nice to have).

### 4.1 Miner Registration and Resource Reporting

- **FR-MIN-1 (Must):** The miner app SHALL register the device with the orchestrator on first launch, generating a persistent miner ID and wallet address.
- **FR-MIN-2 (Must):** The miner app SHALL report, at minimum every 60 seconds while active: battery level, charging state, CPU load, thermal state, network type (WiFi/cellular), and available storage bytes.
- **FR-MIN-3 (Must):** The miner app SHALL only accept new shard-storage or compute tasks when ALL of the following hold: charging state is true OR battery > 50%; CPU load < 60%; thermal state is nominal (not "serious" or "critical" per platform thermal API); network is WiFi for storage transfers > 5MB.
- **FR-MIN-4 (Must):** The miner app SHALL pause all active background work within 5 seconds of any monitored condition in FR-MIN-3 crossing its threshold, and resume automatically when conditions recover.
- **FR-MIN-5 (Should):** The miner app SHALL display, in a units-obscured "mining" presentation: tokens earned (session/total), estimated contributed storage, uptime percentage, and network status.
- **FR-MIN-6 (Must):** Resource claims reported by a miner (available storage, compute) SHALL be treated as untrusted by the orchestrator and independently spot-verified (see FR-PRF-1 through FR-PRF-4).

### 4.2 Orchestration and Shard Placement

- **FR-ORC-1 (Must):** The orchestrator SHALL maintain a live registry of miners with current capacity, geographic region (IP-geolocation derived, not self-reported), reputation score, and last-heartbeat timestamp.
- **FR-ORC-2 (Must):** The orchestrator SHALL support concurrent handling of at least 500 simultaneous miner connections in the Phase 1 target environment, verified by load test (see Section 4.7).
- **FR-ORC-3 (Must):** Upon a new customer deployment request, the orchestrator SHALL select N miners for shard placement, preferring (in order): matching geographic region, higher reputation score, lower current load.
- **FR-ORC-4 (Must):** The orchestrator SHALL detect a miner as offline after 3 consecutive missed heartbeats (default heartbeat interval 60s ⇒ 180s detection ceiling) and trigger re-replication of any shards that miner held below the redundancy floor.
- **FR-ORC-5 (Must):** All orchestrator state mutations (miner registration, shard assignment, replication events) SHALL be persisted to PostgreSQL. In-memory-only operation is permitted solely as a documented local-development fallback, never as a production mode.

### 4.3 Sharding, Encryption, and Reconstruction

- **FR-SHD-1 (Must):** The shard engine SHALL split any input object into `data_shards` (default 6) + `parity_shards` (default 3) using Reed-Solomon erasure coding, such that any 6 of the 9 shards reconstruct the original object exactly.
- **FR-SHD-2 (Must):** Each shard SHALL be independently encrypted with AES-256-GCM before leaving the shard engine's trust boundary.
- **FR-SHD-3 (Must) — [AUDIT-FIX]:** Each shard SHALL use a unique, randomly generated 96-bit IV/nonce. The same (key, IV) pair SHALL NEVER be reused across more than one plaintext encryption operation, under any circumstances. This corrects a defect in the prior implementation where a single IV was reused across all 9 shards of a file, which is a critical AES-GCM nonce-reuse vulnerability (potential plaintext-XOR recovery and forgery). The manifest format SHALL store one IV per shard, not one IV per file.
- **FR-SHD-4 (Must) — [AUDIT-FIX]:** The per-file encryption key SHALL NOT be derived solely from the content hash of the plaintext (convergent encryption). Instead, the key SHALL be derived from a securely generated random value at shard time, encrypted itself with a customer-specific key (envelope encryption), and stored in the manifest in encrypted form. Content-hash-based addressing MAY still be used for deduplication/lookup purposes, but MUST NOT be the sole input to the data encryption key.
- **FR-SHD-5 (Must):** Every shard SHALL be covered by a Merkle tree constructed over all shards of the object, enabling proof-of-inclusion challenges (see 4.4) without requiring a verifier to hold the full object.
- **FR-SHD-6 (Must):** The reconstruction function SHALL verify the reassembled object's content hash against the manifest before returning success, and SHALL fail closed (return an explicit error) on any mismatch.
- **FR-SHD-7 (Must):** No API surface anywhere in the system SHALL permit writing an unsharded customer object directly to a single miner's storage. This SHALL be enforced at the type/interface level (i.e., the function signature accepting data for miner storage must only accept already-sharded, already-encrypted shard objects — never a raw customer blob), not merely by caller discipline.
- **FR-SHD-8 (Should):** Default shard size SHALL be configurable, with a starting default of 1MB per shard, tunable per deployment.

### 4.4 Proof of Storage and Proof of Compute

- **FR-PRF-1 (Must):** The orchestrator SHALL issue a random byte-range storage challenge to each miner holding at least one shard, on average once per 10-minute window per shard held.
- **FR-PRF-2 (Must):** A miner SHALL respond to a storage challenge with the requested bytes plus a valid Merkle proof within 30 seconds. Failure (timeout, wrong bytes, invalid proof) SHALL mark the shard lost, decrement the miner's reputation score, and trigger re-replication.
- **FR-PRF-3 (Should, Phase 2):** For active customer compute instances, the orchestrator SHALL issue periodic deterministic compute-verification tasks and compare results across a redundant group of miners executing the same workload; divergent results SHALL be treated as a Byzantine fault per the slashing policy in Section 8.
- **FR-PRF-4 (Must):** No token minting event (see 4.5) SHALL occur for a given interval of storage or compute unless at least one successful proof was recorded for that interval.

### 4.5 Token Ledger and Minting

- **FR-TKN-1 (Must) — enforces DP-1:** The token ledger SHALL expose exactly one code path for minting tokens tied to verified customer consumption ("demand mint"), and, if a subsidy program is active, exactly one separate, distinctly labeled code path for subsidy mints ("subsidy mint"). These SHALL never share an implementation function, and every ledger entry SHALL be tagged with its mint type.
- **FR-TKN-2 (Must):** A demand mint event SHALL require, as inputs: a verified proof event (FR-PRF-2 or FR-PRF-3 success), a linked active customer billing record, and the current network reward rate. Minting SHALL be computed as:

  `tokens_minted = (storage_bytes_served × storage_rate) + (compute_units_served × compute_rate)`

  where `storage_rate` and `compute_rate` are 0 whenever there is no active, paying customer demand for the relevant resource class.

- **FR-TKN-3 (Must):** Every mint event SHALL be cryptographically signed by the orchestrator/ledger service and independently verifiable by the receiving miner, so a miner can detect if they were shorted.
- **FR-TKN-4 (Should):** The ledger schema SHALL be designed so that off-chain rows can be migrated to on-chain events in Phase 4 without a breaking schema change (i.e., include fields for future on-chain transaction hash, block height, etc., nullable until Phase 4).
- **FR-TKN-5 (Must):** The subsidy mint path (see Section 8.4) SHALL be rate-limited and capped by a configurable total subsidy budget, with remaining budget queryable at any time.

### 4.6 Customer Deployment and Cloud Services

- **FR-CST-1 (Must):** The customer dashboard SHALL support account creation and authentication (email + password, and/or wallet connect).
- **FR-CST-2 (Must):** The dashboard SHALL support deployment of: (a) static sites, (b) containerized/WASM server workloads, (c) object storage buckets.
- **FR-CST-3 (Must):** Every deployment SHALL show live status, uptime percentage, resource usage, and accrued cost.
- **FR-CST-4 (Must):** Billing SHALL support fiat payment (Stripe or regional equivalent) and native token payment.
- **FR-CST-5 (Should):** The dashboard SHALL provide a free tier (minimum: one static site, capped bandwidth/storage) with no payment method required.
- **FR-CST-6 (Must):** Custom domain support SHALL be provided via CNAME to the edge proxy, with automatic TLS certificate issuance.

### 4.7 Testing and Verification Requirements

- **FR-TST-1 (Must) — [AUDIT-FIX]:** The end-to-end testnet SHALL exercise the actual production shard-engine binary/library (via its real bindings), not a reimplementation in another language. A parallel reference implementation MAY exist for cross-validation purposes, but MUST be explicitly labeled as a reference/fuzzing aid in its own directory and documentation, and MUST NOT be the implementation exercised by the pipeline that is reported as "verifying" the production system.
- **FR-TST-2 (Must):** The testnet SHALL simulate at least 10 miners, randomly terminate at least 30% of them mid-operation, and verify successful reconstruction of all affected objects from the remaining miners.
- **FR-TST-3 (Must):** The shard engine test suite SHALL include explicit security-property tests, not only correctness tests. At minimum: (a) a test asserting that encrypting the same plaintext object twice produces different ciphertext and different IVs; (b) a test asserting that no two shards of the same object ever share an IV; (c) a test asserting that a tampered shard fails authentication rather than silently decrypting.
- **FR-TST-4 (Must):** The orchestrator SHALL have a load test demonstrating correct behavior under at least 500 concurrent miner connections (see FR-ORC-2), with p99 heartbeat response latency reported.
- **FR-TST-5 (Should):** Every phase's "definition of done" (Section 10) SHALL be verified by an automated test that can be re-run in CI, not by manual inspection alone.

---

## 5. Non-Functional Requirements

### 5.1 Performance and Concurrency — [AUDIT-FIX]

- **NFR-PERF-1 (Must):** The orchestrator SHALL process multiple miner requests concurrently. A design that serializes all requests through a single blocking accept/handle loop is non-conforming, regardless of language choice. This directly corrects a defect found in the prior implementation, where the orchestrator handled exactly one client connection at a time via a blocking `accept()`/`recv()` loop with no threading or async I/O — a design that cannot scale past a handful of miners.
- **NFR-PERF-2 (Must):** End-to-end shard distribution for a 10MB object across a local 10-node testnet SHALL complete in under 5 seconds.
- **NFR-PERF-3 (Should):** Edge proxy p95 time-to-first-byte for a cached static asset SHALL be under 100ms; for an uncached asset requiring live shard reassembly, under 2 seconds.

### 5.2 Battery, Thermal, and Resource Impact (Miner App)

- **NFR-RES-1 (Must):** The miner app SHALL NOT consume more than 3% of battery per hour while running in background/idle-mining mode, measured on a mid-range reference Android device.
- **NFR-RES-2 (Must):** The miner app SHALL NOT cause the host device's foreground app performance (frame rate, input latency) to degrade in any user-perceptible way while mining is active. This SHALL be validated with a scripted UI-interaction benchmark run concurrently with active mining.

### 5.3 Reliability and Availability

- **NFR-REL-1 (Must):** Customer-facing uptime for an active deployment SHALL survive the simultaneous loss of any 2 miners holding shards of that deployment, given the default K=6-of-9 configuration.
- **NFR-REL-2 (Should):** The orchestrator SHALL have no single point of failure by end of Phase 4 (decentralization phase); a single centralized orchestrator instance is acceptable for Phases 1–3 and SHOULD be documented as a known limitation, not silently treated as final-state architecture.

### 5.4 Security

(Full detail in Section 6 — this subsection summarizes cross-cutting requirements.)

- **NFR-SEC-1 (Must):** All data at rest on a miner device SHALL be encrypted per FR-SHD-2 through FR-SHD-4.
- **NFR-SEC-2 (Must):** All data in transit (miner↔orchestrator, customer↔dashboard, dashboard↔API) SHALL use TLS 1.2 or higher.
- **NFR-SEC-3 (Must):** All database access SHALL use parameterized queries exclusively. String-concatenated or manually-escaped SQL construction is prohibited, even when using an escaping helper function, because it is a recurring source of injection defects and is harder to audit than parameterization. **[AUDIT-FIX]** — the prior implementation used `PQescapeLiteral` with string concatenation rather than `PQexecParams`; while escaping reduces risk, it does not meet this requirement and must be replaced with true parameterized queries.
- **NFR-SEC-4 (Must):** Every miner SHALL be treated as a potentially adversarial actor in all protocol design: assume miners may lie about capacity, attempt to read shard contents, collude with other miners holding related shards, or disappear without notice.

### 5.5 Platform-Specific Constraints

- **NFR-PLAT-1 (Must):** The miner app's background mining service SHALL be implemented using Android WorkManager / Foreground Service APIs, respecting Doze mode and battery optimization restrictions.
- **NFR-PLAT-2 (Must):** iOS support SHALL be explicitly documented as constrained: sustained background compute is not achievable within Apple's background execution policies without violating App Store guidelines. The iOS build SHALL implement a gracefully degraded mode (e.g., storage-only contribution during app-open/charging sessions) rather than attempting to simulate full background mining.

### 5.6 Usability

- **NFR-USE-1 (Must):** A first-time miner SHALL be able to install the app and begin earning within 3 screens/steps, with no crypto-wallet setup knowledge required (a wallet SHALL be generated automatically on their behalf, with export/backup available but not mandatory upfront).
- **NFR-USE-2 (Must):** A first-time customer SHALL be able to deploy a static site within 5 minutes of signup, unassisted.

### 5.7 Maintainability

- **NFR-MAINT-1 (Must):** Every architectural decision that deviates from this SRS (e.g., substituting a specified technology) SHALL be recorded in `/docs/decisions.md` with a one-paragraph rationale, at the time it is made — not reconstructed after the fact.
- **NFR-MAINT-2 (Should):** Code and services SHOULD favor mature, widely-used libraries/frameworks over custom-built equivalents unless a specific, documented requirement cannot be met by any available library (see DP-6).

---

## 6. Security Requirements (Detailed)

This section exists because the prior implementation's test suite achieved 100% pass rate while shipping a critical cryptographic vulnerability. Passing functional tests is necessary but not sufficient evidence of security. The requirements below are independently verifiable and MUST have their own dedicated tests, separate from correctness tests.

### 6.1 Encryption

- **SEC-1 (Must):** AES-256-GCM SHALL be used for all shard encryption, per NIST SP 800-38D.
- **SEC-2 (Must):** IVs SHALL be 96 bits, generated via a cryptographically secure random number generator, unique per encryption operation, never reused across shards, files, or sessions.
- **SEC-3 (Must):** Data encryption keys SHALL NOT be deterministically derivable from public or guessable information (see FR-SHD-4). Key derivation SHALL use a securely random per-object key wrapped by envelope encryption.
- **SEC-4 (Must):** Authentication tags SHALL be verified before any decrypted plaintext is used or returned; a failed tag verification SHALL discard the output and return an explicit error, never partial or unverified plaintext.

### 6.2 Sharding and Data Isolation

- **SEC-5 (Must):** No single miner SHALL receive more than one shard of the same object unless the total available miner pool is smaller than the total shard count (documented fallback for small testnets only, never production).
- **SEC-6 (Should, Phase 3+):** Shard placement SHOULD avoid assigning multiple shards of the same object to miners under common control where such information is available (e.g., same subnet/ASN), to reduce collusion risk.

### 6.3 Proof System Integrity

- **SEC-7 (Must):** Storage challenges SHALL use fresh, unpredictable byte ranges per challenge — never a fixed or repeating pattern a miner could precompute a response for.
- **SEC-8 (Must):** Reputation and slashing logic SHALL be based only on cryptographically verified proof outcomes, never on self-reported miner status.

### 6.4 Database and API Security

- **SEC-9 (Must):** All SQL SHALL use parameterized queries (`PQexecParams` or equivalent ORM/query-builder parameter binding). No exceptions.
- **SEC-10 (Must):** All external API inputs (orchestrator HTTP endpoints, dashboard API) SHALL be validated against an explicit schema before processing; malformed input SHALL be rejected with a 4xx response, not passed through to internal logic.
- **SEC-11 (Should):** Rate limiting SHALL be applied to all public-facing endpoints (miner registration, heartbeat, customer API) to mitigate abuse and Sybil-style registration floods.

### 6.5 Miner Trust Model

- **SEC-12 (Must):** The system SHALL assume miners can be malicious, not merely unreliable. Every requirement in this section exists to hold even if a meaningful fraction of miners are actively adversarial, not just offline or slow.

---

## 7. Data Requirements

### 7.1 Core Entities

- **Miner**: id, wallet address, capacity (storage/compute), current status, reputation score, geographic region, registration timestamp.
- **Shard**: id, parent object id, index, encrypted bytes location (miner reference), IV, auth tag, Merkle proof reference.
- **Manifest**: object id, content hash, original size, shard count (data/parity), per-shard IV references, wrapped encryption key.
- **Customer**: id, auth credentials, billing info, wallet address (optional).
- **Deployment/Instance**: id, customer id, type (static/compute/storage), manifest reference(s), status, resource usage metrics.
- **LedgerEntry**: id, miner id, amount, mint type (demand/subsidy), linked proof event id, linked billing event id (nullable for subsidy), signature, timestamp.

### 7.2 Data Retention

- Proof challenge logs SHALL be retained for a minimum of 30 days for dispute resolution and reputation auditing.
- Ledger entries SHALL be retained indefinitely (append-only, no deletion), consistent with eventual on-chain migration.

---

## 8. Token Economics Requirements

This section formalizes the tokenomics design, incorporating the staking model from the prior implementation (a genuine improvement worth keeping) while resolving the bootstrap conflict identified in audit.

### 8.1 Demand-Gated Minting Formula

As specified in FR-TKN-2. Reward rates adjust per epoch based on the ratio of active customer demand to available miner capacity, increasing payouts when demand outstrips supply.

### 8.2 Staking and Slashing

- **TOK-1 (Should, Phase 4):** Miners above a configurable capacity threshold SHALL stake tokens as collateral before receiving high-value shard assignments, per a tiered model (basic/premium).
- **TOK-2 (Should, Phase 4):** Stake SHALL be slashed on verified proof failures (storage) or Byzantine computation results (compute), at rates proportional to severity, per the tiered slashing schedule established in the existing tokenomics design.

### 8.3 Reputation

- **TOK-3 (Should):** Reputation score SHALL increase on successful proof challenges and decrease on failures, with a bounded range (e.g., 0–100), and SHALL directly influence shard assignment priority and reward multiplier, as already designed.

### 8.4 Bootstrap Subsidy — Resolves audit-identified gap

- **TOK-4 (Must):** New miners with zero token balance SHALL be able to participate at a "Basic" tier without pre-staking, funded by a capped subsidy pool, until either (a) they accumulate enough earned tokens to self-stake for higher tiers, or (b) the subsidy program ends per its configured budget/timeline.
- **TOK-5 (Must):** Any staking requirement that would prevent a zero-balance new miner from earning their first tokens SHALL NOT apply to the Basic tier. Staking requirements apply only to elevated tiers (Premium and above), which existing, already-earning miners can graduate into.
- **TOK-6 (Must):** The subsidy pool's remaining balance and burn rate SHALL be visible to the platform operator via an internal dashboard/metric, to allow the bootstrap phase to be would-be extended, throttled, or ended deliberately rather than discovered as exhausted.

---

## 9. External Interface Requirements

### 9.1 Miner App ↔ Orchestrator API

- `POST /miner/register` — register a new miner, returns miner id + wallet.
- `POST /heartbeat` — report current status/capacity.
- `GET /miners`, `GET /miners/online`, `GET /miner/{id}` — registry queries (internal/admin use).
- `POST /shard/assign` — orchestrator pushes a shard storage task to a miner.
- `POST /proof/challenge` — orchestrator issues a storage/compute challenge.
- `POST /proof/respond` — miner responds to a challenge.

### 9.2 Customer Dashboard ↔ Backend API

- Standard REST/JSON API for auth, deployment CRUD, billing, logs (WebSocket or SSE for live log streaming).

### 9.3 Public Edge Interface

- Standard HTTPS on customer custom domains and platform-provided subdomains, terminated at the edge proxy.

---

## 10. Definition of Done, By Phase

### Phase 1 — Foundation

Done when ALL of the following automated checks pass:

- Shard engine: 100% of correctness tests AND 100% of security-property tests (FR-TST-3) pass.
- Orchestrator: handles 500 concurrent simulated miner connections (FR-ORC-2/NFR-PERF-1) without serialized blocking.
- Testnet: exercises the real shard engine (FR-TST-1), survives 30% simulated miner loss, reconstructs correctly, completes in under 5 seconds for a 10MB object.
- Miner app: runs as a real background service on a physical Android device, measured battery draw ≤ 3%/hour idle-mining (NFR-RES-1), with a functioning resource monitor and mining dashboard UI (not the default template).
- All Phase 1 code paths reviewed against Section 6 security requirements, with defects resolved, not merely logged.

### Phase 2 — Proof System and Token Ledger

Done when: proof challenges run continuously against live testnet miners; failed challenges correctly trigger re-replication and reputation penalties; demand-gated minting (FR-TKN-1/2) is demonstrated end-to-end with a simulated customer consumption event producing a signed, verifiable ledger entry; subsidy path (Section 8.4) is implemented and budget-capped.

### Phase 3 — Customer Cloud Layer

Done when: a real static site can be deployed via the dashboard, sharded, distributed, and served through the edge proxy with a custom domain and automatic TLS, end-to-end, by someone unfamiliar with the system's internals (usability test, not just functional test).

### Phase 4 — Decentralization

Done when: token issuance and staking are enforced by an on-chain contract rather than the off-chain ledger, and orchestrator discovery no longer depends on a single centralized registry for miner-to-miner discovery.

---

## 11. Open Questions Requiring a Founder Decision

These are flagged, not resolved, because they are business/product decisions rather than engineering ones:

1. Final product/brand name — resolve the "CloudMine" vs "Poot" inconsistency found across code, bundle IDs, and docs before it propagates further into app store listings and domains.
2. Subsidy pool total budget and duration — needed to bound TOK-4/TOK-6.
3. Blockchain choice for Phase 4 (Substrate vs Cosmos vs EVM L2) — deferred per the original build prompt, but should be revisited once Phase 3 is complete and real usage data exists.
4. Minimum viable miner count before opening customer signups publicly (previously suggested: 50 active miners) — confirm or adjust.

---

## 12. Traceability Note for the Build Agent

Every requirement tagged **[AUDIT-FIX]** in this document corrects a specific, verified defect in the existing `Poot` codebase (encryption nonce reuse, single-threaded orchestrator, non-parameterized SQL, testnet validating a non-production shard engine, and a fully unimplemented miner-app UI misrepresented as "in progress"). When implementing against this SRS, treat these not as new features but as corrections that take priority over any unrelated new work, since they block Phase 1 sign-off under Section 10.
