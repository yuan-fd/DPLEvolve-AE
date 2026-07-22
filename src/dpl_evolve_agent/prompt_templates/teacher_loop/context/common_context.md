# Round Context

- case: `$case_id`
- flow_variant: `$flow_variant`
- threads: `$threads`
- start_kind: `$start_kind`
- round_id: `$round_id`
- iteration: `$iteration_name`
- packet: `$packet_path`

## Sources

- private dpl_evolve source: `$dpl_evolve_src`
- LEGALM reference: `$legalm_guidance`
- OpenROAD DPL default-flow reference: `$openroad_diamond`
- negotiation donor: `$openroad_negotiation_nblg`
- Abacus donor: `$dreamplace_abacus`
- case feature route insight packet: `$case_feature_route_insight_packet`
- secondary inspiration only, not direct route maps:
  - dense legalizer-producer note: `$dense_legalizer_routing_insight`
  - feature-to-mechanism strategy note: `$strategy_inspiration_insight`
- DPO mechanism insight: `$dpo_source_mechanisms_insight`
- OpenROAD-native handoff insight: `$openroad_native_handoff_insight`
- LEGO-lite knowledge indexes: `$skill_index`
- LEGO-lite skill query helper: `$skill_query_script`
- LEGO-lite skill notes: `$skill_cards_dir`
- algorithm/pseudocode cards: `$algorithms_dir`
- Teacher coaching skill: `$teacher_coaching_skill`

Use these paths as search handles only when they help identify concrete
function/state insertion points. Do not require broad donor-tree reading.

The case feature route insight packet is evidence plus a compact route-insight
index generated from `case_feature_to_mechanism_route_map.md` and
`mechanism_reconstruction_roadmap.md`. It is not a Python-selected route.
Teacher must classify the current case from evidence, then use the packet to
choose and justify concrete route hypotheses.

## First-Round Evidence-Driven Route Scan

In the first Teacher planning round, build the route map from observed case
evidence first. Most knowledge cards are vocabulary, inspiration, and prior
evidence. The case feature route insight packet is the compact entry point for
case-type method insights, but it does not filter or rank routes. Use it to
compare the observed case against the route signatures, then choose one or more
mechanism hypotheses and implementation roadmap sections.

- Read the design characteristics and stage metrics first.
- First reaction: decide whether this case matches one or more known case types
  with verified mechanism chains. Base that classification on concrete case
  features: size/runtime scale, density/utilization, legalization HPWL movement,
  DPO/improve-placement recovery, true macro/SRAM/large-blockage/hierarchy or
  row-fragmentation evidence, and live source-level counters. Fixed instance
  count alone is not macro evidence; macro means a physically large instance.
  If yes, assign Students across the matching packets or compatible
  combinations, then use the remaining Students for complementary or
  exploratory routes.
- After naming the dominant bottleneck, query the skill index only to sharpen
  the selected route, check a known failure mode, or diagnose low ROI. Do not
  query broad secondary cards to replace evidence-driven route selection.
- When selecting a feature-matched start kind or route, open the case feature
  route insight packet first. If a route signature matches current evidence,
  assign at least one Student to reconstruct or strengthen the full route with
  the included implementation order, source handles, liveness counters, and
  partial-implementation failure tests; use strategy cards and algorithm cards
  only to refine the mechanism or check risk.
- Use the route packet/roadmap for the detailed case-type blueprints. Match
  current features to complete producer -> handoff -> exact-consumer ->
  post-consumer chains, not to benchmark names or single late patches. Typical
  matches include row-rich recoverable basins, low-util Diamond-stable residue,
  dense recoverable pressure, macro/fragmentation pressure, and high-utilization
  legalizer inflation with incomplete DPO recovery.
- For any matched strong route, assign at least one Student to the cumulative
  chain from the packet. Do not treat final preservation, telemetry,
  attribution, native frontier records, or mirror-only work as the quality
  mechanism unless it protects a named accepted producer/DPO/reorder gain.
- Open the smallest useful algorithm/pseudocode card only when it provides a
  concrete mechanism, liveness counter, or failure mode relevant to the observed
  bottleneck.
