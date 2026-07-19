# Orchestration Scripts

Owns human-facing runtime orchestration and observability helpers.

## Canonical Helpers

- `watch_teacher_round.py`: terminal or local-web dashboard for one
  Teacher/Student round.
- `check_evolve_now.py`: compact live status for active experiment batches.
- `monitor_evolve_heartbeat.sh`: repeated batch heartbeat for long-running
  campaigns.

Stable top-level entrypoints are intentionally limited to
`scripts/optimize_case_with_codex.py`, `scripts/run_codex_exec.py`, and
`scripts/runtime_env.sh`.

New implementation code should live here only when it coordinates existing
repo helpers or reports live runtime state. Build, evaluation, matrix replay,
analysis, and repo-contract logic belong in their role namespaces.
