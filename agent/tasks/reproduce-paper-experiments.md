# Reproduce Paper Experiments

## Environment

```bash
make doctor
bash scripts/agent/run_artifact.sh --artifact prepare
```

## EDA results without model calls

```bash
bash scripts/agent/run_artifact.sh --artifact table4
bash scripts/agent/run_artifact.sh --artifact table5
bash scripts/agent/run_artifact.sh --artifact table6
bash scripts/agent/run_artifact.sh --artifact ariane
bash scripts/agent/run_artifact.sh --artifact figures
```

Table 5 is expected to return `BLOCKED` until the exact missing sources are
recovered. Continue the independent Table 6, Ariane, and figure stages.

## ReviewDSE search

```bash
# No model calls
bash scripts/agent/run_artifact.sh --artifact search

# Bounded live search; requires user-authorized model/API use
make run-dse-small CASE=aes_nangate45 STUDENTS=1 ITERATIONS=1 THREADS=8

# Full paper search; requires explicit paper-scale cost authorization
make reproduce-level1 ACKNOWLEDGE_LLM_COST=yes THREADS=10
make run-dse-paper ACKNOWLEDGE_LLM_COST=yes \
  DSE_RUN_PREFIX=review_run_01 THREADS=10
make summarize-dse-paper DSE_RUN_PREFIX=review_run_01
make reproduce-figures FIGURE_SOURCE=fresh DSE_RUN_PREFIX=review_run_01
```

## Completion report

For every stage report:

- dispatcher manifest and exit status;
- generated result path;
- legality and numerical verdict;
- missing-data status, if `BLOCKED`; and
- first error and retained log path, if `FAIL`.
