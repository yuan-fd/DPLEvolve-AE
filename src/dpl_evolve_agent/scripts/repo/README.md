# Repo Scripts

Owns repository quality gates and release-readiness checks.

These scripts should inspect repo structure, generated prompt contracts,
knowledge paths, script layout, and local static validation. They should
not mutate ORFS/OpenROAD workspaces or candidate sources.

## Canonical Helpers

- `case_registry.py`: resolve cases from `problems/` and `case_sets.json`.
- `query_knowledge.py`: query or validate `knowledge/index/skill_cards.jsonl`
  and the companion blueprint stack index.
- `checkpoint.py`: manage local restorable operation checkpoints under ignored
  runtime state.
- `audit_repo_hygiene.py`: enforce repo hygiene, script namespace layout, and
  deprecated knowledge-path rules.
- `check_release_readiness.sh`: run the static release gate and optional
  Teacher dry-run prompt audit.

`scripts/runtime_env.sh` remains top-level because callers source it to mutate shell environment variables.
