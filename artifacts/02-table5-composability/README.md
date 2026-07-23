# Table 5: stage-composability counterexamples

Table 5 studies cases where a legalizer improves post-legalization HPWL
`H_lg`, but the complete downstream flow produces worse final HPWL `H_f`.

## Fresh reproduction

```bash
make check-table5-data
make reproduce-table5 THREADS=10
# or
bash artifacts/02-table5-composability/reproduce.sh --threads 10
```

The runner is implemented to regenerate inputs, build six selected/reference
source trees, execute legalization + DPO + final mirroring, and derive the
three counterexamples from new metrics.

Current status is **BLOCKED**. SWERV's untracked `config_dense2.mk` and all six
complete source trees were not retained. The command stops before EDA and does
not substitute the standard SWERV config or archived values.

## Directory contents

- `inputs/counterexamples.tsv`: retained paper values for result interpretation.
- `inputs/provenance.json`: retained-record origin.
- `expected/table5.json`: paper comparison target.
- `output/`: output-contract placeholder; fresh EDA products use the state root.
