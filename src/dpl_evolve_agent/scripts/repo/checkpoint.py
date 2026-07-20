#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

AGENT_ROOT = Path(__file__).resolve().parents[2]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from runtime_paths import resolve_input_path, resolve_runtime_paths


def runtime_paths():
    return resolve_runtime_paths(
        anchor_file=__file__,
        agent_root_levels_up=2,
        script_name="checkpoint.py",
    )


REPO_ROOT: Path | None = None
AGENT_ROOT_DIR: Path | None = None
STATE_ROOT: Path | None = None
CHECKPOINTS_DIR: Path | None = None
OPERATIONS_DIR: Path | None = None
TARGET_DIR: Path | None = None


def initialize_paths() -> None:
    global REPO_ROOT, AGENT_ROOT_DIR, STATE_ROOT, CHECKPOINTS_DIR, OPERATIONS_DIR, TARGET_DIR
    if REPO_ROOT is not None:
        return
    runtime = runtime_paths()
    REPO_ROOT = runtime.orfs_root
    AGENT_ROOT_DIR = runtime.agent_root
    STATE_ROOT = runtime.state_root
    CHECKPOINTS_DIR = runtime.checkpoints_dir
    OPERATIONS_DIR = runtime.operations_dir
    TARGET_DIR = REPO_ROOT / "tools" / "OpenROAD" / "src" / "dpl_evolve"

CONTROL_SNAPSHOT_DIRNAME = "agent_root"
TARGET_SNAPSHOT_DIRNAME = "dpl_evolve"

CONTROL_EXCLUDES = {
    "checkpoints",
    "checkpoint_packages",
    "__pycache__",
    ".git",
}
TARGET_EXCLUDES = {
    "__pycache__",
}
FILE_SUFFIX_EXCLUDES = {
    ".pyc",
    ".pyo",
}


def run_cmd(cmd: list[str], cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        check=check,
        text=True,
        capture_output=True,
    )


def git_output(repo: Path, *args: str, check: bool = True) -> str:
    result = run_cmd(["git", *args], cwd=repo, check=check)
    return result.stdout


def git_ref_exists(repo: Path, ref: str) -> bool:
    result = run_cmd(["git", "show-ref", "--verify", "--quiet", ref], cwd=repo, check=False)
    return result.returncode == 0


def current_branch(repo: Path) -> str:
    result = run_cmd(["git", "symbolic-ref", "--quiet", "--short", "HEAD"], cwd=repo, check=False)
    branch = result.stdout.strip()
    return branch if branch else "DETACHED"


def ensure_operation_dir(checkpoint_id: str) -> Path:
    operation_dir = OPERATIONS_DIR / checkpoint_id
    if operation_dir.exists():
        raise SystemExit(f"Checkpoint '{checkpoint_id}' already exists: {operation_dir}")
    operation_dir.mkdir(parents=True, exist_ok=False)
    return operation_dir


def should_skip(name: str, suffix: str, excluded_names: set[str]) -> bool:
    return name in excluded_names or suffix in FILE_SUFFIX_EXCLUDES


def copy_tree(src: Path, dst: Path, excluded_names: set[str]) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True, exist_ok=True)
    for entry in src.iterdir():
        if should_skip(entry.name, entry.suffix, excluded_names):
            continue
        target = dst / entry.name
        if entry.is_dir():
            copy_tree(entry, target, excluded_names)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(entry, target)


def remove_path(path: Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    elif path.exists() or path.is_symlink():
        path.unlink()


def sync_tree(src: Path, dst: Path, excluded_names: set[str], preserve_names: set[str] | None = None) -> None:
    preserve_names = preserve_names or set()
    dst.mkdir(parents=True, exist_ok=True)
    src_entries = {
        entry.name for entry in src.iterdir()
        if not should_skip(entry.name, entry.suffix, excluded_names)
    }

    for entry in list(dst.iterdir()):
        if entry.name in preserve_names:
            continue
        if should_skip(entry.name, entry.suffix, excluded_names):
            continue
        if entry.name not in src_entries:
            remove_path(entry)

    for src_entry in src.iterdir():
        if should_skip(src_entry.name, src_entry.suffix, excluded_names):
            continue
        dst_entry = dst / src_entry.name
        if src_entry.is_dir():
            if dst_entry.exists() and not dst_entry.is_dir():
                remove_path(dst_entry)
            sync_tree(src_entry, dst_entry, excluded_names, preserve_names=set())
        else:
            dst_entry.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src_entry, dst_entry)


