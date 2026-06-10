# Poot Tokenomics Model

This document outlines the economic parameters, minting mechanics, staking incentives, and slashing rules of the Poot network.

---

## 1. The Core Innovation: Demand-Gated Minting

Traditional DePIN models suffer from speculative hyperinflation: they reward miners for contributing raw resources (storage/compute) regardless of actual customer usage. This leads to massive token dilution, dump cycles, and ultimate network collapse.

Poot solves this with **Demand-Gated Minting (DGM)**:
* **No customer demand = No mining rewards = Zero token inflation**.
* Tokens are minted *only* when a paying customer initiates a real container instance, stores files, or serves web traffic.
* Customer payments (in fiat or stablecoins) directly buy and burn Poot tokens on the open market, or fund the reward pool, providing stable tokenomics backed by real economic utility.

---

## 2. Minting & Reward Mechanics

When a customer's instance actively consumes resources, the network mints tokens into the reward pool according to the following formula:

$$\Delta T = (S_{\text{bytes}} \times R_{\text{storage}}) + (C_{\text{units}} \times R_{\text{compute}})$$

Where:
* $\Delta T$: Poot tokens minted.
* $S_{\text{bytes}}$: Gigabyte-hours of storage successfully served and verified via Proof-of-Storage.
* $R_{\text{storage}}$: The current storage reward rate, adjusted dynamically by the Orchestrator based on network demand.
* $C_{\text{units}}$: Compute-seconds successfully processed and verified via Proof-of-Compute.
* $R_{\text{compute}}$: The current compute reward rate.

### 2.1. Dynamic Rate Adjustments
The reward rates ($R_{\text{storage}}$ and $R_{\text{compute}}$) are updated algorithmically every epoch based on target network utilization:

$$R = R_{\text{base}} \times \left(1 + \alpha \frac{\text{Active Consumers}}{\text{Total Registered Miners}}\right)$$

This increases payouts when consumer demand outstrips active miner availability, prompting more mobile devices to go online.

---

## 3. Staking & Slashing (Security Collateral)

To ensure high-availability and prevent sybil/adversarial behavior, miners must post **staking collateral** to participate.

### 3.1. Staking Tiers
Before receiving shard assignments, miners must stake native Poot tokens:
* **Basic Tier**: Small storage assignments (up to 5GB). Requires a minimum stake of $100$ POOT.
* **Premium Tier**: High-throughput storage and WebAssembly compute assignments. Requires a minimum stake of $1,000$ POOT.

### 3.2. Slashing Conditions
Staked tokens are locked in a smart contract (Phase 4) or marked in the cryptographic ledger (Phase 2). Collateral is slashed under the following conditions:
* **Failed Proof-of-Storage**: If a miner fails to respond to a random byte challenge with a valid Merkle proof within 30 seconds, the shard is marked lost, and $5\%$ of the staked collateral is slashed.
* **Byzantine Computation**: If a miner returns a compute result that diverges from the consensus majority of a verified compute group, $50\%$ of their stake is slashed.
* **Extended Silent Outages**: Going offline without a clean disconnection handshake for more than 24 hours results in a $10\%$ stake slash to cover the network re-replication costs.

---

## 4. Reputation & Reward Multipliers

A miner's reputation score directly affects their likelihood of receiving lucrative customer shard assignments and their payout multipliers:

$$\text{Final Reward} = \Delta T \times \left(\frac{\text{Reputation Score}}{100}\right)$$

### 4.1. Reputation Modifiers
* **Successful Proof Challenge**: $+1$ point (up to a maximum of $100$).
* **Failed Challenge**: $-15$ points.
* **Clean Disconnection Handshake**: $+0$ points (preserves score when going offline gracefully).
* **Dirty Disconnection (Sudden Off)**: $-10$ points.
