# LEGALM Self-Legal Closure Milestone

## Agent Use

- Role: LEGALM reference/support. Open only when the selected route uses LEGALM-style guidance or the legalizer is the diagnosed bottleneck.
- Treat paper alignment and LEGALM tuning as support for a full-flow mechanism, not as a route selector by itself.


## Status

The current `detailed_placement_evolve` implementation has a LEGALM-style
self-legal closure stage.  It is no longer merely a guidance hook followed by
Diamond when the full LEGALM stage succeeds.

The implemented sequence is:

1. relaxed/static target guidance,
2. coarse ALM multiplier update with bounded BGD-like stencil,
3. seed row-assignment guidance,
4. row free-segment capacity assignment that places all movable standard cells,
5. no-overflow refinement that only moves cells inside available free intervals.

Diamond remains compiled and available as a donor path, but successful
LEGALM-full runs can return before Diamond.

## Observed Evidence

The path produced strict legal placements on:

- a small smoke design,
- a medium wirelength-sensitive design,
- a large row-rich design.

All evidence used the evolved detailed-placement command with the same thread
budget.  The proof is legality closure, not quality competitiveness.

## What This Proves

The LEGALM-style path can produce site-aligned, row-aligned, non-overlapping
placements without relying on Diamond for legality on the tested design
classes.

It does not prove that the objective or final full-flow HPWL is competitive.

## Main Quality Problem

The main quality issue is greedy row free-segment assignment:

- it prioritizes finding capacity over preserving local wirelength,
- large or scarce cells can dominate ordering,
- dense rows can push cells far horizontally,
- no-overflow refinement is still too displacement-oriented and too local.

Next quality work should focus on assignment objective and partition strategy,
not on legality closure alone.

## Next Implementation Targets

Keep these as paper-faithful algorithm work, not hidden flow policy:

- replace greedy capacity assignment with bucketed scanline or min-cost local
  row assignment,
- add partition/local-optimum escape based on overflow or displacement
  stagnation,
- improve BGD candidate stencil so targets are less horizontally destructive,
- make no-overflow refinement optimize touched-net HPWL under legal capacity,
- add per-stage convergence traces to separate objective failure from legality
  failure.

## Guardrail

Do not call this final quality-ready LEGALM.  It is a self-legal closure
milestone and a donor for future evolution.  Promote it only when the complete
strict flow beats the active baseline and best-so-far evolved artifact.
