# DPO Cards: Classic HPWL Descent Kernels

Scope: legality-preserving detailed-placement optimization after legalization.
These mechanisms should reduce `HPWLlg -> HPWLimprove` and preserve or improve
`HPWLfinal` after mirroring.

## D0. FastDP-Style Global Swap, Local Reorder, And Segment Clustering

Source status: `source-handle-only`.

Source handles: `fastdp_2005`.

Best use: baseline CPU DPO mechanism and local DIAMOND-style repair idea.

Core thesis: combine global swap, local reorder, and segment-level shifting so
the search can escape simple row-order defects with scoped, measured candidate
expansion rather than blind random trial-and-error.

Pseudo code:

```text
build touched-net exact HPWL evaluator
build legal row/segment map
repeat until pass budget or no gain:
    for cell in candidate cells ordered by net criticality:
        region = optimal region from connected-net bounding boxes
        partners = legal cells/sites near region
        accept best legal swap if exact HPWL delta improves
    for hot row segment:
        for sliding window:
            test bounded reorder permutations
            accept best legal exact-HPWL improvement
    for touched segment:
        shift cells within whitespace by local HPWL/displacement objective
        accept only exact improvement
```

Implementation handles:

- exact touched-net deltas with cached net extrema;
- partner generation from optimal regions, not full scans;
- deterministic window ordering;
- logs for swap/reorder/shift attempts, accepts, reject reasons, and exact
  delta runtime.

Failure pattern: if attempts are high but accepts are near zero, the candidate
source is wrong or legalization created a basin DPO cannot exploit.

## D1. OpenROAD DPO Command Taxonomy

Source status: `derived-from-openroad/context`.

Source handles: `openroad_dpl_docs`.

Best use: stage attribution and source-surface routing.

Core thesis: legalizer gains and DPO gains must be attributed separately because
the same final HPWL can arise from very different stage behavior.

Pseudo code:

```text
run detailed_placement_evolve
record HPWLlg, legality, avg displacement, max displacement
run improve_placement_evolve
record HPWLimprove and DPO counters
run optimize_mirroring_evolve
record HPWLfinal
run check_placement
if illegal:
    mark testcase FAIL regardless of HPWL
```

Implementation handles:

- keep command-level metrics separated;
- report DPO runtime/counters separately when available;
- compare full-flow final HPWL before promoting a legalizer donor;
- logs for stage command, source commit, binary path, and mechanism counters.

Failure pattern: without stage attribution, Teacher may wrongly promote a
legalizer whose apparent gain was produced by DPO or mirroring.

## D3. ABCDPlace / Batch-Concurrent Detailed Placement

Source status: `source-handle-only`.

Source handles: `abcdplace_2020`.

Best use: high-performance CPU candidate evaluation and parallel commit.

Core thesis: evaluate many independent candidates in parallel, then commit a
non-conflicting subset deterministically.

Pseudo code:

```text
generate batches of legal candidate moves or swaps
parallel for candidate in batch:
    dry-run legality
    compute exact or cached HPWL delta
    record candidate if beneficial
build conflict graph over accepted candidates
choose deterministic independent set by gain density
commit chosen candidates
```

Implementation handles:

- thread-local candidate buffers;
- conflict detection by cell, row interval, and touched nets;
- deterministic independent-set selection;
- logs for generated candidates, evaluated candidates, chosen independent set,
  rejected conflicts, and parallel time.

Failure pattern: batch execution is only useful if candidate generation is
strong.  Parallelizing weak candidates hides the actual algorithm gap.

## D4. Density-Aware Detailed Placement With Instant Legalization

Source status: `source-handle-only`.

Source handles: `density_aware_dp_2014`.

Best use: DPO candidate scoring when legalizer output has localized density
stress or scarce whitespace.

Core thesis: a move should be evaluated by both HPWL and local density legality
pressure.  Instant legalization rejects moves that would create unrecoverable
local stress.

Pseudo code:

```text
compute local density and whitespace pressure per row segment
for candidate move or swap:
    dry-run local legality and density pressure
    if pressure exceeds recoverable threshold:
        reject
    score = exact HPWL delta - density relief bonus + displacement cost
    commit if legal and score improves
```

Implementation handles:

- cheap per-segment density/whitespace cache;
- instant legal dry run before exact HPWL work when possible;
- pressure-aware candidate ordering;
- logs for density rejects, pressure-relief accepts, and post-pass pressure.

Failure pattern: density bonuses can fight HPWL.  Use them as a feasibility or
tie-break signal unless stage-wise metrics show density pressure is the blocker.
