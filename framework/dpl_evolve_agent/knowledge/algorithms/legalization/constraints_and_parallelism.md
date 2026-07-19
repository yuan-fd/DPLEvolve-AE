# Legalization Cards: Constraints And Parallelism

Scope: legalization under fragmented rows, fence constraints, pin-access rules,
technology constraints, and parallel execution.  Use these cards to avoid
building algorithms that only work on clean uniform-row layouts.

## L9. Fence-Region / Fragmented-Row Mixed-Height Legalization

Source status: `checked-source-text` for the committed fence-region reference.

Source handles: `fence_region_aware_2019`.

Best path: stage-agnostic constraint modeling for all legalizers.

Core thesis: the legal resource model must include fence regions, fragmented
rows, fixed cells, and mixed-height compatibility before optimization starts.

Pseudo code:

```text
preprocess legal resources:
    split rows by fixed cells, fences, blockages, and row compatibility
    tag each interval with allowed cell classes
for each movable cell:
    generate only compatible intervals
run chosen legalizer on fragmented interval resources
after placement:
    repair local boundary violations
    run quality refinement on legal components
```

Implementation handles:

- interval splitting as a shared preprocessing layer;
- compatibility filtering before candidate scoring;
- boundary repair around fences/macros/fixed cells;
- logs for interval count, filtered candidates, rejected incompatibilities, and
  boundary repairs.

Failure pattern: if fixed physical cells or fragmented capacity are invisible
to the candidate model, the algorithm may look strong on normal rows but fail
hard on stress cases.

## L10. Parallel Region Legalization

Source status: `source-handle-only`.

Source handles: `domocus_parallel`.

Best path: candidate generation and independent-region repair for all paths.

Core thesis: parallelism is useful when regions can be processed independently
with deterministic boundary reconciliation.  It is not useful if it creates
non-reproducible legal conflicts.

Pseudo code:

```text
partition layout into regions with guard bands
for each region in parallel:
    run path-native local legalizer or repair candidate generation
    record boundary cells and unresolved capacity
merge regions deterministically:
    resolve boundary conflicts
    rerun small serial repair on touched boundary zones
validate full placement
```

Implementation handles:

- deterministic partitioning and merge order;
- thread-local candidate buffers;
- boundary repair after parallel work;
- logs for region count, parallel time, boundary conflicts, merge rejects, and
  serial repair time.

Failure pattern: parallel local wins can disappear after merge.  Always report
boundary conflict counts and final full-flow HPWL, not only per-region gains.

## L11. Pin-Accessible / Ripple-Style Mixed-Cell-Height Legalization

Source status: `source-handle-only`.

Source handles: `pin_accessible_ripple`, `pin_access_refinement_2021`.

Best path: constraint-aware candidate filtering and local repair.

Core thesis: legal placement should preserve pin accessibility and avoid moves
that create likely routing or access failures, especially in mixed-height
layouts.

Pseudo code:

```text
build pin-access and row-compatibility filters
for each candidate cell move:
    reject incompatible row/height/access patterns
    score = HPWL/displacement objective + pin-access risk penalty
    reserve or commit only if legal and access-safe
for local violations:
    ripple affected cells through nearby compatible intervals
```

Implementation handles:

- cheap access-risk features in hot loops;
- bounded ripple queue;
- exact legality and access filter before commit;
- logs for access rejects, ripple queue size, accepted ripple moves, and
  remaining violations.

Failure pattern: access-aware scoring can over-penalize useful HPWL moves.
Keep it as a filter or tie-break unless the current design feature clearly
needs stronger access preservation.

## L12. Technology/Region/VAC/NIMH Constraint-Aware Legalization

Source status: `source-handle-only`.

Source handles: `constraint_aware_mch_legalization`.

Best path: constraint model shared by all legalizer families.

Core thesis: modern mixed-height legalization must price or filter technology
constraints before the optimizer commits moves.

Pseudo code:

```text
annotate intervals with legal classes, voltage/region constraints, and row type
for each cell:
    enumerate only compatible intervals
    score candidate by HPWL, displacement, congestion, and constraint risk
run path-native assignment or repair on compatible candidates
after commit:
    verify region and technology rules
    repair only touched components
```

Implementation handles:

- unified compatibility predicate used by candidate generation and validation;
- reason-coded rejects, not silent candidate drops;
- component repair for rule violations;
- logs for compatibility rejects by reason, accepted constrained moves, and
  post-check violation count.

Failure pattern: constraints can shrink the search space enough to hurt HPWL.
Use reason-coded logs to decide whether to relax candidate ordering, expand row
windows, or add exact component repair.
