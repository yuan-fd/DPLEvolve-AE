# Replay selected programs

1. Run `make prepare-paper-inputs CASE=<case>` if the ODB is absent.
2. Dry-run `bash scripts/reproduce/replay_selected.sh --track hpwl --case <case> --dry-run`.
3. Run `make replay-reviewdse TRACK=hpwl CASE=<case>`.
4. Confirm the source digest, build, protected evaluator status, legality, and
   new `results.tsv` all pass.
5. Report the fresh legality, HPWL/runtime result, generated ODB hash when
   available, and whether the numerical acceptance window passed.

`scripts/agent/run_artifact.sh --artifact table4` runs the complete fresh Table
4 pipeline. Use the case-specific Make command above for a bounded replay.
