# Runtime Value Support

## Agent Use

- Role: inspiration/support only. This card must not directly select a case route or override the core blueprint map.
- First map the case through `case_feature_to_mechanism_route_map.md`; then use this card only to refine a selected mechanism, risk, or failure diagnosis.
- If this card repeats a core idea, treat the core blueprint as authoritative and keep this card as background evidence.


## Evidence Type

Measured runtime-value interpretation and support policy.  This card explains runtime tradeoff reporting; it does not choose the case route.

This card summarizes three experiment families that were previously reported
separately but should be interpreted together:

- single-design evolution, which shows how far a case-specific route can move,
- feature-diverse source replay, which separates robust mechanisms from overfit
  case tuning,
- multi-design legalizer-line evaluation, which shows whether a legalizer family
  still helps after the full DPL flow.

Teacher should use the core blueprint map for route choice, then use the
feature-level lessons below only to decide whether a route is an HPWL elite, a
runtime-repair donor, or a failed path.  Concrete benchmark ids, program ids,
numeric tables, and file paths should stay outside default knowledge.

`G_HR = HPWL_gain_percent - runtime_penalty_pp`.

Here `HPWL_gain_percent = 100 * (HPWL_ref - HPWL_sol) / HPWL_ref` and
`runtime_penalty_pp = 0` for runtime ratios up to `1.10`; otherwise
`runtime_penalty_pp = 1.0 * (sqrt(runtime_sol / runtime_ref) - sqrt(1.10)) / (sqrt(2) - sqrt(1.10))`.
Higher is better.  The reference is the complete OpenROAD DPL default flow with
Diamond search (`detailed_placement`, `improve_placement`, and
`optimize_mirroring`) at `G_HR = 0`.  HPWL remains the primary research
objective.  `G_HR` is an analysis field for judging whether the runtime cost is
reasonable and whether a HPWL donor needs runtime repair; it does not decide
promotion, elite continuation, or the default parent.  A 2x runtime increase
requires 1 reference-normalized final-HPWL percentage point of improvement to
break even in this analysis.

## Main Result Pattern

Single-design evolution can produce HPWL wins, but not every HPWL win is
value-positive after runtime.  The square-root runtime penalty is moderate near
baseline and still discourages runaway runtime.  A candidate with negative
`G_HR` can still be a useful HPWL donor, but the next step should either add a
new HPWL source or rewrite the mechanism for bounded runtime before promotion.

The same source snapshot can look strong on one design and weak on another feature class.  Treat a winner as a mechanism to distill, then check it on feature-diverse designs before writing a global rule.

## Strategy Inspiration By Design Feature

The feature notes below are mechanism inspiration, not a route-selection table.
Do not implement them as a case/scenario selector among complete legalizers.
Teacher should translate the feature into a staged source flow with producer,
native closure/repair, and DPO consumer counters.

Medium dense, mostly standard-cell designs with moderate local conflict:

- Negotiation/resource-aware legalization can be a first-class route when it
  reduces conflict without a large runtime increase.
- LEGALM/global guidance and Diamond/local greedy can still be HPWL donors, but
  they need runtime repair before promotion if their search expands broadly.

Very high-utilization or image-compression-like dense designs with little
whitespace and strong global-to-legal HPWL disturbance:

- Do not assume a global LEGALM-style prior is the right parent just because the
  design is dense.  Full-flow evidence can favor local-greedy/Diamond plus DPO
  or handoff repair when global legalization creates a basin that DPO cannot
  recover.
- If a dense route consumes much more runtime for only a small HPWL win, make the
  next iteration a runtime-rewrite or cache/cap iteration, not another broader
  search.

Large row-rich or wrapper-like designs with many local conflicts:

- Negotiation-style residual/conflict handling is promising when it remains
  bounded and produces nonzero conflict-resolution counters.
- LEGALM and Diamond may provide HPWL movement, but broad global searches or
  wide local windows must be distilled before becoming HPWL elites.

Macro, cut-row, fence, or fragmented-row stress:

- Prefer resource/capacity-aware routes that explicitly reason about row
  segments, fixed obstacles, and infeasible resources.
- Treat hband/fixed-physical-cell construction artifacts separately from
  ordinary legalizer quality.  Do not train Teacher policy on broken cutRow
  construction.

Unknown or mixed-feature designs:

- Prefer the robust bounded donor family: mechanisms with modest HPWL gain,
  legal completion across diverse features, and near-baseline runtime.
- Use aggressive high-HPWL donors only when the next step explicitly rewrites
  their candidate generation, exact-delta scoring, caches, or parallel scoring.
- Keep the lowest-HPWL donor visible, but do not call high-runtime code directly
  deployable until the mechanism is distilled into a cached/capped
  implementation.

## Teacher Policy

- Report both raw HPWL gain and `G_HR`.  A candidate with better HPWL but
  `G_HR <= 0` is not a failure; it is a donor that needs either runtime
  repair or additional HPWL source.
- Separate three roles in every review: `HPWL elite`, `HPWL donor`, and
  `failed/regression path`.
- When the best HPWL donor and best `G_HR` donor differ, assign at least one
  route to preserve the HPWL donor mechanism and one route to rewrite it for
  runtime.
- If a route wins by less than about `1%` HPWL and runtime grows beyond about
  `1.5x`, require a specific explanation of why the mechanism should be kept.
- If a route wins about `2%` HPWL at around `2x` runtime, preserve it as
  promising: the current analysis formula makes that tradeoff approximately
  break-even, but HPWL remains the elite-continuation target inside hard gates.

## Student Policy

- Before coding, state whether the target is lower-final-HPWL promotion, HPWL
  donor extraction, or runtime repair.
- If borrowing a high-HPWL/high-runtime donor, do not copy the broad search
  literally.  Replace repeated full scans with affected-net scoring, cached
  legality checks, dirty-row queues, bounded top-k candidate selection, or
  parallel scoring.
- For high-utilization dense designs, first prove that a LEGALM or negotiation
  change improves strict final HPWL before investing deeply in that line.
- For conflict-heavy dense or row-rich designs, negotiation can be a first-class
  route when conflict counters and full-flow metrics agree.
