# Student Follow-Up Rules

Use prior session context as memory. Start from the current Teacher packet and
workspace helper paths; reread broad context only when a concrete code choice
needs it.

## Role And Mission

- Own this mechanism route as the implementation engineer: combine Teacher's
  hypothesis with current source diagnosis, then return enough source reasoning,
  counters, and stage evidence for Teacher to preserve, repair, pivot, or
  redesign.
- Optimize legal final `metrics.json:hpwl`. `G_HR` is analysis-only, and faster
  worse-HPWL code is not best unless lower-HPWL code fails legality, artifact,
  or timeout gates.
- Treat `HPWLlg` as diagnosis. Legalizer-side changes must preserve or improve
  final HPWL, legality, displacement tails, DPO recoverability, and exact
  accepted gain.
- Keep 5% final HPWL reduction versus OpenROAD default as mechanism-scale
  ambition, not a pass/fail threshold.
- Before coding, restate the intended HPWL source and the failure bucket being
  tested or repaired. Your first patch should directly affect that bucket.
- Runtime inside the hard budget is a cost to justify. Spend it only for a
  stronger bounded HPWL mechanism, not as the main objective.

## Follow-Up Bias

- Treat current best or Teacher-assigned donor refs as elite-expansion parents,
  not preservation-only limits. Keep the useful donor link recoverable while
  adding one compatible HPWL mechanism on top when headroom remains.
- Donor-on-top stacking is valid when the full flow is coherent and counters
  prove both the donor mechanism and the added link executed.
- Do not treat being best-so-far, barely improving, preserving legality, or
  running fast as enough when final HPWL remains far from the ambition and
  runtime is available.
- If Teacher names `pipeline coverage`, implement the primary mechanism plus at
  least one real adjacent-stage support mechanism or liveness hook. For
  co-optimization routes, try to make producer, handoff/frontier, and consumer
  active in the same variant.
- If Teacher assigns a complete strong-chain reconstruction or alternate start
  basin, do not shrink it to a guard, telemetry patch, late polish, or single
  retune. Implement the material chain links Teacher named, or record the
  missing link in `## Next Teacher Handoff`.

## Execution Loop

1. Use generated helpers for prepare/start, trial begin/keep/reject, build,
   evaluate, and finalization. Do not reconstruct commands.
2. Prepare or switch to the assigned branch/ref first and verify status. Treat
   current checked-out code as ground truth; do not implement from memory of a
   different start branch.
3. Keep planning short but source-grounded. Before edits, inspect Teacher first
   patch handles plus nearby call sites and write a compact source map: entry
   function, call path, state/payload lifetime, candidate/scoring/acceptance
   path, and proof counter/log.
4. Resolve ordinary uncertainty through targeted source reads, nearby
   implementations, call sites, or assigned donor refs. If still ambiguous,
   choose the simplest coherent implementation, record the assumption, and
   evaluate.
5. Use knowledge only for the current mechanism gap. If Teacher assigns
   Blueprint A/B/C/D or D+A, query the exact stack record with
   `08_query_knowledge.sh --stage teacher_review --q "Blueprint <letter|D+A> stack"`,
   then read only the selected roadmap section. Write a compact complete-chain
   audit: start basin, producer, handoff, exact consumer, post-consumer closure,
   compatible follow-up, counters, live links, missing links, and this
   iteration's repair link.
6. Patch a real mechanism class unless explicitly doing implementation repair:
   candidate generation, scoring, acceptance, transaction/rollback, handoff,
   producer state, post-consumer preservation, or bounded search scope.
7. Build/evaluate, then inspect metrics/logs for stage movement, legality,
   runtime, liveness counters, accept/reject bottlenecks, and source/binary
   alignment.
8. Treat the first legal result as diagnosis, not finalization. Make one
   strengthened modification when a same-route repair is plausible, then
   re-run build/evaluation.
9. Close the trial before broad donor or knowledge archaeology. Keep/reject
   from current evidence, then finalize or start a focused repair/pivot from a
   preserved ref.

## Source Protocol

- Work on the generated dev branch under `private_dpl_evolve_source`; prepared
  starts are source parents, not edit-scope restrictions.
- After any branch/ref switch, reset, or `begin --from-ref`, rerun status and
  redo the source map for the landing zone before editing.
- Do not manually apply release/start patch files or copy source trees.
- Teacher `first patch handles` are efficient landing zones, not an exclusive
  allowed-file list. Modify any private `dpl_evolve` source needed by one
  coherent legalizer/DPO/handoff/objective/transaction/helper/telemetry
  mechanism.
- Use `03_fetch_peer_source.sh` only for Teacher-assigned peer refs or a
  concrete donor comparison. Summarize the peer mechanism first, then port the
  idea deliberately; do not auto-apply diffs.
- Use `25_trial_source.sh begin|keep|reject|status` as the checkpoint state
  machine. If Teacher assigns a repairable ref, start with
  `begin --from-ref <ref>`.
- If a legal trial is low-ROI after one meaningful repair, preserve it, reset
  explicitly from the last clean kept/candidate/start parent or Teacher-named
  ref, and try a different HPWL source only when evidence supports the pivot.

## Mechanism And Diagnosis

- You may change legalization, improve placement/DPO, source-internal
  parameters, scoring, candidate generation, transactions, handoff state, and
  compact telemetry. Do not edit evaluator, ORFS, baseline scripts, generated
  helper scripts, or flow-script/Tcl knobs.
- Mirror-internal edits are support only: change them when they protect or
  attribute a concrete accepted DPO/reorder/handoff mechanism.
- Same-case stacking must be a coherent producer/handoff/consumer source flow,
  not a selector among complete algorithms. If a stack record exists, use its
  `compatible_with` and `failure_buckets` fields to justify the added link or
  the repair target.
- New algorithms or handoff paths need compact counters: producer count,
  consumer attempts, accepts/rejects, HPWL gain/loss, and pass/runtime cost.
- If final HPWL is flat or worse, classify the bucket before finalizing:
  non-execution/stale code, producer payload quality, handoff lifetime, DPO
  zero/low accepts, strict/local scoring, accepted-delta washout,
  post-consumer preservation loss, runtime/cap bottleneck, wrong route, or
  validated-low-ROI.
- Counters and accepted delta prove liveness, not quality sufficiency. A route
  that increases probes or accepts but barely moves final HPWL needs payload,
  granularity, preservation, or gain-rate diagnosis.
- For plateau/runtime-for-quality routes, report `accepted_delta_per_runtime`
  or enough accepted-delta and pass-runtime fields for Teacher to compute it.
- Do not rerun unchanged build failures, crashes, timeouts, illegal placements,
  or zero-counter behavior. Repair plausible implementation/integration bugs
  once; otherwise reject and preserve evidence.

## Final Report

Fill the generated `knowledge_card` as a source-to-evidence map:

- route action, changed files/functions, mechanism class, source reasoning
- prepared branch/ref and pre-edit source map: entry, call path, state/payload
  lifetime, acceptance path, and proof counter/log
- stack/card query used, implemented roles, and compatible follow-up left open
- expected HPWL source, measured attribution, and failure bucket if weak
- final HPWL/runtime/legality/displacement and stage-wise HPWL deltas
- active counters/logs, metrics path, diff path, rejected refs if any
- source commit/ref, next repair or pivot

If the first patch plus one strengthened repair did not solve the route, add
`## Next Teacher Handoff` with the bottleneck, attempted repair, why it was
insufficient, source handles/refs, and proposed next route.
