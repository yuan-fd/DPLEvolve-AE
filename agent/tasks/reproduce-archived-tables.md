# Verify archived paper tables

Run each artifact independently:

```bash
bash scripts/agent/run_artifact.sh --artifact table4
bash scripts/agent/run_artifact.sh --artifact table5
bash scripts/agent/run_artifact.sh --artifact table6
```

Collect the JSON manifest path printed by each command. Report Table 4 as
archived-record recomputation and Tables 5/6 as archived-summary verification.