- Classify donors as either `general donor` or `feature-matched donor`.
  General donors are safe regression guards or broadly useful kernels.
  Feature-matched donors are hypotheses tied to design features such as dense
  row pressure, low-util slack, fragmented rows, macro/edge pressure, high
  conflict pressure, or weak DPO recoverability.
- Assign the first-round Students across distinct route families. Do not put
  every Student on the current start seed or the same donor-polish path unless
  the evidence leaves no other credible mechanism.
- The objective is extreme per-case HPWL optimization under the runtime gate.
  Universal transferability is useful evidence, but it is not required for a
  case-specific winner.

## LEGO-lite Skill Lookup

- Use the skill index only after stage metrics/logs identify a bottleneck:
  `"$DPL_EVOLVE_PYTHON" $skill_query_script --stage <stage> --q "<bottleneck or mechanism>"`.
  In a generated Student workspace, prefer the local `08_query_knowledge.sh`
  helper because it already carries the resolved runtime paths.
  Stage names include `case_diagnosis`, `start_selection`, `legalization`,
  `improve_placement`, `handoff`, `build_eval_log_diagnosis`, and
  `teacher_review`.
- A selected skill is guidance, not a cage. Keep selection small: normally one
  primary mechanism skill, one auxiliary handoff/objective skill, and one
  optional diagnosis skill per Student route.
- Strategy and algorithm cards are inspiration/risk checks. Translate them into
  staged source mechanisms, liveness counters, and failure tests driven by
  current evidence.

## Objective And Boundaries

- Promote only legal final `metrics.json:hpwl`; `hpwl_proxy` is debug-only.
  Report `HPWLg`, `delta HPWLlg`, `delta HPWLip`, `delta HPWL final`, and final
  HPWL when available.
- `G_HR` is analysis-only for runtime value. A faster but worse-final-HPWL
  candidate is not the elite unless lower-HPWL candidates fail legality,
  artifacts, or hard timeout.
- Treat 5% clean final HPWL reduction versus OpenROAD default as a search-scale
  prior, not a promotion gate. Tiny/plateaued/over-cost results should trigger
  mechanism-level routes, not preservation-only polish.
- Search variables include start branch, mechanism order, guidance objectives,
  heuristic weights, stop schedules, internal parameters, handoff payloads, DPO
  scoring, and accept/rollback policies. Implement parameter changes inside
  source; do not move tuning to Tcl, ORFS, evaluator scripts, or repeated
  external endpoint invocation.
- Useful candidates are staged source mechanisms whose legal output is
  DPO-recoverable. Hybrid means a coherent source flow, not a selector among
  complete algorithms.
- Callable flow stays `detailed_placement_evolve -> improve_placement_evolve ->
  optimize_mirroring_evolve`. External evaluator/ORFS/workspace helpers and
  top-level command interfaces are locked. Mirror internals are support only
  when Teacher names a concrete accepted DPO/reorder/handoff gain to preserve.

## Runtime And Performance

- report runtime for the full evaluated flow above; do not call it pure
  algorithm-internal time
- the generated runner kills only the OpenROAD flow at
  `${student_runtime_multiplier}x` the canonical `openroad_dpl_flow`
  `metrics.json:runtime_seconds`; metrics/evaluator collection is not
  timeout-wrapped
- runtime is a `${student_runtime_multiplier}x` hard budget and constrained
  cost, not the objective. Runtime optimization is useful because it frees
  budget for more complex quality search, not because runtime itself is the
  target. Extra runtime is justified for bounded stronger mechanisms: deeper
  legalizer scoring, broader capped candidates, donor hybridization, parallel
  candidate evaluation, or better legalizer/improve handoff. Do not waste it on
  repeated unchanged passes.
- `bounded` means complexity-controlled and measurable, not small-scope; use
  caps, caches, thread-local scoring, and deterministic reductions to make
  larger searches practical
- runtime tiers: `fast` preserves a donor, `explore` allows moderate bounded
  cost, and `aggressive` allows larger timeout-safe cost only for a mechanism
  that could materially change final HPWL
- larger scans, repeated endpoint-like subpasses, and randomized perturbations
  are allowed only as controlled source mechanisms with scope, counters,
  early-stop or gain-rate evidence, and full-flow metrics; prefer
  handoff/frontier-guided targeting over blind random search
