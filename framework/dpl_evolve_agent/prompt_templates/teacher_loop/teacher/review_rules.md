## Evidence Discipline

- Read the artifact packet once first; it already contains validity, metrics, artifact sizes, and exact paths.
- Prefer each Student's `knowledge_card` for implemented mechanism, bottleneck
  diagnosis, liveness evidence, and lesson. Use final messages for compact
  summary only.
- When a candidate is weak, rejected, or repairable, read `## Next Teacher
  Handoff` in the Student `knowledge_card` before assigning the next packet.
  Treat it as the Student's bounded self-diagnosis, then verify it against
  metrics, source refs, counters, and diff.
- Artifact read order: `teacher_review_artifacts.md` -> per-student
  `knowledge_card` -> `source_trials.jsonl` for kept/rejected refs ->
  `implementation_diff`/logs only when the card or validity gate is unclear.
- Do not rerun or reread artifacts broadly unless a validity conflict needs it. Repeated artifact reads without a mechanism decision are review inefficiency.
- Use Student log/efficiency diagnosis when present; if missing, infer only the minimum from `metrics.json`, DPL logs, counters, and artifact state.

## Validity Gate

- A candidate is not promotable if build/evaluator failed, metrics are missing, diff is missing/empty, agent message is missing, legality failed, source/binary/metrics are stale, or the DPL flow timed out.
- Rank promotable candidates by legal final `metrics.json:hpwl`. Ignore bbox/proxy HPWL for promotion.
- When reading `candidate_metrics_summary_md`, use `Headline Versus OpenROAD
  Default` for OpenROAD-default comparison and 5% ambition tracking. Treat
  `Stage Metrics` / `flow_internal_delta_*` as attribution only; a large
  `HPWLg -> HPWLfinal` delta is not evidence that the candidate beat the
  OpenROAD default final HPWL.
- Review with a high-ambition prior: a case that remains far below 5% final
  HPWL reduction is not solved. Preserve useful donors, but the next plan must
  include aggressive mechanism-level attempts unless evidence proves the search
  surface is exhausted.
- If the visible early iterations are weak or remain far below the matched
  case-type potential, review this as a search-loop problem unless a strong
  mechanism route is still executing. Diagnose the missing HPWL source before
  writing the next plan: producer/legalizer, handoff/frontier, DPO
  move/scoring/acceptance, post-consumer polish, runtime bottleneck, or
  incomplete chain reconstruction.
- Use `G_HR = 100 * (HPWL_ref - HPWL_sol) / HPWL_ref - P(runtime_sol / runtime_ref), where P(r)=0 for r <= 1.10 and P(2.0)=1.0 percentage point` only to explain runtime value and repair priority. A faster worse-HPWL candidate is not the headline elite unless lower-HPWL candidates fail validity gates.
- Treat runtime and displacement as diagnostics: controlled cost, under-exploration, fixable implementation cost, or unsupported work. Do not over-reward baseline-like runtime that avoids a needed HPWL mechanism; when a legal candidate stays far under budget and HPWL is weak, first identify the missing HPWL source, then route a stronger bounded mechanism if justified.

## Review Focus

- Identify the real executed mechanism, not only the claimed mechanism. Zero counters or stale artifacts mean non-executed/negative evidence even if inherited code produced acceptable metrics.
- Treat nonzero counters, accept counts, and large accepted-delta as liveness
  proof only. If legal final HPWL barely moves versus OpenROAD default/current
  elite or remains far from the 5% ambition, classify the route as
  final-effect insufficient until the review names the missing producer,
  handoff, consumer, acceptance, or post-consumer link.
- Use the knowledge card as a source-to-evidence map. Prefer cards that name
  changed files/functions, route action, executed counters, stage movement, and
  `Next Teacher Handoff`; treat vague cards as incomplete review evidence even
  when the metrics are valid.
- If a Student spends substantial time in diagnosis without reaching a first patch/build/eval loop, review that as execution inefficiency and route the next packet toward a faster concrete implementation path.
- Treat `reject` as "not the final candidate for this trial", not as automatic method death. Inspect the source diff, counters, stage movement, and logs before deciding whether the failure is implementation, handoff/composition, or method-level.
- Attribute stage movement across legalization, improve placement/DPO, handoff,
  final mirror contribution, runtime, and counters. Treat mirror contribution
  as diagnostic/support evidence; do not promote mirror-only preservation as a
  standalone optimization mechanism.
- For every promising or weak candidate, answer: did final HPWL movement come
  from the mechanism the Student intended? If not, classify whether the claimed
  mechanism was non-live, only stage-local, erased downstream, too shallow, or
  missing a chain link. Use this answer to route the next mechanism, not only
  to rank candidates.
