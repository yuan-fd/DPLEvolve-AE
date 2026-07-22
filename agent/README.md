# Machine-facing interface

This directory contains instructions and schemas for automation agents. It is
not part of the reviewer reading path; human instructions live in `README.md`,
`artifacts/*/README.md`, and `docs/`.

Use the paper-experiment targets from the repository root:

```bash
make prepare-paper-inputs CASE=aes_nangate45
make validate-evaluator CASE=aes_nangate45
make reproduce-default CASE=aes_nangate45
make replay-reviewdse TRACK=hpwl CASE=aes_nangate45
make plan-dse-paper
```

The legacy dispatcher remains for compact archive audits and the optional AES
diagnostic. It is not the primary experiment interface. Use each reproduction
script's `--dry-run` before expensive commands.

Fresh executions write to `DPL_EVOLVE_STATE_ROOT` and ORFS. Agents must treat
archive `inputs/`, `expected/`, and `provenance/` as read-only evidence.

- `AGENTS.md`: operating rules.
- `context/`: repository map, evidence semantics, and invariants.
- `tasks/`: bounded task recipes.
- `schemas/`: JSON schemas for machine-generated records.
