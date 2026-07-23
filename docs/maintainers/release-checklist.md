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

`make zenodo` refuses missing author metadata or an unavailable/mismatched
Table 6 data package. `make zenodo-audit` permits those only so maintainers can
inspect archive composition. The missing Table 5 SWERV config and six sources
remain a required prominent limitation; the release must not describe Table 5
as reproduced.

The packaging script runs repository and configuration tests, includes the
Table 4 BO campaign records, 18 frozen programs, framework, execution scripts,
documentation, and provenance, then creates `MANIFEST.sha256` and checks
excluded state. The paper PDF is distributed by the publisher and is not
duplicated in the repository.

A formal release must pass `make check-table6-data` and publish the exact
Table 6 package described in `docs/paper-data-layout.md`. Because Table 5's
SWERV input config and six source trees are unrecovered, even the formal release
is not a complete Table 5 reproduction package.

Before upload:

- inspect the reported archive path, size, and SHA-256;
- extract it in a clean temporary directory and run `make test` and
  `make validate-configs`;
- run the one-case fresh path through `make validate-evaluator` on the Rocky
  Linux reference environment;
- verify the separately distributed Table 6 archive, checksum, and public URL;
- confirm every public page discloses the unrecovered Table 5 config/source gap;
- confirm every paper experiment directory contains an executable
  `reproduce.sh`, `inputs/`, `expected/`, and `output/` contract;
- confirm `make reviewer-prepare`, `make reviewer-aes-result`, and
  `make reviewer-table6-one` remain bounded reviewer paths;
- confirm `.zenodo.json` and `CITATION.cff` use the camera-ready order;
- upload to Zenodo, reserve/finalize the DOI, and add the DOI to the paper's
  artifact appendix and repository metadata.
