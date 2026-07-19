#!/usr/bin/env python3
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
AGENT_ROOT = SCRIPT_DIR.parents[1]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from runtime_paths import clean_subprocess_env, resolve_input_path, resolve_runtime_paths


def is_relative_to(path: Path, other: Path) -> bool:
    try:
        path.relative_to(other)
        return True
    except ValueError:
        return False


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def read_cmake_cache_value(cache_file: Path, key: str) -> str | None:
    if not cache_file.is_file():
        return None
    prefix = f"{key}:"
    for line in read_text(cache_file).splitlines():
        if line.startswith(prefix):
            _, value = line.split("=", 1)
            return value
    return None


def replace_paths(text: str, replacements: list[tuple[str, str]]) -> str:
    updated = text
    for old, new in replacements:
        updated = updated.replace(old, new)
    return updated


def extract_recipe_commands(makefile: Path, target: str) -> list[str]:
    lines = read_text(makefile).splitlines()
    start = None
    for idx, line in enumerate(lines):
        if line.startswith(f"{target}:"):
            start = idx
            break
    if start is None:
        raise SystemExit(f"Target not found in {makefile}: {target}")

    idx = start
    while idx < len(lines) and lines[idx].startswith(f"{target}:"):
        idx += 1

    commands: list[str] = []
    while idx < len(lines):
        line = lines[idx]
        if line.startswith("\t"):
            command = line[1:]
            if command.startswith("@"):
                command = command[1:]
            if "cmake_echo_color" not in command:
                commands.append(command)
            idx += 1
            continue
        break
    return commands


def extract_make_variables(makefile: Path) -> dict[str, str]:
    variables: dict[str, str] = {}
    for line in read_text(makefile).splitlines():
        if not line or line.startswith("#") or line.startswith("\t"):
            continue
        if " = " not in line:
            continue
        key, value = line.split(" = ", 1)
        if key and " " not in key:
            variables[key] = value
    return variables


def expand_make_variables(command: str, variables: dict[str, str]) -> str:
    def repl(match: re.Match[str]) -> str:
        name = match.group(1)
        return variables.get(name, match.group(0))

    return re.sub(r"\$\(([^)]+)\)", repl, command)


def run_shell(command: str, *, cwd: Path | None = None) -> None:
    subprocess.run(
        command,
        cwd=str(cwd) if cwd is not None else None,
        env=clean_subprocess_env(),
        shell=True,
        check=True,
        executable="/bin/bash",
    )


def rewrite_command(
    command: str,
    *,
    core_build: Path,
    core_dpl_src: Path,
    variant_build: Path,
    variant_dpl_src: Path,
) -> str:
    replacements = []
    for old, new in [
        (core_build / "src" / "dpl_evolve", variant_build / "src" / "dpl_evolve"),
        (core_dpl_src, variant_dpl_src),
    ]:
        replacements.append((str(old), str(new)))
        try:
            replacements.append((str(old.resolve()), str(new)))
        except FileNotFoundError:
            pass
    return replace_paths(command, replacements)


