# Invariants

1. Reviewer commands never modify retained inputs, expected values, paper
   files, or provenance records.
2. Every paper experiment has a human README and direct `reproduce.sh` wrapper;
   the root Make target remains the stable public interface.
3. Fresh outputs remain under ORFS, `DPL_EVOLVE_STATE_ROOT`, or
   `PAPER_DATA_ROOT`, never an immutable expected directory.
4. Human guidance stays in README/docs/artifact packages; agent rules and
   bounded recipes stay under `agent/` and `scripts/agent/`.
6. Hash availability is provenance metadata. Missing paper-time hashes are
   disclosed but do not replace legality and numerical acceptance.
7. Table 5 missing assets produce `BLOCKED`; no standard config or archived
   value may be substituted.
8. Paid model calls require explicit user authorization and the repository's
   acknowledgement gate.
9. Failed execution, legality, or numerical checks produce a nonzero result and
   retain the newly generated evidence.
10. Release archives exclude Git state, caches, local environments, generated
    outputs, credentials, and ignored unsupported material.
