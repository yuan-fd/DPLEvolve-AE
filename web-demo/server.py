#!/usr/bin/env python3
"""
DPLEvolve Artifact Reviewer Console

FastAPI backend for the DPLEvolve Artifact Evaluation web interface.
Provides:
  - Static frontend serving (single-page HTML app)
  - SSH connection testing endpoint (local + paramiko remote)
  - Command execution endpoints (env check, install, minimal test, full repro)
  - WebSocket real-time output streaming
  - Command queue with asyncio.Lock (serialized execution, no dropped jobs)
  - Process tracking for safe cancellation
  - Server status info (uptime, queue length, OS/Python info)
  - Structured error reporting with exit-code interpretation + troubleshooting
"""

import asyncio
import logging
import os
import platform
import re
import shlex
import signal
import sys
import time
from contextlib import asynccontextmanager
from contextlib import suppress
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, field_validator

# ── Structured Logging ───────────────────────────────────────────────────────

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    stream=sys.stderr,
)
logger = logging.getLogger("dplevolve-demo")

# ── Optional: paramiko for real SSH ──────────────────────────────────────────

try:
    import paramiko as _paramiko

    _HAS_PARAMIKO = True
except ImportError:
    _HAS_PARAMIKO = False

# ── Configuration ────────────────────────────────────────────────────────────

AE_ROOT = Path(
    os.environ.get("DPLEVOLVE_AE_ROOT", str(Path(__file__).resolve().parent.parent))
).resolve()
STATIC_DIR = Path(__file__).parent / "static"
TEMPLATE_DIR = Path(__file__).parent / "templates"

WEB_HOST = os.environ.get("DPLEVOLVE_WEB_HOST", "127.0.0.1")
WEB_PORT = int(os.environ.get("DPLEVOLVE_WEB_PORT", "8080"))

SERVER_START_TIME = time.time()
MAX_HISTORY = 50

# ── Pydantic Models ──────────────────────────────────────────────────────────

class SSHConfig(BaseModel):
    host: str = "localhost"
    user: str = ""
    port: int = 22
    key_path: str = ""
    password: str = ""
    root_path: str = ""

    @field_validator("host")
    @classmethod
    def host_must_be_safe(cls, v: str) -> str:
        unsafe = "\n\r\t;&|`$(){}[]\\"
        for ch in unsafe:
            if ch in v:
                raise ValueError(f"Host contains invalid character: {repr(ch)}")
        return v.strip()

    @field_validator("port")
    @classmethod
    def port_in_range(cls, v: int) -> int:
        if not 1 <= v <= 65535:
            raise ValueError("Port must be between 1 and 65535")
        return v

    @field_validator("user")
    @classmethod
    def user_must_be_safe(cls, v: str) -> str:
        unsafe = "\n\r;&|`$(){}[]\\"
        for ch in unsafe:
            if ch in v:
                raise ValueError(f"User contains invalid character: {repr(ch)}")
        return v.strip()

    @field_validator("root_path")
    @classmethod
    def root_path_must_be_single_line(cls, v: str) -> str:
        if "\n" in v or "\r" in v or "\x00" in v:
            raise ValueError("Repository path must be a single line")
        return v.strip()


class RunRequest(BaseModel):
    ssh: Optional[SSHConfig] = None


# ── Global State ─────────────────────────────────────────────────────────────

RUN_STATE = {
    "running": False,
    "current_command": "",
    "start_time": None,
    "exit_code": None,
    "history": [],
    "process": None,  # asyncio.subprocess.Process reference for safe cancellation
    "ssh_channel": None,  # paramiko channel reference for SSH cancellation
}

_ws_clients: list[WebSocket] = []
_cmd_queue: asyncio.Queue = asyncio.Queue()
_queue_worker_task: Optional[asyncio.Task] = None
_queue_lock = asyncio.Lock()


# ── WebSocket Helpers ────────────────────────────────────────────────────────

