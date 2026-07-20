# Configs

This directory stores reusable control-plane configuration presets.

## Current files

- `codex_smoke.yaml`
  Small-budget preset for smoke loops and quick validation.  The evaluation
  budget is long enough for large-case strict DP flows to finish the
  legalization/check/parasitics path without accidentally clipping at the final
  metrics step.
- `codex_design_specific.yaml`
  Larger-budget preset for design-level iteration.
- `mutation_contract.yaml`
  Patch-scope and mechanism contract for bounded evolution work.
- `run_manifest.template.yaml`
  Template for structured run metadata.
- `openroad_variant_example.yaml`
  Example showing how to point evaluation at an isolated
  `build/install/openroad` variant.
- `bo_search_spaces/`
  YAML search spaces for `scripts/bo/bo_tune_case.py`.  These files define only
  command-line parameters, not source-code changes, so black-box tuning remains
  comparable against white-box Agent evolution.
- `raytune_requirements.txt`
  Python dependencies for the required Ray Tune BO environment.

## Usage rule

Prefer configuration here over hardcoding paths or budgets into scripts.

If multiple agents need different OpenROAD binaries, use
`openroad_variant_example.yaml` as the model and keep each variant in its own
local build/install root.