def build_generated_sources(
    *,
    core_build: Path,
    core_dpl_src: Path,
    variant_build: Path,
    variant_dpl_src: Path,
) -> None:
    dpl_make = core_build / "src" / "dpl_evolve" / "CMakeFiles" / "dpl_evolve.dir" / "build.make"
    py_make = core_build / "src" / "dpl_evolve" / "CMakeFiles" / "dpl_evolve_py.dir" / "build.make"
    dpl_vars = extract_make_variables(dpl_make)
    py_vars = extract_make_variables(py_make) if py_make.exists() else {}

    targets = [
        (dpl_make, dpl_vars, "src/dpl_evolve/CMakeFiles/dpl_evolve.dir/OpendpTCL_wrap.cxx"),
        (dpl_make, dpl_vars, "src/dpl_evolve/dpl_evolve-tclInitVar.cc"),
    ]
    if py_make.exists():
        targets.extend(
            [
                (
                    py_make,
                    py_vars,
                    "src/dpl_evolve/CMakeFiles/dpl_evolve_py.dir/Opendp-pyPYTHON_wrap.cxx",
                ),
                (py_make, py_vars, "src/dpl_evolve/dpl_evolve_py.py"),
                (py_make, py_vars, "src/dpl_evolve/dpl_evolve_py-pythonInitVar.cc"),
            ]
        )

    ensure_dir(variant_build / "src" / "dpl_evolve")
    for makefile, make_vars, target in targets:
        for command in extract_recipe_commands(makefile, target):
            expanded = expand_make_variables(command, make_vars)
            rewritten = rewrite_command(
                expanded,
                core_build=core_build,
                core_dpl_src=core_dpl_src,
                variant_build=variant_build,
                variant_dpl_src=variant_dpl_src,
            )
            run_shell(rewritten)


def collect_dpl_compile_commands(
    *,
    compile_commands_path: Path,
    core_build: Path,
    core_dpl_src: Path,
) -> list[dict[str, str]]:
    entries = json.loads(read_text(compile_commands_path))
    selected = []
    core_dpl_src_resolved = core_dpl_src.resolve()
    core_build_dpl = core_build / "src" / "dpl_evolve"
    core_build_dpl_resolved = core_build_dpl.resolve()
    for entry in entries:
        file_path = Path(entry["file"])
        file_path_resolved = file_path.resolve()
        in_core_src = is_relative_to(file_path, core_dpl_src) or is_relative_to(
            file_path_resolved, core_dpl_src_resolved
        )
        in_core_build = is_relative_to(file_path, core_build_dpl) or is_relative_to(
            file_path_resolved, core_build_dpl_resolved
        )
        if in_core_src:
            if is_relative_to(file_path, core_dpl_src / "test") or is_relative_to(
                file_path_resolved, core_dpl_src_resolved / "test"
            ):
                continue
            selected.append(entry)
            continue
        if in_core_build:
            selected.append(entry)
    return selected


def add_variant_only_source_compile_entries(
    *,
    compile_entries: list[dict[str, str]],
    core_dpl_src: Path,
    variant_dpl_src: Path,
) -> list[dict[str, str]]:
    represented: set[str] = set()
    templates: dict[str, dict[str, str]] = {}
    core_dpl_src_resolved = core_dpl_src.resolve()
    for entry in compile_entries:
        file_path = Path(entry["file"])
        try:
            rel = file_path.resolve().relative_to(core_dpl_src_resolved).as_posix()
        except ValueError:
            continue
        represented.add(rel)
        templates.setdefault(file_path.suffix, entry)

    extra_entries: list[dict[str, str]] = []
    for source in sorted((variant_dpl_src / "src").rglob("*")):
        if source.suffix not in {".cpp", ".cxx", ".cc", ".c"}:
            continue
        try:
            rel = source.relative_to(variant_dpl_src).as_posix()
        except ValueError:
            continue
        if rel.startswith("test/") or "/test/" in rel:
            continue
        if rel in represented:
            continue

        template = templates.get(source.suffix) or templates.get(".cpp")
        if template is None:
            raise SystemExit(f"Could not synthesize compile command for new source: {source}")

        template_file = Path(template["file"])
        fake_core_source = core_dpl_src / rel
        parts = shlex.split(template["command"])
        replaced_file = False
        for idx, token in enumerate(parts):
            if token == str(template_file):
                parts[idx] = str(fake_core_source)
                replaced_file = True
        if not replaced_file:
            parts.append(str(fake_core_source))

        for idx, token in enumerate(parts[:-1]):
            if token == "-o":
                parts[idx + 1] = f"CMakeFiles/dpl_evolve_lib.dir/{rel}.o"
                break
        else:
            raise SystemExit(f"Could not find -o output in template compile command: {template['command']}")

        extra = dict(template)
        extra["file"] = str(fake_core_source)
        extra["command"] = " ".join(shlex.quote(part) for part in parts)
        extra_entries.append(extra)

    return compile_entries + extra_entries