async def ws_broadcast(msg: dict):
    """Send a JSON message to all connected WebSocket clients."""
    dead = []
    for ws in _ws_clients:
        try:
            await ws.send_json(msg)
        except Exception:
            dead.append(ws)
    for ws in dead:
        if ws in _ws_clients:
            _ws_clients.remove(ws)


async def ws_broadcast_output(text: str, stream: str = "stdout"):
    """Broadcast a single line of command output."""
    await ws_broadcast(
        {
            "type": "output",
            "stream": stream,
            "text": text,
            "timestamp": time.time(),
        }
    )


async def ws_broadcast_status():
    """Broadcast the current run state to all clients."""
    await ws_broadcast(
        {
            "type": "status",
            "running": RUN_STATE["running"],
            "current_command": RUN_STATE["current_command"],
            "start_time": RUN_STATE["start_time"],
            "exit_code": RUN_STATE["exit_code"],
            "history": RUN_STATE["history"][-20:],
            "queue_length": len(_queued_jobs),
            "queued_jobs": [{"id": j.get("id", ""), "label": j.get("label", "")} for j in _queued_jobs],
            "uptime": time.time() - SERVER_START_TIME,
        }
    )


# ── SSH Helpers ──────────────────────────────────────────────────────────────

def _is_local(ssh: Optional[SSHConfig]) -> bool:
    """Check if the SSH config points to localhost."""
    if not ssh or not ssh.host:
        return True
    return ssh.host.strip() in ("localhost", "127.0.0.1", "::1")


def _execution_root(ssh: Optional[SSHConfig]) -> str:
    """Return the repository path on the machine that will run the task."""
    if ssh and not _is_local(ssh) and ssh.root_path:
        return ssh.root_path
    return str(AE_ROOT)


async def _ssh_execute_remote(
    ssh: SSHConfig,
    cmd: list[str],
    cwd: Optional[Path] = None,
) -> int:
    """Execute a command via paramiko SSH and stream output. Returns exit code."""
    if not _HAS_PARAMIKO:
        message = "SSH remote execution requires Paramiko. Install the web-demo requirements."
        RUN_STATE["_last_stderr_lines"].append(message)
        await ws_broadcast_output(f"ERROR: {message}", "stderr")
        return 1

    client = _paramiko.SSHClient()
    client.set_missing_host_key_policy(_paramiko.AutoAddPolicy())

    try:
        connect_kwargs = {
            "hostname": ssh.host,
            "port": ssh.port,
            "timeout": 15,
        }
        if ssh.user:
            connect_kwargs["username"] = ssh.user
        if ssh.key_path:
            expanded = os.path.expanduser(ssh.key_path)
            if os.path.exists(expanded):
                connect_kwargs["key_filename"] = expanded
            else:
                await ws_broadcast_output(
                    f"WARNING: SSH key file not found: {expanded}",
                    "stderr",
                )
        if ssh.password:
            connect_kwargs["password"] = ssh.password

        await asyncio.get_event_loop().run_in_executor(
            None, lambda: client.connect(**connect_kwargs)
        )

        # Quote every argument and the remote path before invoking a shell.
        cmd_str = shlex.join(cmd)
        remote_root = str(cwd) if cwd else _execution_root(ssh)
        cmd_str = f"cd -- {shlex.quote(remote_root)} && {cmd_str}"

        await ws_broadcast_output(f"[SSH] {ssh.user}@{ssh.host} — {cmd_str}\n", "info")

        transport = client.get_transport()
        channel = transport.open_session()
        channel.settimeout(15.0)
        channel.exec_command(cmd_str)

        # Store channel for cancellation support
        RUN_STATE["ssh_channel"] = channel

        async def read_channel(chan, stream_name):
            buf = b""
            while not chan.exit_status_ready():
                if chan.recv_ready():
                    data = chan.recv(4096)
                    if data:
                        buf += data
                        while b"\n" in buf:
                            line, buf = buf.split(b"\n", 1)
                            text = line.decode("utf-8", errors="replace")
                            await ws_broadcast_output(text, stream_name)
                else:
                    await asyncio.sleep(0.1)
            # Drain remaining
            while chan.recv_ready():
                buf += chan.recv(4096)
            if buf:
                text = buf.decode("utf-8", errors="replace").rstrip("\n")
                if text:
                    await ws_broadcast_output(text, stream_name)

        async def read_stderr(chan):
            buf = b""
            while not chan.exit_status_ready():
                if chan.recv_stderr_ready():
                    data = chan.recv_stderr(4096)
                    if data:
                        buf += data
                        while b"\n" in buf:
                            line, buf = buf.split(b"\n", 1)
                            text = line.decode("utf-8", errors="replace")
                            if text.strip():
                                RUN_STATE["_last_stderr_lines"].append(text)
                            await ws_broadcast_output(text, "stderr")
                else:
                    await asyncio.sleep(0.1)
            while chan.recv_stderr_ready():
                buf += chan.recv_stderr(4096)
            if buf:
                text = buf.decode("utf-8", errors="replace").rstrip("\n")
                if text:
                    RUN_STATE["_last_stderr_lines"].append(text)
                    await ws_broadcast_output(text, "stderr")

        await asyncio.gather(read_channel(channel, "stdout"), read_stderr(channel))
        exit_code = channel.recv_exit_status()
        return exit_code

    except Exception as e:
        RUN_STATE["_last_stderr_lines"].append(f"SSH error: {e}")
        await ws_broadcast_output(f"SSH ERROR: {e}", "stderr")
        return 1
    finally:
        RUN_STATE["ssh_channel"] = None
        try:
            client.close()
        except Exception:
            pass


