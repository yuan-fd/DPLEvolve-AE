# Framework Database

This package is the stable append-only evidence layer for the constrained
`detailed_placement_evolve` framework.  It is intentionally small and local:
records are JSONL files under `DPL_EVOLVE_STATE_ROOT/database/`.

The database is framework-neutral.  It records what was materialized, built,
evaluated, reviewed, and promoted, regardless of which algorithm stage an agent
edited.

## Tables

- `candidate_programs.jsonl`: one materialized source variant or patch attempt.
- `build_results.jsonl`: private relink/build outcome for a candidate program.
- `eval_results.jsonl`: strict evaluator outcome and metric paths.
- `teacher_reviews.jsonl`: teacher-agent review, decision, and next guidance.
- `database_commits.jsonl`: audit trail for each append operation.

## Usage

Append a record from a JSON file:

```bash
python3 -m database.commit \
  --state-root "$DPL_EVOLVE_STATE_ROOT" \
  --table candidate_programs \
  --record-json /path/to/record.json
```

Append an inline JSON record:

```bash
python3 -m database.commit \
  --state-root "$DPL_EVOLVE_STATE_ROOT" \
  --table teacher_reviews \
  --record '{"review_id":"R_001","decision":"revise","notes":"needs telemetry"}'
```

The command emits the audit commit record as JSON on stdout.
