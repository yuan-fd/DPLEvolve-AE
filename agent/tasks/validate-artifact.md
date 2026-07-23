# Validate the repository

Run from the repository root:

```bash
make test
make validate-configs
make plan-level1
make plan-dse-paper
git diff --check
```

Then verify that all public Markdown and metadata are English and contain no
developer-specific absolute paths. Verify that Table 5 returns `BLOCKED` when
its recovery data is absent, that Table 6 instructs the reviewer to fetch its
published package when absent, and that neither loads archived results as fresh. If the
pinned EDA workspace is available, run one `make validate-evaluator` trajectory
and one frozen-source replay dry-run. Use `make zenodo-audit` to validate
exclusions without bypassing the formal author-metadata release gate.

Do not repair failed reference data automatically. Report the affected fresh
execution, legality and numerical result, available input provenance, and the
first failing invariant.
