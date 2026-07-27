# Invariants

1. Commands never modify retained inputs, expected values, configs, or
   provenance records.
2. Every paper experiment is entered through its `artifacts/*/reproduce.sh`
   wrapper or the matching root Make target.
3. Fresh outputs remain under ORFS, `DPL_EVOLVE_STATE_ROOT`, or
   `PAPER_DATA_ROOT`.
4. Human guidance stays in `README.md` and `docs/`; machine rules and recipes
   stay in `agent/` and `scripts/agent/`.
5. Missing hashes affect provenance only; legality and fresh numerical
   evaluation remain mandatory.
6. Table 5 accepts only its three checksummed program snapshots and locally
   regenerated 70/90/60-utilization inputs; no substitute source, global config
   edit, or retained number is accepted.
7. Paid model calls require explicit user authorization and the cost gate.
8. Failed execution, legality, or numerical checks return nonzero and retain
   their generated logs.
9. Release archives exclude Git state, caches, credentials, local environments,
   and generated experiment outputs.
