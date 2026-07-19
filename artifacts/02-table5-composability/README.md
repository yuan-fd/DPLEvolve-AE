# Table 5: stage-composability counterexamples

This bundle checks the three examples in which a legalizer improves
post-legalization HPWL but produces a worse final post-DPL HPWL after the
downstream optimization stages.

Run only this artifact:

```bash
bash artifacts/02-table5-composability/run.sh
```

The verifier reads the four archived HPWL values for each case, recomputes the
legalization and final percentage changes, checks that the stage improves while
the final flow regresses, and compares the results with the Table 5
transcription in `expected/table5.json`.

Success is reported as:

```text
[PASS] 3/3 stage-local counterexamples match paper Table 5
```

The input TSV is a compact extract of the original article-staging summary.
The original per-run EDA logs are not packaged, so this is archived-summary
verification rather than a fresh OpenROAD execution.

- `inputs/counterexamples.tsv`: archived HPWL values.
- `inputs/provenance.json`: source description and original summary hash.
- `expected/table5.json`: paper transcription.
- `output/summary.json`: generated machine-readable report.
