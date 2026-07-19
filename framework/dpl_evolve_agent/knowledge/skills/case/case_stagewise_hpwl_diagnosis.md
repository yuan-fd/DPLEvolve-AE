# Skill Note: case_stagewise_hpwl_diagnosis

Canonical structured fields live in `knowledge/index/skill_cards.jsonl`.
Use `scripts/repo/query_knowledge.py --q case_stagewise_hpwl_diagnosis --show-full`
when Teacher needs the exact metrics/log checklist.

## Extra Guidance

- Start every route decision from the stage table, not from final HPWL alone.
- Treat `HPWLlg` as attribution, not a standalone objective. A legal-stage
  improvement is only useful when final HPWL, legality, avg/max displacement,
  and downstream DPO recovery agree with it.
- Check whether legalization destroys too much of the global-placement basin:
  a large `HPWLg -> HPWLlg` disruption plus weak DPO recovery is evidence that
  the legalizer output may need repair before adding more DPO polish.
- For an active launch, use `scripts/analysis/report_experiment_status.py` to combine
  process status, best-donor summaries, and recent stage rows.  Use
  `scripts/evaluator/report_stage_metrics.py` when only one round's full per-student
  stage table is needed.
- Preserve a stage donor even when a later stage erases it; assign the next
  route to repair the consumer or handoff.
- Treat runtime-only wins as evidence only if HPWL, displacement, and legality
  stay comparable.
- Treat `HPWLlg`-only wins as stage evidence only if avg/max displacement and
  DPO exact accepted gain do not show a worse handoff state.

## Common Failure

- Promoting the best final donor while discarding a stronger legalization-stage
  donor that only needs improve-placement repair.
