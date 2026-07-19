# LEGALM Paper-To-Code Alignment

## Agent Use

- Role: LEGALM reference/support. Open only when the selected route uses LEGALM-style guidance or the legalizer is the diagnosed bottleneck.
- Treat paper alignment and LEGALM tuning as support for a full-flow mechanism, not as a route selector by itself.


This note maps the current OpenROAD `dpl_evolve` LEGALM implementation to the
LEGALM 2.0 paper so future edits do not silently replace the paper flow with
unrelated heuristics.

## Top-Level Flow

Current entry:

- `StudentAlgorithm.cpp::Opendp::runStudentAlgorithm`
- `LegalmGuidance.cpp::Opendp::runDifferentialGuidance`
- `LegalmFullLegalization.cpp::Opendp::runLegalmFullLegalization`

Paper alignment:

- Stage 1 relaxed/static legalization: implemented in `runDifferentialGuidance`
  by snapping movable cells to statically valid row/site/fence/blockage
  positions while allowing movable-cell overlap.
- Stage 2 ALM + BGD: implemented in `runDifferentialGuidance` as the main
  overflow-removal loop.
- Stage 3 no-overflow refinement: implemented in `runLegalmFullLegalization`
  as lambda-infinity BGD after a self-legal placement has been constructed.

## Parameter Sources

The following constants are taken from the paper implementation paragraph:

- `alpha_max = 1.5`
- `delta_thre = 3H`
- `Kpart = 50`
- `Kely = 2`
- `Kthre = 300`
- `Kh = 100`
- `alpha_sigma = 3`
- `alpha_lambda = 0.5`
- `alpha_hf = 0.2`
- `ptech = 1`
- `(xhint, yhint) = (250um, 25H)`

The paper leaves maximum ALM iteration `T` as an input.  The current CPU
reference defaults `T = Kthre + 500 = 800` so the documented 500-iteration
local-optimum window can be observed after the `Kthre` restart.

Runtime-experiment parameters exposed through `detailed_placement_evolve` keep
paper defaults unless the caller overrides them:

- `-threads`: default `10`.
- `-legalm_iterations`: Algorithm 1 input `T`, default `800`.
- `-legalm_candidate_vertical_radius`: default `3`.
- `-legalm_candidate_horizontal_steps`: default `17`.
- `-legalm_stage3_partition_schemes`: default `3`.
- `-legalm_stage3_rounds_per_scheme`: default `10`.

The default BGD stencil is `(2*3+1)*(2*17+1) = 245`, matching the paper's
`D = 245` candidate set in Fig. 5.  Stage 3 defaults to the paper's Section
III-H refinement setting: three partition schemes and roughly ten rounds per
scheme.

## Formula Mapping

- Eq. 3-4 displacement objective: implemented as weighted Manhattan
  displacement plus `alpha_max` tail penalty in `LegalmGuidance.cpp` candidate
  cost and `LegalmFullLegalization.cpp` placement/refinement cost.
- Eq. 6 overflow `g_j(x)`: represented by per-row/site occupancy minus unit
  capacity in `collect_stats`, `footprint_overflow_score`, and
  `almFootprintCost`.
- Eq. 20 clipped overflow `g_tilde`: implemented in the lambda update as
  `max(g_j, -1 / sigma)`.
- Eq. 23 lambda update: implemented in the ALM loop as
  `lambda = max(lambda + hf * (g_tilde + sigma/2 * g_tilde^2), 0)`.
- Eq. 32 and Eq. 34 ALM candidate cost: implemented in `almFootprintCost` as
  `lambda_j * max(1 + sigma * g_j, 0)`; displacement `w_i,t,j` is added by the
  caller.
- Eq. 35 technology/routability penalty: implemented as a candidate-local
  `ptech * H * V_i,t,j` addend in Stage 2 and Stage 3 candidate scoring.
  `ptech = 1` and `H = row_equiv_sites`.  The currently exact CPU term is
  LEF58 cell-edge spacing, using
  `PlacementDRC::countEdgeSpacingViolations(cell, x, y, orient)`.  Pin-short
  and pin-access terms remain zero until a dedicated routing/pin-access cache
  exists; unrelated OpenROAD padding, blocked-layer, and one-site-gap checks are
  deliberately not folded into Eq. 35.
- Eq. 36 cell demand `d_i(x)`: implemented by `cell_demand` and
  `average_cell_demand` as total density over all sites covered by the cell.
- Eq. 37 sigma initialization: implemented as
  `sigma = max(1, alpha_sigma * average_i d_i(x0))`.
- Eq. 38 augmented demand: implemented by `average_lambda_seed` as the total
  sum of `1 + density + sigma/2 * density^2` over covered sites.
- Eq. 39 lambda initialization: implemented as
  `alpha_lambda * average_i(H / d_tilde_i)`.