def sha256_tree(path: Path) -> str:
    digest = hashlib.sha256()
    for file_path in sorted(p for p in path.rglob("*") if p.is_file()):
        relative = file_path.relative_to(path).as_posix()
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(file_path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def copy_evidence_files(evidence_paths: list[Path], destination_dir: Path) -> list[str]:
    copied: list[str] = []
    used_names: set[str] = set()
    destination_dir.mkdir(parents=True, exist_ok=True)
    for evidence in evidence_paths:
        resolved = resolve_input_path(
            evidence,
            cwd=Path.cwd(),
            agent_root=AGENT_ROOT_DIR,
            orfs_root=REPO_ROOT,
        )
        if not resolved.exists():
            raise SystemExit(f"Evidence path does not exist: {evidence}")
        base_name = resolved.name
        final_name = base_name
        counter = 1
        while final_name in used_names:
            counter += 1
            final_name = f"{resolved.stem}_{counter}{resolved.suffix}"
        used_names.add(final_name)
        shutil.copy2(resolved, destination_dir / final_name)
        copied.append(final_name)
    return copied


def gather_git_metadata(pathspec_root: Path, pathspec: str) -> dict[str, Any]:
    return {
        "branch": current_branch(pathspec_root),
        "head": git_output(pathspec_root, "rev-parse", "HEAD").strip(),
        "status": git_output(
            pathspec_root,
            "status",
            "--short",
            "--branch",
            "--untracked-files=all",
            "--",
            pathspec,
        ),
        "diff": git_output(
            pathspec_root,
            "diff",
            "--binary",
            "--",
            pathspec,
            check=False,
        ),
        "untracked": git_output(
            pathspec_root,
            "ls-files",
            "--others",
            "--exclude-standard",
            "--",
            pathspec,
            check=False,
        ),
    }


def render_scalar(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    if isinstance(value, (int, float)):
        return str(value)
    text = str(value)
    needs_quotes = (
        text == ""
        or text.strip() != text
        or ":" in text
        or text.startswith(("-", "{", "[", "#", "&", "*", "!", "|", ">", "@", "`", "\"", "'"))
        or "\n" in text
    )
    if not needs_quotes:
        return text
    escaped = text.replace("\\", "\\\\").replace("\"", "\\\"")
    return f"\"{escaped}\""


def to_yaml(value: Any, indent: int = 0) -> str:
    prefix = " " * indent
    if isinstance(value, dict):
        lines: list[str] = []
        for key, item in value.items():
            if isinstance(item, (dict, list)):
                lines.append(f"{prefix}{key}:")
                lines.append(to_yaml(item, indent + 2))
            else:
                lines.append(f"{prefix}{key}: {render_scalar(item)}")
        return "\n".join(lines)
    if isinstance(value, list):
        lines = []
        for item in value:
            if isinstance(item, (dict, list)):
                lines.append(f"{prefix}-")
                lines.append(to_yaml(item, indent + 2))
            else:
                lines.append(f"{prefix}- {render_scalar(item)}")
        return "\n".join(lines)
    return f"{prefix}{render_scalar(value)}"


def build_checkpoint_payload(
    checkpoint_id: str,
    title: str,
    summary: str,
    operation_type: str,
    base_checkpoint: str | None,
    changed_scope: list[str],
    what_changed: list[str],
    what_was_run: list[str],
    result_summary: str,
    evidence_files: list[str],
    control_snapshot_hash: str,
    target_snapshot_hash: str,
    root_git: dict[str, Any],
    agent_git: dict[str, Any],
    openroad_git: dict[str, Any],
) -> dict[str, Any]:
    return {
        "checkpoint_id": checkpoint_id,
        "title": title,
        "status": "completed",
        "operation_type": operation_type,
        "restorable": True,
        "base_checkpoint": base_checkpoint,
        "summary": summary,
        "changed_scope": changed_scope,
        "what_changed": what_changed,
        "what_was_run": what_was_run,
        "result_summary": result_summary,
        "primary_evidence": evidence_files,
        "git_anchor": {
            "root_branch": root_git["branch"],
            "root_head": root_git["head"],
            "agent_branch": agent_git["branch"],
            "agent_head": agent_git["head"],
            "openroad_branch": openroad_git["branch"],
            "openroad_head": openroad_git["head"],
        },
        "snapshot_hashes": {
            "agent_root": control_snapshot_hash,
            "dpl_evolve": target_snapshot_hash,
        },
    }


def switch_repo_to_anchor(repo: Path, branch: str, commit: str) -> None:
    current = current_branch(repo)
    if branch != "DETACHED":
        if current != branch:
            if git_ref_exists(repo, f"refs/heads/{branch}"):
                run_cmd(["git", "switch", branch], cwd=repo)
            else:
                run_cmd(["git", "switch", "-c", branch, commit], cwd=repo)
        return

    head = git_output(repo, "rev-parse", "HEAD").strip()
    if head != commit or current != "DETACHED":
        run_cmd(["git", "checkout", "--detach", commit], cwd=repo)


def write_checkpoint_files(operation_dir: Path, checkpoint_payload: dict[str, Any]) -> None:
    yaml_text = to_yaml(checkpoint_payload) + "\n"
    write_text(operation_dir / "CHECKPOINT.yaml", yaml_text)
    write_text(
        operation_dir / "checkpoint.json",
        json.dumps(checkpoint_payload, indent=2, sort_keys=True) + "\n",
    )

    readme_lines = [
        f"# {checkpoint_payload['checkpoint_id']}",
        "",
        checkpoint_payload["title"],
        "",
        f"- Status: `{checkpoint_payload['status']}`",
        f"- Type: `{checkpoint_payload['operation_type']}`",
        f"- Restorable: `{str(checkpoint_payload['restorable']).lower()}`",
    ]
    if checkpoint_payload.get("base_checkpoint"):
        readme_lines.append(f"- Base checkpoint: `{checkpoint_payload['base_checkpoint']}`")
    readme_lines.extend(
        [
            "",
            "## Summary",
            checkpoint_payload["summary"],
            "",
            "## Git Anchors",
            f"- Root: `{checkpoint_payload['git_anchor']['root_branch']}` @ `{checkpoint_payload['git_anchor']['root_head']}`",
            f"- Agent: `{checkpoint_payload['git_anchor']['agent_branch']}` @ `{checkpoint_payload['git_anchor']['agent_head']}`",
            f"- OpenROAD: `{checkpoint_payload['git_anchor']['openroad_branch']}` @ `{checkpoint_payload['git_anchor']['openroad_head']}`",
            "",
            "## What Changed",
        ]
    )
    for item in checkpoint_payload["what_changed"]:
        readme_lines.append(f"- {item}")
    readme_lines.extend(["", "## What Was Run"])
    for item in checkpoint_payload["what_was_run"]:
        readme_lines.append(f"- {item}")
    readme_lines.extend(
        [
            "",
            "## Result",
            f"- {checkpoint_payload['result_summary']}",
            "",
            "## Restore",
            f"```bash\n\"$DPL_EVOLVE_PYTHON\" \"$DPL_EVOLVE_AGENT_ROOT/scripts/repo/checkpoint.py\" restore --checkpoint-id {checkpoint_payload['checkpoint_id']} --force\n```",
            "",
            "## Evidence",
        ]
    )
    if checkpoint_payload["primary_evidence"]:
        for evidence in checkpoint_payload["primary_evidence"]:
            readme_lines.append(f"- `evidence/{evidence}`")
    else:
        readme_lines.append("- none")
    readme_lines.extend(
        [
            "",
            "## Snapshot Hashes",
            f"- agent_root: `{checkpoint_payload['snapshot_hashes']['agent_root']}`",
            f"- dpl_evolve: `{checkpoint_payload['snapshot_hashes']['dpl_evolve']}`",
            "",
        ]
    )
    write_text(operation_dir / "README.md", "\n".join(readme_lines))


def update_index(checkpoint_payload: dict[str, Any]) -> None:
    index_path = CHECKPOINTS_DIR / "CHECKPOINT_INDEX.md"
    if not index_path.exists():
        return
    text = index_path.read_text(encoding="utf-8")
    checkpoint_id = checkpoint_payload["checkpoint_id"]
    if f"| `{checkpoint_id}` |" in text:
        return

    changed_scope = ", ".join(checkpoint_payload["changed_scope"]) or "-"
    row = (
        f"| `{checkpoint_id}` | {checkpoint_payload['title']} | {changed_scope} | "
        f"{checkpoint_payload['result_summary']} | "
        f"[operations/{checkpoint_id}]({(OPERATIONS_DIR / checkpoint_id).as_posix()}) |"
    )

    lines = text.splitlines()
    insert_at = None
    for idx, line in enumerate(lines):
        if line.startswith("| `"):
            insert_at = idx + 1
    if insert_at is None:
        lines.extend(["", "## Current operations", "", "| checkpoint_id | purpose | changed scope | result summary | directory |", "| --- | --- | --- | --- | --- |", row])
    else:
        lines.insert(insert_at, row)
    index_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def cmd_create(args: argparse.Namespace) -> None:
    operation_dir = ensure_operation_dir(args.checkpoint_id)
    snapshot_dir = operation_dir / "snapshot"
    control_snapshot_dir = snapshot_dir / CONTROL_SNAPSHOT_DIRNAME
    target_snapshot_dir = snapshot_dir / TARGET_SNAPSHOT_DIRNAME
    evidence_dir = operation_dir / "evidence"
    git_dir = operation_dir / "git"
    git_dir.mkdir(parents=True, exist_ok=True)

    copy_tree(AGENT_ROOT_DIR, control_snapshot_dir, CONTROL_EXCLUDES)
    copy_tree(TARGET_DIR, target_snapshot_dir, TARGET_EXCLUDES)

    root_git = gather_git_metadata(REPO_ROOT, ".")
    agent_git = gather_git_metadata(AGENT_ROOT_DIR, ".")
    openroad_git = gather_git_metadata(REPO_ROOT / "tools" / "OpenROAD", "src/dpl_evolve")

    write_text(git_dir / "root_status.txt", root_git["status"])
    write_text(git_dir / "root_diff.patch", root_git["diff"])
    write_text(git_dir / "root_untracked.txt", root_git["untracked"])
    write_text(git_dir / "agent_status.txt", agent_git["status"])
    write_text(git_dir / "agent_diff.patch", agent_git["diff"])
    write_text(git_dir / "agent_untracked.txt", agent_git["untracked"])
    write_text(git_dir / "openroad_status.txt", openroad_git["status"])
    write_text(git_dir / "openroad_diff.patch", openroad_git["diff"])
    write_text(git_dir / "openroad_untracked.txt", openroad_git["untracked"])

    evidence_files = copy_evidence_files([Path(p) for p in args.evidence], evidence_dir)

    checkpoint_payload = build_checkpoint_payload(
        checkpoint_id=args.checkpoint_id,
        title=args.title,
        summary=args.summary,
        operation_type=args.operation_type,
        base_checkpoint=args.base_checkpoint,
        changed_scope=args.changed_scope or ["control plane + target plane snapshot"],
        what_changed=args.what_changed or ["captured a local restorable checkpoint"],
        what_was_run=args.what_was_run or ["no new run; snapshot only"],
        result_summary=args.result_summary or "snapshot captured",
        evidence_files=evidence_files,
        control_snapshot_hash=sha256_tree(control_snapshot_dir),
        target_snapshot_hash=sha256_tree(target_snapshot_dir),
        root_git=root_git,
        agent_git=agent_git,
        openroad_git=openroad_git,
    )
    write_checkpoint_files(operation_dir, checkpoint_payload)
    update_index(checkpoint_payload)
    print(f"Created checkpoint: {operation_dir}")


def cmd_restore(args: argparse.Namespace) -> None:
    if not args.force:
        raise SystemExit("Restore overwrites local files. Re-run with --force.")

    operation_dir = OPERATIONS_DIR / args.checkpoint_id
    if not operation_dir.exists():
        raise SystemExit(f"Unknown checkpoint: {args.checkpoint_id}")

    control_snapshot_dir = operation_dir / "snapshot" / CONTROL_SNAPSHOT_DIRNAME
    target_snapshot_dir = operation_dir / "snapshot" / TARGET_SNAPSHOT_DIRNAME
    if not control_snapshot_dir.exists() or not target_snapshot_dir.exists():
        raise SystemExit(f"Checkpoint is not restorable: {args.checkpoint_id}")

    checkpoint_data = checkpoint_summary(args.checkpoint_id)
    git_anchor = checkpoint_data.get("git_anchor", {})
    if git_anchor:
        root_branch = git_anchor.get("root_branch", "DETACHED")
        root_head = git_anchor.get("root_head")
        agent_branch = git_anchor.get("agent_branch", "DETACHED")
        agent_head = git_anchor.get("agent_head")
        openroad_branch = git_anchor.get("openroad_branch", "DETACHED")
        openroad_head = git_anchor.get("openroad_head")
        if root_head:
            switch_repo_to_anchor(REPO_ROOT, root_branch, root_head)
        if agent_head:
            switch_repo_to_anchor(AGENT_ROOT_DIR, agent_branch, agent_head)
        if openroad_head:
            switch_repo_to_anchor(REPO_ROOT / "tools" / "OpenROAD", openroad_branch, openroad_head)

    sync_tree(control_snapshot_dir, AGENT_ROOT_DIR, CONTROL_EXCLUDES)
    sync_tree(target_snapshot_dir, TARGET_DIR, TARGET_EXCLUDES)
    print(f"Restored checkpoint: {args.checkpoint_id}")


def checkpoint_summary(checkpoint_id: str) -> dict[str, Any]:
    operation_dir = OPERATIONS_DIR / checkpoint_id
    json_path = operation_dir / "checkpoint.json"
    yaml_path = operation_dir / "CHECKPOINT.yaml"
    if json_path.exists():
        return json.loads(json_path.read_text(encoding="utf-8"))
    if yaml_path.exists():
        summary: dict[str, Any] = {
            "checkpoint_id": checkpoint_id,
            "title": checkpoint_id,
            "result_summary": "",
            "restorable": (operation_dir / "snapshot").exists(),
        }
        for raw_line in yaml_path.read_text(encoding="utf-8").splitlines():
            if not raw_line or raw_line.startswith(" ") or ":" not in raw_line:
                continue
            key, value = raw_line.split(":", 1)
            summary[key.strip()] = value.strip().strip("\"")
        return summary
    raise SystemExit(f"Unknown checkpoint: {checkpoint_id}")


def cmd_list(_: argparse.Namespace) -> None:
    if not OPERATIONS_DIR.exists():
        return
    for operation_dir in sorted(OPERATIONS_DIR.iterdir()):
        if not operation_dir.is_dir():
            continue
        try:
            data = checkpoint_summary(operation_dir.name)
        except SystemExit:
            continue
        title = data.get("title", operation_dir.name)
        result = data.get("result_summary", "")
        print(f"{operation_dir.name}\t{title}\t{result}")


def cmd_show(args: argparse.Namespace) -> None:
    data = checkpoint_summary(args.checkpoint_id)
    print(json.dumps(data, indent=2, sort_keys=True))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local restorable checkpoints for agent_root + dpl_evolve.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    create_parser = subparsers.add_parser("create", help="Capture a local restorable checkpoint.")
    create_parser.add_argument("--checkpoint-id", required=True)
    create_parser.add_argument("--title", required=True)
    create_parser.add_argument("--summary", required=True)
    create_parser.add_argument("--operation-type", default="workspace_snapshot")
    create_parser.add_argument("--base-checkpoint")
    create_parser.add_argument("--changed-scope", action="append", default=[])
    create_parser.add_argument("--what-changed", action="append", default=[])
    create_parser.add_argument("--what-was-run", action="append", default=[])
    create_parser.add_argument("--result-summary", default="")
    create_parser.add_argument("--evidence", action="append", default=[])
    create_parser.set_defaults(func=cmd_create)

    restore_parser = subparsers.add_parser("restore", help="Restore one local checkpoint into the working tree.")
    restore_parser.add_argument("--checkpoint-id", required=True)
    restore_parser.add_argument("--force", action="store_true")
    restore_parser.set_defaults(func=cmd_restore)

    list_parser = subparsers.add_parser("list", help="List known checkpoints.")
    list_parser.set_defaults(func=cmd_list)

    show_parser = subparsers.add_parser("show", help="Show one checkpoint payload.")
    show_parser.add_argument("--checkpoint-id", required=True)
    show_parser.set_defaults(func=cmd_show)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    initialize_paths()
    args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
