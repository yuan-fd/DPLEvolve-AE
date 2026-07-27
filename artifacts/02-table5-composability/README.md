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

The runner regenerates only the Table 5 inputs, using `CORE_UTILIZATION=70`
for AES, 90 for JPEG, and 60 for SWERV. It maps the six roles to three retained,
checksummed implementations: LEGALM/ Diamond for AES, Negotiation/Negotiation
for JPEG, and Diamond/Negotiation for SWERV. It then executes legalization,
DPO, and final mirroring and derives the three counterexamples from new
`metrics.json` files. The retained TSV is not copied into fresh results.

## Directory contents

- `inputs/counterexamples.tsv`: retained paper values for result interpretation.
- `inputs/provenance.json`: retained-record origin.
- `programs/{legalm,diamond,negotiation}/dpl_evolve/`: source snapshots.
- `programs/MANIFEST.sha256`: complete snapshot integrity manifest.
- `expected/table5.json`: paper comparison target.
- `output/`: output-contract placeholder; fresh EDA products use the state root.

The fresh summary is written to
`../dpl_evolve_state/paper_reproduction/table5_*/table5-fresh.tsv`.
