# DPL-Evolve Skill Notes

`knowledge/index/skill_cards.jsonl` is the canonical skill database. It keeps the
machine-readable stage, source handles, patches, metrics, log signals, and done
criteria used by Teacher and Student prompts.
`knowledge/index/mechanism_stack_cards.jsonl` is the companion database for
blueprint composition: producer/handoff/consumer roles, compatible stacking,
handoff payloads, failure buckets, and first patch handles.

The markdown files under this directory are short notes only. They add route
pitfalls and extra guidance that should not be duplicated in every prompt. Do
not bulk-read this directory; query the index first and open only the matched
note when it is useful.

Use the query helper from the repo root:

```bash
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --stage improve_placement --q "final HPWL weak runtime controlled" --limit 3
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --stage teacher_review --q "Blueprint D+A stack" --limit 3
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --q dpo_source_edge_cached_scoring --show-full
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --validate
```

Recommended query stages:

- `case_diagnosis`
- `start_selection`
- `legalization`
- `improve_placement`
- `handoff`
- `build_eval_log_diagnosis`
- `teacher_review`
