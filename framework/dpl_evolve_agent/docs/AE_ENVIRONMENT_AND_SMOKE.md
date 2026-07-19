# AE Environment And AES Smoke Test

This guide is the human-facing entrypoint for the first reproducible Artifact
Evaluation slice. It validates the native OpenROAD detailed-placement baseline
on AES/Nangate45. It does not run Teacher, Student, or any model API.

## What This Validates

The smoke test checks the complete low-cost path:

```text
pinned ORFS/Yosys synthesis
  -> AES 3_4_place_resized.odb
  -> native OpenROAD detailed placement
  -> structured metrics.json
  -> checksum, structure, HPWL, legality, and error validation
```

This is an environment and baseline gate. Passing it does not reproduce the
paper's full multi-agent search or its aggregate claims.

## Resource Expectations

- Linux x86-64 server with Environment Modules or equivalent build tools.
- A prepared ORFS workspace beside the control repository, or `ORFS_ROOT` set
  explicitly.
- At least 8 CPU threads recommended for setup and the smoke run.
- User-writable project and state directories.
- No root privileges.
- Network access only when submodules or the pinned Python package are absent.

Exact source revisions and expected AES values are recorded in
`metadata/ae_reproduction_lock.json`.

## 1. Read-Only Check

On a machine that has already been prepared:

```bash
./scripts/ae/check_environment.sh
```

The checker verifies repository ancestry, exact prepared revisions, nested
submodule commits, Python/PyYAML, binary versions, shared libraries, and
reference binary hashes. Binary hashes are warnings by default because local
compiler and linker differences may change bytes without changing source.
Use `--strict-hashes` only for bit-for-bit comparison with the audited server.

## 2. User-Level Setup

```bash
./scripts/ae/setup_user_environment.sh --jobs 8
```

The setup script:

1. loads the server compiler/EDA modules when available;
2. checks the prepared ORFS and OpenROAD revisions;
3. initializes the exact ORFS Yosys submodule recursively;
4. creates a project virtual environment and pins PyYAML;
5. builds Yosys and OpenROAD only when their expected binaries are missing;
6. writes machine-local exports to
   `$DPL_EVOLVE_STATE_ROOT/ae/environment.sh`;
7. runs the read-only checker.

It is safe to repeat. It does not patch the workspace, delete results, install
system packages, or invoke `sudo`.

## 3. Validate Existing Reference Results

```bash
./scripts/ae/run_aes_smoke.sh --check-only
```

This is fast and does not run EDA. It checks the audited reference flow variant
and baseline tag from the lock file. A successful run reports three `[OK]`
validation lines and exits with status 0.

## 4. Run A Fresh Smoke Test

```bash
./scripts/ae/run_aes_smoke.sh --run --threads 8
```

The wrapper creates a timestamped flow variant, generates the AES input with
the pinned Yosys/OpenROAD binaries, runs only `openroad_dpl_flow`, and validates
the output. Existing variants and baseline tags are never overwritten.

Use `--rebuild` when the run is intended to document a clean rebuild attempt;
it also creates a new timestamped variant and preserves all earlier evidence.

## Expected Result

The strict gate is:

| Check | Expected |
| --- | ---: |
| input ODB SHA-256 | value in the reproduction lock |
| instances | 14,676 |
| instance area | 18,648.2 um^2 |
| global HPWL | 188,569.2 um |
| final HPWL | 176,845.1 um |
| placement violations | none |
| metric errors | 0 |

Runtime is recorded but is not a pass/fail value because it is machine
dependent.

## Failure Interpretation

- Wrong ODB checksum or instance count: synthesis/input drift, usually Yosys.
- Correct ODB but wrong global HPWL: placement input or OpenROAD drift.
- Correct global HPWL but wrong final HPWL: detailed-placement source/binary
  drift.
- Missing shared libraries: server module or runtime loader issue.
- Hash warning with correct source revisions and numerical result: retain the
  warning in provenance; do not treat it alone as a failed reproduction.
