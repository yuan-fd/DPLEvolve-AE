# DPO Source-Level Mechanism Lessons

## Agent Use

- Role: DPO mechanism support. Open after Teacher or current logs identify an improve-placement/DPO bottleneck.
- Use it to design source-local consumers, counters, and repair checks; do not use it as a case-type route selector.


Evidence type: generalized source-level DPO mechanism observations.  Use this
card to select and adapt mechanisms from current case features, stage metrics,
and liveness logs.

For legalization-to-DPO state transfer, pair this card with
`knowledge/support/dpo/openroad_native_handoff.md`.  Handoff records should be
OpenROAD-native in-process structures, not external files or log-derived data.

## Useful Mechanism Families

Observed priority:

- Prefer mechanisms that couple a producer frontier with an exact consumer:
  residual-net queues, grouped windows, exact transaction/rollback, and
  affected-net scoring.
- Treat DPO-only tails as secondary donors unless stage metrics show the
  legalizer is already stable and the remaining HPWL is local DPO residue.
- Treat telemetry-only, guard-only, threshold-only, mirror-only, and passive
  ordering changes as diagnosis or elite-preservation evidence, not active
  search mechanisms.

1. Bounded source-edge and local-net scoring.
   - Build a compact candidate set from cells near active source edges, touched
     nets, dirty rows, or high-pressure windows.
   - Use cheap filters first, then exact affected-net HPWL only for top-K
     candidates.
   - Track attempted, filtered, exact-scored, accepted, and rejected counts.

2. Transactional improve placement.
   - Evaluate moves through a small journal: proposed cell positions, affected
     rows, affected nets, exact HPWL delta, legality check, accept or rollback.
   - Keep rollback local and deterministic so broader searches do not corrupt
     legality.
   - Reject zero-counter or no-op transactions as non-executed mechanisms.
   - A lightweight post-DPO basin pass is a good first donor; an aggressive
     schedule should be treated as a continuation only after the light donor is
     proven live.

3. Selective staged descent.
   - Start with the cheapest high-confidence move family.
   - Escalate to stronger reorder, swap, or multi-row repair only when the
     frontier is active and expected HPWL gain justifies the extra work.
   - Stop escalation after a capped number of failed windows or low accepted
     gain rate.

4. Scoped LSMC-style basin escape.
   - Use a small, legal perturbation around an active window, then run local
     descent and accept only if canonical or exact local HPWL improves.
   - Keep perturbation scope bounded by dirty rows, touched nets, displacement
     limits, and runtime budget.
   - LSMC is a basin-escape tool: use controlled perturbations, accepted-gain
     counters, and handoff/frontier targeting so the runtime buys a specific
     recoverability test instead of blind random trial-and-error.
   - Use deterministic parallel work for independent windows when the candidate
     set is already bounded.  Do not use parallelism to hide an unbounded or
     low-accept search.
   - Treat LSMC as a secondary quality donor unless a light exact consumer is
     already live and final HPWL still has a clear local-minimum gap.

5. Legalizer-to-DPO handoff consumers.
   - Legalization should expose compact frontier signals that DPO can consume:
     dirty rows, pressure segments, moved cells, touched nets, boundary-active
     cells, and local repair windows.
   - A handoff mechanism is live only when producer and consumer counters are
     both nonzero and the after-improve stage responds.
   - Frontier-aware basin ranking is often more valuable than frontier-only DPO
     ordering because it converts producer state into exact local search rather
     than passive queue bias.

## What To Avoid

- Do not depend on archived source patches or prepared DPO start branches.
- Do not spend most of a round on guard-only preservation of an already retained
  elite.  Keep one guard when useful, then route other Students to producer,
  consumer, or handoff mechanisms that can change final HPWL.
- Do not call a route handoff-aware when it only records metadata.  The DPO
  consumer must probe and accept candidates that are visibly influenced by the
  producer state.
- Do not promote mirror/orientation priority changes as the main DPO mechanism.
  Mirror work is valid only when it preserves or attributes named accepted
  DPO/reorder/handoff moves; otherwise keep it as diagnostics.
  They are useful only as protection or observability for accepted exact moves.
- Extra endpoint-like subpasses are useful only when they are transformed into
  an algorithmic source mechanism with scope, counters, and stop conditions.
- Randomized or repeated descent can be explored when it is scoped, capped,
  instrumented, and tied to a handoff/frontier hypothesis; avoid blind random
  trial-and-error when the same budget can target known dirty rows, touched
  nets, pressure segments, or boundary-active cells.
- Do not add huge logs. Prefer one compact line per pass plus aggregate
  counters and elapsed time.
- Do not change public Tcl/SWIG-wrapped class layout for ordinary caches or
  handoff state; keep implementation-local state private when possible.

## Teacher Guidance

- Assign the smallest source-local mechanism that can attack the observed
  stage-wise gap.
- Useful source handles are `src/Optdp.cpp`, `src/optimization/*`,
  `src/objective/*`, `src/StudentAlgorithm.cpp`, and compact handoff structs.
- Require logs that prove liveness, cost, and acceptance quality:
  candidate cap, filtered/scored count, accept rate, exact HPWL gain, rollback
  count, handoff produced/consumed count, and elapsed pass time.
- If a high-quality clue is slow, guide the Student to rewrite it with caches,
  top-K exact scoring, thread-local scratch, or deterministic parallel
  reduction before discarding it.
- If a route has become guard-only or low-ROI after one meaningful repair,
  preserve the source ref and switch the next active route to a different
  mechanism family.
