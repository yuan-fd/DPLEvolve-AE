# Skills

Repo-local operating notes for `dpl_evolve_agent`.

Read only the skill needed for the current action:

- `patch_rules.md`: Student active patch surfaces, donor boundaries, and patch
  protocol.
- `source_git_workflow.md`: Student local git branch/commit workflow.
- `build_openroad.md`: Student generated build-script usage and manual build
  fallback only when a script fails.
- `evaluate_run.md`: Student generated evaluator-script usage and canonical
  result interpretation.
- `report_metrics.md`: Human/Teacher launch-status and stage-wise metrics
  reporting, including active-process checks and best-donor summaries.
- `trace_logging.md`: Student lightweight counters and logs for mechanism
  verification.
- `../knowledge/contracts/metric_contract.md`: canonical HPWL and
  report interpretation contract.
- `run_single_baseline.md`: manual one-line baseline/evolve runs.
- `run_baseline_suite.md`: manual suite runs.
- `run_codex_exec.md`: external `codex exec` logging wrapper.
- `teacher_peer_coaching.md`: Teacher-side peer-learning and feedback protocol
  for multi-student optimization rounds.

Default Student skills for source-edit workers:

- read `patch_rules.md`
- read `source_git_workflow.md` when editing a Student workspace
- run the generated workspace packet scripts for source prepare, build,
  evaluation, commit, and diff
- read `build_openroad.md` and `evaluate_run.md` only when a generated script
  fails or when interpreting metrics
- read `trace_logging.md` when adding a new mechanism whose execution needs
  counters
- implement one coherent route/mechanism on the assigned private `dpl_evolve`
  source
- touch legalization/detailed placement, improve placement, or their shared
  objective/handoff as directed by Teacher
- when Teacher assigns a pseudocode/algorithm family, read only the relevant
  card under `../knowledge/algorithms/` and translate it into a concrete
  source-level plan shaped by the current metrics, logs, and source path before
  patching
- let the orchestrator-owned packet decide whether this worker should build
  and evaluate

Default Teacher skills for planning/review workers:

- read `teacher_peer_coaching.md`
- read `report_metrics.md` when asked to summarize active experiments,
  compare donors, or prepare status tables
- use context packets, canonical metrics, stage-wise attribution, peer learning,
  knowledge cards, and the relevant algorithm/pseudocode cards
- assign Student skills explicitly; do not attach build/debug/run skills unless
  the Student task is specifically an infrastructure repair
