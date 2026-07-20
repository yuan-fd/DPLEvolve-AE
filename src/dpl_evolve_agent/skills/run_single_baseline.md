# Skill: run_single_baseline

Manual one-line runner notes. Prefer `evaluator.py` for agent-managed runs.

## Lines

- `openroad_dpl_flow`
- `openroad_dpl_negotiation`
- `evolve_default`

## Preferred Manual Wrapper

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/evaluator/run_canonical_line.sh" \
  --line evolve_default \
  --case <case_id> \
  --flow-variant <FLOW_VARIANT_WITH_3_4_PLACE_RESIZED_ODB> \
  --run-tag <RUN_TAG> \
  --threads 10
```

For a variant/family binary:

```bash
source "${DPL_EVOLVE_STATE_ROOT}/variants/<agent_id>/variant_env.sh"
"$DPL_EVOLVE_AGENT_ROOT/scripts/evaluator/run_canonical_line.sh" \
  --line evolve_default \
  --openroad-binary "${OPENROAD_BINARY}" \
  --case <case_id> \
  --flow-variant <FLOW_VARIANT_WITH_3_4_PLACE_RESIZED_ODB> \
  --run-tag <RUN_TAG> \
  --threads 10
```

## Output

```text
flow/reports/<platform>/<design>/<FLOW_VARIANT>/dpl_evolve_baseline/<run_tag>/metrics.json
```

## Rules

- Run strict legalization-only first.
- Reuse an existing place snapshot when iterating.
- Do not run full suites until one-line behavior is promising.
