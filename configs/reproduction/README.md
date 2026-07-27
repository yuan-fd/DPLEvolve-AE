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

The public reproduction commands execute OpenROAD and produce new
`metrics.json` records. Reference values under `artifacts/*/expected/` are used
by the fresh summarizers only to apply the documented numerical tolerances.

`table6-replay.tsv` records the retained cut-row data identifiers, design names,
macro LEF/Liberty dependencies, and 7200-second caps. `table5-sources.tsv`
records how the six selected/reference roles reuse the three tracked LEGALM,
Diamond, and Negotiation snapshots. `table5-inputs.tsv` records the local
AES/JPEG/SWERV utilization overrides 70/90/60 and the pinned ORFS design
configuration used to regenerate each input.
