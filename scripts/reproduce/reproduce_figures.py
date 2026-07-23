#!/usr/bin/env python3
"""Rebuild paper Figures 4 and 5 from retained or freshly executed data.

The retained mode redraws the manuscript figures from compact author-run logs.
The fresh mode reads a completed paper-profile ReviewDSE run plus fresh Table 4
default/BO outputs.  Both modes emit normalized TSV data before rendering SVG;
no PDF or generated image is stored in Git.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


CASE_LABELS = {
    "aes_asap7": "AES ASAP7",
    "aes_nangate45": "AES N45",
    "ariane133_nangate45": "Ariane133 N45",
    "ibex_asap7": "Ibex ASAP7",
    "ibex_nangate45": "Ibex N45",
    "jpeg_asap7": "JPEG ASAP7",
    "jpeg_nangate45": "JPEG N45",
    "swerv_wrapper_asap7": "SWERV ASAP7",
    "swerv_wrapper_nangate45": "SWERV N45",
}
FIG5_CASES = {
    "aes_nangate45": "AES N45",
    "ariane133_nangate45": "Ariane133 N45",
}
COLORS = [
    "#1f4e79", "#b45f06", "#38761d", "#741b47", "#674ea7",
    "#134f5c", "#cc0000", "#7f6000", "#45818e",
]


def fail(message: str) -> None:
    raise ValueError(message)


def as_float(value: Any, field: str) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        fail(f"{field} is not numeric: {value!r}")
    if not math.isfinite(number):
        fail(f"{field} is not finite: {value!r}")
    return number


def read_tsv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        fail(f"missing input TSV: {path}")
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_manifest(root: Path, manifest: Path) -> None:
    if not manifest.is_file():
        fail(f"missing retained-data manifest: {manifest}")
    checked = 0
    for line in manifest.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        expected, relative = line.split(maxsplit=1)
        relative = relative.lstrip("* ")
        path = root / relative
        if not path.is_file():
            fail(f"manifest input is missing: {path}")
        actual = sha256(path)
        if actual != expected:
            fail(f"retained input checksum mismatch: {relative}")
        checked += 1
    if checked == 0:
        fail(f"empty retained-data manifest: {manifest}")
    print(f"[PASS] verified {checked} retained Figure 4/5 input checksums")


def write_tsv(path: Path, rows: list[dict[str, Any]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=fields, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)


def case_from_text(text: str, cases: Iterable[str]) -> str | None:
    matches = [case for case in cases if case in text]
    if not matches:
        return None
    return max(matches, key=len)


def iteration_from_path(path: Path) -> int | None:
    for part in path.parts:
        if part.startswith("iter_"):
            try:
                return int(part.split("_", 1)[1])
            except ValueError:
                return None
    return None


def fresh_defaults(path: Path) -> dict[str, dict[str, float]]:
    defaults: dict[str, dict[str, float]] = {}
    for row in read_tsv(path):
        case = row.get("case", "")
        if case not in CASE_LABELS:
            continue
        defaults[case] = {
            "hpwl": as_float(row.get("default_H_f"), "default_H_f"),
            "runtime": as_float(row.get("default_runtime_s"), "default_runtime_s"),
        }
    missing = sorted(set(CASE_LABELS) - set(defaults))
    if missing:
        fail("fresh Table 4 summary lacks defaults for: " + ", ".join(missing))
    return defaults


def discover_round_dirs(state_root: Path, run_prefix: str, cases: Iterable[str]) -> dict[str, Path]:
    if not run_prefix:
        fail("fresh figure mode requires --run-prefix")
    candidates: dict[str, list[Path]] = defaultdict(list)
    for path in state_root.glob(f"{run_prefix}*"):
        if not (path / "teacher_rounds").is_dir():
            continue
        case = case_from_text(path.name, cases)
        if case:
            candidates[case].append(path)
    selected: dict[str, Path] = {}
    for case in cases:
        matches = candidates.get(case, [])
        if len(matches) != 1:
            fail(
                f"expected one ReviewDSE round for {case} with prefix {run_prefix!r}, "
                f"found {len(matches)}"
            )
        selected[case] = matches[0]
    return selected


def candidate_payloads(round_dir: Path) -> Iterable[tuple[Path, dict[str, Any]]]:
    pattern = "teacher_rounds/students/*/iter_*/artifacts/candidate_metrics_summary.json"
    for path in sorted(round_dir.glob(pattern)):
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        yield path, payload


def normalize_figure4(
    points: dict[str, dict[int, float]], defaults: dict[str, float], max_iteration: int
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for case, label in CASE_LABELS.items():
        if case not in defaults:
            fail(f"Figure 4 lacks a default HPWL for {case}")
        default = defaults[case]
        best = default
        for iteration in range(0, max_iteration + 1):
            if iteration in points.get(case, {}):
                best = min(best, points[case][iteration])
            delta = 100.0 * (best - default) / default
            rows.append(
                {
                    "case": case,
                    "case_label": label,
                    "iteration": iteration,
                    "best_so_far_hpwl_micron": f"{best:.6f}",
                    "best_so_far_delta_hpwl_percent": f"{delta:.9f}",
                    "best_so_far_reduction_percent": f"{-delta:.9f}",
                }
            )
    expected = len(CASE_LABELS) * (max_iteration + 1)
    if len(rows) != expected:
        fail(f"Figure 4 expected {expected} normalized rows, found {len(rows)}")
    return rows


def retained_figure4(path: Path, max_iteration: int) -> list[dict[str, Any]]:
    points: dict[str, dict[int, float]] = defaultdict(dict)
    defaults: dict[str, float] = {}
    for row in read_tsv(path):
        case = row.get("case", "")
        if case not in CASE_LABELS:
            fail(f"unexpected Figure 4 case: {case!r}")
        iteration = int(row["iteration"])
        if not 0 <= iteration <= max_iteration:
            continue
        hpwl = as_float(row.get("best_so_far_hpwl_micron"), "best_so_far_hpwl_micron")
        points[case][iteration] = hpwl
        if iteration == 0:
            defaults[case] = hpwl
    return normalize_figure4(points, defaults, max_iteration)


def fresh_figure4(
    state_root: Path, run_prefix: str, default_table: Path, max_iteration: int
) -> list[dict[str, Any]]:
    default_rows = fresh_defaults(default_table)
    defaults = {case: row["hpwl"] for case, row in default_rows.items()}
    points: dict[str, dict[int, float]] = defaultdict(dict)
    rounds = discover_round_dirs(state_root, run_prefix, CASE_LABELS)
    for case, round_dir in rounds.items():
        for path, payload in candidate_payloads(round_dir):
            iteration = iteration_from_path(path)
            if iteration is None or not 1 <= iteration <= max_iteration:
                continue
            canonical = payload.get("canonical") or {}
            if payload.get("status") not in (None, "ok") or canonical.get("legality") != "clean":
                continue
            hpwl = as_float(canonical.get("final_hpwl_micron"), "final_hpwl_micron")
            current = points[case].get(iteration)
            if current is None or hpwl < current:
                points[case][iteration] = hpwl
        if not points[case]:
            fail(f"no clean fresh ReviewDSE candidates found for {case}")
    return normalize_figure4(points, defaults, max_iteration)


def pareto_indices(rows: list[dict[str, Any]]) -> set[int]:
    ordered = sorted(
        enumerate(rows),
        key=lambda item: (
            as_float(item[1]["runtime_ratio"], "runtime_ratio"),
            as_float(item[1]["delta_hpwl_percent"], "delta_hpwl_percent"),
        ),
    )
    keep: set[int] = set()
    best_delta = float("inf")
    for index, row in ordered:
        delta = as_float(row["delta_hpwl_percent"], "delta_hpwl_percent")
        if delta < best_delta - 1e-12:
            keep.add(index)
            best_delta = delta
    return keep


def normalize_figure5(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    normalized: list[dict[str, Any]] = []
    for row in rows:
        case = str(row.get("case", ""))
        method = str(row.get("method", ""))
        if case not in FIG5_CASES or method not in ("OpenROAD BO", "OpenROAD Evolve"):
            continue
        runtime_ratio = as_float(row.get("runtime_ratio"), "runtime_ratio")
        delta = as_float(row.get("delta_hpwl_percent"), "delta_hpwl_percent")
        if runtime_ratio <= 0:
            fail(f"nonpositive runtime ratio for {case}/{method}")
        normalized.append(
            {
                "case": case,
                "case_label": FIG5_CASES[case],
                "method": method,
                "point_id": row.get("point_id", ""),
                "iteration": row.get("iteration", ""),
                "student": row.get("student", ""),
                "hpwl_final_micron": row.get("hpwl_final_micron", ""),
                "runtime_seconds": row.get("runtime_seconds", ""),
                "runtime_ratio": f"{runtime_ratio:.9f}",
                "delta_hpwl_percent": f"{delta:.9f}",
                "pareto": "false",
                "source_path": row.get("source_path", ""),
            }
        )
    grouped: dict[tuple[str, str], list[int]] = defaultdict(list)
    for index, row in enumerate(normalized):
        grouped[(row["case"], row["method"])].append(index)
    for indices in grouped.values():
        subset = [normalized[index] for index in indices]
        for local_index in pareto_indices(subset):
            normalized[indices[local_index]]["pareto"] = "true"
    for case in FIG5_CASES:
        bo_count = sum(
            row["case"] == case and row["method"] == "OpenROAD BO" for row in normalized
        )
        evolve_count = sum(
            row["case"] == case and row["method"] == "OpenROAD Evolve" for row in normalized
        )
        if bo_count != 400:
            fail(f"Figure 5 requires 400 BO trials for {case}, found {bo_count}")
        if evolve_count == 0:
            fail(f"Figure 5 has no ReviewDSE candidates for {case}")
    return normalized


def retained_figure5(path: Path) -> list[dict[str, Any]]:
    return normalize_figure5(read_tsv(path))


def locate_bo_trials(state_root: Path, flow_variant: str, case: str) -> Path:
    exact = (
        state_root
        / "bo_runs"
        / f"openroad_dpl_hpwl_only_9case_bo_{flow_variant}_{case}"
        / "trials.tsv"
    )
    if exact.is_file():
        return exact
    matches = list((state_root / "bo_runs").glob(f"*{flow_variant}_{case}/trials.tsv"))
    if len(matches) != 1:
        fail(f"expected one fresh BO trials.tsv for {case}, found {len(matches)}")
    return matches[0]


def fresh_figure5(
    state_root: Path, run_prefix: str, default_table: Path, flow_variant: str
) -> list[dict[str, Any]]:
    defaults = fresh_defaults(default_table)
    rows: list[dict[str, Any]] = []
    for case in FIG5_CASES:
        path = locate_bo_trials(state_root, flow_variant, case)
        for row in read_tsv(path):
            if row.get("status") != "ok":
                continue
            runtime = as_float(row.get("runtime"), "runtime")
            hpwl = as_float(row.get("hpwl_final"), "hpwl_final")
            rows.append(
                {
                    "case": case,
                    "method": "OpenROAD BO",
                    "point_id": row.get("trial", ""),
                    "hpwl_final_micron": hpwl,
                    "runtime_seconds": runtime,
                    "runtime_ratio": runtime / defaults[case]["runtime"],
                    "delta_hpwl_percent": 100.0
                    * (hpwl - defaults[case]["hpwl"])
                    / defaults[case]["hpwl"],
                    "source_path": str(path),
                }
            )
    rounds = discover_round_dirs(state_root, run_prefix, FIG5_CASES)
    for case, round_dir in rounds.items():
        for path, payload in candidate_payloads(round_dir):
            canonical = payload.get("canonical") or {}
            if payload.get("status") not in (None, "ok") or canonical.get("legality") != "clean":
                continue
            hpwl = as_float(canonical.get("final_hpwl_micron"), "final_hpwl_micron")
            runtime = as_float(canonical.get("runtime_seconds"), "runtime_seconds")
            iteration = iteration_from_path(path)
            if iteration is None:
                continue
            student = next((part for part in path.parts if part.startswith("student_")), "")
            rows.append(
                {
                    "case": case,
                    "method": "OpenROAD Evolve",
                    "point_id": f"iter_{iteration:02d}/{student}",
                    "iteration": f"iter_{iteration:02d}",
                    "student": student,
                    "hpwl_final_micron": hpwl,
                    "runtime_seconds": runtime,
                    "runtime_ratio": runtime / defaults[case]["runtime"],
                    "delta_hpwl_percent": 100.0
                    * (hpwl - defaults[case]["hpwl"])
                    / defaults[case]["hpwl"],
                    "source_path": str(path),
                }
            )
    return normalize_figure5(rows)


def svg_text(x: float, y: float, value: str, **attrs: Any) -> str:
    extras = " ".join(f'{key.replace("_", "-")}="{html.escape(str(val))}"' for key, val in attrs.items())
    return f'<text x="{x:.2f}" y="{y:.2f}" {extras}>{html.escape(value)}</text>'


def render_figure4(path: Path, rows: list[dict[str, Any]]) -> None:
    width, height = 900, 520
    left, right, top, bottom = 85, 245, 35, 65
    plot_w, plot_h = width - left - right, height - top - bottom
    deltas = [as_float(row["best_so_far_delta_hpwl_percent"], "delta") for row in rows]
    y_min = min(deltas) - 0.25
    y_max = max(0.1, max(deltas) + 0.1)

    def xy(iteration: int, delta: float) -> tuple[float, float]:
        x = left + plot_w * iteration / 10.0
        y = top + plot_h * (y_max - delta) / (y_max - y_min)
        return x, y

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:serif;fill:#222}.axis{stroke:#333;stroke-width:1}.grid{stroke:#ddd;stroke-width:1}.line{fill:none;stroke-width:2}</style>',
        svg_text(width / 2, 22, "Figure 4: 10-iteration best-so-far ΔHf", text_anchor="middle", font_size="16"),
    ]
    for iteration in range(0, 11, 2):
        x, _ = xy(iteration, 0)
        parts.append(f'<line class="grid" x1="{x}" y1="{top}" x2="{x}" y2="{top + plot_h}"/>')
        parts.append(svg_text(x, top + plot_h + 22, str(iteration), text_anchor="middle", font_size="12"))
    for tick in range(6):
        delta = y_min + (y_max - y_min) * tick / 5
        _, y = xy(0, delta)
        parts.append(f'<line class="grid" x1="{left}" y1="{y}" x2="{left + plot_w}" y2="{y}"/>')
        parts.append(svg_text(left - 8, y + 4, f"{delta:.1f}", text_anchor="end", font_size="12"))
    parts.extend([
        f'<line class="axis" x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}"/>',
        f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}"/>',
        svg_text(left + plot_w / 2, height - 20, "Evolve iteration", text_anchor="middle", font_size="14"),
        f'<text x="18" y="{top + plot_h / 2}" font-size="14" text-anchor="middle" transform="rotate(-90 18 {top + plot_h / 2})">Best-so-far ΔHf (%)</text>',
    ])
    by_case: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        by_case[row["case"]].append(row)
    for index, (case, label) in enumerate(CASE_LABELS.items()):
        color = COLORS[index]
        series = sorted(by_case[case], key=lambda row: int(row["iteration"]))
        points = " ".join(
            f"{x:.2f},{y:.2f}"
            for x, y in (
                xy(int(row["iteration"]), as_float(row["best_so_far_delta_hpwl_percent"], "delta"))
                for row in series
            )
        )
        parts.append(f'<polyline class="line" stroke="{color}" points="{points}"/>')
        legend_y = top + 18 + index * 31
        parts.append(f'<line x1="{left + plot_w + 25}" y1="{legend_y}" x2="{left + plot_w + 55}" y2="{legend_y}" stroke="{color}" stroke-width="3"/>')
        parts.append(svg_text(left + plot_w + 65, legend_y + 4, label, font_size="12"))
    parts.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def render_figure5(path: Path, rows: list[dict[str, Any]]) -> None:
    width, height = 940, 440
    panel_w, panel_h, top = 380, 320, 45
    origins = [(70, top), (540, top)]
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:serif;fill:#222}.axis{stroke:#333}.grid{stroke:#ddd}.frontier{fill:none;stroke-width:2}</style>',
        svg_text(width / 2, 22, "Figure 5: Runtime-quality frontier", text_anchor="middle", font_size="16"),
    ]
    for (case, label), (left, panel_top) in zip(FIG5_CASES.items(), origins):
        case_rows = [row for row in rows if row["case"] == case]
        xs = [as_float(row["runtime_ratio"], "runtime_ratio") for row in case_rows] + [1.0]
        gains = [-as_float(row["delta_hpwl_percent"], "delta") for row in case_rows] + [0.0]
        x_min, x_max = min(xs) - 0.04, max(xs) + 0.04
        y_min, y_max = min(gains) - 0.25, max(gains) + 0.4

        def xy(runtime: float, gain: float) -> tuple[float, float]:
            x = left + panel_w * (runtime - x_min) / (x_max - x_min)
            y = panel_top + panel_h * (y_max - gain) / (y_max - y_min)
            return x, y

        parts.append(svg_text(left, panel_top - 10, label, font_size="14", font_weight="bold"))
        for tick in range(5):
            runtime = x_min + (x_max - x_min) * tick / 4
            x, _ = xy(runtime, 0)
            parts.append(f'<line class="grid" x1="{x}" y1="{panel_top}" x2="{x}" y2="{panel_top + panel_h}"/>')
            parts.append(svg_text(x, panel_top + panel_h + 20, f"{runtime:.1f}", text_anchor="middle", font_size="11"))
        for tick in range(5):
            gain = y_min + (y_max - y_min) * tick / 4
            _, y = xy(1, gain)
            parts.append(f'<line class="grid" x1="{left}" y1="{y}" x2="{left + panel_w}" y2="{y}"/>')
            if left == origins[0][0]:
                parts.append(svg_text(left - 7, y + 4, f"{gain:.1f}", text_anchor="end", font_size="11"))
        for method, color, radius in (("OpenROAD BO", "#c7771b", 2.2), ("OpenROAD Evolve", "#245a86", 4.0)):
            method_rows = [row for row in case_rows if row["method"] == method]
            for row in method_rows:
                x, y = xy(
                    as_float(row["runtime_ratio"], "runtime_ratio"),
                    -as_float(row["delta_hpwl_percent"], "delta"),
                )
                opacity = "0.28" if method == "OpenROAD BO" else "0.82"
                parts.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{radius}" fill="{color}" opacity="{opacity}"/>')
            frontier = sorted(
                (row for row in method_rows if row["pareto"] == "true"),
                key=lambda row: as_float(row["runtime_ratio"], "runtime_ratio"),
            )
            if frontier:
                points = " ".join(
                    f"{x:.2f},{y:.2f}"
                    for x, y in (
                        xy(as_float(row["runtime_ratio"], "runtime_ratio"), -as_float(row["delta_hpwl_percent"], "delta"))
                        for row in frontier
                    )
                )
                parts.append(f'<polyline class="frontier" stroke="{color}" points="{points}"/>')
        x, y = xy(1.0, 0.0)
        parts.append(f'<polygon points="{x},{y-7} {x+2},{y-2} {x+7},{y-2} {x+3},{y+1} {x+5},{y+7} {x},{y+3} {x-5},{y+7} {x-3},{y+1} {x-7},{y-2} {x-2},{y-2}" fill="#222"/>')
        parts.extend([
            f'<line class="axis" x1="{left}" y1="{panel_top + panel_h}" x2="{left + panel_w}" y2="{panel_top + panel_h}"/>',
            f'<line class="axis" x1="{left}" y1="{panel_top}" x2="{left}" y2="{panel_top + panel_h}"/>',
            svg_text(left + panel_w / 2, height - 22, "Runtime / default", text_anchor="middle", font_size="13"),
        ])
    parts.append(f'<text x="18" y="{top + panel_h / 2}" font-size="13" text-anchor="middle" transform="rotate(-90 18 {top + panel_h / 2})">Hf reduction (%)</text>')
    parts.extend([
        '<circle cx="345" cy="420" r="3" fill="#c7771b"/><text x="354" y="424" font-size="11">BO trials</text>',
        '<circle cx="455" cy="420" r="4" fill="#245a86"/><text x="465" y="424" font-size="11">ReviewDSE candidates</text>',
        '<text x="610" y="424" font-size="14">★</text><text x="628" y="424" font-size="11">Default</text>',
        "</svg>",
    ])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("figure", choices=("figure4", "figure5"))
    parser.add_argument("--source", choices=("retained", "fresh"), default="retained")
    parser.add_argument("--artifact-root", type=Path, required=True)
    parser.add_argument("--state-root", type=Path, required=True)
    parser.add_argument("--run-prefix", default="")
    parser.add_argument("--flow-variant", default="paper9_place")
    parser.add_argument("--max-iteration", type=int, default=10)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if args.max_iteration != 10:
        fail("the paper Figure 4 contract requires exactly 10 iterations")
    default_table = args.state_root / "paper_reproduction/table4/table4-fresh.tsv"
    retained_root = args.artifact_root / "artifacts/01-table4-qor/inputs/figures"
    if args.source == "retained":
        verify_manifest(retained_root, retained_root / "MANIFEST.sha256")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    if args.figure == "figure4":
        rows = (
            retained_figure4(retained_root / "evolve_hpwl_reduction_curves.tsv", 10)
            if args.source == "retained"
            else fresh_figure4(args.state_root, args.run_prefix, default_table, 10)
        )
        fields = [
            "case", "case_label", "iteration", "best_so_far_hpwl_micron",
            "best_so_far_delta_hpwl_percent", "best_so_far_reduction_percent",
        ]
        data_path = args.output_dir / "figure4-best-so-far.tsv"
        figure_path = args.output_dir / "figure4-best-so-far.svg"
        write_tsv(data_path, rows, fields)
        render_figure4(figure_path, rows)
        print(f"[PASS] Figure 4: 9 cases x 11 points (iteration 0..10): {figure_path}")
    else:
        rows = (
            retained_figure5(retained_root / "bo_evolve_pareto_points.tsv")
            if args.source == "retained"
            else fresh_figure5(args.state_root, args.run_prefix, default_table, args.flow_variant)
        )
        fields = [
            "case", "case_label", "method", "point_id", "iteration", "student",
            "hpwl_final_micron", "runtime_seconds", "runtime_ratio",
            "delta_hpwl_percent", "pareto", "source_path",
        ]
        data_path = args.output_dir / "figure5-runtime-quality.tsv"
        figure_path = args.output_dir / "figure5-runtime-quality.svg"
        write_tsv(data_path, rows, fields)
        render_figure5(figure_path, rows)
        print(f"[PASS] Figure 5: 800 BO trials plus ReviewDSE candidates: {figure_path}")
    print(f"[PASS] normalized data: {data_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, csv.Error, json.JSONDecodeError) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