- Classify each result as `final donor`, `stage donor`, `active mechanism workbench`, `validated-low-ROI`, `non-executed route`, or `negative evidence`.
- Also classify donor scope when useful: `general donor` if it is a reusable
  fallback/kernel, or `feature-matched donor` if it appears useful because this
  design has matching density, row-fragmentation, conflict, macro-edge, or DPO
  recoverability features.
- Preserve stage donors when downstream erases a gain; the next route should repair the consumer/handoff rather than discard the producer mechanism.
- Preserve an active workbench when counters are nonzero, legality is clean, runtime is bounded, and either stage-local HPWL improves or the final erase is diagnosable.
- If the Student ended on a rejected or weak workbench, check
  `source_trials.jsonl` for rejected or kept refs and inspect whether the code
  is repairable evidence rather than method death. If repairable, route the
  next Student to start from it with `25_trial_source.sh begin --from-ref
  <ref>`.
- Use this rejection taxonomy: implementation-bug means the idea may be sound but source/counters/logs show it did not execute correctly; composition/handoff issue means stage-local gain was erased downstream; method-low-ROI means the mechanism is live, legal, bounded, and still cannot move HPWL after a meaningful repair; non-promotable means build/eval/legality/timeout/stale-artifact failure.
- If a live/correct mechanism still gives extremely small full-flow HPWL movement after a meaningful repair attempt, classify it as `validated-low-ROI` and stop assigning repeated variants of the same idea as primary routes.
- If multiple candidates only extend one donor family with constants,
  tie-breaks, telemetry, or small candidate-order changes, mark the round as
  same-family incremental exploitation and require the next plan to include a
  `switch`, `hybridize`, or `mechanism-redesign` route.
- If multiple Students have identical stage-wise tuples or fall back to the
  same start-basin HPWL, treat that as evidence the assigned variants did not
  change the reachable solution space. Do not route another main plan to
  frontier retune, fallback parity, or liveness-only edits; require a mechanism
  that changes producer policy, consumer acceptance/move family, or their
  composition.
- Separate algorithmic failure from implementation cost: timeout, broad untargeted scans, low accept rate, legality rejects, fallback domination, duplicate endpoint work, weak cache reuse, or missing parallelism.
- If current-run legal candidates are tiny-gain, same-family, or too heavy for
  the HPWL gained, diagnose whether the issue is implementation cost,
  producer/consumer composition, or a weak mechanism. The next plan should
  preserve useful donor refs for rollback and include more aggressive
  within-run routes: elite-expand the current best with a stronger mechanism,
  switch family, redesign the mechanism, or repair a current-run kept/rejected
  ref with `25_trial_source.sh begin --from-ref <ref>`.
- If a candidate only changes mirror/orientation behavior, classify it as
  support evidence unless it preserves a named accepted DPO/reorder/handoff
  mechanism and final HPWL improves. Next plans should not allocate a primary
  route to mirror-only preservation; pair mirror work with an upstream producer or DPO
  consumer, or drop it.
- The best route should be reviewed as elite expansion, not preservation-only work.
  Preserving the donor/ref is fallback discipline only. If the best-line work
  does not attempt a stronger HPWL mechanism, classify it as under-explored
  even if it preserves quality. Efficiency work is useful only when it frees
  budget for a stronger search on the same donor path. If repeated increments
  stay flat or only change a tiny cap, threshold, telemetry, or parity fastpath
  while runtime headroom remains, route next to a deeper donor-on-top quality
  branch or a different producer/handoff/consumer family.
- When current-case mechanisms have nonzero counters and improve final HPWL,
  route follow-up toward compatible stacking or runtime repair of those
  mechanisms before assigning more same-basin threshold variants. Stacking must
  be a coherent producer/handoff/consumer source flow, not a selector among
  complete algorithms. If a mechanism-stack record exists for the route, use its
  `compatible_with`, `failure_buckets`, and handoff payload fields to name the
  next link.
- If a Student completed one self-diagnosis/strengthening loop and left a
  concrete `Next Teacher Handoff`, decide whether to continue that workbench,
  switch family, or redesign composition. Do not ask the same Student line to
  repeat the identical tiny repair without a new source hypothesis.
- For a live but tiny-gain mechanism, check chain completeness. If it only
  reproduces part of a stronger producer-handoff-consumer pattern, name the
  missing producer, handoff, move family, acceptance rule, or post-consumer
  polish instead of declaring the family exhausted.
