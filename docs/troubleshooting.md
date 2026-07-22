# Troubleshooting

## Start with the read-only preflight

```bash
make doctor
```

Doctor works before ORFS or the EDA binaries exist. It never installs packages.
On Rocky/RHEL-family systems it prints commands for missing dependencies; review
them with the system administrator before running `sudo`.

## Environment preparation

### ORFS is missing

```bash
make bootstrap
```

This needs network access to fetch pinned upstream repositories. Configure the
site Git proxy first when working behind a firewall.

### A pinned binary is missing or compilation fails

```bash
make build-tools THREADS=16
```

Check `docs/environment.md` and the first compiler error. On a memory-constrained
host, reduce `THREADS`; do not rely on the obsolete 8/16 GiB smoke estimate.
The authors used Rocky Linux 8.10 with 314 GiB RAM.

### A non-default workspace layout is used

Set paths on the command or in an untracked `env.local.sh`:

```bash
ORFS_ROOT=/path/to/OpenROAD-flow-scripts \
DPL_EVOLVE_STATE_ROOT=/path/to/dpl_evolve_state \
make check
```

`make check` is for an already prepared environment. On a clean checkout use
`make doctor` first.

## Fresh Table 4 failures

### Input ODB not found

```bash
make prepare-paper-inputs CASE=aes_nangate45 THREADS=8
```

Remove `CASE=...` to generate all nine. Products are under the stable
`paper9_place` flow variant. The preparation command records ODB hashes and
ORFS/OpenROAD revisions.

### BO environment is missing

```bash
make setup-bo
make reproduce-bo CASE=aes_nangate45 THREADS=8
```

BO is large: the complete run performs 3,600 placements. Reduce
`MAX_CONCURRENT_CASES` or run one `CASE=` at a time if memory is tight.

### Frozen-source replay reports an input checksum mismatch

Do not bypass the check. Confirm the flow variant and pinned revisions, rerun
`make prepare-paper-inputs`, and compare the hash record under
`$DPL_EVOLVE_STATE_ROOT/paper_reproduction/inputs/`. Only AES Nangate45 has a
retained paper-time input hash; report the other eight generated hashes with the
revision record.

### Fresh Table 4 differs from the reference

Keep the fresh TSV and all underlying metrics. Record the compiler, host,
thread count, input ODB hash, ORFS/OpenROAD revisions, and exact per-case delta.
Do not modify `artifacts/*/expected/` or substitute `make audit-archive` for the
failed execution.

## Table 5/6 reports `BLOCKED`

This is intentional when exact paper data is missing:

```bash
make paper-data-check
```

Install the exact ODBs and source trees using `docs/paper-data-layout.md` and
set `PAPER_DATA_ROOT` if they live outside the checkout. The reproduction
commands never fall back to packaged TSV/JSON transcriptions.

## ReviewDSE search failures

### Paper profile refuses to start

Run Level 1 first:

```bash
make reproduce-level1 ACKNOWLEDGE_LLM_COST=yes
```

The paper Level 2 profile requires an immutable Level 1 evidence packet. Use
`make plan-level1` and `make plan-dse-paper` to inspect commands without model
calls.

### API authentication or budget failure

Verify the model provider configuration outside the repository and start with
`make run-dse-small`. Never commit API keys. The full paper profile is
deliberately gated by `ACKNOWLEDGE_LLM_COST=yes` because it is extremely costly.

## Archive audit failures

`make audit-archive` checks the integrity of packaged records. A digest mismatch
usually means the checkout is modified or corrupt. Inspect `git status` and
restore from a clean clone; do not edit expected values to silence the check.

## Optional toolchain diagnostic

```bash
make toolchain-smoke THREADS=8
```

Use this only to diagnose one AES default flow. It does not run BO, selected
ReviewDSE programs, or the paper search and therefore cannot replace a paper
experiment.
