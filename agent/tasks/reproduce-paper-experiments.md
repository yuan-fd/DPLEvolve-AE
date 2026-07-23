# Reproduce paper experiments

Run the fixed fresh-experiment wrappers independently:

```bash
bash scripts/agent/run_artifact.sh --artifact table4
bash scripts/agent/run_artifact.sh --artifact table5
bash scripts/agent/run_artifact.sh --artifact table6
```

Table 4 and Table 6 execute EDA and evaluate newly generated outputs. Table 5
currently reports `BLOCKED` until its missing paper-time configuration and six
source trees are recovered. Collect the JSON manifest path printed by each
completed command.
