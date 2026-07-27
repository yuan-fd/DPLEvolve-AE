# Troubleshooting

## Web Demo cannot be opened

Start the service on the evaluation server:

```bash
bash web-demo/start.sh
```

For a remote server, the browser must connect through a tunnel created on the
reviewer's laptop:

```bash
ssh -N -L 8080:127.0.0.1:8080 USER@SERVER
```

Then open `http://127.0.0.1:8080` locally. If port 8080 is occupied, choose a
different server port and tunnel it explicitly:

```bash
DPLEVOLVE_WEB_PORT=8090 bash web-demo/start.sh
ssh -N -L 8090:127.0.0.1:8090 USER@SERVER
```

Do not expose the console directly to the Internet.

## Doctor reports missing tools

```bash
make doctor
```

Doctor never installs packages. Review its Rocky/RHEL-family suggestion and
use the site's normal administrator process. SWIG, bison, flex, CMake, GCC/G++,
rsync, Python venv, and Git are common missing prerequisites.

## ORFS or pinned binaries are missing

```bash
make bootstrap
make build-tools THREADS=16
make check
```

Bootstrap requires network access. If compilation runs out of memory, lower
`THREADS`. The complete campaign should not be planned around obsolete 8/16 GiB
memory estimates.

## A Table 4 input ODB is missing

```bash
make prepare-paper-inputs CASE=aes_nangate45 THREADS=8
```

Remove `CASE=` to prepare all nine. Inputs are written to the stable
`paper9_place` flow variant.

## BO environment is missing

```bash
make setup-bo
make reproduce-bo CASE=aes_nangate45 THREADS=8
```

The complete BO experiment runs 3,600 placements. Start with one case and
reduce concurrency if the host is constrained.

## A fresh result differs numerically

Do not edit `artifacts/*/expected/`. Preserve the fresh metrics and record:

- case and exact command;
- input path and available hash;
- compiler, ORFS/OpenROAD revision, and thread count;
- fresh HPWL delta, runtime ratio, legality, and configured tolerance.

Missing paper-time hashes do not by themselves invalidate a run. The important
questions are whether the execution was complete and legal and whether the new
result lies within the documented scientific window.

## Table 6 reports missing data

```bash
make fetch-table6-data
make check-table6-data
```

For a private GitHub repository, authenticate with `gh auth login` if anonymous
release download returns 404. The fetch target retries through the GitHub CLI.

## Table 5 snapshot verification fails

Run `make check-table5-data` and use the reported path to identify a missing or
modified snapshot file. Restore `artifacts/02-table5-composability/programs/`
from the evaluated Git commit or Zenodo deposit; do not regenerate the manifest
to bless a local edit. If `--check-inputs` fails instead, run
`make prepare-table5-inputs THREADS=10` to regenerate the three ORFS inputs.

## Paper-profile search refuses to start

First create the public Level 1 reconstruction packet:

```bash
make reproduce-level1 ACKNOWLEDGE_LLM_COST=yes
```

Then launch Level 2 with a stable prefix:

```bash
make run-dse-paper ACKNOWLEDGE_LLM_COST=yes \
  DSE_RUN_PREFIX=review_run_01
```

The launcher validates the Level 1 Markdown/JSON packet before starting. Use
`make plan-level1` and `make plan-dse-paper` to inspect both configured
commands without dispatching them; this inspection is not an alternative
method path.

## API authentication or budget failure

Configure the model provider outside the repository and test
`make run-dse-small` first. Never commit API keys. The full search is
deliberately gated because its reported token budget is very large.

## Need only an installation diagnostic

```bash
make toolchain-smoke THREADS=8
```

This exercises one AES default path. It does not run BO, selected ReviewDSE
programs, Table 6, or the Teacher/Student search.
