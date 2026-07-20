# Skill: trace_logging

Use this when a Student adds or changes a legalization or improve-placement
mechanism and needs enough evidence for Teacher to verify that the mechanism
actually executed.

## What To Log

Keep logs lightweight and stage-local.  Prefer existing telemetry or logger
patterns in `dpl_evolve` when available.

For legalization/detailed placement:

- candidate count and accepted count,
- overflow or legality repair count,
- affected-net or HPWL-cost queue size,
- rejected candidates by main reason,
- elapsed pass time if an existing timer/log facility is available.

For improve placement:

- frontier/window/candidate count,
- accepted moves or swaps,
- total local HPWL delta estimate versus committed delta when available,
- stop reason,
- whether legalization handoff state was consumed.

For coordination/handoff:

- number of cells/nets marked by legalization,
- number consumed by improve placement,
- number pruned by runtime or candidate caps.

## Performance Rules

- Do not log inside hot inner loops per candidate or per net.
- Aggregate counters in local variables or thread-local counters, then print
  once per pass.
- Avoid full-cell or full-net rescans just to produce telemetry.
- Log enough to prove behavior and cost, not enough to change runtime.

## Report

The knowledge card should include the key counters and explain whether they
match the expected mechanism.  If counters are zero or implausible, report that
the mechanism probably did not execute as intended.
