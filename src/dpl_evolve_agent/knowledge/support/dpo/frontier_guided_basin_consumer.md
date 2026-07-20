# Frontier-Guided Basin Consumer

## Agent Use

- Role: DPO mechanism support. Open after Teacher or current logs identify an improve-placement/DPO bottleneck.
- Use it to design source-local consumers, counters, and repair checks; do not use it as a case-type route selector.


Evidence type: source-level co-optimization insight.  This card captures a
handoff-aware improve-placement mechanism rather than a complete legalizer.

## Core Idea

Differential guidance, LEGALM-style target fields, or other legalizer-side
frontiers should not be judged only by their own producer counters.  They are
useful when a downstream basin consumer uses them to rank, probe, and accept
exact local DPO moves.

The mechanism therefore has two parts:

- producer: bounded frontier or residual state from legalization,
- consumer: frontier-aware candidate ordering and exact local basin search in
  improve placement.

## Recommended Producer Signals

Use compact in-process state only:

- moved cells with unresolved HPWL residue,
- target-miss cells,
- touched nets,
- residual row or segment pressure,
- group/region pressure near boundaries or fixed objects,
- frontier priority scores derived from legalizer-side evidence.

Do not pass coordinates that are already recoverable from OpenDB import.

## Recommended Consumer Behavior

In the basin search:

- rank candidates by frontier priority before pure displacement ranking,
- boost candidate score by incident-net HPWL or touched-net importance,
- keep a cap on frontier-ranked cells,
- use exact affected-net or journal HPWL to commit the selected candidates,
- prefer grouped residual-net windows when independent single-cell moves cannot
  shrink the net box,
- measure how many frontier-ranked candidates are selected, probed, and
  accepted,
- compare frontier-driven accepts against non-frontier basin accepts.

This turns handoff from passive metadata into an explicit DPO search bias.

## When It Is Worth Trying

- a producer route is clearly live but final HPWL still lags,
- stage-wise metrics show that after-improve recovery is the missing piece,
- a pure basin donor works but still leaves additional residue,
- a global or differential route improves some structures but the downstream DPO
  consumer is too weak.

## Main Failure Modes

1. Producer live, consumer dead.
   - Frontier counters are nonzero, but frontier-ranked probes or accepts stay
     at zero.
   - Treat this as consumer implementation failure, not producer failure.

2. Producer dead, consumer alive.
   - Basin search runs, but it is not using meaningful frontier signals.
   - This is just a generic basin donor, not a handoff mechanism.

3. Both live, final HPWL still weak.
   - Preserve the route as handoff evidence, but mark the current frontier
     ranking policy as low ROI and redesign the consumer or producer score.

4. Metadata live, exact consumer unchanged.
   - Producer and ranking counters move, but the exact accepted moves are the
     same as the generic DPO path or final HPWL barely changes.
   - Treat this as passive handoff.  The next route should change the exact
     consumer: grouped windows, transaction scoring, candidate generator, or
     accepted-move protection.

## Teacher Guidance

- Use this route when a guidance or LEGALM-like producer is already available
  and a pure DPO donor has shown that exact basin search can help.
- Ask for producer and consumer counters in the same report.
- Promote only if full-flow final HPWL improves; otherwise keep it as handoff
  knowledge.

## Student Guidance

- Keep frontier payload compact: `dbInst*`, `dbNet*`, `dbGroup*`, `dbRegion*`,
  dense ids, vectors, bitsets, fixed-cap queues.
- Rebuild consumer mappings after DPO import and never persist transient DPO
  pointers across the stage boundary.
- Report frontier produced, frontier ranked, frontier selected, frontier
  probed, frontier accepted, and runtime spent in frontier-aware ranking.
