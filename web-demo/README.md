# DPLEvolve Artifact Reviewer Console

This directory contains the browser-based reviewer interface for the DPLEvolve
MLCAD 2026 artifact. It runs a fixed set of reviewer-facing Make targets,
streams their output, records run history, and presents an architecture-led
paper and artifact deck. The primary evaluation path does not require a GPU, EDA license, or
model API key.

## What the console provides

- Local or remote-SSH execution of fixed artifact commands
- Serialized job queue with removal of waiting jobs
- Live stdout/stderr over WebSocket
- Active-process cancellation and run history
- Structured failure explanations
- Downloadable terminal logs and JSON session exports
- Eight-slide English architecture and method presentation
- An embedded reviewer connection and verification guide

The backend is FastAPI. The frontend is a dependency-free HTML/CSS/JavaScript
application.

## Directory layout

```text
web-demo/
├── README.md                         # Reviewer and operator guide
├── requirements.txt                  # Web and SSH dependencies
├── server.py                         # FastAPI, WebSocket, queue, and SSH backend
├── start.sh                          # One-command local startup
├── static/
│   └── dplevolve-architecture.png    # Slides architecture figure
├── templates/
│   └── index.html                    # English single-page reviewer interface
└── tests/
    └── test_server.py                # Queue, failure, and cancellation regressions
```

Virtual environments, bytecode caches, and generated artifact outputs are
runtime-only and excluded by the repository `.gitignore`.

## Requirements

- Linux x86-64
- Python 3.11 or newer
- Bash and GNU Make
- Network access for the initial web dependency installation
- Network access during `make bootstrap` and parts of `make setup`

Remote execution uses Paramiko, which is included in `requirements.txt`.

## Start the console

From the artifact repository:

```bash
git clone https://github.com/yuan-fd/DPLEvolve-AE.git
cd DPLEvolve-AE/web-demo
bash start.sh
```

For a private GitHub repository, accept the collaborator invitation and
authenticate Git before cloning; private repositories cannot be cloned
anonymously.

The script creates `web-demo/.venv`, installs the web dependencies, discovers
the repository root, and starts the service at:

```text
http://127.0.0.1:8080
```

Manual startup is also supported:

```bash
cd DPLEvolve-AE/web-demo
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
python server.py
```

The server listens only on localhost by default. This is intentional: the
console can start builds and should not be exposed directly to an untrusted
network.

## Connect as a reviewer

### Option A: browser and artifact are on the same machine

1. Start the console with `bash start.sh`.
2. Open `http://127.0.0.1:8080`.
3. Leave **Execution machine** set to **This machine**.
4. Select **Test connection**. The console reports the detected AE root.

### Option B: artifact is on a remote evaluation machine

Start the console on the evaluation machine. On the reviewer's laptop, create
an SSH tunnel:

```bash
ssh -N -L 8080:127.0.0.1:8080 reviewer@evaluation-host
```

Run the command on the reviewer's computer. Keep it open (`-N` means tunnel
only, with no remote shell), browse to `http://127.0.0.1:8080` on the laptop,
and use **This machine** execution in the console. “This machine” means local to the web
server, which is the remote evaluation machine in this arrangement.

This is the recommended remote-review workflow because the browser traffic is
encrypted by SSH and the console remains bound to localhost.

### Option C: console and execution environment are separate machines

Select **Another machine via SSH** in the console and provide:

- SSH host, user, and port;
- absolute path to `DPLEvolve-AE` on the execution host;
- a private-key path readable by the web-server process, or a password.

Select **Test connection** before submitting work. The test verifies both SSH
access and the repository directory. The remote path must be absolute; `~` is
not expanded inside the quoted remote command.

Passwords and SSH configuration are kept only in process memory while the job
is queued or running. They are never returned by status/export endpoints and
are not stored in browser local storage.

## Suggested verification sequence

### 1. Diagnose the reviewer machine

Run the read-only doctor before any verification:

```bash
make doctor
```

If Make itself is missing, use `bash scripts/human/doctor.sh` from the
repository root.

It does not install or modify anything. If a prerequisite is missing, copy the
Ubuntu/Debian or Rocky/RHEL command printed under `Suggested commands` into a
normal terminal, review it, run it manually, and then repeat `make doctor`.

For a fully prepared source-rebuild environment, use the stricter check:

```bash
make doctor-smoke
```

### 2. Primary packaged-evidence check

Run:

```bash
make evidence
```

This invokes `make table4`, `make table5`, and `make table6`. It should finish
in under five seconds on a normal machine and end with:

```text
[PASS] All packaged paper-evidence bundles passed
```

Download the terminal log after the run. This is the minimum recommended
paper-facing verification.

### 3. Inspect individual claims

Run the table targets independently if a more focused log is useful:

```bash
make table4
make table5
make table6
```

Then inspect:

- `artifacts/01-table4-qor/expected/` and `traces/`;
- the 18 trees and SHA-256 manifests under `selected-programs/`;
- `artifacts/02-table5-composability/`;
- `artifacts/03-table6-cutrow/`;
- `docs/claims-to-artifacts.md` for the full claim mapping.

### 4. Check the archived smoke result

```bash
make smoke-check
```

This validates the prepared reference run without performing a new EDA flow.

### 5. Optional full toolchain reproduction

On a Linux x86-64 machine with roughly 10 GB free disk and 8 GB RAM or more:

```bash
make bootstrap
make setup
make check
make smoke
```

The final command should end with:

```text
[OK] AES smoke test PASSED
```

`make bootstrap` and `make setup` are one-time preparation steps. The console's
**Full reproduction** action runs bootstrap, setup, and smoke in sequence, but
running the four explicit steps above makes failures easier to diagnose.

## Make target reference

| Target | Purpose | Typical cost | Side effects |
|---|---|---:|---|
| `make doctor` | Diagnose evidence/web prerequisites and report exact remediation commands | < 1 s | Read-only |
| `make doctor-smoke` | Strictly require rebuild tools, ORFS, resources, and EDA binaries | < 1 s | Read-only |
| `make evidence` | Verify all Table 4/5/6 packaged claims | < 5 s | No network; read-only checks |
| `make table4` | Verify QoR, tolerances, manifests, and 18 selected source trees | < 5 s | No network |
| `make table5` | Verify three composability counterexamples | < 1 s | No network |
| `make table6` | Verify nine cut-row cases across three designs | < 1 s | No network |
| `make check` | Report tools, paths, revisions, binary hashes, and shared libraries | < 1 s | Read-only |
| `make bootstrap` | Create the pinned sibling ORFS/OpenROAD workspace | ~2 min | Downloads and writes outside the repository |
| `make setup` | Create the Python environment and build Yosys/OpenROAD | 10–30 min | Build output and Python packages |
| `make smoke` | Run a fresh AES/Nangate45 EDA flow and validate it | 2–5 min | Writes ORFS results, reports, and logs |
| `make smoke-check` | Validate the archived reference smoke run | Seconds | Read-only |
| `make test` | Run structure, integration, and Python unit tests | Varies | May create test output |
| `make clean` | Remove generated artifact output, preserving inputs/expected data | Seconds | Destructive to generated output |

If memory is constrained, start the smoke flow with fewer threads outside the
web interface:

```bash
SMOKE_THREADS=2 make smoke
```

## Web API

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/` | Reviewer interface |
| `GET` | `/api/status` | Active task, queue, and history |
| `GET` | `/api/server-info` | Runtime and AE-root information |
| `POST` | `/api/ssh/connect` | Test local or remote execution access |
| `POST` | `/api/run/task/{name}` | Queue a fixed reviewer task |
| `POST` | `/api/run/cancel` | Request cancellation of the active task |
| `GET` | `/api/queue/info` | Detailed waiting queue |
| `POST` | `/api/queue/cancel/{job_id}` | Remove a waiting task |
| `GET` | `/api/export/results` | Export session metadata and history |
| `WS` | `/ws/output` | Live output and status stream |

Valid task names are `doctor`, `doctor-smoke`, `check`, `bootstrap`, `setup`, `evidence`, `table4`,
`table5`, `table6`, `smoke`, `smoke-check`, and `full`. Compatibility endpoints
for the earlier demo remain available.

## Configuration

| Environment variable | Default | Description |
|---|---|---|
| `DPLEVOLVE_AE_ROOT` | Parent of `web-demo/` | Artifact repository root |
| `DPLEVOLVE_WEB_HOST` | `127.0.0.1` | Web bind address |
| `DPLEVOLVE_WEB_PORT` | `8080` | Web port |

For a different local port:

```bash
DPLEVOLVE_WEB_PORT=8090 bash start.sh
```

Binding to `0.0.0.0` is possible but not recommended because the console does
not implement user accounts. Prefer an SSH tunnel.

## Troubleshooting

- **Port already in use:** set `DPLEVOLVE_WEB_PORT` to another port and update
  both sides of the SSH tunnel.
- **Remote key not found:** the key path is resolved on the machine running the
  web server, not in the browser.
- **Remote repository not found:** enter an absolute path in the Remote SSH
  form and test the connection again.
- **A command or package is missing:** run `make doctor`, review its suggested
  command, execute that command manually in a normal terminal, and run Doctor again.
- **Prepared-environment check reports missing EDA tools:** run `make bootstrap`
  and `make setup`, then repeat `make check`.
- **Smoke run is out of memory:** run `SMOKE_THREADS=1 make smoke` or
  `SMOKE_THREADS=2 make smoke` in a shell.
- **A task fails:** the worker remains available for subsequent jobs. Review
  the structured failure card and terminal stderr, then retry the relevant
  preparation step.

Additional artifact-specific guidance is available in
`../docs/troubleshooting.md`, `../docs/quickstart.md`, and
`../docs/claims-to-artifacts.md`.