- If the route was assigned from Blueprint A/B/C/D or D+A, compare the implemented
  code and counters against the selected roadmap checklist. Classify it as
  `full-chain`, `partial-chain`, or `non-live`. A partial-chain result should
  route the next packet to the first missing producer/handoff/consumer/
  post-consumer link or to an alternate start basin, not to another guard or
  same-link retune.
- When review sees an incomplete strong chain, the next plan should repair or
  complete the missing link before abandoning the case-type route. Examples:
  legalizer created a frontier but DPO did not consume it; DPO probed but
  accepted zero moves; exact accepts happened but post-consumer polish erased or
  failed to preserve gains; runtime caps stopped before the quality source was
  reached.
- When runtime grows but final HPWL is flat, do not treat raw accept count or
  large accepted delta as sufficient.  Review whether the accepted delta was
  useful per unit runtime and preserved to final flow.  The next plan should
  include `accepted_delta_per_runtime` or an equivalent pass-local
  gain-per-second diagnosis, plus at least one route that rebuilds the complete
  producer/payload-handoff/larger-grain-exact-consumer chain.
- When a next route switches parent/ref, name the current-run source ref, new
  trial label, and preservation expectation.

## Next-Plan Requirements

- Next packets should be evidence-driven, not quota-driven. Do not force a fixed number of legalizer, DPO, or hybrid routes; choose routes from the current largest HPWL source and mechanism uncertainty.
- Before assigning next packets, synthesize canonical stage metrics,
  logs/counters, source behavior, and Student knowledge-card reasoning. Use a
  knowledge/algorithm handle only as inspiration or a risk check when it helps
  explain the route; if source evidence is sufficient, say `none needed`.
- Each next route needs one primary HPWL-source hypothesis, a minimal first-patch landing zone, concise dominant-mechanism plan, adjacent-stage compatibility proof, runtime tier, liveness counters, expected metric proof, and a stop/pivot rule. Usually give 1-3 primary handles and at most 2 support handles.
- For each next route, include `expected HPWL source`, `mechanism strength`,
  and `stage-wise proof target`. The proof target must be concrete enough that
  the next review can tell whether the route reached the intended source of
  quality improvement.
- For weak, flat, over-cost, or surprising results, verify the Student's
  explanation against concrete source changes and concrete metrics/log evidence
  before planning the next route. Convert the result into a failure bucket:
  non-executed/stale implementation, producer payload quality, handoff lifetime
  loss, DPO consumer zero/low accepts, acceptance/scoring too strict or too
  local, accepted-delta washout, post-consumer preservation loss, runtime/cap
  bottleneck, or wrong case-type/start-basin route. At least one next route
  should directly repair or refute the named bucket.
- Review whether the route mix is too narrow. If all next routes would polish
  one donor family while the case still trails baseline, require one broader
  new-family route justified by the observed failure mode. Knowledge cards may
  inspire that route, but evidence must be the reason.
- Adjacent-stage notes can include real source changes or explicit preservation/diagnosis. Do not require cosmetic edits in a stage just to fill the plan.
- Emphasis labels (`legalizer-emphasis`, `DPO-emphasis`, `co-optimization-emphasis`) are priorities, not exclusive permissions. Students may patch multiple stages when a coherent source mechanism needs producer/consumer support.
- Parameter tuning is valid only as source-internal mechanism work: constants, thresholds, scoring weights, pass schedules, adaptive policies, or compact internal config structs in `dpl_evolve`. Reject Tcl/flow-script knob sweeps.
- Hybridize only as staged source-level mechanism composition inside one candidate implementation. Do not implement case/scenario selectors among complete legalizers or fallback-to-default success paths.
- When feature-level insight suggests a strategy, translate it into source mechanisms and liveness counters. The feature card is inspiration, not a fixed route table.
- A case may match multiple feature-level insights. Review should decide which
  packets to test, combine, continue, or drop based on same-case evidence.
- Broader-search plans should state bounded scope, runtime tier, counters, and
  how cost is controlled. Candidate/window bounds, cache/reuse, affected-net
  deltas, parallel scoring, and deterministic reductions are examples, not an
  allowed-list. Extra runtime is acceptable only as controlled cost for a
  stronger identified HPWL mechanism.
- Weak-gain or over-cost plans should explicitly name the stronger mechanism
  family or repaired current-run ref and why it is different from donor
  polishing.
- Under-exploration is a review failure mode: if candidates are fast and weak,
  require one controlled-runtime route against a materially larger HPWL source,
  not another preservation-only or same-family threshold variant.
- Keep next packets concise and non-accumulative. Replace stale guidance with the current mechanism decision.
