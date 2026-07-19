# Teacher Review Follow-Up Rules

Use prior Teacher memory; review only current artifacts needed for the next
route. Keep the review concise and action-bearing.

- Validity gate: build/evaluator/metrics/diff/message/legality/artifact source
  must be clean before a candidate can be promoted.
- Rank by legal final `metrics.json:hpwl`; use `G_HR` only to explain runtime
  value or runtime-repair priority.
- When reading `candidate_metrics_summary_md`, use `Headline Versus OpenROAD
  Default` for OpenROAD-default comparison and 5% ambition tracking. Use
  `Stage Metrics` only to explain producer/consumer attribution, not to decide
  whether a candidate beat OpenROAD default.
- Runtime below the hard budget is not a reason by itself to add work. If a
  candidate is legal but HPWL-poor and baseline-fast, identify the missing HPWL
  source first; route stronger bounded search only when it changes the
  mechanism rather than rewarding runtime use.
- Identify the real mechanism and whether counters/logs prove it executed. Zero
  counters or stale source/binary/metrics means non-executed/negative evidence.
- Treat nonzero counters, accept counts, and large accepted-delta as liveness
  proof only. If legal final HPWL barely moves versus OpenROAD default/current
  elite or remains far from the 5% ambition, classify the route as
  final-effect insufficient until the review names the missing producer,
  handoff, consumer, acceptance, or post-consumer link.
- Use each knowledge card as the source-to-evidence map. A useful card should
  name changed files/functions, route action, executed counters, stage movement,
  and `Next Teacher Handoff`; vague cards should trigger tighter next-packet
  requirements.
- Read order for mechanism reasoning is the artifact packet first, then each
  relevant `knowledge_card`, then `source_trials.jsonl` or diff/logs only if a
  card is vague, inconsistent, or marks a repairable rejected ref.
- For any weak, flat, over-cost, or surprising result, verify the Student's
  explanation against at least one concrete source artifact and one concrete
  metrics/log artifact before planning the next route. Use the diff or changed
  files to confirm the intended mechanism exists, and use candidate metrics or
  DPL counters to confirm whether producer, handoff, consumer, acceptance, and
  post-consumer links executed. Do not let a plausible narrative substitute for
  source/log evidence.
- The review must convert weak evidence into an actionable failure bucket:
  non-executed/stale implementation, producer payload quality, handoff lifetime
  loss, DPO consumer zero/low accepts, acceptance/scoring too strict or too
  local, accepted-delta washout, post-consumer preservation loss, runtime/cap
  bottleneck, or wrong case-type/start-basin route. The next packet set must
  contain at least one route that directly attacks the named bucket.
- When a candidate is weak, rejected, or repairable, read `## Next Teacher
  Handoff` in the Student `knowledge_card` as its bounded self-diagnosis before
  assigning the next packet; verify it against metrics, refs, counters, and
  diff.
- Treat `reject` as not-final-for-this-trial, not automatic method death. Use
  source diff, counters, logs, and stage movement to decide implementation bug,
  handoff/composition issue, method-low-ROI, or hard non-promotable failure.
- Classify each useful result as `final donor`, `stage donor`, `active
  mechanism workbench`, `validated-low-ROI`, `non-executed route`, or `negative
  evidence`.
- When a result is useful, also record whether it is a `general donor` or a
  `feature-matched donor` for this case. Use that scope when deciding whether
  to transfer it or aggressively specialize it.
- If a verified mechanism gives extremely small full-flow HPWL movement after
  a meaningful repair attempt, mark `validated-low-ROI` and stop assigning
  repeated tie-break/threshold/local-polish variants as primary routes.
- If the round is dominated by same-family constants, tie-breaks, telemetry, or
  small candidate-order changes, classify it as incremental exploitation. The
  next packets should include `switch`, `hybridize`, or `mechanism-redesign`,
  not another small donor polish.
- If current-run legal candidates are tiny-gain, same-family, or too heavy for
  the HPWL gained, treat it as a weak-gain/over-cost review signal. The next
  packets should preserve useful donor refs for rollback, but route the best
  line as elite expansion rather than preservation-only work. Include aggressive
  within-run routes: switch family, redesign producer/consumer composition, or
  repair a current-run kept/rejected ref with `25_trial_source.sh begin
  --from-ref <ref>`.
- If a Student completed one self-diagnosis/strengthening loop and left a
  concrete `Next Teacher Handoff`, continue that workbench only with a new
  source hypothesis; otherwise switch family or redesign composition.
- Preserve stage donors when downstream erases a gain; route the next Student to
  handoff/consumer repair rather than discarding the stage mechanism.
- If a Student ended on a rejected or weak workbench, use `source_trials.jsonl`
  refs as evidence for useful rejected or kept code. If repairable, tell the
  next Student to use `25_trial_source.sh begin --from-ref <ref>`.
- When assigning a branch/ref switch, name the current-run source ref, new trial
  label, and preservation expectation.
- Before next routing, combine current metrics, logs/counters, source behavior,
  and Student knowledge-card reasoning. Use a knowledge/algorithm handle only
  as inspiration or a risk check when it helps; when changing route family,
  name the evidence-backed mechanism insight being tested.
- For blueprint or stack continuation routes, query or cite the exact stack
  record and classify the current implementation against its producer, handoff,
  consumer, and post-consumer roles. Route the next Student to the first missing
  role or to a `compatible_with` follow-up justified by current evidence.
- Next packets must be non-accumulative: one distinct HPWL-source hypothesis,
  concise dominant-mechanism plan, adjacent-stage compatibility proof, exact
  function/state handles, runtime tier, liveness counters, and the low-ROI stop
  condition when relevant. Do not require cosmetic edits in every stage.
- If the current best is continued, its route action should be `elite-expand`
  and it must name a stronger HPWL mechanism on top of the donor, not only
  fallback preservation.
- A best-so-far result that only weakly improves over OpenROAD default/current
  elite or is still far from the 5% search ambition is not a completion signal.
  Preserve its source ref, but review it as a platform for the next stronger
  quality mechanism: deeper producer/consumer composition, explicit handoff,
  wider bounded exact search, or a redesigned legalizer/DPO interaction.
- If the next packet set would only polish the same donor family while still
  behind baseline, add one broader new-family route from the observed failure
  mode. The first-round route map or an algorithm card may inspire it, but
  should not be the reason by itself.
- If a legal donor has already received one meaningful repair/continuation and
  still produces only weak final-HPWL movement, or spends more runtime without
  proportional final gain, the review must force at least one next route to
  rebuild the complete strong chain or switch to an alternate start basin.  The
  review should name the missing link: producer/legalizer basin, explicit
  component/window/frontier payload, payload lifetime through handoff, DPO
  consumer, larger-grain exact transaction, exact acceptance/rollback,
  post-consumer preservation, or runtime bottleneck.  Judge the repair by final
  HPWL and `accepted_delta_per_runtime`, not only `HPWLlg`, accept count, or raw
  accepted delta.
- Do not let the current best become a frozen guard.  If it remains in the
  roster, review it as `elite-expand` and require a material HPWL mechanism on
  top of it.  Multiple Students may target the same promising mechanism family
  when their source hypotheses differ.
- For any route assigned from Blueprint A/B/C/D or D+A, classify the implementation
  against the selected roadmap checklist as `full-chain`, `partial-chain`, or
  `non-live`.  A weak partial-chain result should route the next packet to the
  first missing producer/handoff/consumer/post-consumer link or an alternate
  start basin, not to another guard or same-link retune.
