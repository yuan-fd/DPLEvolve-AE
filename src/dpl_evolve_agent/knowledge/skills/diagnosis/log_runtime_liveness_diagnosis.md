# Skill Note: log_runtime_liveness_diagnosis

Canonical structured fields live in `knowledge/index/skill_cards.jsonl`.
Use `scripts/repo/query_knowledge.py --q log_runtime_liveness_diagnosis --show-full`
for source handles, metrics, and log signals.

## Extra Guidance

- Add aggregate counters near stage boundaries, not per-candidate hot-loop logs.
- Use logs to distinguish non-execution, fallback domination, stale binary,
  legality rejection, acceptance bottleneck, and real algorithm weakness.
- Runtime evidence should identify whether to cache, cap, parallelize, or change
  the hypothesis.

## Common Failure

- Reporting a mechanism as successful when its primary counter is zero or the
  metrics came from an old binary.
