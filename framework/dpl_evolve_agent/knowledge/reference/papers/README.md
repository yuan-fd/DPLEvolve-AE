# Paper References

This directory is the stable source catalog for papers used by the DPL-Evolve
notes.  Default Agent knowledge should cite paper ids and reusable mechanisms,
not machine-specific artifact paths.

## Committed Reference

- OpenDP-named reference: fence-region-aware mixed-height legalization paper,
  not OpenROAD DPL engine documentation.

## Optional Source-Text Verification

Use the helper from the repo root:

```bash
./scripts/analysis/fetch_reference_papers.sh
```

The script downloads clearly fetchable papers into an ignored cache.  That
cache is for optional source-text verification, not default knowledge or
redistribution.

For author-version papers that explicitly say personal use only, the script
does not download them by default.  You can opt in only for source-text
verification:

```bash
./scripts/analysis/fetch_reference_papers.sh --include-personal-use
```

Do not commit the resulting cache.

## Paper Roles

- OpenDP-named committed reference: fence/fragmented-row legalization ideas.
- NBLG: negotiation-based legalization mechanism reference.
- LEGALM / LEGALM 2.0: ALM-style legalization and partition/refinement
  mechanism reference.
- DREAMPlace: GPU/global-placement and row-assignment/Abacus donor reference.
- Abacus: row/cluster legalization and minimal-displacement donor reference.

See `paper_sources.yaml` for source links, fetch policy, and notes.
