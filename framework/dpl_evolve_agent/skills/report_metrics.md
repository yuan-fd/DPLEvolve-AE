# Skill: report_metrics

Use this when a human or Teacher asks for experiment status, stage-wise metrics,
current best donors, runtime, legality, or whether an active Teacher/Student run
is still healthy.

## Canonical Source Of Truth

Use evaluator/OpenROAD-DPL metrics only:

- `metrics.json:hpwl_stages` for `HPWLg`, `HPWLlg`, `HPWLimprove`, and
  `HPWL final`.
- `metrics.json:runtime_seconds` for the full detailed placement evolve flow
  runtime, including legalize-driver overhead such as read/write ODB.
- `metrics.json:displacement` for avg/max displacement.
- `metrics.json:legality` for strict placement legality.

The bbox/proxy HPWL fields are debug-only. Do not use them for promotion,
headline comparisons, or final reports.

Partial DPL logs may be used only to describe in-flight runs before
`metrics.json` lands. Label them as `log` or `clean_or_partial`; do not compare
partial-log rows as clean final winners.

## Main Status Command

From the repo root:

```bash
python3 scripts/analysis/report_experiment_status.py \
  --launch-dir .dpl_evolve_state/launch_runs/<launch_id> \
  --expected-iterations <N> \
  --children <M> \
  --detail-round active \
  --detail-tail 20
```

Useful variants:

```bash
# Latest launch directory, compact per-case best summary.
python3 scripts/analysis/report_experiment_status.py

# One explicit round, full landed stage table.
python3 scripts/analysis/report_experiment_status.py \
  --round-id <round_id> \
  --detail-round <round_id>

# Machine-readable output for a notebook or another script.
python3 scripts/analysis/report_experiment_status.py \
  --launch-dir .dpl_evolve_state/launch_runs/<launch_id> \
  --format json
```

## Stage Table Only

For a direct per-student table without launch/process status:

```bash
python3 scripts/evaluator/report_stage_metrics.py \
  --round-id <round_id> \
  --expected-iterations <N> \
  --children <M> \
  --no-expected \
  --format markdown
```

Use `--include-path` when debugging source/binary/metrics alignment.

## Report Shape

A good status report should include:

- active case, round id, iteration, and whether Teacher or Students are running;
- global-placement HPWL (`HPWLg`) for each case before legalize/improve/mirror;
- completed-case best baseline vs best evolved final HPWL, runtime, and legality;
- active-case stage-wise rows for baselines and recent Student results;
- current best clean donor and whether a newer row is only partial-log evidence;
- agent behavior notes only when they affect correctness or efficiency.

Use these columns when space permits:

```text
result, HPWLg, HPWLlg, dLG%, HPWLimprove, dIP%, HPWL final,
dFinal%, runtime_s, avg_disp, max_disp, legality, source
```

## Interpretation Rules

- Negative `vs baseline` means HPWL improved versus the best clean canonical
  baseline for that case.
- `dLG%`, `dIP%`, and `dFinal%` are relative to `HPWLg` / stage inputs as
  normalized by the evaluator; do not recompute from proxy tables.
- Pick best evolved rows only from clean `metrics` / `metrics_packet` /
  `metrics_reused` evidence.
- Preserve stage donors in the analysis even when final HPWL is weaker; Teacher
  may need to repair the downstream consumer instead of discarding the route.
- Always report runtime and avg/max displacement next to HPWL so a donor is not
  promoted on wirelength alone.

## Failure Checks

If the table is stale or missing rows, inspect in this order:

1. `summary.tsv` under the launch directory.
2. `run.log` under the launch directory.
3. `scripts/analysis/report_experiment_status.py --show-processes` for active sessions.
4. `.dpl_evolve_state/checkpoints/operations/<operation_id>/codex_stderr.log`.
5. The `metrics.json` and DPL log paths printed by `--include-path`.
