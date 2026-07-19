# Machine-facing interface

This directory contains instructions and schemas for automation agents. It is
not part of the reviewer reading path; human instructions live in `README.md`,
`artifacts/*/README.md`, and `docs/`.

Use the stable dispatcher from the repository root:

```bash
bash scripts/agent/run_artifact.sh --artifact table4
bash scripts/agent/run_artifact.sh --artifact table5
bash scripts/agent/run_artifact.sh --artifact table6
bash scripts/agent/run_artifact.sh --artifact smoke
```

The smoke command is check-only by default. Add `--run-smoke` only when the
pinned OpenROAD environment is prepared and a fresh EDA execution is wanted.
Use `--dry-run` to inspect the resolved command without executing it.

Each invocation writes a timestamped JSON manifest to the selected bundle's
ignored `output/` directory. Agents must treat `inputs/`, `expected/`,
`provenance/`, and `paper/` as read-only evidence.

- `AGENTS.md`: operating rules.
- `context/`: repository map, evidence semantics, and invariants.
- `tasks/`: bounded task recipes.
- `schemas/`: JSON schemas for machine-generated records.
