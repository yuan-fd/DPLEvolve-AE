from __future__ import annotations

import os
import signal
import subprocess
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Mapping, Sequence

if __package__ in (None, ""):
    from lock import DirectoryLock
else:
    from .lock import DirectoryLock


TAIL_BYTES = 32768


@dataclass
class CommandResult:
    command: list[str]
    cwd: str
    returncode: int
    elapsed_seconds: float
    peak_rss_bytes: int
    timed_out: bool
    memory_exceeded: bool
    stdout_tail: str
    stderr_tail: str
    lock_dir: str | None = None

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


def _read_tail(path: Path, limit: int = TAIL_BYTES) -> str:
    if not path.exists():
        return ""
    with path.open("rb") as fh:
        fh.seek(0, os.SEEK_END)
        size = fh.tell()
        fh.seek(max(0, size - limit))
        data = fh.read()
    return data.decode("utf-8", errors="replace")


def _ps_snapshot() -> dict[int, tuple[int, int]]:
    try:
        output = subprocess.check_output(
            ["ps", "-e", "-o", "pid=,ppid=,rss="],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        return {}

    rows: dict[int, tuple[int, int]] = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        try:
            pid = int(parts[0])
            ppid = int(parts[1])
            rss_kb = int(parts[2])
        except ValueError:
            continue
        rows[pid] = (ppid, rss_kb * 1024)
    return rows


def process_tree_rss_bytes(root_pid: int) -> int:
    rows = _ps_snapshot()
    if not rows:
        return 0

    total = 0
    frontier = [root_pid]
    seen: set[int] = set()
    while frontier:
        pid = frontier.pop()
        if pid in seen:
            continue
        seen.add(pid)
        entry = rows.get(pid)
        if entry is not None:
            total += entry[1]
        for child_pid, (ppid, _rss) in rows.items():
            if ppid == pid and child_pid not in seen:
                frontier.append(child_pid)
    return total


def kill_process_group(proc: subprocess.Popen[object], grace_seconds: float = 1.0) -> None:
    try:
        pgid = os.getpgid(proc.pid)
    except ProcessLookupError:
        return

    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(pgid, sig)
        except ProcessLookupError:
            return
        deadline = time.monotonic() + grace_seconds
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                return
            time.sleep(0.05)


def run_guarded_command(
    command: Sequence[str],
    cwd: Path,
    *,
    timeout_seconds: int | None,
    max_memory_bytes: int | None,
    monitor_interval_seconds: float,
    env: Mapping[str, str] | None = None,
    lock_dir: Path | None = None,
) -> CommandResult:
    start = time.monotonic()

    with tempfile.NamedTemporaryFile(delete=False) as stdout_tmp, tempfile.NamedTemporaryFile(
        delete=False
    ) as stderr_tmp:
        stdout_path = Path(stdout_tmp.name)
        stderr_path = Path(stderr_tmp.name)

    try:
        lock_cm = DirectoryLock(lock_dir) if lock_dir is not None else None
        if lock_cm is not None:
            lock_cm.acquire()
        try:
            with stdout_path.open("wb") as stdout_fh, stderr_path.open("wb") as stderr_fh:
                proc = subprocess.Popen(
                    list(command),
                    cwd=str(cwd),
                    env=dict(env) if env is not None else None,
                    stdout=stdout_fh,
                    stderr=stderr_fh,
                    start_new_session=True,
                )

                peak_rss = 0
                timed_out = False
                memory_exceeded = False

                while True:
                    returncode = proc.poll()
                    peak_rss = max(peak_rss, process_tree_rss_bytes(proc.pid))
                    if returncode is not None:
                        break

                    elapsed = time.monotonic() - start
                    if timeout_seconds is not None and elapsed > timeout_seconds:
                        timed_out = True
                        kill_process_group(proc)
                        returncode = proc.wait()
                        break

                    if max_memory_bytes is not None and peak_rss > max_memory_bytes:
                        memory_exceeded = True
                        kill_process_group(proc)
                        returncode = proc.wait()
                        break

                    time.sleep(monitor_interval_seconds)

            elapsed = time.monotonic() - start
            return CommandResult(
                command=list(command),
                cwd=str(cwd),
                returncode=returncode,
                elapsed_seconds=elapsed,
                peak_rss_bytes=peak_rss,
                timed_out=timed_out,
                memory_exceeded=memory_exceeded,
                stdout_tail=_read_tail(stdout_path),
                stderr_tail=_read_tail(stderr_path),
                lock_dir=str(lock_dir) if lock_dir is not None else None,
            )
        finally:
            if lock_cm is not None:
                lock_cm.release()
    finally:
        stdout_path.unlink(missing_ok=True)
        stderr_path.unlink(missing_ok=True)