def rewrite_compile_entry(
    entry: dict[str, str],
    *,
    core_build: Path,
    core_dpl_src: Path,
    variant_build: Path,
    variant_dpl_src: Path,
) -> tuple[str, Path]:
    directory = Path(entry["directory"]).resolve()
    core_build_dpl = (core_build / "src" / "dpl_evolve").resolve()
    if directory == core_build_dpl:
        directory = variant_build / "src" / "dpl_evolve"

    command = rewrite_command(
        entry["command"],
        core_build=core_build,
        core_dpl_src=core_dpl_src,
        variant_build=variant_build,
        variant_dpl_src=variant_dpl_src,
    )

    output_path = None
    parts = command.split()
    for idx, token in enumerate(parts[:-1]):
        if token == "-o":
            output_path = Path(parts[idx + 1])
            break
    if output_path is None:
        raise SystemExit(f"Could not find -o output in compile command: {command}")
    output_parent = output_path.parent if output_path.is_absolute() else directory / output_path.parent
    ensure_dir(output_parent)
    return command, directory


def compile_variant_objects(
    *,
    compile_entries: list[dict[str, str]],
    core_build: Path,
    core_dpl_src: Path,
    variant_build: Path,
    variant_dpl_src: Path,
    threads: int,
) -> None:
    rewritten = [
        rewrite_compile_entry(
            entry,
            core_build=core_build,
            core_dpl_src=core_dpl_src,
            variant_build=variant_build,
            variant_dpl_src=variant_dpl_src,
        )
        for entry in compile_entries
    ]

    def run_one(item: tuple[str, Path]) -> None:
        command, cwd = item
        run_shell(command, cwd=cwd)

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, threads)) as pool:
        futures = [pool.submit(run_one, item) for item in rewritten]
        for future in concurrent.futures.as_completed(futures):
            future.result()


def run_link_recipe(link_txt: Path, *, cwd: Path) -> None:
    for command in read_text(link_txt).splitlines():
        command = command.strip()
        if not command:
            continue
        run_shell(command, cwd=cwd)


def remove_stale_archives(*, dpl_build_dir: Path) -> None:
    """Drop variant archives before replaying CMake's `ar qc` recipes.

    The relinker may be run repeatedly in the same private build tree.  CMake's
    static-library recipes use quick-append archive commands, so leaving an old
    archive in place can preserve stale members before the newly compiled object
    with the same basename.  The linker then resolves the first member and the
    evaluated binary silently runs old DPL code.
    """
    for name in (
        "libdpl_evolve_lib.a",
        "libdpl_evolve_framework_lib.a",
        "dpl_evolve.a",
        "_dpl_evolve_py.a",
    ):
        archive = dpl_build_dir / name
        if archive.exists():
            archive.unlink()


def infer_archive_tool_from_recipe(link_txt: Path, tool_kind: str) -> str | None:
    """Return the archive tool used by the common-core CMake recipe.

    CMake may select a toolchain-specific `ar`/`ranlib` wrapper.  Reusing that
    command keeps variant relinks ABI-compatible without hardcoding host paths.
    """
    if not link_txt.is_file():
        return None

    for command in read_text(link_txt).splitlines():
        command = command.strip()
        if not command:
            continue
        try:
            parts = shlex.split(command)
        except ValueError:
            continue
        if not parts:
            continue
        tool_name = Path(parts[0]).name
        if (
            tool_name == tool_kind
            or tool_name.endswith(f"-{tool_kind}")
            or f"-{tool_kind}-" in tool_name
        ):
            return parts[0]
    return None


