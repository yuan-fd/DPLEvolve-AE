# Algorithm Role And Liveness Basics

## Agent Use

- Role: legalization mechanism support. Open after a selected route needs legalizer behavior, liveness, or repair detail.
- Do not optimize legalization-stage HPWL alone; final flow HPWL and DPO recoverability remain decisive.


This card gives baseline semantics for the main legalization families used by
DPL-Evolve.  Use it before routing a Student to Diamond, negotiation, LEGALM, or
Differential Guidance so the plan names what is expected to legalize, what is
only a heuristic producer, and which logs prove liveness.

## Role Labels

- `primary_legalizer`: an algorithm that can commit a complete legal placement
  by itself when its route is active.
- `guidance_producer`: a heuristic stage that proposes target fields, row
  pressure, moved cells, touched nets, or frontier queues.  It is not accepted
  until a primary legalizer or DPO consumer uses it.
- `repair_or_polish`: a bounded stage that improves a legal placement or closes
  residual local defects after a primary legalizer.
- `handoff_consumer`: a legalizer or DPO stage that proves it consumed producer
  state through changed ordering, scoring, candidates, or accepted moves.

Teacher should state the intended role before assigning a route.  Student should
log counters that prove the role actually happened.

## Diamond / Local Greedy Legalization

Diamond-style legalization is a `primary_legalizer`.  It can directly produce a
legal placement through local row/site search and local repair.  Its strengths
are simple locality, low overhead, and a placement basin that DPO often handles
well.

Useful mutations:

- candidate row/site ordering;
- local scoring weights and exact touched-net filters;
- bounded row or segment repair around high-pressure cells;
- compact handoff of moved cells, dirty rows, or touched nets to DPO.

Liveness evidence:

- route log proves Diamond/local legalizer executed;
- final legality is clean;
- local attempt/accept/reject counters are nonzero when the route claims a
  changed mechanism;
- stage HPWL separates legalizer effect from improve-placement recovery.

Common failure:

- treating Diamond as either untouchable or automatically best.  It is a strong
  primary legalizer and donor, but a no-op Diamond route is not an evolved
  mechanism.

## Negotiation / Resource-Allocation Legalization

Negotiation is also a `primary_legalizer` when the negotiation route executes.
It should be treated as a resource-allocation and conflict-resolution family,
not just a generic HPWL knob.  It can directly legalize when its conflict queues,
resource prices, candidate search, and closure logic are active.

Useful mutations:

- conflict/resource pricing;
- candidate generation for scarce rows or fragmented intervals;
- repair schedule and post-negotiation local polish;
- compact moved/touched-cell handoff to DPO when final HPWL is weak.

Liveness evidence:

- negotiation-primary route signal;
- nonzero conflict/candidate/round counters when the design has resource
  pressure;
- final violation count and strict legality;
- moved/touched-cell or frontier counters if used as a DPO producer;
- stage HPWL showing whether negotiation changed the legal basin.

Common failure:

- replaying negotiation after Diamond already produced a clean legal placement
  can be a dead producer.  If moved/frontier counters are zero, reject or retarget
  the route instead of tuning a no-op handoff.

## Differential Guidance / LEGALM-Style Target Fields

Differential Guidance is normally a `guidance_producer`, not a complete
legalizer by itself.  A target field, row pressure map, or moved-target list is
only a heuristic until another mechanism uses it.  It must be paired with a
primary legalizer, a repair stage, or a DPO consumer.

Valid timings:

- before Diamond or negotiation, to create a better search basin;
- inside a LEGALM-style full route, followed by explicit legal assignment and
  no-overflow closure;
- after legalization, as residual target misses, dirty rows, or touched-net
  frontiers for DPO;
- inside DPO, as a priority/hot-set or exact-search seed.

Liveness evidence:

- guidance objective and stop point are stated before coding;
- guided cells, moved targets, row moves, target misses, and frontier size are
  logged;
- consumer attempts and consumer accepts are nonzero;
- full-flow final HPWL improves, or the failure is attributed to stop point,
  consumer weakness, legality rejects, or stale/erased handoff state.

Common failure:

- mistaking a nonzero target field for legal progress.  If guidance improves
  legalization HPWL but final HPWL worsens, change the stop point, legalizer
  closure, or DPO consumer instead of blindly increasing guidance iterations.

## Review Checklist

1. Is the route using a complete primary legalizer, or only a guidance producer?
2. If it is guidance, which legalizer or DPO consumer uses the produced state?
3. Are producer and consumer counters both nonzero?
4. Did the mechanism change final strict HPWL, not only legalizer-stage HPWL?
5. If counters are zero, is this implementation bug, wrong timing, or a method
   that should be rejected as low-ROI for this route?
