# Zenodo release checklist

## Author metadata required

The anonymous paper and local Git history do not contain a trustworthy author
list. Before the public deposit, obtain the ordered camera-ready author list
and update the release metadata:

1. `CITATION.cff`: given name, family name, affiliation, email, and ORCID when
   available.
2. `.zenodo.json`: the same authors in the same order; Zenodo creator names use
   `Family name, Given name`.
Also add the public artifact-contact method to `README.md`. Do not infer paper
authors from server accounts or Git commit identities.

## Archive checks

```bash
# Works before authors are known; archive is marked audit-only.
make zenodo-audit

# Release command; refuses placeholder or missing authors.
make zenodo
```

`make zenodo` refuses missing author metadata and missing exact Table 5/6
paper data. `make zenodo-audit` permits both only so maintainers can inspect
archive composition before the release is complete.

The packaging script runs the compact archive audit, includes the Table 4 BO
campaign records, 18 frozen programs, framework, execution scripts,
documentation, and provenance, then creates `MANIFEST.sha256` and checks
excluded state. The paper PDF is distributed by the publisher and is not
duplicated in the repository.

A formal release must also pass `make paper-data-check` and publish the exact
Table 5/6 data package described in `docs/paper-data-layout.md`. Audit-only
packaging may acknowledge missing data, but must not be labeled a complete
reproduction release.

Before upload:

- inspect the reported archive path, size, and SHA-256;
- extract it in a clean temporary directory and run `make test`,
  `make validate-configs`, and `make audit-archive`;
- run the one-case fresh path through `make validate-evaluator` on the Rocky
  Linux reference environment;
- verify the separately distributed Table 5/6 archive, checksums, and public
  URL/DOI;
- confirm `.zenodo.json` and `CITATION.cff` use the camera-ready order;
- upload to Zenodo, reserve/finalize the DOI, and add the DOI to the paper's
  artifact appendix and repository metadata.
