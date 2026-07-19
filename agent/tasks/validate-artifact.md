# Validate the repository

Run from the repository root:

```bash
make evidence
make test
make validate-configs
git diff --check
```

Then verify that all public Markdown and metadata are English and contain no
developer-specific absolute paths. If the pinned EDA workspace is available,
also run `make smoke-check`. Use `make zenodo-audit` to validate exclusions and
archive contents without bypassing the formal author-metadata release gate.

Do not repair failed evidence automatically. Report the affected bundle and
the first failing invariant.
