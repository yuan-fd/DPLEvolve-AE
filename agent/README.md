# Machine-facing interface

This directory is for automation agents, not the reviewer reading path. Human
instructions live in the root README, `docs/reviewer-walkthrough.md`, and each
experiment README.

Agents use the same stable Make interface as humans:

```bash
make prepare-paper-inputs CASE=aes_nangate45
make validate-evaluator CASE=aes_nangate45
make replay-reviewdse TRACK=hpwl CASE=aes_nangate45
make reproduce-table6 CASE=ariane133_placebatch PATTERN=center_band_8 ROLE=reviewdse
make plan-dse-paper
```

The fixed dispatcher provides an additional safe interface:

```bash
# real experiment wrapper
bash scripts/agent/run_artifact.sh --artifact table4

# print search plans; never starts paid calls through the dispatcher
bash scripts/agent/run_artifact.sh --artifact search
```

Generated agent manifests are written under
`$DPL_EVOLVE_STATE_ROOT/agent_runs/`, not into the Git checkout.

- `AGENTS.md`: operational rules;
- `context/`: experiment semantics, invariants, and project map;
- `tasks/`: bounded recipes;
- `schemas/`: machine-generated record schemas.