# ── Command Execution ────────────────────────────────────────────────────────

async def run_command(
    cmd: list[str],
    cwd: Optional[Path] = None,
    ssh: Optional[SSHConfig] = None,
    label: str = "",
) -> int:
    """Run a command with real-time output broadcast. Returns the exit code."""
    RUN_STATE["running"] = True
    RUN_STATE["current_command"] = label or " ".join(cmd)
    RUN_STATE["start_time"] = time.time()
    RUN_STATE["exit_code"] = None
    RUN_STATE["process"] = None
    RUN_STATE["ssh_channel"] = None
    RUN_STATE["_last_stderr_lines"] = []
    await ws_broadcast_status()

    await ws_broadcast_output(f"\n─── {RUN_STATE['current_command']} ───\n")

    # Determine execution mode
    use_ssh = not _is_local(ssh)

    if use_ssh:
        # Keep connection testing and execution behavior consistent: remote jobs
        # always use Paramiko and never silently fall back to a different SSH
        # authentication path.
        exit_code = await _ssh_execute_remote(
            ssh, cmd, cwd=Path(_execution_root(ssh))
        )
    else:
        full_cmd = cmd

        try:
            proc = await asyncio.create_subprocess_exec(
                *full_cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                cwd=str(cwd) if cwd else str(AE_ROOT),
                env={**os.environ},
                start_new_session=True,
            )
            RUN_STATE["process"] = proc

            async def read_stream(stream, name):
                while True:
                    line = await stream.readline()
                    if not line:
                        break
                    text = line.decode("utf-8", errors="replace").rstrip("\n")
                    await ws_broadcast_output(text, name)
                    # Retain stderr for the structured failure report.
                    if name == "stderr" and text.strip():
                        RUN_STATE["_last_stderr_lines"].append(text)

            await asyncio.gather(
                read_stream(proc.stdout, "stdout"),
                read_stream(proc.stderr, "stderr"),
            )
            exit_code = await proc.wait()
        except FileNotFoundError:
            await ws_broadcast_output(
                f"ERROR: Command not found: {' '.join(full_cmd)}", "stderr"
            )
            RUN_STATE["_last_stderr_lines"].append(
                f"Command not found: {' '.join(full_cmd)}"
            )
            exit_code = 127
        except Exception as e:
            await ws_broadcast_output(f"ERROR: {e}", "stderr")
            RUN_STATE["_last_stderr_lines"].append(str(e))
            exit_code = 1

    RUN_STATE["exit_code"] = exit_code
    RUN_STATE["running"] = False
    RUN_STATE["process"] = None

    history_entry = {
        "command": RUN_STATE["current_command"],
        "exit_code": exit_code,
        "started": RUN_STATE["start_time"],
        "finished": time.time(),
    }
    RUN_STATE["history"].append(history_entry)

    if len(RUN_STATE["history"]) > MAX_HISTORY:
        RUN_STATE["history"] = RUN_STATE["history"][-MAX_HISTORY:]

    status_icon = "✅" if exit_code == 0 else "❌"
    await ws_broadcast_output(f"\n{status_icon} Exit code: {exit_code}\n")
    await ws_broadcast_status()

    return exit_code


