# Analysis Scripts

Owns human-facing reports, source exports, prompt compaction, run comparison,
and local reference-paper fetching.

These scripts should not mutate candidate source or build state except for
explicit report/export outputs. Runtime execution belongs in `scripts/evaluator/`
or `scripts/matrix/`.