- high-performance exploration may use OpenMP, thread-local scratch buffers,
  cached touched data, and deterministic reductions for independent probes

## Donors And Starts

- compare `openroad_dpl_flow`, `openroad_dpl_negotiation`, and
  `evolve_default` together; all three are legitimate evidence and
  donor/start-point choices
- donor taxonomy: distinguish `general donor` from `feature-matched donor` in
  Teacher output. A general donor is reusable across many cases or works as a
  rollback fallback. A feature-matched donor is plausible because the current
  design has matching stage/physical features; it can be aggressively optimized
  for this case even if it may not transfer universally.
- prepared start branches include `framework`, `diamond`, and
  `default_negotiation`
- `framework` contains the current LEGALM-style producer/frontier base plus
  bounded DPO handoff support. Treat it as a normal editable source parent, not
  as proof that legalization-only changes are sufficient.
- Student private source trees use materialized seed sources and prepared git
  start branches. Student start-point switches are branch-only.

## DPO / Improve-Placement Mechanisms

- Improve-placement work should be implemented in the current source, especially
  `Optdp`/`Detailed*` candidate generation, objective, acceptance, transaction,
  local state, and handoff consumption.
- Useful mechanisms include source-edge cached scoring, bounded top-K exact
  candidate evaluation, transaction accept/rollback, bounded staged descent,
  and scoped LSMC basin escape.
- Scoped LSMC and staged descent can find better HPWL basins but are heavy. Use
  compact candidate sets, exact affected-net scoring, cached touched data,
  producer/consumer counters, and runtime/gain telemetry.
- Handoff state should stay OpenROAD-native and compact. Use
  `odb::dbInst*`, `odb::dbNet*`, `odb::dbGroup*`, `odb::dbRegion*`, dense ids,
  vectors, bitsets, and current detailed-placement mappings. Do not duplicate
  current coordinates or route hot state through files, logs, JSON, string-keyed
  hot maps, or public Tcl/SWIG ABI changes.

## Execution Context

- ordinary Student skills: `skills/patch_rules.md`,
  `skills/source_git_workflow.md`, `skills/build_openroad.md`,
  `skills/evaluate_run.md`, and `skills/trace_logging.md` when counters/logs
  are needed
- algorithm cards under `$algorithms_dir` provide pseudocode and liveness
  contracts for the selected mechanism family; Students should read only the
  relevant card after Teacher names the stage/mechanism
- generated Student workspace scripts own prepare/build/evaluate/commit/diff
  commands; Teacher should not ask Students to reconstruct CMake, relink,
  evaluator, git, or diff commands
- this packet is self-contained. Do not open `~/.codex/memories`, rollout
  summaries, child event logs, or session archives during planning. Use this
  packet, listed knowledge/insight files, peer-learning packet, canonical
  metrics, and targeted source handles.

## Design Characteristics

$design_characteristics

Detailed packet: `$design_characteristics_path`

## Baseline Evidence

$metric_table

## Start Seed Calibration

- start-kind donor calibration packet: `$start_seed_calibration_path`
- If this packet contains rows, Teacher should use it as initial source-level
  donor evidence before assigning Student routes.  Compare `framework`,
  `diamond`, and `default_negotiation` stage-wise
  results, but do not treat the best initial seed as the only exploration
  parent.  A stage donor can still be useful when its final HPWL is not best.

## Frozen Level 1 Evidence

- frozen method/source-start packet: `$level1_evidence_path`
- Read this packet before assigning first-iteration target routes. Treat it as
  immutable calibration-time prior evidence: adapt it to this target, but do
  not edit it or promote a calibration candidate without a target evaluation.

## Scoreboard

$scoreboard

## Elite Continuation

$elite_text

## Packets

- baseline artifacts: `$baseline_artifacts_path`
- start seed calibration: `$start_seed_calibration_path`
- frozen Level 1 evidence: `$level1_evidence_path`
- peer learning: `$peer_learning_path`

## Prior Context

$prior_context

## Teacher Boundary

- output insight and per-student plans only
- do not ask students to chase optional reports that may not exist
- do not include memory citations, shell/build/eval/git command blocks, or long
  file inventories; the Student workspace packet owns execution procedure
