# Claims-to-Artifacts Mapping

This document provides the complete mapping from every paper claim to
the artifact commands and files that verify it. Refer to the README
for quick-start instructions; use this document for detailed audit.

## C1 — Table 4: ReviewDSE-HPWL vs BO-DSE

**Paper claim:** ReviewDSE-HPWL reduces mean wirelength by 1.78%
compared to the BO-DSE baseline.

**Command:** `make table4`

**What it does:**
1. Reads the BO-DSE baseline results from `artifacts/01-table4-qor/expected/table4.json`.
2. Reads the ReviewDSE-HPWL results from the same file.
3. Computes the difference and compares against the paper's claimed value.
4. Outputs a CSV with per-design QoR metrics.

**Expected output:** All designs pass. The CSV matches Table 4 in the paper.

**Failure modes:**
- Digest mismatch: expected values were modified or evidence is corrupted.
- NULL results: missing trace data in `artifacts/01-table4-qor/traces/`.

## C2 — Table 4: ReviewDSE-GHR

**Paper claim:** ReviewDSE-GHR reduces global route overflow by 1.68%
with 1.11× runtime compared to the baseline.

**Command:** `make table4`

Includes C1 and C2 in a single verification step. The GHR column in the
output CSV should match Table 4.

## C3 — Table 5: Composability Counterexamples

**Paper claim:** Three counterexamples demonstrate that the composability
property of ReviewDSE holds.

**Command:** `make table5`

**What it does:**
1. Loads the three counterexample configurations from
   `artifacts/02-table5-composability/`.
2. Runs each verification and checks the pass/fail verdict.
3. Reports all three verdicts.

**Expected output:** All three verdicts are PASS, matching Table 5.

## C4 — Table 6: Cut-Row Repair Patterns

**Paper claim:** Nine cut-row repair patterns are verified across designs.

**Command:** `make table6`

**Expected output:** Nine pattern verification results, all PASS.

## C5 — AES Smoke Flow

**Paper claim (Sec. V-C):** The full RTL-to-GDS flow runs correctly on
the AES (Nangate45) design using open-source EDA tools.

**Commands:**

```bash
make bootstrap       # Clone Yosys and OpenROAD at pinned commits
make setup           # Build from source
make smoke           # Run the flow
```

**Hardware requirements:** Linux x86-64, 8+ GB RAM, ~10 GB disk.

**Expected output:** `[OK] AES smoke test PASSED`

**What it verifies:**
- Yosys and OpenROAD compile from pinned source revisions.
- Synthesis, floorplanning, placement, and routing complete without errors.
- Final timing and area metrics match the lock file at
  `artifacts/04-aes-smoke/expected/ae_reproduction_lock.json`.

## C6 — Selected Program Source Trees

**Paper claim (Table 4):** 18 program source trees used in the ReviewDSE
experiments are available for audit.

**Command:** `make table4`

The verification includes SHA-256 integrity checks on all 18 source trees
in `artifacts/01-table4-qor/selected-programs/`.

## C7 — ReviewDSE Source Code

**Paper claim (Sec. IV):** The ReviewDSE framework implementation is
provided as open-source code.

**Location:** `src/`

The source code is organized into:
- `src/instrumentation/` — OpenROAD source-level instrumentation hooks
- `src/tracer/` — Optimization path tracing and PPA delta computation
- `src/selector/` — Candidate selection with causal reasoning
- `src/dpl_evolve_agent/` — Agent orchestration and dispatch

No automated verification is provided for source code; this claim is
verified through human inspection.
