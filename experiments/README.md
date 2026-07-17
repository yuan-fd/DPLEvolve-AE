# Experiments

This directory organizes experiment configurations and run logs.

## Structure

```
experiments/
├── baseline/      # Baseline (no-LLM) experiments
├── main/          # Main paper results (Level 2 multi-agent DSE)
├── ablation/      # Ablation studies
├── joint-search/  # Joint black-box + white-box optimization
└── multi-seed/    # Multi-random-seed reproducibility runs
```

## Experiment Lifecycle

1. **Config**: Define experiment in `../configs/paper/` or `../configs/ablation/`.
2. **Run**: Execute via `make reproduce-main` or dedicated launcher scripts.
3. **Results**: Output lands in `../results/reproduced/<experiment_id>/`.
4. **Validate**: Compare against `../results/reference/` using validation scripts.

## Current Status

| Experiment | Status | Notes |
|---|---|---|
| AES Smoke Test | ✅ Reproduced | Exact match |
| Baseline Suite (5 cases) | ⬜ Ready to run | `make reproduce-baseline` |
| BO 9-case | ⬜ Config available | Requires API |
| Evolve 9-case | ⬜ Config available | Requires API |
| Ablation | ⬜ Not yet configured | Post-deadline |

## Adding a New Experiment

1. Create a YAML config in `../configs/paper/` or `../configs/ablation/`.
2. Add a launcher script referencing the config.
3. Document expected results in `../docs/expected-results.md`.
4. Update `../docs/claims-to-artifacts.md`.
