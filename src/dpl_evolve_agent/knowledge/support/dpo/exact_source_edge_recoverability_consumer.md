# Exact Source-Edge Recoverability Consumer

## Agent Use

- Role: DPO mechanism support. Open after Teacher or current logs identify an improve-placement/DPO bottleneck.
- Use it to design source-local consumers, counters, and repair checks; do not use it as a case-type route selector.


Evidence type: measured mechanism insight from clean full-flow replay.  This is
not a benchmark-name rule and not a source patch.  Use it when stage metrics
show that legalization may be legal but not HPWL-optimal, while improve
placement still has a large recoverable basin.

## Core Lesson

A strong final-HPWL result can come from a legalizer that preserves a
DPO-recoverable basin, not from a legalizer that directly minimizes
`HPWLlg`.

The useful pattern is:

```text
global/differential guidance producer
-> primary legalizer closure
-> exact source-edge or global-swap DPO consumer
-> selected-segment exact reorder
-> bounded endpoint/pair/exact polish
```

In the strongest observed replay, the legalization stage slightly worsened
HPWL relative to the global input, but the improve-placement stage recovered a
large amount of HPWL and the final detailed-placement flow beat the default
route substantially.  Teacher should therefore classify this as a
producer-consumer DPO recovery win, not as a legalize-only win.

## Mechanism Structure

1. Guidance producer.
   - Run differential or LEGALM-style guidance only far enough to create a
     target field, pressure map, moved-cell set, or frontier.
   - Treat the output as a seed or frontier, not as proof that legalization is
     solved.
   - Useful counters: guided cells, moved targets, overflow reduction,
     row moves, target-miss count, and guidance runtime.

2. Primary legalizer closure.
   - Close legality with a complete legalizer such as negotiation or Diamond.
   - Preserve global-placement locality and row-order recoverability where
     possible; do not over-optimize legal HPWL in a way that destroys the DPO
     basin.
   - Useful counters: primary route signal, guided seed handoff size,
     conflict/resource resolution counters, final violation count, and
     legalizer runtime.

3. Exact DPO consumer.
   - Build a large but bounded candidate pool from source edges, active
     frontiers, touched nets, dirty rows, or high-impact local windows.
   - Use cheap ranking first, then exact affected-net HPWL or transactional
     journal scoring for top-K candidates.
   - Commit only exact positive legal moves with rollback protection.
   - Useful counters: generated candidates, top-K, exact-scored candidates,
     accepts, accepted exact delta, replay failures, duplicate/same-site
     rejects, and nonpositive exact scores.

4. Edge-critical and segment consumers.
   - Add edge-critical queues when high-impact frontier edges explain residual
     HPWL.
   - Run selected-segment exact reorder when row segments have compact local
     recoverability.
   - Use post-reorder exact, pair, and endpoint polish only as bounded closure,
     not as an unbounded tail.

## Why It Can Be Fast Enough

This mechanism spends runtime where exact accepts are dense.  The guidance and
legalizer closure can be a small fraction of the total flow if they only create
and close a recoverable basin.  The DPO work is valuable when exact global-swap,
edge-critical, reorder, or polish counters show many accepted moves and large
accepted deltas.

Do not interpret a short runtime as proof of correctness.  The proof is the
combination of clean legality, final HPWL improvement, and nonzero producer and
consumer counters.

## When To Try It

Use this card when:

- the placement is large enough or sparse enough to have many legal DPO
  candidate moves;
- `HPWLlg` is neutral or slightly worse, but after-improve recovery is large;
- default DPO appears to leave a large local/global-swap basin;
- a guidance producer is live and a primary legalizer can close legality
  without destroying row-order recoverability;
- exact candidate counters are high enough that top-K scoring is justified.

## When Not To Keep Deepening It

Stop or reroute when:

- exact accepts collapse after one or two passes;
- replay failures or legality rollbacks dominate;
- exact scoring is live but accepted delta is tiny;
- runtime grows without final HPWL improvement;
- the legalizer destroys the global-placement basin so DPO cannot recover;
- the route becomes only telemetry, threshold tuning, or repeated endpoint
  polish.

If producer counters are live but DPO accepts are weak, repair the consumer.  If
DPO is live but the final result remains weak, inspect whether the primary
legalizer created a DPO-hostile legal state before adding more DPO passes.

## Liveness Checklist

A Student report should include compact evidence for:

- guidance prepared a bounded target/frontier;
- the primary legalizer route actually closed legality;
- exact global-swap or source-edge scoring generated, exact-scored, and
  accepted candidates;
- edge-critical or frontier-ranked queues had nonzero accepted moves when
  claimed;
- exact reorder selected segments and accepted windows;
- post-reorder exact/pair/endpoint polish had bounded probes and accepts;
- `HPWLg`, `HPWLlg`, `HPWLimprove`, `HPWLfinal`, runtime, and legality.

## Pseudocode

```text
build_guidance_target():
    run bounded differential/global guidance
    record moved targets, touched nets, pressure windows, and runtime

close_legality_with_primary_legalizer():
    seed the primary legalizer from the guidance target
    close all legality violations
    preserve locality and recoverability where possible
    record route, conflicts, handoff size, and final violations

run_exact_dpo_consumer():
    import legal placement into the detailed-placement manager
    repeat while exact accepted gain remains useful:
        generate source-edge/frontier/global-swap candidates
        cheap-rank candidates and keep top-K
        exact-score affected nets for top-K candidates
        transactionally commit legal positive moves
        record accepts, accepted delta, replay failures, and runtime
    run selected-segment exact reorder on active row windows
    run bounded exact/pair/endpoint polish as closure
    accept only if final full-flow HPWL improves with clean legality
```

## Teacher Guidance

Teacher should route this mechanism as a co-optimization plan, not as a DPO-only
tail.  The plan must name the producer, the primary legalizer, and the exact
DPO consumer.  Promotion requires final full-flow HPWL, not better `HPWLlg`.

If this pattern produces a large final win, preserve it as a strong donor.  If
it is weak on the current design, ask whether the failure is producer timing,
primary legalizer basin damage, consumer candidate quality, or runtime
implementation cost before assigning another small local tweak.
