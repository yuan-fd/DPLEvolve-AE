# Automation rules

These rules apply to agents operating in this repository.

## Stable experiment interface

The primary interface executes paper experiments:

```bash
make prepare-paper-inputs
make validate-evaluator
make reproduce-default
make reproduce-bo
make replay-reviewdse TRACK=hpwl
make replay-reviewdse TRACK=ghr
make reproduce-table5
make reproduce-table6
make plan-level1
make reproduce-level1 ACKNOWLEDGE_LLM_COST=yes
make run-dse-small CASE=aes_nangate45
make plan-dse-paper
```

Use the wrappers in `scripts/reproduce/` when Make is unavailable. Use a
single `CASE=` and/or each script's `--dry-run` before a large launch.

## Read-only archived evidence

Never modify files under:

- `artifacts/*/inputs/`
- `artifacts/*/expected/`
- `artifacts/01-table4-qor/selected-programs/inputs/`
- `provenance/`

Do not change an expected value to turn a failing comparison into a pass.
Fresh experimental outputs belong under `DPL_EVOLVE_STATE_ROOT` and the ORFS
reports/results trees, never in an expected-value directory.

## Claim boundaries

- `make toolchain-smoke` exercises one AES toolchain path. It is not a paper
  experiment and must not be called RTL-to-GDS.
- Table 4 selected-program replay is a fresh OpenROAD experiment when its
  prepared ODB is present.
- A full ReviewDSE search is supported by `make run-dse-paper`, but it must
  retain the explicit LLM-cost acknowledgement.
- Table 6 is runnable after `make fetch-table6-data` installs the published
  exact package. Table 5 remains blocked until the assets described in
  `docs/paper-data-layout.md` are recovered. Never substitute archived TSV/JSON
  rows and label the result fresh.

## Validation sequence

For source changes, run:

```bash
make plan-dse-paper
bash scripts/reproduce/run_bo.sh --case aes_nangate45 --dry-run
bash scripts/reproduce/replay_selected.sh --track hpwl --case aes_nangate45 --dry-run
make test
make validate-configs
```

Run a live EDA case only when the pinned workspace is available. Run a paid LLM
search only when the user explicitly authorizes the cost.

## Failure reporting

Record the experiment, exact command, exit code, and complete output. Separate
environment/data failures from QoR differences. A missing ODB or source tree is
not evidence that a paper value is wrong, and a digest mismatch is not safe to
repair automatically.
