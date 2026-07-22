# Environment and resource planning

## Author reference machine

The paper experiments were run on:

- Rocky Linux 8.10, x86-64;
- 2 × Intel Xeon Platinum 8462Y+;
- 64 physical / 128 logical CPU cores total;
- 314 GiB RAM;
- 22 TiB `/home` filesystem;
- no GPU requirement.

This is a reproducibility reference, not a minimum specification. Peak use
depends strongly on the target size and selected parallelism. OpenROAD alone
can exceed 2 GiB; parallel compilation, four concurrent BO trials, multiple
cases, and large Ariane/SWERV/BPQUAD inputs require substantially more memory
and disk than the old 8/16 GiB and 10 GiB smoke recommendations.

On a smaller machine, reproduce one `CASE=` at a time and lower script
parallelism. Record CPU model, physical/logical cores, RAM, filesystem, thread
count, and observed peak memory in the evaluation report.

## Host tools

`make doctor` is read-only and checks the host before ORFS is present. The
pinned build requires a modern C++ compiler, CMake, GNU Make, Bash, Python,
Git, rsync, bison, flex, SWIG, and the OpenROAD/Yosys build dependencies.
Doctor prints a Rocky/RHEL-family `dnf` suggestion when commands are missing;
the artifact never runs `sudo`.

Exact revisions are recorded in `provenance/source-commits.json`:

- OpenROAD-flow-scripts prepared tree;
- OpenROAD prepared tree;
- Yosys commit;
- framework base commit;
- Python/PyYAML version.

Prepare and inspect them with:

```bash
make bootstrap
make build-tools THREADS=16
make check
```

## Storage locations

- ORFS input and experiment products: `$ORFS_ROOT/flow/{results,reports,logs}`
- builds, DSE rounds, BO sweeps, and summaries: `$DPL_EVOLVE_STATE_ROOT`
- retained Table 6 DEF/V/SDC/source data and any future Table 5 config/source
  recovery: `$PAPER_DATA_ROOT`
- compact immutable archive inputs/expected values: `artifacts/`

Fresh reproduction never overwrites `artifacts/*/expected/`.
