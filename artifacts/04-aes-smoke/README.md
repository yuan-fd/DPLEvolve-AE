# AES Nangate45 smoke flow

This bundle performs a fresh one-case EDA execution. It regenerates the AES
Nangate45 placement input with the pinned Yosys revision, runs the native
OpenROAD detailed-placement baseline, and checks the result against the
reproduction lock.

After preparing the environment from the repository root, run:

```bash
make bootstrap  # omit only when the pinned sibling ORFS workspace exists
make setup
bash artifacts/04-aes-smoke/run.sh --run --threads 8
```

The run checks:

- input ODB SHA-256;
- instance count and instance area;
- global and final HPWL;
- OpenROAD exit status and metric errors;
- strict placement legality.

Expected headline values are 14,676 instances and HPWL
`188569.2 -> 176845.1` micron. Success ends with:

```text
[OK] AES smoke test PASSED
```

Each run uses a new timestamped flow variant and refuses to overwrite an
existing result. EDA products are written to the sibling ORFS workspace under
`flow/results`, `flow/reports`, and `flow/logs`.

To validate a locally prepared reference run without creating a new run:

```bash
bash artifacts/04-aes-smoke/check.sh
```

The reference ORFS result tree is too large for this Git repository and is not
included in a clean clone. In that case the command prints `[SKIP]` and exits
successfully. Run the preparation and fresh-flow commands above for an
end-to-end validation.

This artifact verifies one default OpenROAD case. It does not execute a
selected ReviewDSE program or reproduce all nine Table 4 cases.

- `config/aes_nangate45.yaml`: human-readable smoke configuration.
- `expected/ae_reproduction_lock.json`: commits, hashes, values, and tolerances.
- `inputs/README.md`: how the EDA input is reconstructed.
- `output/`: reserved for wrapper metadata; EDA products remain in ORFS.
