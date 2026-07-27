# Table 5 Source Status

Table 5 is reproduced from three source snapshots tracked in the artifact:

| Row | Selected | Reference | Utilization |
|---|---|---|---:|
| AES dense Nangate45 | LEGALM | Diamond | 70 |
| JPEG dense Nangate45 | Negotiation | Negotiation | 90 |
| SWERV dense Nangate45 | Diamond | Negotiation | 60 |

The six roles intentionally reuse these implementations; duplicate per-row
source directories are unnecessary. Every regular file below
`artifacts/02-table5-composability/programs/` is enumerated by
`programs/MANIFEST.sha256` and is archived by Git and Zenodo.

```bash
make check-table5-data
make reproduce-table5 THREADS=10
```

The first command verifies all three snapshots. The second regenerates only
the Table 5 placement inputs with `CORE_UTILIZATION=70/90/60`, builds the mapped
programs, and writes `table5-fresh.tsv` below the timestamped reproduction
directory. Retained paper values are comparison targets, never fresh observed
results.

The rows, source mapping, and input settings are recorded in:

```text
configs/reproduction/table5-inputs.tsv
configs/reproduction/table5-sources.tsv
```
