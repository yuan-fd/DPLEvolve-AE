# Table 6: hard cut-row repair

This bundle checks the nine hard cut-row patterns reported in Table 6.

Run only this artifact:

```bash
bash artifacts/03-table6-cutrow/run.sh
```

The verifier compares the archived Diamond and Negotiation outcomes, selected
ReviewDSE status, runtime, and archived legality result with the Table 6
transcription in `expected/table6.json`.

Success is reported as:

```text
[PASS] 9/9 archived cut-row rows match paper Table 6
```

The two TSV files are compact extracts of the original fail-search and
candidate summaries. The cut-row ODB inputs, complete OpenROAD logs, and
selected binaries are not packaged. This command therefore verifies the
archived summary; it does not rerun OpenROAD or `check_placement`.

- `inputs/fixed_routes.tsv`: fixed-source outcomes.
- `inputs/reviewdse.tsv`: selected ReviewDSE outcomes.
- `inputs/provenance.json`: original summary paths and hashes.
- `expected/table6.json`: paper transcription.
- `output/`: generated CSV and JSON reports.
