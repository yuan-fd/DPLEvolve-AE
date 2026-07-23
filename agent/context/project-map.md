# Project map

```text
artifacts/
  01-table4-qor/           Table 4 default, BO, and selected-source replay
  02-table5-composability/ Table 5 stage-composability experiment
  03-table6-cutrow/        Table 6 cut-row legality experiment
  04-figures/              Figure 4/5 renderer
  05-reviewdse-search/     Level 1 and Teacher/Student search
  06-ariane-diagnostic/    Ariane six-source diagnostic
configs/reproduction/      Machine-readable paper experiment contract
src/dpl_evolve_agent/      ReviewDSE implementation
scripts/
  human/                   Host setup and environment preparation
  reproduce/               Experiment implementations and summarizers
  agent/                   Fixed machine dispatcher and inspection
  shared/                  Shared environment/configuration utilities
docs/                      Human-facing detailed documentation
agent/                     Agent rules, task recipes, context, and schemas
tests/toolchain/aes-smoke/ Non-paper EDA toolchain test
provenance/                Pinned upstream source revisions
```

## Stable entry points

| Intent | Entry point |
|---|---|
| Inspect environment | `bash scripts/agent/inspect_environment.sh` |
| Prepare pinned EDA tools and inputs | `make reviewer-prepare THREADS=8` |
| Table 4 | `bash scripts/agent/run_artifact.sh --artifact table4` |
| Table 5 | `bash scripts/agent/run_artifact.sh --artifact table5` |
| Table 6 | `bash scripts/agent/run_artifact.sh --artifact table6` |
| Figures 4/5 | `bash scripts/agent/run_artifact.sh --artifact figures` |
| ReviewDSE plan | `bash scripts/agent/run_artifact.sh --artifact search` |
| Ariane diagnostic | `bash scripts/agent/run_artifact.sh --artifact ariane` |
| Repository validation | `make test` |

All experiment wrappers call the root Make interface. Fresh outputs go to the
sibling ORFS workspace or `DPL_EVOLVE_STATE_ROOT`; agents never write generated
results into `artifacts/*/inputs/` or `artifacts/*/expected/`.
