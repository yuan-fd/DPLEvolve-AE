# Check selected programs

1. Run `bash scripts/agent/run_artifact.sh --artifact table4`.
2. Confirm that all 18 source-tree digests match.
3. Report that this is an integrity check, not a numerical replay.
4. Do not pass `--case` to the optional launcher unless the exact matching ODB
   inputs have been installed and independently verified.