- Eq. 40 hf initialization: implemented as `hf = lambda_seed`.
- Eq. 41 hf update: implemented after `Kthre` every `Kh` iterations as
  `hf = hf + alpha_hf * average(lambda)`.

## Pseudocode Mapping

- Algorithm 1 lines 4-5: parameter initialization in `runDifferentialGuidance`.
- Algorithm 1 lines 9-10: triplefold partition rebuild every `Kpart`.
- Algorithm 1 lines 13-16: restart flag is true for `k < Kely` and `k == Kthre`.
- Algorithm 1 lines 19-23: ALM stops when overflow reaches zero.
- Algorithm 1 lines 25-26: `hf` update follows Eq. 41.
- Algorithm 1 lines 29-30: local optimum escape uses the 500-iteration
  overflow-node-count standard-deviation trigger and plate connectivity graph.
- Algorithm 1 line 33: lambda update follows Eq. 23.
- Algorithm 2 lines 3-8: each cell evaluates whether it is in overflow unless
  restart is active; non-overflow cells are skipped.
- Algorithm 2 lines 9-18: candidate enumeration uses the paper 7-by-35 stencil
  for `D = 245` and invalid/out-of-region candidates are rejected.  The
  vertical candidate step is one row for odd-height cells and two rows for
  cells whose height is an even multiple of row height, matching Fig. 5's
  mixed-height candidate convention.
- Algorithm 2 CALCCOST: Stage 2 combines Eq. 32/Eq. 34 ALM penalty,
  height-class weighted displacement, `alpha_max` maximum-displacement tail,
  and Eq. 35 technology penalty.  Because Eq. 35 is non-negative, candidates
  whose base cost cannot beat the current best are pruned before invoking the
  local DRC count.
- Algorithm 2 candidate enumeration and Stage 3 candidate enumeration both use
  `legalmBestBgdCandidate()` from `LegalmCommon.h`.  Stage 2 passes a finite
  ALM evaluator; Stage 3 passes a lambda-infinity evaluator that rejects every
  candidate that cannot fit the current legal free-slot state.
- Algorithm 2 line 20: accepted moves update worker-local occupancy deltas
  during partition processing.  After the parallel region, accepted footprint
  move records are applied exactly to the global occupancy and target row/site
  state.  This preserves the same state as a full rebuild while avoiding a
  full movable-cell rescan after every ALM iteration.
- Algorithm 3: partitions are processed in parallel with OpenMP; cells inside
  each partition are sorted by descending Eq. 36 demand and processed
  sequentially.
- Stage 3 section III-H: cycles three partition schemes for 10 rounds each and
  treats any overflow candidate as infinite cost by requiring exact free-slot
  fit before scoring displacement.

## Known Gaps

- Candidate-level parallel reduction from the GPU implementation is mapped to
  OpenMP partition-level parallelism on CPU.  Candidate scans are still serial
  inside each partition worker to avoid nested OpenMP overhead.
- Eq. 35 pin-short and pin-access terms are not implemented yet.  They need a
  thread-safe local routing/pin-access cache so candidate loops avoid full
  router calls.
- Stage 2 exports explicit row/site target state and overflow telemetry that
  Stage 3 consumes directly.  The CPU implementation still has to construct a
  self-legal placement before lambda-infinity refinement; that construction is
  a CPU data-structure bridge, not a separate algorithmic objective.
- Algorithm 1 input `T` is not specified as a fixed implementation paragraph
  constant by the paper.  The default `800` is therefore a documented CPU
  reference default derived from `Kthre + 500`, not a paper-reported constant.

## CPU Efficiency Notes

- The implementation requires OpenMP at CMake configure time and uses it for
  partition-level BGD, occupancy/statistics construction, and site-orientation
  compatibility table construction.
- Candidate legality checks cache `(site, master_symmetry)` compatibility over
  the row/site grid so the inner BGD loops do not repeatedly traverse
  `Grid::getSiteOrientation` maps.
- Master symmetry and multi-row flags are cached per cell.  This preserves the
  paper's candidate set and cost model while reducing CPU candidate-evaluation
  overhead.
- Stage 2 ALM keeps the paper's BGD iteration order but updates occupancy
  incrementally from accepted footprint moves instead of rebuilding occupancy
  from every cell after every iteration.  The implementation also carries
  final overflow stats into the next iteration start, avoiding a redundant
  full occupancy scan.
- Stage 2 only computes Eq. 36 demand for cells assigned to the current
  triplefold partitions.  Boundary-excluded cells are not processed in that
  iteration, so scoring them does not affect the paper's partition-local order.
- Stage 2 reports internal ALM timing for demand scoring, partition sorting,
  BGD candidate evaluation, accepted-move commit, and overflow-stat collection.
  Use those counters before changing algorithm semantics for runtime reasons.
- The exposed runtime knobs are control-plane parameters only.  Hot-path
  per-cell/per-net data must stay in contiguous vectors and row/site indexes
  built inside each stage, not in `EvolveContext`.
