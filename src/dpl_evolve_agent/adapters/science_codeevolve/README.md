# science-codeevolve Adapter Policy

This directory does **not** reimplement CodeEvolve.
It only borrows the control-plane concepts:

- problem/config separation
- evaluator wrapper
- checkpoint / archive discipline
- run orchestration
- directory locking
- timeout / memory guard
- process-tree cleanup

## In this project

- Codex remains the only patch generator
- baseline harness remains the only judge
- this adapter only schedules and records runs
- target-plane algorithm logic stays in `tools/OpenROAD/src/dpl_evolve/`

## Current local runtime toolbox

The minimal runtime pieces that are intentionally local to this repo are:

- `lock.py`
  per-directory exclusive lock for build/run destinations
- `guarded_exec.py`
  timeout, memory ceiling, process-group cleanup, compact stdout/stderr tails
- `evaluator.py`
  thin wrapper over the existing baseline harness plus the runtime guards above

These pieces are inspired by `science-codeevolve`, but they are intentionally
kept thin and repo-specific.
