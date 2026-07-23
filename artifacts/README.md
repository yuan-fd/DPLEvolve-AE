# Experiment packages

Each reported experiment has a self-contained directory with a human README,
fresh-execution wrapper, input description/data, expected interpretation, and
output contract. Use the root `Makefile` for the shortest reviewer commands.

| Directory | Experiment | Direct entry point |
|---|---|---|
| [`01-table4-qor/`](01-table4-qor/) | Table 4 default, BO, and ReviewDSE QoR | `reproduce.sh` |
| [`02-table5-composability/`](02-table5-composability/) | Table 5 stage composability | `reproduce.sh` |
| [`03-table6-cutrow/`](03-table6-cutrow/) | Table 6 hard cut-row legality | `reproduce.sh` |
| [`05-figures/`](05-figures/) | Figures 4 and 5 | `reproduce.sh` |
| [`06-reviewdse-search/`](06-reviewdse-search/) | ReviewDSE search | `reproduce.sh` |
| [`07-ariane-diagnostic/`](07-ariane-diagnostic/) | Ariane diagnostic | `reproduce.sh` |

`04-aes-smoke/` is an optional toolchain diagnostic and is not a reported paper
experiment. Fresh EDA outputs are written under the sibling ORFS workspace or
`$DPL_EVOLVE_STATE_ROOT`, not copied from packaged reference files.
