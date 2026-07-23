# Experiment packages

Each reported experiment has a self-contained directory with a human README,
fresh-execution wrapper, input description/data, expected interpretation, and
output contract. Use the root `Makefile` for the shortest reviewer commands.

| Directory | Experiment | Direct entry point |
|---|---|---|
| [`01-table4-qor/`](01-table4-qor/) | Table 4 default, BO, and ReviewDSE QoR | `reproduce.sh` |
| [`02-table5-composability/`](02-table5-composability/) | Table 5 stage composability | `reproduce.sh` |
| [`03-table6-cutrow/`](03-table6-cutrow/) | Table 6 hard cut-row legality | `reproduce.sh` |
| [`04-figures/`](04-figures/) | Figures 4 and 5 | `reproduce.sh` |
| [`05-reviewdse-search/`](05-reviewdse-search/) | ReviewDSE search | `reproduce.sh` |
| [`06-ariane-diagnostic/`](06-ariane-diagnostic/) | Ariane diagnostic | `reproduce.sh` |

Fresh EDA outputs are written under the sibling ORFS workspace or
`$DPL_EVOLVE_STATE_ROOT`, not copied from packaged reference files.
