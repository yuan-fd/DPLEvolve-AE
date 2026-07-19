from __future__ import annotations

import atexit
import datetime as dt
import fcntl
import os
import socket
from dataclasses import dataclass
from pathlib import Path


LOCK_FILENAME = ".evolve_agent.lock"


@dataclass
class LockInfo:
    pid: int | None
    host: str | None
    started: str | None
    path: Path


class DirectoryLockError(RuntimeError):
    pass


class DirectoryLock:
    """Exclusive lock for one runtime output directory.

    This is intentionally small and local. It borrows the basic file-locking idea
    from science-codeevolve without importing the full runtime.
    """

    def __init__(self, directory: Path):
        self.directory = directory
        self.lock_path = directory / LOCK_FILENAME
        self._fh = None
        self._locked = False

    def __enter__(self) -> "DirectoryLock":
        self.acquire()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.release()

    def acquire(self) -> None:
        self.directory.mkdir(parents=True, exist_ok=True)
        self._fh = self.lock_path.open("a+", encoding="utf-8")
        try:
            fcntl.flock(self._fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            info = self.read_info()
            msg = f"Directory is already locked: {self.directory}"
            if info:
                msg += (
                    f" (pid={info.pid}, host={info.host}, started={info.started}, "
                    f"lock_file={info.path})"
                )
            raise DirectoryLockError(msg) from exc

        self._locked = True
        self._fh.seek(0)
        self._fh.truncate()
        self._fh.write(f"PID: {os.getpid()}\n")
        self._fh.write(f"Host: {socket.gethostname()}\n")
        self._fh.write(f"Started: {dt.datetime.now().isoformat()}\n")
        self._fh.flush()
        atexit.register(self.release)

    def release(self) -> None:
        if not self._locked:
            return
        assert self._fh is not None
        try:
            fcntl.flock(self._fh.fileno(), fcntl.LOCK_UN)
        finally:
            self._fh.close()
            self._fh = None
            self._locked = False
            try:
                self.lock_path.unlink()
            except FileNotFoundError:
                pass

    def read_info(self) -> LockInfo | None:
        if not self.lock_path.exists():
            return None
        text = self.lock_path.read_text(encoding="utf-8", errors="replace")
        data = {}
        for line in text.splitlines():
            if ": " in line:
                key, value = line.split(": ", 1)
                data[key] = value
        pid = None
        if data.get("PID"):
            try:
                pid = int(data["PID"])
            except ValueError:
                pid = None
        return LockInfo(
            pid=pid,
            host=data.get("Host"),
            started=data.get("Started"),
            path=self.lock_path,
        )