async def _enqueue(
    label: str, cmd: list[str], ssh: Optional[SSHConfig] = None
) -> dict:
    """Enqueue a command into the serial queue. Always puts the job on the queue."""
    global _job_id_counter
    async with _queue_lock:
        was_busy = RUN_STATE["running"] or not _cmd_queue.empty()
        _job_id_counter += 1
        job = {
            "cmd": cmd, "label": label, "ssh": ssh,
            "id": f"job-{_job_id_counter}", "enqueued_at": time.time()
        }
        await _cmd_queue.put(job)
        _queued_jobs.append(job)
        # Waiting positions are one-based and exclude the active job, which the
        # worker removes from _queued_jobs before execution.
        position = len(_queued_jobs) if was_busy else 0
        await ws_broadcast_status()

    if was_busy:
        return {
            "queued": True,
            "position": position,
            "job_id": job["id"],
            "message": f"Queued: '{label}' (waiting position {position}).",
            "label": label,
        }
    return {
        "queued": False,
        "job_id": job["id"],
        "message": f"Starting: '{label}'",
        "label": label,
    }


# ── Queue Worker ─────────────────────────────────────────────────────────────

async def _queue_worker():
    """Process commands from the queue one at a time."""
    while True:
        job = await _cmd_queue.get()
        # Remove from tracked queued list
        job_id = job.get("id", "")
        async with _queue_lock:
            # Check if this job was cancelled before it could start
            if job_id and job_id in _cancelled_job_ids:
                _cancelled_job_ids.discard(job_id)
                _queued_jobs[:] = [j for j in _queued_jobs if j.get("id") != job_id]
                _cmd_queue.task_done()
                await ws_broadcast_output(
                    f"⏭ Skipped cancelled job: '{job.get('label', job_id)}'", "info"
                )
                await ws_broadcast_status()
                continue
            _queued_jobs[:] = [j for j in _queued_jobs if j.get("id") != job_id]
        try:
            await run_command(
                cmd=job["cmd"], label=job.get("label", ""), ssh=job.get("ssh")
            )
            if RUN_STATE.get("_last_stderr_lines") and RUN_STATE.get("exit_code", 0) != 0:
                try:
                    analysis = _analyze_failure(
                        RUN_STATE["exit_code"], RUN_STATE["_last_stderr_lines"]
                    )
                    await ws_broadcast(
                        {
                            "type": "error_detail",
                            **analysis,
                            "command": RUN_STATE.get("current_command", ""),
                            "timestamp": time.time(),
                        }
                    )
                except Exception:
                    # Reporting must never terminate the command worker.
                    logger.exception("Could not build the failure report")
        except asyncio.CancelledError:
            raise
        except Exception as e:
            logger.exception("Queue worker recovered from an unexpected error")
            RUN_STATE["running"] = False
            RUN_STATE["exit_code"] = 1
            await ws_broadcast_output(f"QUEUE ERROR: {e}", "stderr")
            await ws_broadcast_status()
        finally:
            RUN_STATE["_last_stderr_lines"] = []
            _cmd_queue.task_done()


