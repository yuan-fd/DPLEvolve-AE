# Invariants

1. Packaged inputs, expected values, paper files, and provenance locks are
   immutable during reviewer commands.
2. Every supported experiment is independently reachable through one bundle
   `run.sh` and one dispatcher artifact ID.
3. Generated files remain under a bundle `output/` directory or the sibling
   ORFS run tree.
4. Evidence-only commands require no network, EDA binary, GPU, or model key.
5. Machine-facing instructions stay under `agent/`; reviewer prose stays under
   `docs/` and artifact READMEs.
6. Missing original ODBs and complete search populations are disclosed, not
   reconstructed or hidden.
7. A failed digest, arithmetic comparison, legality check, or tool exit status
   produces a nonzero exit code.
8. Release archives exclude Git state, caches, local environments, generated
   outputs, credentials, and `extras/unsupported/`.
9. Formal release is blocked while citation or Zenodo author metadata is
   incomplete.
