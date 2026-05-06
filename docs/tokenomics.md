# CloudMine Tokenomics

## Token Overview

CloudMine token is demand-gated — minted only when customer instances actively consume miner resources.

## Minting Formula

```
tokens_minted = (storage_bytes_served × storage_rate) + (compute_units_served × compute_rate)
```

Both rates are set by the orchestrator based on current customer demand.
If demand = 0, both rates = 0.

## Token Emission

- No demand = no mining = no inflation
- Token has real economic backing from paying customers
- Off-chain ledger initially (Phase 2), migrating to blockchain (Phase 4)