def resolve_archive_tool(*, link_txt: Path, tool_kind: str) -> str:
    env_names = {
        "ar": ("DPL_EVOLVE_AR", "CMAKE_AR", "AR"),
        "ranlib": ("DPL_EVOLVE_RANLIB", "CMAKE_RANLIB", "RANLIB"),
    }[tool_kind]
    for name in env_names:
        value = os.environ.get(name)
        if value:
            return value

    inferred = infer_archive_tool_from_recipe(link_txt, tool_kind)
    if inferred:
        return inferred

    resolved = shutil.which(tool_kind)
    if resolved:
        return resolved

    raise SystemExit(
        f"Could not resolve archive tool '{tool_kind}'. "
        f"Set one of {', '.join(env_names)}."
    )


def append_extra_dpl_lib_objects(*, core_link_txt: Path, variant_dpl_build: Path) -> None:
    """Archive source files that exist only in a materialized family variant.

    The common-core CMake link recipe only knows the clean-base object list.
    Materialized families such as LEGALM can add new `.cpp` files.  They are
    compiled by the rewritten compile commands, but the old archive recipe will
    not include them unless we append the extra objects explicitly.
    """
    commands = [line.strip() for line in read_text(core_link_txt).splitlines() if line.strip()]
    known_objects: set[str] = set()
    for command in commands:
        for token in shlex.split(command):
            if token.endswith(".o"):
                known_objects.add(token)

    object_root = variant_dpl_build / "CMakeFiles" / "dpl_evolve_lib.dir"
    extra_objects: list[str] = []
    for obj in sorted(object_root.rglob("*.o")):
        rel = obj.relative_to(variant_dpl_build).as_posix()
        if rel not in known_objects:
            extra_objects.append(rel)

    if not extra_objects:
        return

    quoted = " ".join(shlex.quote(item) for item in extra_objects)
    ar_tool = resolve_archive_tool(link_txt=core_link_txt, tool_kind="ar")
    ranlib_tool = resolve_archive_tool(link_txt=core_link_txt, tool_kind="ranlib")
    run_shell(f"{shlex.quote(ar_tool)} q libdpl_evolve_lib.a {quoted}", cwd=variant_dpl_build)
    run_shell(f"{shlex.quote(ranlib_tool)} libdpl_evolve_lib.a", cwd=variant_dpl_build)


def relink_openroad(
    *,
    core_build: Path,
    variant_build: Path,
    install_root: Path,
) -> Path:
    link_txt = core_build / "src" / "CMakeFiles" / "openroad.dir" / "link.txt"
    binary_path = install_root / "bin" / "openroad"
    ensure_dir(binary_path.parent)

    command = read_text(link_txt).strip()
    replacements = [
        ("-o ../bin/openroad", f"-o {binary_path}"),
        ("dpl_evolve/dpl_evolve.a", str(variant_build / "src" / "dpl_evolve" / "dpl_evolve.a")),
        (
            "dpl_evolve/libdpl_evolve_lib.a",
            str(variant_build / "src" / "dpl_evolve" / "libdpl_evolve_lib.a"),
        ),
        (
            "dpl_evolve/libdpl_evolve_framework_lib.a",
            str(variant_build / "src" / "dpl_evolve" / "libdpl_evolve_framework_lib.a"),
        ),
        (
            "dpl_evolve/_dpl_evolve_py.a",
            str(variant_build / "src" / "dpl_evolve" / "_dpl_evolve_py.a"),
        ),
    ]
    command = replace_paths(command, replacements)
    run_shell(command, cwd=core_build / "src")
    return binary_path


def link_core_sta(*, core_root: Path, install_root: Path) -> Path | None:
    core_sta = core_root / "install" / "OpenROAD" / "bin" / "sta"
    variant_sta = install_root / "bin" / "sta"
    if not core_sta.is_file():
        return None

    ensure_dir(variant_sta.parent)
    if variant_sta.exists() or variant_sta.is_symlink():
        variant_sta.unlink()
    variant_sta.symlink_to(core_sta)
    return variant_sta