# ── Lifespan ─────────────────────────────────────────────────────────────────

@asynccontextmanager
async def lifespan(app: FastAPI):
    global _queue_worker_task

    # Startup checks
    if _HAS_PARAMIKO:
        logger.info("paramiko available — SSH remote execution enabled")
    else:
        logger.info("paramiko not installed — SSH remote execution disabled (local only)")

    if not AE_ROOT.exists():
        logger.warning(
            "DPLEvolve AE root does not exist: %s — commands may fail", AE_ROOT
        )

    logger.info("Server starting — AE_ROOT=%s", AE_ROOT)
    logger.info("Python %s on %s", platform.python_version(), platform.platform())

    _queue_worker_task = asyncio.create_task(_queue_worker())
    yield
    if _queue_worker_task:
        _queue_worker_task.cancel()
        with suppress(asyncio.CancelledError):
            await _queue_worker_task
    logger.info("Server shutdown complete")


# ── Queue Tracking State ─────────────────────────────────────────────────────

# Tracks queued-but-not-yet-running jobs with IDs for individual cancellation
_queued_jobs: list[dict] = []  # [{"id": str, "label": str, "ssh": ..., "cmd": [...]}]
_job_id_counter = 0
_cancelled_job_ids: set = set()  # IDs of jobs cancelled before the worker could pick them up

# ── FastAPI App ──────────────────────────────────────────────────────────────

app = FastAPI(title="DPLEvolve Artifact Reviewer Console", version="3.0.0", lifespan=lifespan)

if STATIC_DIR.exists():
    app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")


# ── Routes ───────────────────────────────────────────────────────────────────


@app.get("/", response_class=HTMLResponse)
async def index():
    index_path = TEMPLATE_DIR / "index.html"
    if index_path.exists():
        return HTMLResponse(index_path.read_text(encoding="utf-8"))
    return HTMLResponse("<h1>index.html not found</h1>", status_code=404)


@app.websocket("/ws/output")
async def websocket_output(websocket: WebSocket):
    await websocket.accept()
    _ws_clients.append(websocket)
    try:
        await websocket.send_json(
            {
                "type": "status",
                "running": RUN_STATE["running"],
                "current_command": RUN_STATE["current_command"],
                "start_time": RUN_STATE["start_time"],
                "exit_code": RUN_STATE["exit_code"],
                "history": RUN_STATE["history"][-20:],
                "queue_length": len(_queued_jobs),
                "queued_jobs": [{"id": j.get("id", ""), "label": j.get("label", "")} for j in _queued_jobs],
                "uptime": time.time() - SERVER_START_TIME,
            }
        )
    except Exception:
        pass
    try:
        while True:
            try:
                data = await asyncio.wait_for(websocket.receive_text(), timeout=60)
                if data == "ping":
                    await websocket.send_text("pong")
            except asyncio.TimeoutError:
                # Send heartbeat to keep connection alive
                try:
                    await websocket.send_text("pong")
                except Exception:
                    break
    except WebSocketDisconnect:
        pass
    finally:
        if websocket in _ws_clients:
            _ws_clients.remove(websocket)


@app.get("/api/status")
async def get_status():
    return {
        "running": RUN_STATE["running"],
        "current_command": RUN_STATE["current_command"],
        "start_time": RUN_STATE["start_time"],
        "exit_code": RUN_STATE["exit_code"],
        "history": RUN_STATE["history"][-20:],
        "queue_length": len(_queued_jobs),
        "queued_jobs": [{"id": j.get("id", ""), "label": j.get("label", "")} for j in _queued_jobs],
        "uptime": time.time() - SERVER_START_TIME,
    }


