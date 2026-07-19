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

The packaging script runs `make evidence`, includes the paper, raw Table 4 BO
campaigns, Table 5/6 evidence, 18 frozen programs, framework, documentation,
and provenance, then creates `MANIFEST.sha256` and checks excluded state.

Before upload:

- inspect the reported archive path, size, and SHA-256;
- extract it in a clean temporary directory and run `make evidence`;
- confirm `.zenodo.json` and `CITATION.cff` use the camera-ready order;
- upload to Zenodo, reserve/finalize the DOI, and add the DOI to the paper's
  artifact appendix and repository metadata.
