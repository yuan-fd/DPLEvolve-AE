# Full-Flow Timeout And Target Selection

## Agent Use

- Role: inspiration/support only. This card must not directly select a case route or override the core blueprint map.
- First map the case through `case_feature_to_mechanism_route_map.md`; then use this card only to refine a selected mechanism, risk, or failure diagnosis.
- If this card repeats a core idea, treat the core blueprint as authoritative and keep this card as background evidence.


## Repository State

The active project line is the constrained `detailed_placement_evolve`
framework plus lightweight private variant relink.

The durable experiment boundary is:

- keep classic OpenROAD DPL and evaluator logic unchanged,
- edit only private `tools/OpenROAD/src/dpl_evolve` variant sources,
- relink private OpenROAD binaries,
- compare against the canonical baseline suite (`openroad_dpl_flow`,
  `openroad_dpl_negotiation`, and `evolve_default`),
- store runtime artifacts under ignored state/report/result directories.

## Large-Case Timeout Observation

On a very large benchmark, strict full-flow evaluation can exceed outer
timeouts even when legalization and placement checks have already succeeded.
The heavy part can be final metrics and downstream improvement work rather
than the legalizer itself.

Failure pattern:

- legalizer-stage runtime was comparable to the canonical flow,
- downstream improvement and mirroring dominated total elapsed time,
- a wrapper timeout that is acceptable for small and medium designs was too
  tight for full strict metrics on the large design.

Policy: use separate budgets for legalizer-only smoke, full placement flow, and
timing/power-style metrics.  Do not diagnose a legalizer as failed only because
the full metrics wrapper was too tight.

## Target Selection Policy

The main discovery case should be:

- large enough to expose runtime and quality tradeoffs,
- small enough to iterate quickly,
- sensitive to both HPWL and displacement tail,
- capable of running full strict metrics within the normal loop budget.

Before each Teacher/Student round:

- refresh `openroad_dpl_flow`, `openroad_dpl_negotiation`, and
  `evolve_default`,
- require strict legality and independent metrics,
- allow moderate runtime increase if HPWL meaningfully improves,
- report both legalizer-stage and final post-improve HPWL when available,
- preserve displacement tail and max displacement as guard metrics,
- require stage telemetry that explains the mechanism.

Do not treat "faster but same HPWL" as the main success criterion.  The goal is
to test whether aggressive case-specific algorithm changes beat the canonical
flow without changing the evaluator.

## Five-Percent Target Semantics

The 5% HPWL reduction line is a research belief, not a hard validity gate and
not a ceiling.  Teacher and Student agents should assume this scale of
improvement is possible until canonical evidence says otherwise, and should
seek mechanisms that can reach or exceed it.

Candidates below 5% can still be useful when they provide clean stage-donor
evidence, especially if they improve legalization, improve placement, or the
handoff between them in a way another student can combine or compress.  They
should not be reported as final success unless the broader loop explains why
the 5% hypothesis has been falsified for that design and flow variant.

## Evolution Calibration

Medium-large discovery rounds showed that a candidate can beat the canonical
flow in one iteration and then regress in later iterations when students:

- continue weaker lineages,
- trust local proxy metrics too much,
- add more seeds or row coverage without improving final HPWL,
- optimize legalizer-stage numbers that are later erased by improve/mirror.

Policy: preserve the best-so-far strict artifact as an elite parent.  Let one
student continue it each iteration, while other students explore larger
changes.  Promote candidates only on final strict evaluator results, using
stage metrics for explanation rather than proof.

## Runtime Gate And Bad Execution Patterns

The discovery loop should allow runtime to increase when the mechanism is
meaningfully better, but runtime cannot be unbounded.  Use the canonical
baseline full-flow runtime for the same case, utilization, flow variant, and
thread count as the anchor.

Policy:

- a candidate's evaluator runtime must stay within the current experiment's
  configured full-flow runtime multiplier, using the same wall-runtime metric
  as the baseline row,
- a candidate above the configured multiplier is a failure for promotion even
  if final HPWL is good,
- an over-budget candidate may remain a donor only when its stage telemetry and
  code diff show a concrete mechanism that another student can reimplement
  with bounded complexity,
- Teacher must report `runtime_s`, `runtime_vs_baseline`, final HPWL, and
  stage-wise HPWL before assigning follow-up work,
- Student must design candidate counts, subpass counts, perturbation counts,
  and exact-HPWL checks so the expected complexity is controlled before running
  the evaluator.

Negative patterns observed in evolution rounds:

- endpoint-like stacking or random-improve perturbations that approach the
  target only through uncontrolled runtime instead of scoped,
  handoff/frontier-targeted work,
- claiming a stage mechanism was implemented while the runtime log shows zero
  accepted work, zero candidates, or an inactive pass,
- continuing a lineage that Teacher identified as stage-negative, such as
  changing a legal-site choice after stage evidence showed the improvement
  should move to downstream improve placement or the legalization/improve
  handoff,
- relying on stale relinked binaries or logs that do not contain the new
  mechanism's telemetry,
- spending the round on archive/link workarounds instead of producing a clean
  build through the supported variant build path,
- adding broad defensive checks, rollbacks, or telemetry that do not create an
  HPWL source mechanism and still increase runtime.

Required Teacher action:

- preserve the best over-budget candidate as a donor only if it explains a
  real HPWL source,
- assign at least one next-round student to compress that donor into a bounded
  implementation,
- do not let a slower donor replace the elite parent until it satisfies the
  configured runtime gate and strict legality gate.