@app.get("/api/server-info")
async def get_server_info():
    """Return server-side system information."""
    return {
        "uptime": time.time() - SERVER_START_TIME,
        "python_version": platform.python_version(),
        "os": platform.platform(),
        "ae_root": str(AE_ROOT),
        "ae_root_exists": AE_ROOT.exists(),
        "paramiko_available": _HAS_PARAMIKO,
        "queue_length": len(_queued_jobs),
        "running": RUN_STATE["running"],
        "current_command": RUN_STATE["current_command"],
    }


@app.post("/api/ssh/connect")
async def ssh_connect(config: SSHConfig):
    """Test SSH connection to the configured host."""
    if _is_local(config):
        ready = AE_ROOT.is_dir() and (AE_ROOT / "Makefile").is_file()
        return {
            "success": ready,
            "message": (
                f"Local repository ready: {AE_ROOT}"
                if ready
                else f"Local repository or Makefile not found: {AE_ROOT}"
            ),
            "host": "localhost",
        }

    if not config.root_path:
        return {
            "success": False,
            "message": "Repository path is required for remote execution",
            "host": config.host,
        }

    if not _HAS_PARAMIKO:
        return {
            "success": False,
            "message": "paramiko not installed — SSH remote test unavailable",
            "host": config.host,
        }

    client = _paramiko.SSHClient()
    client.set_missing_host_key_policy(_paramiko.AutoAddPolicy())

    try:
        connect_kwargs = {
            "hostname": config.host,
            "port": config.port,
            "timeout": 15,
        }
        if config.user:
            connect_kwargs["username"] = config.user
        if config.key_path:
            expanded = os.path.expanduser(config.key_path)
            if not os.path.isfile(expanded):
                return {
                    "success": False,
                    "message": f"SSH key file not found on the web server: {expanded}",
                    "host": config.host,
                }
            connect_kwargs["key_filename"] = expanded
        if config.password:
            connect_kwargs["password"] = config.password

        await asyncio.get_event_loop().run_in_executor(
            None, lambda: client.connect(**connect_kwargs)
        )

        # Run the blocking probe off the event loop. If a repository path was
        # supplied, verify it during the same connection test.
        probe = "echo SSH_OK && uname -a"
        if config.root_path:
            quoted_root = shlex.quote(config.root_path)
            quoted_makefile = shlex.quote(f"{config.root_path.rstrip('/')}/Makefile")
            probe += f" && test -d {quoted_root} && test -f {quoted_makefile} && echo ROOT_OK"

        def run_probe():
            _stdin, stdout, stderr = client.exec_command(probe, timeout=15)
            return (
                stdout.read().decode("utf-8", errors="replace").strip(),
                stderr.read().decode("utf-8", errors="replace").strip(),
            )

        out, err = await asyncio.get_event_loop().run_in_executor(None, run_probe)

        if "SSH_OK" in out and (not config.root_path or "ROOT_OK" in out):
            sys_line = out.replace("SSH_OK", "").replace("ROOT_OK", "").strip()
            return {
                "success": True,
                "message": f"SSH connection successful — {sys_line}" if sys_line else "SSH connection successful",
                "host": config.host,
            }
        if config.root_path and "ROOT_OK" not in out:
            return {
                "success": False,
                "message": f"Connected, but the repository or Makefile was not found: {config.root_path}",
                "host": config.host,
            }
        else:
            return {"success": False, "message": err or "Connection failed", "host": config.host}
    except Exception as e:
        return {"success": False, "message": str(e), "host": config.host}
    finally:
        try:
            client.close()
        except Exception:
            pass


