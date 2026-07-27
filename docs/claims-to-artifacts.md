# Paper claims to fresh executions

This page maps every evaluated paper result to a command that generates new
outputs. Files under `artifacts/*/expected/` are comparison targets, not
observed fresh results.

| Paper result | Fresh execution | Generated evidence | Status |
|---|---|---|---|
| Table 4 default | `make reproduce-default` | nine default `metrics.json` records | Runnable |
| Table 4 BO-DSE | `make reproduce-bo` | 400 trials/case and `best.json` | Runnable |
| Table 4 ReviewDSE-HPWL | `make replay-reviewdse TRACK=hpwl` | nine rebuilt source runs | Runnable |
| Table 4 ReviewDSE-GHR | `make replay-reviewdse TRACK=ghr` | nine runtime-aware source runs | Runnable |
| Table 5 counterexamples | `make reproduce-table5` | three selected/reference comparisons | Runnable |
| Table 6 cut-row repair | `make reproduce-table6` | 27 status/legality/HPWL/runtime rows | Runnable after data fetch |
| Figure 4 | `make reproduce-figure4` | best-so-far TSV, SVG, missing-point JSON | Runnable |
| Figure 5 | `make reproduce-figure5` | runtime-quality TSV and SVG | Runnable |
| ReviewDSE method | `make run-dse-small` / `make run-dse-paper` | private sources, metrics, reviews, campaign audit | Runnable; model cost |
| Ariane diagnostic | `make reproduce-ariane-diagnostic` | six fresh source replays and two means | Runnable |

## Table 4

`make reproduce-table4` executes all four Table 4 paths and writes
`$DPL_EVOLVE_STATE_ROOT/paper_reproduction/table4/table4-fresh.tsv` from newly
generated metrics. BO performs 3,600 placements. Selected-source replay rebuilds
all 18 retained source trees and does not use an LLM.

All candidates must build, complete the canonical evaluator, pass strict
legality, contain all four HPWL stages and displacement metrics, demonstrate
mechanism liveness, and satisfy the exact 2x runtime eligibility gate when
selected by ReviewDSE.

Eight regenerated inputs lack paper-time ODB hashes. This does not block
execution. Their relative HPWL and runtime results are compared within explicit
scientific tolerances. AES Nangate45 additionally has an absolute rebuilt-HPWL
window.

## Figures 4 and 5

Retained mode validates and redraws author-run plotting data. Figure 4 contains
96 observed points and explicitly reports three missing SWERV points; it does
not normalize the retained data into a fabricated complete grid. Figure 5
recomputes runtime-ratio Pareto membership from exactly 400 BO trials per panel
plus the available ReviewDSE candidates.

Fresh mode requires a completed BO/Table 4 campaign and a ReviewDSE campaign
identified by `DSE_RUN_PREFIX`.

## Table 5

The runner regenerates dense inputs with Table-5-local utilization values
70/90/60, maps the six roles to the retained LEGALM, Diamond, and Negotiation
snapshots, and compares post-legalization `H_lg` with final `H_f`. Archived
counterexample values are never used as fresh observations.

## Table 6

The published external package contains nine exact cut-row DEF/Verilog pairs,
three SDC files, and one evolved source tree. After `make fetch-table6-data`,
the runner executes Diamond, Negotiation, and ReviewDSE on all nine patterns.
Every fresh job uses a 7200-second cap and strict `check_placement`.

The reported qualitative pattern is nine legal ReviewDSE repairs, one legal
Diamond case, and one legal Negotiation case. A retained Ariane center-10
Negotiation record used an earlier 600-second timeout; the archive remains
unchanged, while a fresh run records its new 7200-second-cap outcome.

## ReviewDSE search

Level 1 executes three calibration cases and freezes a Markdown evidence packet
plus JSON provenance. Level 2 validates the packet before launching the
nine-case, four-Student, ten-iteration paper profile.

The original Level 1 Student breadth and packet were not retained. The public
50-Student/case profile is a runnable reconstruction, not an assertion of the
exact stochastic author-time sequence. A fresh search is judged by its complete
protected candidate population and observed QoR, not identical LLM proposals.

## Non-claim commands

- `make toolchain-smoke`: one AES toolchain diagnostic;
- `make test`: repository regression tests.

These are supporting checks and do not replace the fresh experiment commands.
