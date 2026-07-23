# Table 4: QoR and runtime comparison

This experiment compares OpenROAD default detailed placement, a 400-trial
Optuna-TPE baseline, ReviewDSE-HPWL, and runtime-aware ReviewDSE-GHR on nine
design/platform targets.

## Fresh reproduction

Prepare the pinned environment and inputs, then run:

```bash
make reproduce-table4 THREADS=10
# or
bash artifacts/01-table4-qor/reproduce.sh --threads 10
```

The complete command executes nine defaults, 3,600 BO placements, and both
nine-program selected-source tracks. Start with one target when validating a
new host:

```bash
make reproduce-default CASE=aes_nangate45 THREADS=8
make setup-bo
make reproduce-bo CASE=aes_nangate45 THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=hpwl THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=ghr THREADS=8
```

Fresh outputs are written below `$DPL_EVOLVE_STATE_ROOT` and ORFS. The final
summary is `paper_reproduction/table4/table4-fresh.tsv`.

## Inputs and acceptance

- `inputs/bo_paper/`: retained per-trial records used for protocol reference.
- `inputs/reviewdse/`: retained selections and metrics.
- `selected-programs/inputs/programs/`: 18 executable source trees.
- `config/`: paper protocol configuration.
- `expected/`: paper comparison targets.
- `output/`: output-contract placeholder; fresh EDA products use the state root.

Fresh runs require complete canonical metrics and strict legality. Reported
HPWL deltas and runtime ratios are compared within the configured numerical
tolerances. Missing paper-time hashes for eight regenerated inputs do not block
execution or numerical comparison.