TASKS: dict[str, tuple[str, list[str]]] = {
    "doctor": ("Environment Doctor", ["make", "doctor"]),
    "bootstrap": ("Bootstrap Pinned Workspace", ["make", "bootstrap"]),
    "build-tools": ("Build Pinned EDA Tools", ["make", "build-tools"]),
    "check": ("Inspect Prepared Environment", ["make", "check"]),
    "prepare-inputs": ("Prepare Table 4 Inputs", ["make", "prepare-paper-inputs"]),
    "fetch-table6-data": ("Download Table 6 Inputs", ["make", "fetch-table6-data"]),
    "table5-data-check": ("Check Table 5 Inputs", ["make", "check-table5-data"]),
    "table4-fresh": ("Reproduce Table 4", ["make", "reproduce-table4"]),
    "table5-fresh": ("Reproduce Table 5", ["make", "reproduce-table5"]),
    "table6-fresh": ("Reproduce Table 6", ["make", "reproduce-table6"]),
    "figures": ("Reproduce Figures 4 and 5", ["make", "reproduce-figures"]),
    "ariane-diagnostic": (
        "Reproduce Ariane Diagnostic",
        ["make", "reproduce-ariane-diagnostic"],
    ),
    "search-paper": (
        "Run Complete ReviewDSE Search",
        ["make", "reproduce-paper-search", "ACKNOWLEDGE_LLM_COST=yes"],
    ),
}


@app.post("/api/run/task/{task_name}")
async def run_named_task(task_name: str, req: RunRequest):
    """Run one of the reviewer-facing, fixed Make targets."""
    if req.ssh and not _is_local(req.ssh) and not req.ssh.root_path:
        from fastapi import HTTPException

        raise HTTPException(
            status_code=400,
            detail="Repository path is required for remote execution",
        )
    task = TASKS.get(task_name)
    if task is None:
        from fastapi import HTTPException

        raise HTTPException(status_code=404, detail=f"Unknown task: {task_name}")
    label, command = task
    return await _enqueue(label, command, req.ssh)


@app.post("/api/run/cancel")
async def cancel_run():
    """Signal the active command; run_command owns final state and history."""
    if not RUN_STATE["running"]:
        return {"cancelled": False, "message": "No command is running"}

    cancelled = False

    # Try local process cancellation first
    proc = RUN_STATE.get("process")
    if proc is not None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
            await ws_broadcast_output("\nCancellation requested by the reviewer.\n", "info")
            cancelled = True
        except ProcessLookupError:
            return {"cancelled": False, "message": "The process already exited"}
        except Exception as e:
            await ws_broadcast_output(f"\nCancel error: {e}\n", "stderr")

    # Try SSH channel cancellation (paramiko)
    if not cancelled:
        chan = RUN_STATE.get("ssh_channel")
        if chan:
            try:
                chan.close()
                await ws_broadcast_output("\nRemote cancellation requested.\n", "info")
                cancelled = True
            except Exception as e:
                await ws_broadcast_output(f"\n⚠️  SSH cancel error: {e}\n", "stderr")

    if not cancelled:
        return {
            "cancelled": False,
            "message": "The task is starting or already finishing; no process is available",
        }

    return {"cancelled": True, "message": "Cancellation requested"}


# ── Queue Information & Waiting-job Cancellation ─────────────────────────────


@app.get("/api/queue/info")
async def get_queue_info():
    """Return detailed queue information for visualization."""
    jobs = []
    async with _queue_lock:
        for job in _queued_jobs:
            jobs.append({
                "id": job.get("id", ""),
                "label": job.get("label", ""),
            })
    return {
        "running": RUN_STATE["running"],
        "current_command": RUN_STATE["current_command"],
        "queue_length": len(_queued_jobs),
        "queued_jobs": jobs,
    }


