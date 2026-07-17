# Invariants

Non-negotiable rules that must hold before, during, and after every experiment.

---

## Build Invariants

### I-BUILD-1: Pinned Yosys
Yosys used for synthesis MUST be the pinned version.
- Commit: `8449dd4700821ea021b241a6addaaf8ccd171dfc`
- Version: 0.64
- Check: `$YOSYS_EXE -V` must report this commit

**Violation**: Using a different Yosys version changes the synthesized netlist.
This was the root cause of the 8.88% HPWL deviation in Phase 2.

### I-BUILD-2: Pinned OpenROAD
OpenROAD binary MUST be built from the pinned commit with DPLEvolve overlay.
- Commit: `d5ff63abe1a1470581feef22063362641b32e41b`
- Overlay: `patches/openroad_dpl_evolve_base.patch`

**Violation**: Different OpenROAD versions have different detailed-placement
behavior. Results are not comparable.

### I-BUILD-3: Project-Local Installation
All build artifacts MUST go under `$DPL_EVOLVE_STATE_ROOT/`.
No system-wide installation, no sudo, no `/usr/local/`.

---

## Run Invariants

### I-RUN-1: Timestamped Isolation
Every experiment run MUST use a unique, timestamped identifier.
Existing results MUST NEVER be overwritten.

**Correct**: `FLOW_VARIANT="ae_baseline_aes_20260718_143022"`
**Wrong**: `FLOW_VARIANT="baseline"` (overwrites)

### I-RUN-2: Input Checksum Before Run
Before running placement, verify the input ODB checksum against
`provenance/original-artifact-checksums.txt`.

**Violation**: Running on a wrong input and reporting results as if they
match the reference is data fabrication.

### I-RUN-3: Random Seed Recording
If an experiment uses random seeds, record every seed in the run manifest.

### I-RUN-4: Thread Count Recording
Record the exact thread count used. Thread count affects global placement
in some configurations.

### I-RUN-5: Complete Error Capture
Always capture both stdout and stderr. Never filter or truncate logs.
A run with hidden errors is worse than a declared failure.

---

## Metric Invariants

### I-METRIC-1: Pin-Based HPWL
HPWL MUST be pin-based (not net-based, not Steiner-tree-based).
The Phase 2 audit confirmed this is the case in the reference pipeline.

### I-METRIC-2: Final Stage HPWL
HPWL compared to paper claims MUST be from the final placement stage
(after detailed placement, improve, and optimize mirroring), not from
an intermediate stage.

- ❌ Global HPWL (before detailed placement)
- ❌ Legalized HPWL (before improve_placement)
- ✅ Final HPWL (after optimize_mirroring)

### I-METRIC-3: Per-Design Accounting
Token counts, runtimes, and HPWL improvements MUST be reported per-design,
not just as aggregates. Averages without per-design breakdowns hide variance.

---

## Provenance Invariants

### I-PROV-1: Commit + Binary Hash
Every result directory MUST record:
- Source repository commits
- Binary SHA-256 hashes
- The exact command executed
- Start time and end time

### I-PROV-2: Machine Provenance
Before starting any experiment, run `make provenance` to capture the
machine state.

### I-PROV-3: Reference vs Reproduced Separation
`results/reference/` (paper's results) and `results/reproduced/` (your results)
MUST remain separate. Never mix them, never copy one to the other.

---

## Agent Invariants

### I-AGENT-1: Read-Only Except Output
Agents may read any file freely but may only write to:
- `results/reproduced/`
- `results/tables/`
- `$DPL_EVOLVE_STATE_ROOT/`
- `provenance/current-machine.json`

### I-AGENT-2: No Fabrication
Agents MUST NOT:
- Invent metric values
- Copy reference results as reproduced
- Cherry-pick successful runs
- Suppress failure logs

### I-AGENT-3: No Budget Expansion
Agents MUST NOT:
- Add cases to experiments
- Increase iteration counts
- Extend timeouts without explicit instruction
- Retry indefinitely after failures

### I-AGENT-4: No Secret Leakage
Agents MUST NOT:
- Write API keys to files
- Include tokens in logs
- Include credentials in error output

---

## Checksum Invariants

### I-CSUM-1: AES Input ODB
```
SHA-256: d8e58c2422cf54894ca7debc67c9aed2d877d8c2d0288db89303411dc1240adc
```
This is the canonical AES Nangate45 input for all experiments.

### I-CSUM-2: Yosys Binary (Reference)
```
SHA-256: 0024df9b188fd031f2caf6f5334ba3632831e4f48a73a66867f0f5332e2df667
```

### I-CSUM-3: OpenROAD Binary (Reference)
```
SHA-256: f26d9b88ff246c114249a06d25f9098b50f16e99f59db22bc2d823bbf91cb775
```

Note: Binary hashes are compiler/linker dependent. These are reference values
from the Phase 2 audit machine. Cross-machine reproduction may differ.
