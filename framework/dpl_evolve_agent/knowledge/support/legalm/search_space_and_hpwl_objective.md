# LEGALM Search Space And HPWL Objective

## Agent Use

- Role: LEGALM reference/support. Open only when the selected route uses LEGALM-style guidance or the legalizer is the diagnosed bottleneck.
- Treat paper alignment and LEGALM tuning as support for a full-flow mechanism, not as a route selector by itself.


## Observation

Expanding the BGD candidate stencil can improve final HPWL, but only until the
current paper-objective search saturates.  On medium-density single-height
designs, horizontal-only expansion can expose useful moves, while excessive
horizontal expansion can converge to the same solution and vertical expansion
can regress quality or runtime.

This is not primarily a geometric enumeration bug.  The shared
`legalmCandidateStencil()` path enumerates the rectangular `(dx, dy)` stencil.
The effective search space is narrowed later by:

- triplefold partition containment,
- displacement and overflow incumbent-cost pruning,
- no HPWL-aware signal before candidate rejection.

Therefore, larger `D` alone will eventually stop exposing useful HPWL moves.
The candidates may be geometrically present but economically invisible because
the current paper-cost objective rejects them before local wirelength benefit is
measured.

## Practical Rule

Keep the paper-default mode intact for paper-faithful experiments:

- `T = 800`,
- `D = 245`,
- vertical radius `3`,
- horizontal steps `17`,
- Stage 3 `3 x 10`.

For case-optimization experiments, use a separate HPWL-oriented mode instead of
pretending the paper objective already optimizes HPWL.  The next useful
algorithmic step is a bounded local net delta term in candidate scoring:

- evaluate only nets incident to the moved cell,
- cache or incrementally maintain local net bounding boxes where possible,
- avoid full HPWL recomputation in the candidate loop,
- keep candidate caps and accepted-move telemetry explicit,
- compare strict final HPWL against both the diamond reference and the best
  evolved donor.

## What Not To Do

- Do not keep increasing horizontal radius after the solution saturates.
- Do not expand vertical radius by default; it can increase runtime and damage
  row-choice stability.
- Do not add extra Stage 3 rounds unless telemetry shows Stage 3 is still
  finding useful moves.
- Do not call small HPWL gains proof of a stronger algorithm.  Use them as
  donor evidence, then test whether the underlying mechanism transfers.

## Implication For Future Agents

If the goal is large HPWL improvement, the agent should change candidate
ranking or acceptance, not just the candidate count.  A useful implementation
should make the legalization search aware of local wirelength while preserving
bounded complexity and strict legality.
