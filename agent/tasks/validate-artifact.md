# Validate the repository

Run from the repository root:

```bash
make test
make validate-configs
make audit-archive
make plan-level1
make plan-dse-paper
git diff --check
```

Then verify that all public Markdown and metadata are English and contain no
developer-specific absolute paths. Verify that Table 5/6 return `BLOCKED` when
exact data is absent and never load archived results as fresh results. If the
pinned EDA workspace is available, run one `make validate-evaluator` trajectory
and one frozen-source replay dry-run. Use `make zenodo-audit` to validate
exclusions without bypassing the formal author-metadata release gate.

Do not repair failed reference data automatically. Report the affected fresh
execution, its input hash/revisions, and the first failing invariant.
