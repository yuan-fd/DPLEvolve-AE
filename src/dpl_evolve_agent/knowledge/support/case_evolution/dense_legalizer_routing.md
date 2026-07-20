# Dense Producer/Consumer Routing Insight

## Agent Use

- Role: inspiration/support only. This card must not directly select a case route or override the core blueprint map.
- First map the case through `case_feature_to_mechanism_route_map.md`; then use this card only to refine a selected mechanism, risk, or failure diagnosis.
- If this card repeats a core idea, treat the core blueprint as authoritative and keep this card as background evidence.


## Evidence Type

Measured stage evidence for dense standard-cell cases.  Use this as
stage-level mechanism support, not as a first-route selector.  For current
full-flow promotion and case routing, read
`routing/case_feature_to_mechanism_route_map.md` first and open this
card only after the Teacher-selected route needs dense-producer detail.

## Observation

On some high-utilization, standard-cell-dense placements, LEGALM-style
guidance/frontier production is useful as a legalization-stage mechanism.  It
can reduce global-to-legal HPWL disturbance or create a recoverable basin for
downstream exact DPO consumers.

The mechanism evidence is not a tiny metadata change.  The useful route
activates high-pressure refinement or residual frontier production, then
requires downstream counters: frontier attempts, accepted interval/swap/chain
moves, exact DPO accepts, and accepted HPWL-gain counters.

The same mechanism ordering does not hold for all design archetypes, and
full-flow evidence showed that even dense designs can prefer local closure plus
DPO or handoff repair.  Treat LEGALM-style guidance/frontier as a
dense-standard-cell mechanism prior inside `framework`, not as proof that
legalization-stage quality alone is enough.

Important calibration: this card is about legalization-stage behavior.  Later
full-flow rescoring showed that some high-utilization dense designs can regress
after LEGALM or NEGOTIATION even when a legal-stage metric looks better.  On
those features, the guidance/frontier producer is diagnostic unless strict
full-flow metrics prove it improves final HPWL. Prefer current
full-flow HPWL evidence when it names a different route such as
Diamond/local-DPO repair.

## Teacher Policy

When the core route map selects a dense producer/consumer route, diversify the
concrete hypotheses:

- optionally assign one diagnostic dense-prior route from `framework` when the
  case is high-utilization, standard-cell dense, or shows large legalization
  HPWL inflation from the global placement, unless current full-flow evidence
  already shows that guidance/frontier production damages final HPWL for that
  case archetype,
- assign another route from `diamond` or Diamond-local behavior when
  the case has larger row/macro structure or needs local greedy closure,
- assign another route from `default_negotiation` when the case shows conflict
  or residual-overlap patterns that may need negotiation-style resolution,
- keep one route free for a hybrid or improve-placement-primary hypothesis when
  stage evidence says final HPWL is dominated by after-legal recovery.

From iteration 2 onward, Teacher should not keep all students on the same
initial line.  Use stage-wise attribution to decide one of three actions for
each student:

- continue the original direction if legal HPWL, after-improve recovery, and
  final strict HPWL move in the right direction,
- switch the student to a stronger route when its original direction loses at
  the stage it was meant to improve or improves a stage but damages final HPWL,
- hybridize routes when one legalizer gives a better legal state but another
  route or improve-placement mechanism recovers final HPWL better.

## Hybrid Pattern To Try

For dense cases, a plausible hybrid is:

- first run a bounded LEGALM/global-differential or high-pressure legalization
  phase to reduce the large global-to-legal HPWL disturbance,
- then run a bounded Diamond/local-greedy or negotiation-style residual phase
  on the cells/rows/nets left in bad local states,
- pass the frontier, touched nets, row pressure, or accepted-move tags into
  `improve_placement_evolve`,
- make `Optdp` consume that state with bounded local HPWL candidate scoring,
  not just a generic random tail.

This is an algorithmic stage sequence inside the evolved source, not a license
to rerun unchanged placement endpoints.  The Student should implement a clear
producer/consumer handoff between legalization and improve placement.

## Student Execution Expectation

Teacher packets should give a step-by-step chain, while still leaving the
Student room to plan the concrete patch:

1. Diagnose the case and current source around the assigned legalizer/improve
   hooks.
2. Implement the legalizer-stage change.
3. Implement the improve-placement consumer or companion change.
4. Build and evaluate canonical metrics.
5. Inspect stage-wise HPWL and runtime.
6. Revise once by either keeping the route, switching the weak stage to another
   legalizer family, or hybridizing two families.
7. Keep the best clean candidate and report the source-level evidence.

Use this card as feature-level guidance: dense high-utilization designs can
benefit from LEGALM-style producer/frontier mechanisms, but final-flow
promotion still requires strict stage-wise evidence on the active design.
