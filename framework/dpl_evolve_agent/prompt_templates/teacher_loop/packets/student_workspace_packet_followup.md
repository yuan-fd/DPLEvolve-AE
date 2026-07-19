# Generated Workspace Packet: $student_id Follow-Up

This packet gives exact paths and helper scripts for this iteration. Use prior
session memory for global rules; open only the files needed for the current
mechanism.

## Identity

- student_id: `$student_id`
- iteration: `$iteration_name`
- case: `$case_id`
- flow_variant: `$flow_variant`
- run_tag: `$run_tag`
- source_branch: `$source_branch`
- stable_candidate_ref: `$source_candidate_ref`

## Source And Artifacts

- private_dpl_evolve_source: `$dpl_src`
- source_layout: use module-relative `src/...`, `src/optimization/...`, and
  `src/objective/...`; do not prepend `tools/OpenROAD/src/dpl_evolve/`
- private_binary: `$private_binary`
- metrics: `$metrics_path`
- artifact_dir: `$artifact_dir`
- candidate_metrics_summary_json: `$candidate_metrics_summary_json`
- candidate_metrics_summary_md: `$candidate_metrics_summary_md`
- implementation_diff: `$implementation_diff`
- knowledge_card: `$knowledge_card`
- source_commit_record: `$source_commit_record`
- peer_learning_packet: `$peer_learning_path`
- Teacher output: `$teacher_plan_last_message`

## Source Reading Boundary

- Implement from the Teacher route, selected knowledge/roadmap section, and the
  current private source tree.  Do not search or copy old experiment source
  implementations from `$HOME/local_backups`, `local_backups`, or historical
  `DPL_EVOLVE_STATE_ROOT` round directories.
- Allowed reads are: this generated round's packet/prompts/artifacts/helpers,
  `private_dpl_evolve_source`, current prepared start branches inside that source
  repo, explicitly fetched peer refs via the helper, `agent_root/knowledge`,
  `agent_root/knowledge/index`, and current OpenROAD/DPL source needed for API or
  call-site understanding.
- `DPL_EVOLVE_STATE_ROOT` is allowed only for this round's generated workspace
  files and prepared seed sources used by helper scripts.  It is not a general
  search space for old implementations.

## Helpers

Primary flow:

1. prepare source or assigned start:
   - `$prepare_source_script`
   - `$prepare_start_source_script --kind <framework|diamond|default_negotiation>`
2. for each mechanism trial:
   - after source preparation, branch switch, or `begin --from-ref`, run status
     and inspect Teacher first-patch handles on the checked-out branch before
     editing
   - write a pre-edit source map in your notes/report: entry point, call path,
     state/payload lifetime, candidate/scoring/acceptance path, and proof
     counter/log
   - `$trial_source_script begin --label <short-mechanism>`
   - repair a prior kept/rejected ref when assigned: `$trial_source_script begin --label <repair-name> --from-ref <ref>`
   - start the first real trial as soon as the current mechanism path is clear; do not spend long in pre-edit diagnosis
   - before editing, state the HPWL-source hypothesis: expected producer, handoff, DPO consumer, post-consumer, or composition gain; why the mechanism is strong enough; and what stage/counter proof should change
   - also state the failure bucket being tested or repaired: non-executed/stale code, producer payload, handoff lifetime, DPO consumer zero/low accepts, acceptance/scoring too strict/local, accepted-delta washout, post-consumer preservation, runtime/cap bottleneck, or wrong start/case-type route
   - edit source, then run `$build_variant_script` and `$evaluate_candidate_script`
   - read `$candidate_metrics_summary_md`; self-diagnose whether HPWL movement came from the intended source, using actual DPL log counters when the summary cannot prove liveness, accept/reject behavior, stop reason, or mechanism-local runtime
   - if final HPWL is flat/weak/worse, classify the failure bucket from changed source plus metrics/log counters before deciding keep/reject
   - if weak with runtime headroom, make one strengthened modification toward the missing quality source, then rerun `$build_variant_script` and `$evaluate_candidate_script`
   - if the strengthened result still needs follow-up, write `## Next Teacher Handoff` in `$knowledge_card`
   - decide keep/reject only after the strengthened evaluation, unless the first result proves no plausible repair exists
   - rejected path: `$trial_source_script reject --reason <why-rejected>`
   - retained path: `$trial_source_script keep --reason <why-kept>`
3. close a legal retained result only after self-diagnosis plus strengthened evaluation:
   - `$keep_and_finalize_script --reason <why-kept>`
4. final retained source: `$finalize_source_script`

Optional helpers:

- status/refs: `$source_status_script`
- peer mechanism diff only when Teacher assigns a peer Student mechanism or a
  concrete donor comparison is needed:
  `$fetch_peer_source_script --peer-repo <repo> --peer-ref <ref> --label <name>`
- switch prepared start after prepare: `$switch_start_branch_script --kind <kind>`
- source context: `$source_context_script <src/...> [symbol]`
- knowledge query, only when Teacher names a card/skill, the mechanism role is
  unclear, or metrics/logs suggest a pivot:
  `$query_knowledge_script --stage <stage> --q <text>`
- for an assigned case-type Blueprint A/B/C/D or D+A, query that exact stack
  record before editing:
  `$query_knowledge_script --stage teacher_review --q "Blueprint <letter|D+A> stack"`
- if the first result exposes a named failure bucket, query that bucket with the
  relevant stage, for example
  `$query_knowledge_script --stage handoff --q "handoff lifetime producer consumer"`
  or `$query_knowledge_script --stage improve_placement --q "accepted-delta washout exact consumer"`
