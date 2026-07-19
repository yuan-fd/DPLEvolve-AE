# Round Context Follow-Up

This is a delta context for `$iteration_name`. Use session memory for stable
global rules from the first iteration. Do not reread broad context top-to-bottom;
open only the current evidence or knowledge handles needed for the next
mechanism decision.

- case: `$case_id`
- flow_variant: `$flow_variant`
- threads: `$threads`
- start_kind: `$start_kind`
- round_id: `$round_id`
- packet: `$packet_path`

## Current Objective

- Promote only legal candidates by final `metrics.json:hpwl`.
- Keep the 5% clean final-HPWL reduction prior as search ambition. It is not a
  promotion gate, but far-below-5% candidates should trigger mechanism-level
  plans instead of small polish.
- Runtime is a `$student_runtime_multiplier`x hard budget and an analysis
  signal. `G_HR` explains whether an HPWL gain is worth its runtime; it is not
  the elite selector.
- Search source-level mechanisms in detailed placement/legalization and
  improve-placement/DPO. Source-internal parameter schedules are allowed; Tcl,
  ORFS, evaluator, and baseline-script tuning are not.
- Search variables include branch/start choice, mechanism order, guidance or
  differential objective terms, heuristic weights, stop/activation policy,
  internal parameters, handoff payload, DPO scoring, and accept/rollback rules.
  Use logs and stage-wise evidence to choose which variable changes next.
- If a live/correct mechanism has extremely small full-flow HPWL movement after
  a meaningful repair attempt, preserve it as `validated-low-ROI` evidence and pivot
  to a different HPWL source.
- Follow-up rounds should use the first-round global route map as memory, then
  read less and optimize the current case more sharply. Keep source scope clear:
  `general source` for reusable kernels/fallback guardrails, `feature-matched
  source` for mechanisms selected because this specific design feature suggests
  upside.
- If the next decision needs a new feature-matched start kind or case-type
  route, open the case feature route insight packet first. It is an evidence
  packet plus route-insight index, not a preselected route list: use it to
  recognize the design pattern from concrete features: size/runtime scale,
  density/utilization,
  legalization HPWL movement, DPO/improve-placement recovery,
  true macro/SRAM/large-blockage/hierarchy or row-fragmentation evidence, and
  live counters. Fixed instance count alone is not macro evidence; macro means
  a physically large instance. Then choose a plausible start family and select
  concrete route hypotheses. Use the relevant roadmap section before writing
  Student guidance. A case may match multiple routes; use them as hypotheses or
  compatible combinations, not as a single fixed selector.
- For a strong retained route, route from the cumulative mechanism chain. Do not
  assign only the final preservation, telemetry, or attribution diff as if it
  were the quality mechanism.

## Current Evidence

$design_characteristics

Detailed design packet: `$design_characteristics_path`

## Scoreboard

$scoreboard

## Elite Continuation

$elite_text

## New Or Recently Useful Pointers

- start calibration: `$start_seed_calibration_path`
- peer learning: `$peer_learning_path`
- case feature route insight packet: `$case_feature_route_insight_packet`
- secondary feature inspiration only: `$strategy_inspiration_insight`
- DPO mechanism insight: `$dpo_source_mechanisms_insight`
- OpenROAD-native handoff insight: `$openroad_native_handoff_insight`
- skill query: use the generated `08_query_knowledge.sh` helper in the Student
  workspace, or `"$DPL_EVOLVE_PYTHON" $skill_query_script --stage <stage> --q "<bottleneck or mechanism>"`
- algorithm cards: `$algorithms_dir`

For Diamond/negotiation/LEGALM/Differential Guidance mixtures, query
`--stage legalization --q "diamond negotiation differential guidance liveness"`
and explicitly separate primary legalizer, guidance producer, repair/polish,
and handoff consumer roles.
Differential guidance objectives can be heuristic: when counters show no useful
moves, no frontier consumer, or DPO-hostile states, change the target
terms/weights, stop point, payload, or consumer instead of only repeating the
same objective.

Use at most a few targeted `rg`/excerpt reads around the selected stage,
mechanism, and source symbols. Pseudocode cards are inspiration contracts, not
case-level algorithm selectors. Current metrics/logs/source evidence override a
card when they conflict.

If current-run legal candidates show tiny HPWL movement, repeat the same family,
or incur runtime cost without proportional HPWL, treat it as weak-gain/over-cost
evidence. Preserve the best donor/ref as rollback if useful, but route from the
observed failure mode: use at most one broader stage/mechanism query or card to
identify a different source-level hypothesis, then assign stronger within-run
routes. If the case feature route insight packet contains a route signature that
matches current evidence, prefer the full mechanism chain from that packet over
a single-stage subset.
For large row-rich or
datapath-like recoverable basins where final HPWL follows downstream DPO
accepts, the primary route should normally be a cumulative
producer/closure/DPO/reorder chain; pure Diamond source-edge work is a
complementary local-consumer route, not the only main route. Prefer
reconstructing or deepening that route over another same-basin queue-depth,
threshold, or preservation-only edit. Current-run kept/rejected refs may be
repaired with `25_trial_source.sh begin --from-ref <ref>`.
If the best donor is continued, it should be an elite-expansion route: attempt
quality improvement and efficiency release on top of the donor; runtime repair
is useful to make room for more complex search, not as a standalone success.
Before assigning a heavy donor, first check whether the selected route has a
lighter reconstruction or runtime-repair path. Query secondary runtime cards
only to refine cost control after the blueprint is chosen.

Do not over-optimize for universal donors in follow-up rounds. The target is
the best legal final HPWL for this specific case under the runtime gate; transfer
evidence is a secondary classification.

## Prior Iteration Delta

$prior_context

## Teacher Boundary

Output only the next routing/review decision and concise per-student mechanism
guidance. The Student workspace packet owns commands and exact helper paths.