def write_variant_env(
    *,
    variant_root: Path,
    core_root: Path,
    build_dir: Path,
    install_root: Path,
    dpl_evolve_src: Path,
    sta_binary: Path | None,
) -> Path:
    lines = [
        "#!/usr/bin/env bash",
        "# Generated by build_openroad_variant_relink.py",
        "export OPENROAD_VARIANT_MODE=relink",
        f"export OPENROAD_VARIANT_ROOT={shlex.quote(variant_root.as_posix())}",
        f"export OPENROAD_CORE_ROOT={shlex.quote(core_root.as_posix())}",
        f"export OPENROAD_BUILD_DIR={shlex.quote(build_dir.as_posix())}",
        f"export OPENROAD_INSTALL_ROOT={shlex.quote(install_root.as_posix())}",
        f"export OPENROAD_BINARY={shlex.quote((install_root / 'bin' / 'openroad').as_posix())}",
        f"export DPL_EVOLVE_SRC_DIR={shlex.quote(dpl_evolve_src.as_posix())}",
    ]
    if sta_binary is not None:
        lines.append(f"export OPENROAD_STA_BINARY={shlex.quote(sta_binary.as_posix())}")

    env_path = variant_root / "variant_env.sh"
    env_path.write_text(
        "\n".join(lines) + "\n",
        encoding="utf-8",
    )
    env_path.chmod(0o755)
    return env_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Rebuild variant-owned dpl_evolve artifacts and relink a private openroad binary."
    )
    parser.add_argument("--variant-root")
    parser.add_argument("--build-dir")
    parser.add_argument("--install-root")
    parser.add_argument("--dpl-src")
    parser.add_argument("--core-root")
    parser.add_argument("--threads", type=int, default=8)
    return parser.parse_args()