- metrics summary on demand: `$report_candidate_metrics_script`
- trial state/status: `$trial_source_script status`
- fresh build only if normal build fails: `$fresh_build_script`
- one-shot after the strengthened legal kept result: `$keep_and_finalize_script --reason <why-kept>`
- one-shot after edits with no active trial: `$after_edit_script`

For Diamond/negotiation/LEGALM/Differential Guidance mixtures, use the
Teacher-assigned route insight or roadmap section first. Query
`--stage legalization --q "diamond negotiation differential guidance liveness"`
only to refine role/liveness details, then keep producer/consumer counters
aligned with the route role.
For plateau, weak-gain, or over-cost routes, query only the named diagnosis,
algorithm, or skill card that answers the current mechanism gap; convert it into
a patch target, liveness counter, and proof criterion before editing.

If Teacher assigns a case-type route, use these handles only for the assigned
mechanism section rather than broad reading:

- case feature route insight packet: `$optional_case_feature_route_insight_packet`
- mechanism reconstruction roadmap: `$optional_mechanism_reconstruction_roadmap`
- pre-edit complete-chain audit: start basin, producer, handoff, exact
  consumer, post-consumer closure, counters, already-live links, missing links,
  and the link this iteration implements or repairs

Run helpers that mutate the private source repo serially. Parallel reads are OK.
`fetch_peer_source_script` fetches a local `peer/<label>` ref and writes diff
artifacts; it does not modify your working tree. Summarize the peer idea before
porting it, and port the mechanism deliberately rather than auto-applying diffs.
`trial_source_script` is the safe checkpoint/keep/reject state machine; reject
preserves the rejected branch and leaves rejected code checked out on the dev
branch so the final diff still records this iteration's edits. Teacher can
later continue a repairable rejected line with `begin --from-ref`. Use the
explicit reset helper with `--ref` or `--kind` only when Teacher/Student decides
to change the dev starting point.

## Mechanism Contract

- Plan and implement the assigned mechanism hypothesis in
  `private_dpl_evolve_source`. Mechanism-level rewrites and direct supporting
  changes across legalization, DPO, and handoff are allowed when justified by
  Teacher guidance and source diagnosis. Keep the diff coherent, bounded,
  reviewable, and validated.
- Prepared starts are source-parent branches, not edit-scope restrictions. Do
  not manually apply release/start patch files.
- Use source-internal parameters/heuristics only; do not move tuning into Tcl,
  evaluator, ORFS, or baseline scripts.
- If a live/correct mechanism remains extremely low ROI after a meaningful
  repair attempt, record `validated-low-ROI`, preserve the rejected branch or
  clean checkpoint, and pivot to a different HPWL source.
- For the protected best-so-far source, preservation is fallback discipline
  only. Treat the trial as elite expansion: keep rollback available, but use
  the iteration to attack a larger final-HPWL source rather than only guarding
  or retuning the donor.
- Compatible mechanism stacking is allowed: keep a live donor mechanism in the
  same flow, add one new producer/handoff/consumer link aimed at a different
  HPWL bottleneck, and log counters for both links. Use the selected stack
  record's `compatible_with` field when available; otherwise write a short local
  compatibility proof before editing.
- When a trial is low-ROI after one meaningful repair, close it with
  `keep`/`reject`, then explicitly reset from a clean kept/candidate/start ref
  before trying a different HPWL source. Do not silently overwrite a failed
  mechanism.
- New algorithms or handoff paths should emit compact counters/logs for
  producer/consumer liveness, accept/reject behavior, HPWL movement, and
  pass/runtime cost.
- A failed first evaluation should trigger one coherent same-route repair when
  the failure looks like an implementation or integration bug. Reject when
  there is no plausible repair, repair fails, the route is non-executed, or
  HPWL regresses with unjustified runtime cost.
- Final evidence must align: source commit/ref, binary, metrics, diff, and
  knowledge card describe the same code.
- Fill the knowledge card as a source-to-evidence map: route action, changed
  files/functions, mechanism class, counters/logs, stage metrics, rejected
  refs, and `## Next Teacher Handoff` when follow-up is needed.
- The knowledge card should also state expected HPWL source, mechanism
  strength, stage-wise proof target, measured HPWL-source attribution, and
  whether the first result came from the intended mechanism.
- Resolve ordinary code uncertainty with targeted source inspection of nearby
  call sites inside the allowed source-reading boundary above. If still
  ambiguous, choose the simplest coherent implementation of the assigned
  mechanism; do not pause the iteration for ordinary uncertainty.
- Prefer patch/build/eval over repeated broad reading once the current
  mechanism is understood.
- Reuse Teacher `first patch handles` and the main workspace packet `Algorithm
  Entry Points` section as preferred landing zones, not whitelists. Keep
  Student-side research narrow to the current mechanism, nearby state, adjacent
  implementation support, and any required OpenROAD interface/call site.
- Do not modify the top-level command interfaces for
  `detailed_placement_evolve`, `improve_placement_evolve`, or
  `optimize_mirroring_evolve`. Keep command names and outer invocation
  contract stable; do not edit `src/Opendp.i` unless Teacher explicitly assigns
  command-interface work.
- After the first legal evaluator result, do not finalize immediately. Diagnose
  the bottleneck from metrics/logs/counters, including actual DPL log lines when
  needed to prove liveness, accept/reject causes, stop reason, or
  mechanism-local runtime. Make one strengthened modification and re-evaluate
  before keep/finalize. Close the active trial before broader donor reading; use
  broader knowledge only when current evidence shows the assigned hypothesis is
  low-ROI or wrong.

## Report Fields

Touched files; mechanism; stage-wise HPWL/final HPWL/runtime/legality; active
counters; metrics path; diff path; rejected refs if any; knowledge card; source
commit/ref; failure mode; next repair or pivot.
