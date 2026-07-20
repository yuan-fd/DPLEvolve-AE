# Skill: evaluate_run

Use this when you are responsible for running or reading evaluator output. In
Teacher/Student rounds, Students should normally run `evaluate_candidate_script`
from the workspace packet.

## Preferred Student Evaluation Path

Run the generated `evaluate_candidate_script`. It already selects the private
OpenROAD binary, case, flow variant, run tag, thread count, and DPL-flow
timeout.
Do not reconstruct the evaluator command unless that generated script itself
fails and its error is insufficient.
Treat that script as the Student-facing evaluator interface; spend analysis on
canonical metrics and logs, not on wrapper mechanics.

## Manual Fallback For Maintainers

```bash
python3 "$DPL_EVOLVE_AGENT_ROOT/adapters/science_codeevolve/evaluator.py" \
  --case <case_id> \
  --mode single \
  --line evolve_default \
  --flow-variant <FLOW_VARIANT_WITH_3_4_PLACE_RESIZED_ODB> \
  --run-tag <RUN_TAG> \
  --skip-build \
  --openroad-binary "${OPENROAD_BINARY}" \
  --out "${DPL_EVOLVE_STATE_ROOT}/checkpoints/operations/<RUN_TAG>/eval.json"
```

For variant/family runs, always pass `--openroad-binary`.

## Status

Read `status` first:

- `ok`: metrics were produced.
- `dry_run`: command construction only.
- `locked`: another run owns the same output directory.
- `build_failed`: build step failed.
- `run_failed`: evaluator run failed.
- `missing_artifact`: expected output was absent.

If status is not `ok`, read `command_results.*.stderr_tail` and
`command_results.*.stdout_tail`.

## Primary Result

```text
flow/reports/<platform>/<design>/<FLOW_VARIANT>/dpl_evolve_baseline/<run_tag>/metrics.json
```

Compare these first:

- legality
- HPWL delta from `metrics.json:hpwl`
- stage HPWL from `metrics.json:hpwl_stages`
- `G_HR = 100 * (HPWL_ref - HPWL_sol) / HPWL_ref - P(runtime_sol / runtime_ref), where P(r)=0 for r <= 1.10 and P(2.0)=1.0 percentage point`
  versus the OpenROAD DPL default-flow reference, when present, as an analysis
  metric. Higher `G_HR` is better and the reference is 0, but evolve selection
  remains legal final HPWL under the hard runtime budget.
- displacement
- runtime from `metrics.json:runtime_seconds`
- memory, when recorded

`metrics.json:hpwl` is the canonical OpenROAD/DPL pin-based log HPWL.  The
legacy `hpwl_proxy` field is a cell-bbox diagnostic only.

`runtime_seconds` is the reported wall runtime for the full DPL evolve
evaluation flow, including legalize-driver overhead such as read/write ODB.
The Student timeout threshold is derived from this value, but the timeout is
applied only to the OpenROAD legalize/improve/mirror flow run. Post-run
metrics/evaluator collection is not timeout-wrapped. Do not describe
`runtime_seconds` as pure algorithm-internal time.

Classify runtime before judging a result:

- `fast`: near the canonical baseline; useful for donor preservation.
- `explore`: moderate extra runtime spent on a bounded stronger search.
- `aggressive`: larger but timeout-safe runtime spent on a mechanism that could
  materially change final HPWL.

Runtime growth is not automatically bad, but it is bounded by the generated
hard timeout and analyzed through `G_HR`. Broad scans, repeated endpoint-like
subpasses, and randomized perturbations are acceptable when they are
source-level, scoped, capped or early-stopped, instrumented, and tied to a real
HPWL source. Prefer handoff/frontier signals that reduce blind random search and
focus effort on recoverable rows, cells, windows, or nets. Preserve
extra-runtime HPWL donors when counters show cached/capped/parallel
HPWL-source search; use `G_HR` only to decide whether the donor needs runtime
repair.

## Strict Track Rule

Strict legalization-only runs require an existing:

```text
flow/results/<platform>/<design>/<FLOW_VARIANT>/3_4_place_resized.odb
```

The wrapper reuses this snapshot and should not rebuild upstream placement.

## Keep / Reject

Keep only if:

- legality is clean
- canonical final HPWL improves inside the hard runtime budget, or HPWL/stage
  evidence creates a clear donor that can be repaired for runtime
- displacement remains controlled

Reject if:

- legality fails
- `metrics.json` is missing or malformed
- runtime/displacement regresses without a clear offsetting gain
- the OpenROAD DPL flow exceeds the generated timeout; timeout failures are
  non-promotable, but their `metrics.json` failure record is still useful
  evidence when present
- the generated timeout applies to the OpenROAD legalize/improve/mirror flow
  only. Metrics/evaluator collection should still run after a flow timeout and
  write failure evidence when possible.
