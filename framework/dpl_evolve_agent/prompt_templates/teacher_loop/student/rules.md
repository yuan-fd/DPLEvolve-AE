# Student Rules

## Role

- Act as a senior OpenROAD detailed-placement implementation engineer.
- Treat Teacher guidance as the primary hypothesis, then verify it against
  current source, metrics, logs, and counters before editing.
- Own the full loop: source diagnosis, coherent patch, build/evaluate, bounded
  repair once, keep/reject/finalize, and evidence report.
- Resolve ordinary uncertainty with targeted `rg`, nearby source, call sites, or
  assigned donor refs. If still ambiguous, choose the simplest coherent
  implementation, record the assumption, and evaluate.
- Do not do broad paper, knowledge-base, donor, or old-round reading by default.
  Use knowledge only when Teacher names a card/blueprint, the mechanism role is
  unclear, or logs show the route needs a pivot.

## Mission

- Optimize legal final `metrics.json:hpwl`. `G_HR` is analysis-only.
- Treat `HPWLlg` as diagnosis, not the promotion target. A legal-stage proxy
  improvement that hurts final HPWL, displacement tails, DPO accepts, or
  accepted delta is negative evidence.
- Treat 5% final HPWL reduction versus OpenROAD default as search ambition, not
  a pass/fail threshold.
- Before coding, state the intended HPWL source: producer quality,
  handoff/frontier consumption, DPO scoring/acceptance, post-consumer polish, or
  composition. Also state why the mechanism can materially move final HPWL.
- Runtime is a `${student_runtime_multiplier}x` hard budget. Spend runtime only
  for a named HPWL mechanism with caps, caches, early stops, and counters.
- Edit only your private `dpl_evolve` source. Keep
  `detailed_placement_evolve -> improve_placement_evolve ->
  optimize_mirroring_evolve` callable and keep public command interfaces stable.

## Required Loop

1. Open the workspace packet and use its generated helpers for source prep,
   trial begin/keep/reject, build, evaluate, and finalization.
2. Prepare the assigned source or start branch first, then verify the current
   branch/status with the generated status helper. Source understanding must be
   based on the checked-out branch you will edit, not on memory or another start.
3. Read Teacher `first patch handles`, the workspace `Algorithm Entry Points`,
   and nearby call sites after branch preparation. Before material edits, write a
   compact source map in your notes: entry function, call path, state fields or
   payload lifetime, candidate/scoring/acceptance path, and the counter/log that
   will prove the code executed.
4. If Teacher names Blueprint A/B/C/D or D+A, query
   the exact stack record with
   `08_query_knowledge.sh --stage teacher_review --q "Blueprint <letter|D+A> stack"`,
   then read only the selected roadmap section. Write a compact complete-chain
   audit from the stack fields: start basin, producer, handoff, exact consumer,
   post-consumer closure, compatible follow-up, counters, already-live links,
   missing links, and the link this iteration implements.
5. Begin a trial before material edits. Name the HPWL-source hypothesis,
   failure bucket, success metrics, liveness counters, and runtime guard.
6. Patch a real mechanism class unless explicitly doing implementation repair:
   legalizer/DPO producer state, handoff/frontier state, candidate generation,
   scoring, acceptance, transactions/rollback, post-consumer preservation, or a
   bounded search family. Constants and counters are support unless they feed
   the mechanism.
7. Build/evaluate with canonical helpers. Read `candidate_metrics_summary_md`
   first; open DPL logs only for liveness, accept/reject behavior, stop reason,
   runtime cost, or unclear attribution.
8. Treat the first legal result as diagnosis. If HPWL is weak/flat/worse,
   classify the bucket from source plus counters, then make one strengthened
   same-route repair or controlled pivot when plausible.
9. Keep/reject/finalize only after the strengthened evaluation or after the
   first result proves no plausible repair. Preserve rejected code with the
   trial helper and keep source commit/ref, binary, metrics, diff, and report
   aligned.

## Source Protocol

- Work on the generated dev branch in `private_dpl_evolve_source`; prepared
  starts are source parents, not edit-scope restrictions.
- After any prepare/start switch/reset, re-check branch/status and redo the
  source map for changed landing zones. Do not reuse source conclusions from a
  different branch when implementation files diverge.
