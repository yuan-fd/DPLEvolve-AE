# Paper experiment contract

`paper-experiments.json` is the machine-readable execution contract transcribed
from Sections 3 and 4 of the paper. It records the actual target cases, model
profiles, iteration count, BO budget, metrics, runtime gate, and the Table 5/6
stress cases.

`paper9.tsv` is the reviewer-readable nine-case launch plan mirrored by the
JSON contract and framework case registry. A regenerated input set uses the
stable `paper9_place` flow variant. Exact paper-time input
snapshots may instead be installed under the variant recorded in the selected
program manifest.

The manifest is not an expected-value checker. Public reproduction commands
must execute OpenROAD and produce new `metrics.json` records. Archived expected
values remain under `artifacts/` and are used only by `make audit-archive`.
