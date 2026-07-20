#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def nested(data: dict[str, Any], *keys: str) -> Any:
    value: Any = data
    for key in keys:
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return value


def validate(
    lock: dict[str, Any], input_odb: Path, metrics: dict[str, Any]
) -> list[str]:
    failures: list[str] = []
    smoke = lock["aes_nangate45_smoke"]
    expected = smoke["expected"]
    tolerance = smoke["absolute_tolerances"]

    if not input_odb.is_file():
        return [f"input ODB is missing: {input_odb}"]
    actual_hash = file_sha256(input_odb)
    if actual_hash != smoke["input_odb_sha256"]:
        failures.append(
            f"input ODB SHA-256 is {actual_hash}, expected {smoke['input_odb_sha256']}"
        )

    exact_checks = (
        ("status", metrics.get("status"), "ok"),
        ("legalize_exit_status", metrics.get("legalize_exit_status"), expected["legalize_exit_status"]),
        ("instance_count", nested(metrics, "design_metrics", "instance_count"), expected["instance_count"]),
        ("metric_error_count", nested(metrics, "metrics_stage", "error_count"), expected["metric_error_count"]),
        (
            "placement_violations",
            nested(metrics, "legality", "placement_violations"),
            expected["placement_violations"],
        ),
    )
    for label, actual, wanted in exact_checks:
        if actual != wanted:
            failures.append(f"{label} is {actual!r}, expected {wanted!r}")

    float_checks = (
        (
            "instance_area_micron2",
            nested(metrics, "design_metrics", "instance_area"),
            expected["instance_area_micron2"],
        ),
        (
            "global_hpwl_micron",
            nested(metrics, "hpwl_stages", "global_micron"),
            expected["global_hpwl_micron"],
        ),
        (
            "final_hpwl_micron",
            nested(metrics, "hpwl_stages", "final_micron"),
            expected["final_hpwl_micron"],
        ),
    )
    for label, actual, wanted in float_checks:
        if actual is None:
            failures.append(f"{label} is missing")
            continue
        try:
            delta = abs(float(actual) - float(wanted))
        except (TypeError, ValueError):
            failures.append(f"{label} is not numeric: {actual!r}")
            continue
        if delta > float(tolerance[label]):
            failures.append(
                f"{label} is {actual}, expected {wanted} +/- {tolerance[label]}"
            )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the pinned AES smoke result.")
    parser.add_argument("--lock", required=True, type=Path)
    parser.add_argument("--input-odb", required=True, type=Path)
    parser.add_argument("--metrics", required=True, type=Path)
    args = parser.parse_args()

    if not args.metrics.is_file():
        print(f"[FAIL] metrics JSON is missing: {args.metrics}")
        return 1
    try:
        lock = json.loads(args.lock.read_text(encoding="utf-8"))
        metrics = json.loads(args.metrics.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"[FAIL] could not read validation input: {error}")
        return 1

    failures = validate(lock, args.input_odb, metrics)
    if failures:
        for failure in failures:
            print(f"[FAIL] {failure}")
        return 1
    smoke = lock["aes_nangate45_smoke"]
    print("[OK] AES input checksum and structural metrics match the pinned reference.")
    print(
        "[OK] OpenROAD default HPWL: "
        f"{smoke['expected']['global_hpwl_micron']} -> "
        f"{smoke['expected']['final_hpwl_micron']} micron."
    )
    print("[OK] Exit status, legality, and metric error count are clean.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
