# DPLEvolve reviewer console

The console is an optional browser frontend to the same paper-experiment Make
targets documented in the root README. It does not contain a separate
verification logic and it does not turn archived-number checks into fresh
reproduction.

## Start

```bash
cd DPLEvolve-AE
bash web-demo/start.sh
```

Open `http://127.0.0.1:8080`. On a remote server, keep the service bound to
loopback and tunnel it from the reviewer's laptop:

```bash
ssh -N -L 8080:127.0.0.1:8080 USER@SERVER
```

Do not expose the console to the public Internet: it can launch builds and
experiments.

## Recommended order

1. Run `make doctor`.
2. Run `bootstrap`, `build-tools`, and `prepare-paper-inputs`.
3. Run `validate-evaluator` to produce a fresh four-stage metric trajectory.
4. Run a Table 4 default, BO, or ReviewDSE fixed-source replay.
5. Run `paper-data-check` before Table 5/6.
6. Use `audit-archive` only as a secondary integrity check.

Full BO is 3,600 placements. Full ReviewDSE search is not exposed as a one-click
browser action because it requires an explicit token-cost acknowledgement; use
`make plan-dse-paper` and the guarded terminal command from the root README.

## Fixed API task names

The backend accepts only predefined tasks:

`doctor`, `check`, `bootstrap`, `build-tools`, `prepare-inputs`,
`validate-evaluator`, `default`, `setup-bo`, `bo`, `replay-hpwl`, `replay-ghr`,
`paper-data-check`, `table5-fresh`, `table6-fresh`, `dse-plan`,
`level1-plan`, `audit-archive`, `toolchain-smoke`, and `full`.

Commands are serialized, streamed over WebSocket, and recorded in the in-memory
session history. SSH mode runs those same fixed commands on the selected remote
repository path.
