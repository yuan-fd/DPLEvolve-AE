# Experiment semantics

## Fresh paper execution

- `prepare-paper-inputs` runs the pinned ORFS place target for the nine Table 4
  cases and creates the incoming `3_4_place_resized.odb` snapshots.
- `reproduce-default` runs the fixed OpenROAD DPL reference.
- `reproduce-bo` runs Optuna TPE over public controls (400 trials/case and four
  trials in parallel for the paper configuration).
- `replay-reviewdse` builds each complete frozen HPWL- or GHR-selected source
  and runs it on its own target through legalization, DPO, and final processing.
- `run-dse-small` and `run-dse-paper` invoke the real Teacher/Student source
  exploration loop. The paper profile is 1 GPT-5.5 xhigh Teacher, 4 GPT-5.4
  xhigh Students, 10 iterations, and a 2x runtime gate.
- `reproduce-table6` is a fresh replay path after the published external data
  is fetched. `reproduce-table5` is implemented but remains blocked by missing
  recovery assets declared in `docs/table5-status.md`.

Paper-time hashes strengthen provenance but are not the scientific pass/fail
criterion. Fresh execution, legality, canonical metrics, and numerical or
qualitative acceptance are mandatory.

## Toolchain diagnostic

`toolchain-smoke` regenerates one AES Nangate45 placement input, executes
detailed placement, and validates the result. It is useful for diagnosis but is
not one of the paper's nine-case, composability, or cut-row experiments.