def main() -> None:
    start_time = time.perf_counter()
    args = parse_args()
    runtime = resolve_runtime_paths(
        anchor_file=__file__,
        agent_root_levels_up=2,
        script_name="build_openroad_variant_relink.py",
    )
    orfs_root = runtime.orfs_root
    agent_root = runtime.agent_root
    state_root = runtime.state_root
    openroad_src = orfs_root / "tools" / "OpenROAD"
    openroad_head = subprocess.check_output(
        ["git", "-C", str(openroad_src), "rev-parse", "--short", "HEAD"],
        text=True,
    ).strip()

    variant_root_input = args.variant_root or str(state_root / "variants" / "default")
    variant_root = resolve_input_path(
        variant_root_input, cwd=Path.cwd(), agent_root=agent_root, orfs_root=orfs_root
    )
    build_dir_input = args.build_dir or str(variant_root / "build")
    install_root_input = args.install_root or str(variant_root / "install" / "OpenROAD")
    dpl_src_input = args.dpl_src or str(variant_root / "dpl_evolve")
    core_root_input = args.core_root or str(state_root / "openroad_core" / openroad_head)

    build_dir = resolve_input_path(build_dir_input, cwd=Path.cwd(), agent_root=agent_root, orfs_root=orfs_root)
    install_root = resolve_input_path(
        install_root_input, cwd=Path.cwd(), agent_root=agent_root, orfs_root=orfs_root
    )
    dpl_evolve_src = resolve_input_path(
        dpl_src_input, cwd=Path.cwd(), agent_root=agent_root, orfs_root=orfs_root
    )
    core_root = resolve_input_path(core_root_input, cwd=Path.cwd(), agent_root=agent_root, orfs_root=orfs_root)

    core_build = core_root / "build"
    compile_commands_path = core_build / "compile_commands.json"
    core_home_raw = read_cmake_cache_value(core_build / "CMakeCache.txt", "CMAKE_HOME_DIRECTORY")
    core_openroad_src = Path(core_home_raw) if core_home_raw else openroad_src
    core_dpl_src = core_openroad_src / "src" / "dpl_evolve"

    if not compile_commands_path.is_file():
        raise SystemExit(f"Missing common-core compile_commands.json: {compile_commands_path}")
    if not (core_build / "src" / "CMakeFiles" / "openroad.dir" / "link.txt").is_file():
        raise SystemExit(f"Missing common-core openroad link recipe under {core_build}")
    if not dpl_evolve_src.joinpath("CMakeLists.txt").is_file():
        raise SystemExit(f"Variant dpl_evolve source dir lacks CMakeLists.txt: {dpl_evolve_src}")

    ensure_dir(build_dir / "src" / "dpl_evolve")
    ensure_dir(install_root / "bin")

    print("[INFO] Lightweight variant relink")
    print(f"[INFO] orfs_root={orfs_root}")
    print(f"[INFO] core_root={core_root}")
    print(f"[INFO] variant_root={variant_root}")
    print(f"[INFO] build_dir={build_dir}")
    print(f"[INFO] install_root={install_root}")
    print(f"[INFO] dpl_evolve_src={dpl_evolve_src}")
    print(f"[INFO] threads={args.threads}")

    build_generated_sources(
        core_build=core_build,
        core_dpl_src=core_dpl_src,
        variant_build=build_dir,
        variant_dpl_src=dpl_evolve_src,
    )
    compile_entries = collect_dpl_compile_commands(
        compile_commands_path=compile_commands_path,
        core_build=core_build,
        core_dpl_src=core_dpl_src,
    )
    compile_entries = add_variant_only_source_compile_entries(
        compile_entries=compile_entries,
        core_dpl_src=core_dpl_src,
        variant_dpl_src=dpl_evolve_src,
    )
    compile_variant_objects(
        compile_entries=compile_entries,
        core_build=core_build,
        core_dpl_src=core_dpl_src,
        variant_build=build_dir,
        variant_dpl_src=dpl_evolve_src,
        threads=args.threads,
    )

    dpl_build_dir = build_dir / "src" / "dpl_evolve"
    dpl_lib_link = core_build / "src" / "dpl_evolve" / "CMakeFiles" / "dpl_evolve_lib.dir" / "link.txt"
    framework_lib_link = (
        core_build
        / "src"
        / "dpl_evolve"
        / "CMakeFiles"
        / "dpl_evolve_framework_lib.dir"
        / "link.txt"
    )
    remove_stale_archives(dpl_build_dir=dpl_build_dir)
    run_link_recipe(dpl_lib_link, cwd=dpl_build_dir)
    append_extra_dpl_lib_objects(core_link_txt=dpl_lib_link, variant_dpl_build=dpl_build_dir)
    if framework_lib_link.is_file():
        run_link_recipe(framework_lib_link, cwd=dpl_build_dir)
    run_link_recipe(core_build / "src" / "dpl_evolve" / "CMakeFiles" / "dpl_evolve.dir" / "link.txt", cwd=dpl_build_dir)
    py_link = core_build / "src" / "dpl_evolve" / "CMakeFiles" / "dpl_evolve_py.dir" / "link.txt"
    if py_link.is_file():
        run_link_recipe(py_link, cwd=dpl_build_dir)

    binary_path = relink_openroad(core_build=core_build, variant_build=build_dir, install_root=install_root)
    sta_binary = link_core_sta(core_root=core_root, install_root=install_root)
    env_path = write_variant_env(
        variant_root=variant_root,
        core_root=core_root,
        build_dir=build_dir,
        install_root=install_root,
        dpl_evolve_src=dpl_evolve_src,
        sta_binary=sta_binary,
    )
    print(f"[INFO] Variant relink complete: {binary_path}")
    if sta_binary is not None:
        print(f"[INFO] Variant sta symlink: {sta_binary}")
    print(f"[INFO] variant_env={env_path}")
    print(f"[INFO] relink_elapsed_sec={time.perf_counter() - start_time:.2f}")


if __name__ == "__main__":
    main()
