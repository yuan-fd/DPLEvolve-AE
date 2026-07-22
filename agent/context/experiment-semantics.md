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
- `reproduce-table5` and `reproduce-table6` are fresh replay paths, but require
  the exact external data declared in `docs/paper-data-layout.md`.

## Archived support

`table4`, `table5`, and `table6` beneath `artifacts/` recompute compact archived
records. `make audit-archive` dispatches them. These commands perform no fresh
EDA execution and must never be presented as reproduction.

## Toolchain diagnostic

`toolchain-smoke` regenerates one AES Nangate45 placement input, executes
detailed placement, and validates the result. It is useful for diagnosis but is
not one of the paper's nine-case, composability, or cut-row experiments.
