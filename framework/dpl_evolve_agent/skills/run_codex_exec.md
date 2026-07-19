# Skill: run_codex_exec

## Purpose
Run `codex exec` through the local control plane and record the invocation as a
local checkpoint operation.

## Canonical entrypoint
- `$DPL_EVOLVE_AGENT_ROOT/scripts/run_codex_exec.py`

## What it records
- copied prompt text
- raw `codex exec --json` event stream
- stderr log
- last agent message
- token usage summary
- optional cost estimate if explicit price arguments are provided

## Important rule
Treat token counts as the primary ground truth.

Do not assume the CLI exposes account credits directly. If a cost estimate is
needed, pass explicit price-per-1M-token values at invocation time and treat
the result as an estimate, not account truth.

## Example

```bash
python3 "$DPL_EVOLVE_AGENT_ROOT/scripts/run_codex_exec.py" \
  --operation-id codex_exec_smoke \
  --prompt "Inspect AGENTS.md and report the active detailed_placement_evolve patch contract." \
  --sandbox workspace-write
```

## Output location
- `${DPL_EVOLVE_STATE_ROOT}/checkpoints/operations/<operation_id>/codex_usage_summary.json`
