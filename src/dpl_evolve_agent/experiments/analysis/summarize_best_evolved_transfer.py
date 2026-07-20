#!/usr/bin/env python3
"""Summarize fixed-source cross-case transfer matrices for article tables."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


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


def as_float(value: str | None) -> float | None:
    if value in (None, ""):
        return None
    try:
        return float(value)
    except ValueError:
        return None


def runtime_penalty(runtime_ratio: float | None) -> float:
    if runtime_ratio is None:
        return 0.0
    if runtime_ratio <= 1.10:
        return 0.0
    return (math.sqrt(runtime_ratio) - math.sqrt(1.10)) / (math.sqrt(2.0) - math.sqrt(1.10))


def score(default_hpwl: float, candidate_hpwl: float, runtime_ratio: float | None) -> float:
    hpwl_gain = 100.0 * (default_hpwl - candidate_hpwl) / default_hpwl
    return hpwl_gain - runtime_penalty(runtime_ratio)


def read_default_table(path: Path) -> dict[str, dict[str, float]]:
    defaults: dict[str, dict[str, float]] = {}
    with path.open(encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            if row.get("method") != "OpenROAD default":
                continue
            hpwl = as_float(row.get("hpwl_final_micron"))
            runtime = as_float(row.get("runtime_seconds"))
            if hpwl is None or runtime is None:
                continue
            defaults[row["case"]] = {"hpwl": hpwl, "runtime": runtime}
    return defaults


def read_manifest(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def format_pct(value: float | None) -> str:
    if value is None:
        return ""
    return f"{value:+.3f}"


def format_float(value: float | None, digits: int = 3) -> str:
    if value is None:
        return ""
    return f"{value:.{digits}f}"


def summarize(args: argparse.Namespace) -> None:
    defaults = read_default_table(args.default_table)
    manifest_rows = read_manifest(args.manifest)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    matrix_fields = [
        "program",
        "discovery_case",
        "target_case",
        "status",
        "default_hpwl",
        "candidate_hpwl",
        "delta_hpwl_percent",
        "default_runtime_s",
        "candidate_runtime_s",
        "runtime_ratio",
        "score",
        "results_tsv",
    ]
    summary_fields = [
        "program",
        "discovery_case",
        "discovery_delta_hpwl_percent",
        "wins_all",
        "valid_all",
        "mean_delta_hpwl_percent_all",
        "mean_score_all",
        "mean_runtime_ratio_all",
        "wins_transfer",
        "valid_transfer",
        "mean_delta_hpwl_percent_transfer",
        "mean_score_transfer",
        "mean_runtime_ratio_transfer",
        "best_target",
        "best_target_delta_hpwl_percent",
        "worst_target",
        "worst_target_delta_hpwl_percent",
    ]

    matrix_rows: list[dict[str, str]] = []
    summary_rows: list[dict[str, str]] = []
    for item in manifest_rows:
        program = item["program"]
        discovery_case = item["discovery_case"]
        results_tsv = args.matrix_root / program / "results.tsv"
        if not results_tsv.is_file():
            results_tsv = args.matrix_root / f"{args.matrix_prefix}_{program}" / "results.tsv"
        if not results_tsv.is_file():
            summary_rows.append(
                {
                    "program": program,
                    "discovery_case": discovery_case,
                    "valid_all": "0",
                    "valid_transfer": "0",
                }
            )
            continue
        with results_tsv.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        per_program: list[dict[str, float | str]] = []
        for row in rows:
            target_case = row.get("case", "")
            default = defaults.get(target_case)
            candidate_hpwl = as_float(row.get("hpwl_after_micron"))
            candidate_runtime = as_float(row.get("runtime_seconds"))
            status = row.get("status", "")
            if not default or candidate_hpwl is None or status != "PASS":
                matrix_rows.append(
                    {
                        "program": program,
                        "discovery_case": discovery_case,
                        "target_case": target_case,
                        "status": status or "MISSING",
                        "results_tsv": str(results_tsv),
                    }
                )
                continue
            runtime_ratio = None
            if candidate_runtime is not None and default["runtime"] > 0:
                runtime_ratio = candidate_runtime / default["runtime"]
            delta_pct = 100.0 * (candidate_hpwl - default["hpwl"]) / default["hpwl"]
            row_score = score(default["hpwl"], candidate_hpwl, runtime_ratio)
            per_program.append(
                {
                    "target_case": target_case,
                    "delta_pct": delta_pct,
                    "runtime_ratio": runtime_ratio if runtime_ratio is not None else "",
                    "score": row_score,
                }
            )
            matrix_rows.append(
                {
                    "program": program,
                    "discovery_case": discovery_case,
                    "target_case": target_case,
                    "status": "PASS",
                    "default_hpwl": format_float(default["hpwl"], 1),
                    "candidate_hpwl": format_float(candidate_hpwl, 1),
                    "delta_hpwl_percent": format_pct(delta_pct),
                    "default_runtime_s": format_float(default["runtime"], 3),
                    "candidate_runtime_s": format_float(candidate_runtime, 3),
                    "runtime_ratio": format_float(runtime_ratio),
                    "score": format_float(row_score),
                    "results_tsv": str(results_tsv),
                }
            )

        def aggregate(items: list[dict[str, float | str]]) -> dict[str, float | int | str | None]:
            numeric = [x for x in items if isinstance(x.get("delta_pct"), float)]
            if not numeric:
                return {
                    "valid": 0,
                    "wins": 0,
                    "mean_delta": None,
                    "mean_score": None,
                    "mean_rtx": None,
                    "best_target": "",
                    "best_delta": None,
                    "worst_target": "",
                    "worst_delta": None,
                }
            best = min(numeric, key=lambda x: float(x["delta_pct"]))
            worst = max(numeric, key=lambda x: float(x["delta_pct"]))
            rtx_values = [float(x["runtime_ratio"]) for x in numeric if isinstance(x.get("runtime_ratio"), float)]
            return {
                "valid": len(numeric),
                "wins": sum(1 for x in numeric if float(x["delta_pct"]) < 0.0),
                "mean_delta": sum(float(x["delta_pct"]) for x in numeric) / len(numeric),
                "mean_score": sum(float(x["score"]) for x in numeric) / len(numeric),
                "mean_rtx": sum(rtx_values) / len(rtx_values) if rtx_values else None,
                "best_target": str(best["target_case"]),
                "best_delta": float(best["delta_pct"]),
                "worst_target": str(worst["target_case"]),
                "worst_delta": float(worst["delta_pct"]),
            }

        all_stats = aggregate(per_program)
        transfer_stats = aggregate([x for x in per_program if x["target_case"] != discovery_case])
        discovery_delta = next(
            (float(x["delta_pct"]) for x in per_program if x["target_case"] == discovery_case),
            None,
        )
        summary_rows.append(
            {
                "program": program,
                "discovery_case": discovery_case,
                "discovery_delta_hpwl_percent": format_pct(discovery_delta),
                "wins_all": str(all_stats["wins"]),
                "valid_all": str(all_stats["valid"]),
                "mean_delta_hpwl_percent_all": format_pct(all_stats["mean_delta"]),
                "mean_score_all": format_float(all_stats["mean_score"]),
                "mean_runtime_ratio_all": format_float(all_stats["mean_rtx"]),
                "wins_transfer": str(transfer_stats["wins"]),
                "valid_transfer": str(transfer_stats["valid"]),
                "mean_delta_hpwl_percent_transfer": format_pct(transfer_stats["mean_delta"]),
                "mean_score_transfer": format_float(transfer_stats["mean_score"]),
                "mean_runtime_ratio_transfer": format_float(transfer_stats["mean_rtx"]),
                "best_target": str(best := all_stats["best_target"]),
                "best_target_delta_hpwl_percent": format_pct(all_stats["best_delta"]),
                "worst_target": str(worst := all_stats["worst_target"]),
                "worst_target_delta_hpwl_percent": format_pct(all_stats["worst_delta"]),
            }
        )

    summary_rows.sort(
        key=lambda row: as_float(row.get("mean_delta_hpwl_percent_transfer")) or 999.0
    )

    matrix_path = args.output_dir / "evolved_program_cross_case_matrix.tsv"
    summary_path = args.output_dir / "evolved_program_transfer_summary.tsv"
    markdown_path = args.output_dir / "evolved_program_transfer_summary.md"
    with matrix_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=matrix_fields, delimiter="\t")
        writer.writeheader()
        writer.writerows(matrix_rows)
    with summary_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=summary_fields, delimiter="\t")
        writer.writeheader()
        writer.writerows(summary_rows)
    with markdown_path.open("w", encoding="utf-8") as stream:
        stream.write(
            "| Program | Discovery | Discovery delta | Transfer wins | Mean transfer delta | Mean transfer score | Mean transfer RTx | Best target | Worst target |\n"
        )
        stream.write("|---|---|---:|---:|---:|---:|---:|---|---|\n")
        for row in summary_rows:
            stream.write(
                "| {program} | {discovery} | {disc_delta}% | {wins}/{valid} | {mean_delta}% | {mean_score} | {mean_rtx}x | {best} ({best_delta}%) | {worst} ({worst_delta}%) |\n".format(
                    program=row["program"].replace("P_", "").replace("_", " "),
                    discovery=CASE_LABELS.get(row["discovery_case"], row["discovery_case"]),
                    disc_delta=row["discovery_delta_hpwl_percent"] or "",
                    wins=row["wins_transfer"],
                    valid=row["valid_transfer"],
                    mean_delta=row["mean_delta_hpwl_percent_transfer"] or "",
                    mean_score=row["mean_score_transfer"] or "",
                    mean_rtx=row["mean_runtime_ratio_transfer"] or "",
                    best=CASE_LABELS.get(row["best_target"], row["best_target"]),
                    best_delta=row["best_target_delta_hpwl_percent"] or "",
                    worst=CASE_LABELS.get(row["worst_target"], row["worst_target"]),
                    worst_delta=row["worst_target_delta_hpwl_percent"] or "",
                )
            )
    print(f"[INFO] wrote {matrix_path}")
    print(f"[INFO] wrote {summary_path}")
    print(f"[INFO] wrote {markdown_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--matrix-root", required=True, type=Path)
    parser.add_argument("--matrix-prefix", default="")
    parser.add_argument("--default-table", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    summarize(parser.parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
