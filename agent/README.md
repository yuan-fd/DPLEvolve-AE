# Agent Entry Point

This directory is the machine-facing interface. Human instructions live in the
root README and `docs/`.

## Required reading order

1. `AGENTS.md` — execution and safety rules.
2. `context/project-map.md` — repository ownership and stable entry points.
3. `context/experiment-semantics.md` — what each experiment executes.
4. `context/invariants.md` — conditions an agent must not bypass.
5. the matching recipe under `tasks/`.

## Automated reproduction sequence

Run from the repository root:

```bash
# 1. Inspect the host without starting an experiment
make doctor

# 2. Build the pinned tools and prepare all paper inputs
bash scripts/agent/run_artifact.sh --artifact prepare

# 3. Run the paper experiments independently
bash scripts/agent/run_artifact.sh --artifact table4
bash scripts/agent/run_artifact.sh --artifact table5
bash scripts/agent/run_artifact.sh --artifact table6
bash scripts/agent/run_artifact.sh --artifact figures
bash scripts/agent/run_artifact.sh --artifact ariane

# 4. Print the ReviewDSE search launch plan
bash scripts/agent/run_artifact.sh --artifact search
```

Table 5 verifies the three checksummed program snapshots, regenerates the
70/90/60-utilization inputs used only by that experiment, and executes all six
selected/reference roles. See `docs/table5-status.md` for the exact mapping.

The search dispatcher prints the exact configured plan. This inspection is not
an alternative method path: Teacher and Student configuration remains required.
A live or full model-backed search must use the cost-gated commands in
`tasks/reproduce-paper-experiments.md` after the user authorizes API/model use.

## Machine outputs

Every dispatcher execution writes a JSON run manifest to:

```text
$DPL_EVOLVE_STATE_ROOT/agent_runs/
```

Manifest status is `PASS`, `BLOCKED`, or `FAIL`. Experiment results are written
under:

```text
$DPL_EVOLVE_STATE_ROOT/paper_reproduction/
$DPL_EVOLVE_STATE_ROOT/experiment_batches/
```

Agents must report the command, manifest path, result path, exit code, and first
failed invariant. They must never rewrite expected values or fill missing
inputs with guessed substitutes.