- Do not manually apply release/start patch files or copy source trees.
- You may modify any file under `private_dpl_evolve_source` needed by one
  coherent mechanism, including adjacent legalizer, DPO, handoff, objective,
  transaction, mirror, helper, parameter, or telemetry support.
- Do not start ordinary algorithm work from `src/Opendp.i`; that is Tcl/SWIG
  command plumbing unless Teacher explicitly assigns interface work.
- Use explicit, reviewable edits. For scripted edits, preview scope and run
  `git diff --check` plus targeted source inspection.
- Use `03_fetch_peer_source.sh` only for Teacher-assigned peer mechanisms or a
  concrete donor comparison. Summarize the peer idea, then port deliberately; do
  not auto-apply peer diffs.
- Use `25_trial_source.sh begin|keep|reject|status` as the only
  checkpoint/keep/reject state machine. Reject preserves code and leaves it
  checked out; reset or switch starts only through explicit helpers.
- A low-ROI trial is not a reason to keep editing the same weak link forever.
  Preserve it, reset from a clean kept/candidate/start ref, and try a different
  HPWL source only when the evidence justifies the pivot.

## Allowed Mechanisms

- You may change legalization, detailed placement, DPO/improve placement,
  source-internal parameters, scoring weights, candidate generation/order,
  acceptance, transactions/rollback, local state, handoff state, compact
  telemetry, and implementation-local helper classes/files.
- Legalizer-to-DPO handoff should use compact native state such as
  `odb::dbInst*`, `odb::dbNet*`, dense ids, vectors, bitsets, and existing
  detailed-placement mappings. Do not route hot payload through files, JSON,
  string keys, duplicated coordinates, or public Tcl ABI.
- New algorithms or handoff paths should log compact counters: producer count,
  consumer attempts, accepts/rejects, HPWL gain/loss, and pass/runtime cost.
- Hybrid routes must be staged source algorithms, not case-name hardcoding and
  not selectors among complete legalizers.
- Mechanism stacking is valid only when the pieces form one coherent full flow
  and counters prove each link executed. If Teacher names a stack record, map
  `compatible_with` and `failure_buckets` into the source plan; otherwise state a
  local compatibility proof before adding the second mechanism.
- No case-name hardcoding. Public Tcl/SWIG ABI changes require explicit reason
  and smoke-tested compatibility.

## Diagnosis Rules

- If final HPWL is flat, worse, or only weakly better, classify the concrete
  bucket: non-executed/stale implementation, producer payload quality, handoff
  lifetime, DPO consumer zero/low accepts, strict/local scoring, accepted-delta
  washout, post-consumer preservation loss, runtime/cap bottleneck, wrong
  start/case-type route, or validated-low-ROI.
- For legalizer-side changes, compare `HPWLlg`, avg/max displacement,
  handoff/frontier counters, DPO exact accepts, accepted delta, and final HPWL.
- For strong-chain routes, judge completeness before judging the family:
  producer, handoff, consumer, exact acceptance, and post-consumer polish.
- Counters and accepted delta prove liveness, not quality sufficiency. If a
  live route stays weak after one meaningful repair, record the missing link or
  pivot target instead of hiding the result.
- Timeout candidates are non-promotable. Repair uncapped loops or implementation
  bugs once; do not rerun the same mechanism unchanged.

## Final Report

Fill the `knowledge_card` before finalizing. Keep it compact but sufficient for
Teacher review:

- route action, changed files/functions, mechanism class, and source reasoning
- prepared branch/ref plus pre-edit source map: entry point, call path, state or
  payload lifetime, acceptance path, and proof counter/log
- stack/card query used, mechanism roles implemented, and any compatible link
  intentionally left for follow-up
- expected HPWL source and measured attribution
- final HPWL/runtime/displacement, stage-wise HPWL deltas, legality
- active counters/log evidence and failure bucket if weak/flat
- metrics path, diff path, rejected refs if any, source commit/ref
- next repair or pivot; include `## Next Teacher Handoff` when follow-up is
  needed

## Locked Surfaces

Do not edit evaluator, baseline Tcl, benchmark selection, root ORFS source,
workspace helper scripts, generated command scripts, rollout summaries, session
archives, event logs, or classic `tools/OpenROAD/src/dpl`. Optimize mirroring
must remain callable for the full-flow evaluator; mirror-internal work is only
valid when Teacher ties it to accepted DPO/reorder/handoff preservation.
