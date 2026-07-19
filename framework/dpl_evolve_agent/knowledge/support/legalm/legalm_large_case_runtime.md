# LEGALM Large-Case Runtime

## Agent Use

- Role: LEGALM reference/support. Open only when the selected route uses LEGALM-style guidance or the legalizer is the diagnosed bottleneck.
- Treat paper alignment and LEGALM tuning as support for a full-flow mechanism, not as a route selector by itself.


## Context

Large placement instances expose constant-factor mistakes in the evolved
LEGALM path.  Optimize legalizer-stage runtime first; full strict metrics can
be much heavier because ODB write/read and downstream metric collection are
separate costs.

## Observed Bottleneck

Earlier large-case runs showed that the slow path was not primarily final
no-overflow polish.  The dominant legalizer-stage cost was row/interval
assignment inside full LEGALM legalization:

- per-cell construction of fresh row-order vectors,
- scanning many free intervals in candidate rows,
- millions of avoidable allocations and interval probes after rows become
  fragmented.

This pattern scales poorly on large, fragmented designs.

Later optimized runs changed the bottleneck profile.  After replacing full
occupancy rebuilds with exact incremental footprint updates, the LEGALM
pipeline can be around one second on a medium-density large standard-cell
case.  In that profile, the full strict-flow elapsed time is dominated by
downstream detailed-improvement work, not by differential guidance itself.

Within differential guidance, the useful breakdown is:

- demand scoring: Eq. 36 demand over cells that actually participate in the
  current triplefold partition scheme,
- partition sorting: stable descending demand order inside each partition,
- BGD candidate evaluation: finite-ALM candidate scoring and pruning,
- occupancy commit: accepted footprint deltas applied after the parallel
  partition region,
- overflow stats: the end-of-iteration ALM stopping/telemetry scan.

Recent repair-boundary runs showed partition sorting can again become a visible
Stage 2 cost when more cells participate in the LEGALM-only path.  Sorting is
independent across triplefold partitions, so it is safe to parallelize over
partitions while keeping each partition's stable descending demand order.  This
preserves deterministic per-partition cell order and does not change the paper
cost model.

If the LEGALM pipeline is already small relative to the rest of the flow, do
not keep simplifying the paper algorithm just to chase total wall time.  First
separate legalizer-stage runtime from downstream improvement/mirroring time.

## Effective Fix

Two changes were high value:

- replace per-cell row-offset vector construction with direct nearest-row
  iteration,
- replace full row interval scans with bounded nearest-interval probes around
  desired, original, and midpoint targets; use full scans only as diagnostic or
  rare fallback behavior.

The bounded interval probe was the main win.  It preserved strict legality in
the measured large-case legalizer path while cutting legalizer-stage elapsed
time by a large factor.

## Guidance For Future Agents

For large cases:

- do not scan full rows in inner loops,
- do not allocate row-order vectors per cell,
- use row-id keyed vectors, sorted interval arrays, `lower_bound`, and bounded
  neighbor probes,
- keep candidate caps explicit in telemetry,
- keep default LEGALM runtime parameters aligned to the paper: `T=800`
  (`Kthre + 500` to cover local-optimum detection), `D=245` candidate stencil
  (`vertical_radius=3`, `horizontal_steps=17`), and Stage 3 `3 x 10`
  partition-scheme refinement,
- when extra runtime budget is available, first expand horizontal candidate
  reach while keeping vertical radius fixed.  In the latest medium-density
  standard-cell sweep, horizontal-only expansion improved HPWL while staying
  faster than the diamond baseline, but the benefit saturated after moderate
  expansion.  After saturation, spend effort on HPWL-aware candidate scoring
  rather than larger `D`; vertical-radius expansion regressed both quality and
  runtime,
- do not assume more Stage 3 rounds help.  If Stage 3 reports no additional
  HPWL movement, spend the budget on Stage 2 candidate reach or objective
  quality instead,
- score demand only for cells that can be processed by the current partition
  scheme; boundary-excluded cells do not affect that iteration's order,
- avoid full target/occupancy vector reconstruction inside ALM iterations when
  accepted move records can update the same state incrementally,
- parallelize per-partition stable sorts before changing candidate costs or
  candidate stencil order,
- keep legalizer-stage timing separate from downstream `improve_placement` and
  mirroring timing,
- treat detailed-improvement attempt-count reductions as quality-risk
  experiments, not default runtime fixes; lower random attempt factors can save
  seconds but may lose enough HPWL to erase the legalizer-quality advantage,
- only add richer scoring after the bounded probe path is already efficient.

If quality regresses, improve target scoring or commit criteria around bounded
probes.  Do not return full scans to the normal hot path.
