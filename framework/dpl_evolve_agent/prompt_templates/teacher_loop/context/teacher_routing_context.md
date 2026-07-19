# Teacher Routing Context

- case: `$case_id`
- flow_variant: `$flow_variant`
- threads: `$threads`
- start_kind: `$start_kind`
- round_id: `$round_id`
- iteration: `$iteration_name`
- full_context: `$full_context_path`
- case_feature_route_insight_packet: `$case_feature_route_insight_packet_path`
- manual_teacher_guidance: `$manual_teacher_guidance_path`

Use this file as the Teacher context entry point for initial routing.

- Start from `current_run_packet.md`, `design_characteristics.md`,
  `baseline_routing_note.md`, `peer_learning_routing_note.md`, and
  `start_seed_calibration.md`.
- Treat `case_feature_route_insight_packet` as current evidence plus a compact
  route-insight index. It does not rank or select routes in Python. Teacher must
  classify the case from stage metrics and physical features, then choose and
  justify the route hypotheses. Other case-evolution/strategy cards are
  secondary inspiration after a route or bottleneck is selected.
- Before assigning routes, check whether the stage metrics match a verified
  case-type route signature in `case_feature_route_insight_packet`.  For large
  row-rich or datapath-like recoverable basins, do not collapse the plan to
  guard or late closure.  Route the strongest Student to the one-student D+A core
  chain when the signature matches: fast clean Diamond start -> broad
  sourceTopK/global-swap exact producer with full-source traversal and
  per-source top-K replay -> cumulative native accepted-node/hot-segment
  handoff -> hot-frontier selected-segment reorder with bounded fallback.
  Exact local closure is a follow-on repair when the source surface is ready.
  Use `diamond`/Blueprint D+A for first-shot reconstruction when the feature
  evidence is large row-rich/control-datapath, clean Diamond legal HPWL rises or
  is weak, and producer liveness is absent, gated, or tiny.  Do not split D and A
  across different Students for a one-student warm-up check.  Use `diamond` as
  the first source parent; use `default_negotiation`/Blueprint B only as a
  separate quality lane when endpoint/source residue is explicit.
- For the Diamond/sourceTopK feature route, describe critical-row micro-start,
  critical-net chain assignment, multi-row residual transactions, and
  segment-local residual swaps as continuation consumers over the same native
  accepted/hot footprint.  They should not delay the first build/evaluation of
  sourceTopK plus selected reorder plus exact local closure.
- If the design is large row-rich/control-datapath, has little true macro
  evidence, clean Diamond legal HPWL rises or is weak, and final HPWL is
  dominated by improve-placement recovery, the first one-Student route must be
  `diamond`/Blueprint D+A, not `default_negotiation`.  The Student should query
  `Blueprint D+A` and implement the core chain in this order: forced exact
  sourceTopK/global swap with full-source traversal, small ranked per-source
  candidate pools, exact-probe caps, and gain-rate stop -> cumulative native
  accepted-node/hot-segment handoff -> hot-segment selected reorder with bounded
  fallback.  Exact local closure and critical-frontier consumers are follow-on
  repairs after the core is live.  Many exact accepts with only a tiny surviving
  accepted frontier is a handoff-lifetime failure, not a completed route.
  Negotiation can be a secondary lane only when there is explicit
  endpoint/source residue and enough Students to test it separately.
- For dense cases, density alone is not a Blueprint C decision.  Use
  `framework`/Blueprint C only when target-miss/current-net/exact-anchor
  producer state exists or must be rebuilt and can be consumed by targeted
  global/vertical exact moves plus selected-segment residual reorder.  If dense
  evidence instead shows broad exact-DPO recovery and no current-net anchor
  payload, assign the Diamond value lane first.
- For low-util or Diamond-stable local residue without the large cumulative-chain
  signature, route at least one Student to the Diamond exact lane when evidence
  supports it: preserve a Diamond-compatible legal basin, force the enhanced DPO
  path, and implement exact sourceTopK/source-edge candidate generation,
  exact move/swap scoring with `DetailedHPWL::delta`, replay through
  journal/rollback, accepted hot-segment handoff, and liveness counters.  Do not
  count LEGALM-only, negotiation-only, mirror-only, frontier-only,
  Diamond-frontier-only, or telemetry-only work as a hit for this blueprint.
- If `manual_teacher_guidance` is not `none`, read it before assigning
  students. It is a case-specific continuation note, not a global rule.
- Open `full_context` only with a targeted `rg` or short excerpt when those
  packet files still leave a concrete metric line, symbol, or path unresolved.
- Do not scan the full context file linearly during the initial routing pass.
