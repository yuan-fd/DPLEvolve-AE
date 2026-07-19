# Metadata

This directory stores small tracked metadata files that define repo/workspace
anchors.

Current contents:

- `anchors.json`: expected ORFS/OpenROAD base commits and tracked patch-anchor
  paths used by workspace preparation and release review.
- `ae_reproduction_lock.json`: exact prepared revisions, tool submodules,
  reference binary hashes, and AES smoke-test expectations established by the
  AE reproduction audit.

Metadata files should be stable, portable, and free of machine-local absolute
paths.  Generated manifests, run ledgers, and large experiment artifacts belong
under `DPL_EVOLVE_STATE_ROOT`, not here.