@app.post("/api/queue/cancel/{job_id}")
async def cancel_queued_job(job_id: str):
    """Cancel a specific queued (not-yet-running) job by its ID.

    Marks the job as cancelled in _cancelled_job_ids so the queue worker skips it.
    """
    async with _queue_lock:
        for i, job in enumerate(_queued_jobs):
            if job.get("id") == job_id:
                removed = _queued_jobs.pop(i)
                _cancelled_job_ids.add(job_id)
                await ws_broadcast_output(
                    f"⏹ Cancelled queued job: '{removed['label']}'", "info"
                )
                await ws_broadcast_status()
                return {"cancelled": True, "label": removed["label"]}
    return {"cancelled": False, "message": f"Job '{job_id}' not found in queue"}


# ── Result Export ─────────────────────────────────────────────────────────────


@app.get("/api/export/results")
async def export_results():
    """Export status and history as JSON."""
    return {
        "export_version": "1.0",
        "server": {
            "uptime": time.time() - SERVER_START_TIME,
            "python_version": platform.python_version(),
            "os": platform.platform(),
            "ae_root": str(AE_ROOT),
        },
        "status": {
            "running": RUN_STATE["running"],
            "current_command": RUN_STATE["current_command"],
            "exit_code": RUN_STATE["exit_code"],
        },
        "history": RUN_STATE["history"][-50:],
        "exported_at": time.time(),
    }


# ── Error Analysis ───────────────────────────────────────────────────────────

# Common exit code meanings for AE commands
_EXIT_CODE_MEANINGS = {
    0: "Success — command completed normally",
    1: "General error — check output for details",
    2: "Make/Shell syntax error or misuse",
    126: "Command found but not executable (permission denied)",
    127: "Command not found — missing dependency or typo",
    -15: "Cancelled by user (SIGTERM)",
    -9: "Killed (SIGKILL) — possible OOM or manual kill",
}

# Common error patterns in AE commands and their likely causes
_ERROR_PATTERNS = [
    (r"python3: command not found", "Python 3 is required but not in PATH. Set DPL_EVOLVE_PYTHON env var."),
    (r"make: command not found", "GNU Make is not installed. Install: yum install make / apt install make"),
    (r"Permission denied", "File permissions issue. Try: chmod +x <script> or run with bash <script>"),
    (r"No such file or directory", "Missing file or directory. Check AE_ROOT path and run 'make bootstrap' first."),
    (r"module: command not found", "Environment Modules not installed. This is optional — the check may still pass."),
    (r"cannot find.*ORFS|ORFS_ROOT", "OpenROAD-flow-scripts not found. Run 'make bootstrap' to clone dependencies."),
    (r"fatal:.*not a git repository", "Git repository expected but not found. Ensure the AE repo is a git checkout."),
    (r"No space left on device", "Disk full. Free up space or increase disk allocation."),
    (r"Killed", "Process was killed — likely OOM. Reduce threads with SMOKE_THREADS=2 or increase RAM."),
    (r"(?:cmake|gcc|g\+\+): command not found", "C/C++ build tools missing. Install: yum groupinstall 'Development Tools'"),
]


def _analyze_failure(exit_code: int, stderr_lines: list[str]) -> dict:
    """Analyze a failed command and produce a structured error report."""
    meaning = _EXIT_CODE_MEANINGS.get(
        exit_code, f"Exit code {exit_code} — see output for details"
    )

    suggestions = []
    stderr_text = "\n".join(stderr_lines[-20:])  # last 20 lines of stderr

    for pattern, suggestion in _ERROR_PATTERNS:
        if re.search(pattern, stderr_text, re.IGNORECASE):
            suggestions.append(suggestion)

    if not suggestions and exit_code != 0:
        suggestions.append(
            "Run 'make doctor' and follow the Suggested commands printed in the terminal; "
            "then retry this task. Doctor is read-only and does not install anything."
        )

    return {
        "exit_code": exit_code,
        "meaning": meaning,
        "suggestions": suggestions,
        "stderr_tail": stderr_lines[-15:],  # last 15 lines for context
    }


# ── Entrypoint ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    import uvicorn

    uvicorn.run(
        "server:app", host=WEB_HOST, port=WEB_PORT, reload=False, log_level="info"
    )
