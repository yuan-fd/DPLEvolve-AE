# Codex Exec Recorder

Implementation modules behind `scripts/run_codex_exec.py`.

- `cli.py`: command-line options only.
- `runner.py`: executes `codex exec`, records operation files, and returns the Codex exit code.
- `artifacts.py`: summarizes events, stderr categories, cost estimates, and per-operation README files.

Keep `scripts/run_codex_exec.py` as the stable human/tool entrypoint.
