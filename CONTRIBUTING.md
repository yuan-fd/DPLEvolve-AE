# Contributing

This repository is an Artifact Evaluation package. Changes must preserve a
clear boundary between packaged evidence, generated output, and unsupported
research code.

## Adding an artifact

Create one directory under `artifacts/` containing:

```text
README.md
run.sh
inputs/
expected/
output/.gitkeep
```

The README must state the paper item, exact command, input provenance,
expected result, and evidence limitation. The script must run from any current
working directory, return nonzero on failure, write only to `output/`, and
avoid network or model calls unless the README explicitly defines a live
experiment.

Add the artifact to `artifacts/README.md`, the claims map, the Makefile, the
machine dispatcher, and both repository test suites.

## Interfaces

- Human-facing scripts belong in `scripts/human/`.
- Machine-facing dispatch and validation belong in `scripts/agent/`.
- Reusable runtime helpers belong in `scripts/shared/`.
- Release and provenance tools belong in `scripts/maintenance/`.
- Agent-only instructions belong in `agent/`, not human quick-start guides.

## Conventions

Bash scripts use `set -euo pipefail` and resolve paths relative to their own
location. Python code should use `pathlib` and type hints where practical.
Public documentation is English. Never hardcode developer-specific absolute
paths or commit credentials, compiled EDA files, generated output, or API keys.

Experiment YAML files follow `schemas/experiment_config.schema.json` and live
inside the artifact that uses them.

## Validation

```bash
make evidence
make test
make validate-configs
make zenodo-audit
git diff --check
```

Fresh OpenROAD validation is optional for ordinary documentation changes but
required when the smoke bundle, framework, source lock, or setup scripts
change.
