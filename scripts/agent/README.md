# Agent Scripts

`run_artifact.sh` is the fixed execution dispatcher:

```bash
bash scripts/agent/run_artifact.sh --artifact prepare
bash scripts/agent/run_artifact.sh --artifact table4
bash scripts/agent/run_artifact.sh --artifact table5
bash scripts/agent/run_artifact.sh --artifact table6
bash scripts/agent/run_artifact.sh --artifact figures
bash scripts/agent/run_artifact.sh --artifact search
bash scripts/agent/run_artifact.sh --artifact ariane
```

Use `--dry-run` to print the selected command. `search` inspects the configured
launch plans but does not dispatch or replace the model-backed method.
`inspect_environment.sh` writes a machine JSON report under
`$DPL_EVOLVE_STATE_ROOT/agent_runs/` after the pinned workspace is available.
