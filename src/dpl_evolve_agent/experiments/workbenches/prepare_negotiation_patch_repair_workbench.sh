#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "prepare_negotiation_patch_repair_workbench.sh"

WORKBENCH_ROOT="${DPL_EVOLVE_STATE_ROOT}/negotiation_patch_repair_worker"
FORCE=0

usage() {
  cat <<'EOF'
Usage: prepare_negotiation_patch_repair_workbench.sh [options]

Create an isolated workbench for auditing, repairing, or further improving the
evolved_negotiation legalizer patch.  The generated worker scripts let a
sub-agent:

  1. Materialize a private dpl_evolve candidate from framework seed plus the
     current evolved_negotiation framework-delta patch.
  2. Edit only that private dpl_evolve source.
  3. Build and evaluate it on a fixed 3-case full-flow matrix.
  4. Compare stage-wise metrics against OpenROAD's negotiation baseline.
  5. Export candidate framework-delta and from-clean patch files for review.
  6. Validate exported patches apply to their declared bases.

Options:
  --workbench-root PATH  Output directory. Default:
                         $DPL_EVOLVE_STATE_ROOT/negotiation_patch_repair_worker
  --force                Replace generated worker scripts and source copies.
  --help                 Show this message.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workbench-root)
      WORKBENCH_ROOT="$2"
      shift 2
      ;;
    --force)
      FORCE=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[ERROR] Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

WORKBENCH_ROOT="$(realpath -m "${WORKBENCH_ROOT}")"
FRAMEWORK_SEED="${DPL_EVOLVE_STATE_ROOT}/seed_sources/framework_dpl_evolve"
NEG_PATCH="${DPL_EVOLVE_AGENT_ROOT}/patches/evolved_legalizers/openroad_dpl_evolve_negotiation_iter30_framework_delta.patch"

if [[ ! -f "${FRAMEWORK_SEED}/CMakeLists.txt" ]]; then
  echo "[ERROR] Missing framework seed: ${FRAMEWORK_SEED}" >&2
  echo "[ERROR] Run scripts/workspace/prepare_workspace.sh first." >&2
  exit 1
fi
if [[ ! -s "${NEG_PATCH}" ]]; then
  echo "[ERROR] Missing evolved negotiation framework delta: ${NEG_PATCH}" >&2
  exit 1
fi

mkdir -p \
  "${WORKBENCH_ROOT}/plans" \
  "${WORKBENCH_ROOT}/scripts" \
  "${WORKBENCH_ROOT}/source" \
  "${WORKBENCH_ROOT}/output" \
  "${WORKBENCH_ROOT}/reports" \
  "${WORKBENCH_ROOT}/validation"

cat > "${WORKBENCH_ROOT}/plans/threecase_negotiation_repair.tsv" <<'EOF'
enabled	case	core_utilization	flow_variant	round_id	start_kind	notes
1	aes_nangate45	default	place_batch_20260421_220319	negotiation_patch_repair	evolved_negotiation	fast standard-cell sanity case
1	jpeg_nangate45	default	place_batch_20260421_220319	negotiation_patch_repair	evolved_negotiation	dense datapath-like standard-cell case
1	swerv_wrapper_nangate45	default	place_batch_20260421_220319	negotiation_patch_repair	evolved_negotiation	larger control/datapath case
EOF

cat > "${WORKBENCH_ROOT}/README.md" <<'EOF'
# Negotiation Patch Repair Workbench

This workbench is for auditing, repairing, or improving the
`evolved_negotiation` legalizer patch.

Root:

```text
__WORKBENCH_ROOT__
```

The historical failure mode for this patch family is route semantic mismatch:
an `evolved_negotiation` source can accidentally become LEGALM-first and call
negotiation only as fallback repair.  That makes negotiation internals mostly
non-executed on clean successful runs.  This workbench validates against that
regression and provides a safe place to improve the negotiation-primary route.

The candidate program must remain negotiation-primary under the normal
evaluator path:

```text
detailed_placement_evolve -> improve_placement_evolve -> optimize_mirroring_evolve
```

Use the scripts in order:

```bash
__WORKBENCH_ROOT__/scripts/00_prepare_source.sh --force
# edit __WORKBENCH_ROOT__/source/evolved_negotiation_candidate
__WORKBENCH_ROOT__/scripts/20_run_3case_eval.sh --threads 8 --max-parallel 3
__WORKBENCH_ROOT__/scripts/30_report_vs_negotiation.py
__WORKBENCH_ROOT__/scripts/40_export_candidate_patches.sh
__WORKBENCH_ROOT__/scripts/50_validate_exported_patches.sh
```

Do not edit evaluator scripts, baseline scripts, ORFS flow scripts, or tracked
patch files from the main repo.  Export candidate patches under
`__WORKBENCH_ROOT__/output/`; the parent agent will inspect and replace the
tracked patch artifacts after validation.
EOF

python3 - "${WORKBENCH_ROOT}/README.md" "${WORKBENCH_ROOT}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8").replace("__WORKBENCH_ROOT__", sys.argv[2])
path.write_text(text, encoding="utf-8")
PY

cat > "${WORKBENCH_ROOT}/WORKER_TASK.md" <<'EOF'
# Worker Task: Audit or Improve evolved_negotiation

## Objective

Produce a useful, verified `evolved_negotiation` legalizer program that can
replace or improve the current tracked evolved-negotiation patch.

## Hard Constraints

- Work only in this workbench's private source tree:
  `source/evolved_negotiation_candidate`.
- Do not modify baseline/evaluator/ORFS flow scripts.
- Do not modify tracked repo patch files directly.
- Do not use case-name hardcoding.
- Do not solve the issue by Tcl/flow options.  If a parameter must change,
  encode it inside the C++ algorithm/default policy.
- The candidate route must be negotiation-primary under
  `detailed_placement_evolve`; negotiation must not be only a fallback after a
  successful LEGALM legalization.
- LEGALM/differential guidance may be used only as an input/frontier to
  negotiation.  It must not be the successful primary legalizer for this line.
- The differential guidance objective, stopping point, and handoff payload are
  valid search variables.  Prefer stopping before full LEGALM over-commitment
  when the goal is to seed a negotiation-friendly basin.
- Keep lightweight liveness metrics proving the route ran:
  `dpl_evolve__pipeline__negotiation_primary_used`, negotiation phase metrics,
  candidate/conflict counters, and legality status.

## Required Validation

Run full flow on at least these three cases with `place_batch_20260421_220319`:

- `aes_nangate45`
- `jpeg_nangate45`
- `swerv_wrapper_nangate45`

The comparison baseline is OpenROAD's canonical negotiation line:

```text
detailed_placement -use_negotiation -> improve_placement -> optimize_mirroring
```

Use:

```bash
scripts/20_run_3case_eval.sh --threads 8 --max-parallel 3
scripts/30_report_vs_negotiation.py
```

Report for each case:

- legality
- HPWL global
- HPWL after legalization
- HPWL after improve placement
- final HPWL
- final HPWL delta vs OpenROAD negotiation, both microns and percent
- runtime and runtime ratio vs OpenROAD negotiation
- avg/max displacement
- log evidence that negotiation-primary executed

## Expected Implementation Direction

A reasonable baseline implementation is:

1. `runDifferentialGuidance(context)` may generate guided initial locations.
2. `commitGuidedInitialLocationsToDb()` may seed DB locations if source review
   confirms it is safe.
3. `runEvolveNegotiationRepair(...)` or an equivalent compact call should run
   `NegotiationLegalizer::legalize()` as the primary legalizer.
4. The function should return based on negotiation legality, not LEGALM-only
   success.

If you choose a different route, explain why it is still negotiation-primary.

## Final Deliverables

- Modified source tree:
  `source/evolved_negotiation_candidate`
- Evaluation matrix id and metrics paths.
- Report files under `reports/`.
- Candidate patches under `output/`:
  - `openroad_dpl_evolve_negotiation_iter30_framework_delta.patch`
  - `openroad_dpl_evolve_negotiation_iter30_from_clean.patch`
- Validation output from `scripts/50_validate_exported_patches.sh`.
EOF

cat > "${WORKBENCH_ROOT}/scripts/00_prepare_source.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail

WB_ROOT="${WORKBENCH_ROOT}"
AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT}"
FRAMEWORK_SEED="${FRAMEWORK_SEED}"
NEG_PATCH="${NEG_PATCH}"
FORCE=0

while [[ \$# -gt 0 ]]; do
  case "\$1" in
    --force) FORCE=1; shift ;;
    --help|-h)
      echo "Usage: 00_prepare_source.sh [--force]"
      exit 0
      ;;
    *) echo "[ERROR] Unknown argument: \$1" >&2; exit 1 ;;
  esac
done

FRAMEWORK_COPY="\${WB_ROOT}/source/framework_dpl_evolve"
CANDIDATE="\${WB_ROOT}/source/evolved_negotiation_candidate"

if [[ "\${FORCE}" -eq 1 ]]; then
  rm -rf "\${FRAMEWORK_COPY}" "\${CANDIDATE}"
fi
if [[ -e "\${CANDIDATE}" ]]; then
  echo "[INFO] Candidate source already exists: \${CANDIDATE}"
  echo "[INFO] Use --force to recreate it."
  exit 0
fi

mkdir -p "\${WB_ROOT}/source"
rsync -a --delete --exclude '.git/' "\${FRAMEWORK_SEED}/" "\${FRAMEWORK_COPY}/"
rsync -a --delete --exclude '.git/' "\${FRAMEWORK_SEED}/" "\${CANDIDATE}/"

git init "\${CANDIDATE}" >/dev/null
git -C "\${CANDIDATE}" config user.email "dpl-evolve-agent@example.invalid"
git -C "\${CANDIDATE}" config user.name "dpl-evolve-agent"
git -C "\${CANDIDATE}" add -A
git -C "\${CANDIDATE}" commit -m "Seed framework dpl_evolve source" >/dev/null
git -C "\${CANDIDATE}" rev-parse HEAD > "\${WB_ROOT}/source/framework_base_commit.txt"

git -C "\${CANDIDATE}" apply --whitespace=nowarn -p3 "\${NEG_PATCH}"
git -C "\${CANDIDATE}" add -A
git -C "\${CANDIDATE}" commit -m "Apply current evolved_negotiation patch" >/dev/null
git -C "\${CANDIDATE}" rev-parse HEAD > "\${WB_ROOT}/source/current_negotiation_patch_commit.txt"

{
  echo "# Initial Route Audit"
  echo
  echo "- candidate: \${CANDIDATE}"
  echo "- framework base commit: \$(cat "\${WB_ROOT}/source/framework_base_commit.txt")"
  echo "- current negotiation patch commit: \$(cat "\${WB_ROOT}/source/current_negotiation_patch_commit.txt")"
  echo
  echo "## StudentAlgorithm route lines"
  rg -n "runDifferentialGuidance|runLegalmFullLegalization|runEvolveNegotiationRepair|negotiation_primary|legalm_only|policy_repair_used" "\${CANDIDATE}/src/StudentAlgorithm.cpp" || true
} > "\${WB_ROOT}/reports/initial_route_audit.md"

echo "[INFO] Prepared candidate source: \${CANDIDATE}"
echo "[INFO] Initial route audit: \${WB_ROOT}/reports/initial_route_audit.md"
EOF

cat > "${WORKBENCH_ROOT}/scripts/20_run_3case_eval.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail

WB_ROOT="${WORKBENCH_ROOT}"
AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT}"
CANDIDATE="\${WB_ROOT}/source/evolved_negotiation_candidate"
PLAN="\${WB_ROOT}/plans/threecase_negotiation_repair.tsv"
THREADS=8
MAX_PARALLEL=3
MATRIX_ID=""
SKIP_PLACE=0
SKIP_BASELINE=0

while [[ \$# -gt 0 ]]; do
  case "\$1" in
    --threads) THREADS="\$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="\$2"; shift 2 ;;
    --matrix-id) MATRIX_ID="\$2"; shift 2 ;;
    --skip-place) SKIP_PLACE=1; shift ;;
    --skip-baseline) SKIP_BASELINE=1; shift ;;
    --help|-h)
      echo "Usage: 20_run_3case_eval.sh [--threads N] [--max-parallel N] [--matrix-id ID] [--skip-place] [--skip-baseline]"
      exit 0
      ;;
    *) echo "[ERROR] Unknown argument: \$1" >&2; exit 1 ;;
  esac
done

if [[ ! -f "\${CANDIDATE}/CMakeLists.txt" ]]; then
  echo "[ERROR] Candidate source missing. Run scripts/00_prepare_source.sh first." >&2
  exit 1
fi
if [[ -z "\${MATRIX_ID}" ]]; then
  MATRIX_ID="negotiation_patch_repair_\$(date +%Y%m%d_%H%M%S)"
fi

cmd=(
  "\${AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh"
  --matrix-id "\${MATRIX_ID}"
  --output-root "\${WB_ROOT}/candidate_matrices"
  --candidate-src "\${CANDIDATE}"
  --candidate-label "negotiation_repair_candidate"
  --plan "\${PLAN}"
  --threads "\${THREADS}"
  --max-parallel "\${MAX_PARALLEL}"
)
if [[ "\${SKIP_PLACE}" -eq 1 ]]; then
  cmd+=(--skip-place)
fi
if [[ "\${SKIP_BASELINE}" -eq 1 ]]; then
  cmd+=(--skip-baseline)
fi

printf '%s\n' "\${MATRIX_ID}" > "\${WB_ROOT}/latest_matrix_id.txt"
printf '[INFO] matrix_id=%s\n' "\${MATRIX_ID}"
"\${cmd[@]}"
EOF

cat > "${WORKBENCH_ROOT}/scripts/30_report_vs_negotiation.py" <<'EOF'
#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import subprocess
from pathlib import Path
from typing import Any

WB_ROOT = Path("__WORKBENCH_ROOT__")
AGENT_ROOT = Path("__DPL_EVOLVE_AGENT_ROOT__")
ORFS_ROOT = Path("__ORFS_ROOT__")
STATE_ROOT = Path("__DPL_EVOLVE_STATE_ROOT__")


def as_float(value: Any) -> float | None:
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def fmt(value: float | None, digits: int = 1) -> str:
    if value is None:
        return ""
    return f"{value:.{digits}f}"


def case_field(case_id: str, field: str) -> str:
    out = subprocess.check_output(
        ["python3", str(AGENT_ROOT / "scripts/repo/case_registry.py"), "--case", case_id, "--field", field],
        text=True,
    )
    return out.strip()


def load_metrics(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {"missing": True, "path": str(path)}
    data = json.loads(path.read_text(encoding="utf-8"))
    stages = data.get("hpwl_stages") if isinstance(data.get("hpwl_stages"), dict) else {}
    hpwl = data.get("hpwl") if isinstance(data.get("hpwl"), dict) else {}
    disp = data.get("displacement") if isinstance(data.get("displacement"), dict) else {}
    legality = data.get("legality") if isinstance(data.get("legality"), dict) else {}
    violations = str(legality.get("placement_violations", "")).strip()
    return {
        "missing": False,
        "path": str(path),
        "hpwlg": as_float(stages.get("global_micron")) or as_float(hpwl.get("before_micron")),
        "hpwl_legalized": as_float(stages.get("legalized_micron")),
        "hpwl_after_improve": as_float(stages.get("after_improve_micron")),
        "hpwl_final": as_float(stages.get("final_micron")) or as_float(hpwl.get("after_micron")),
        "runtime": as_float(data.get("runtime_seconds")),
        "avg_disp": as_float(disp.get("average_displacement_micron")),
        "max_disp": as_float(disp.get("max_displacement_micron")),
        "legality": "clean" if violations == "" else violations,
    }


def metric_path(case_id: str, flow_variant: str, run_tag: str) -> Path:
    platform = case_field(case_id, "platform")
    design = case_field(case_id, "design")
    return (
        ORFS_ROOT
        / "flow"
        / "reports"
        / platform
        / design
        / flow_variant
        / "dpl_evolve_baseline"
        / run_tag
        / "metrics.json"
    )


def legalize_log_path(case_id: str, flow_variant: str, run_tag: str) -> Path:
    platform = case_field(case_id, "platform")
    design = case_field(case_id, "design")
    return (
        ORFS_ROOT
        / "flow"
        / "logs"
        / platform
        / design
        / flow_variant
        / "dpl_evolve_baseline"
        / run_tag
        / f"dpl_evolve_{run_tag}_legalize.log"
    )


def log_evidence(path: Path) -> str:
    if not path.is_file():
        return "missing-log"
    text = path.read_text(encoding="utf-8", errors="replace")
    keys = {
        "neg_primary_metric": "negotiation_primary",
        "neg_primary_route": "negotiation-primary legalization",
        "neg_primary_stage": "DPL-Evolve primary stage: NegotiationLegalizer",
        "policy_repair": "dpl_evolve__pipeline__policy_repair_used",
        "negotiation": "NegotiationLegalizer",
        "legalm_only": "legalm_only",
        "legalm": "runLegalmFullLegalization",
    }
    return ";".join(f"{name}={text.count(pattern)}" for name, pattern in keys.items())


def pct_delta(candidate: float | None, baseline: float | None) -> float | None:
    if candidate is None or baseline in (None, 0):
        return None
    return (candidate - baseline) / baseline * 100.0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix-id", help="Candidate matrix id. Defaults to latest_matrix_id.txt.")
    args = parser.parse_args()

    matrix_id = args.matrix_id
    if not matrix_id:
        matrix_id = (WB_ROOT / "latest_matrix_id.txt").read_text(encoding="utf-8").strip()
    matrix_root = WB_ROOT / "candidate_matrices" / matrix_id
    results_tsv = matrix_root / "results.tsv"
    if not results_tsv.is_file():
        raise SystemExit(f"missing matrix results: {results_tsv}")

    rows = []
    with results_tsv.open(encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f, delimiter="\t")
        for row in reader:
            case_id = row["case"]
            flow_variant = row["flow_variant"]
            baseline_tag = row["baseline_tag"]
            candidate_tag = row["candidate_tag"]
            neg_tag = f"{baseline_tag}_openroad_dpl_negotiation"
            candidate_log = legalize_log_path(case_id, flow_variant, candidate_tag)
            neg_log = legalize_log_path(case_id, flow_variant, neg_tag)
            neg = load_metrics(metric_path(case_id, flow_variant, neg_tag))
            cand = load_metrics(Path(row["candidate_metrics"]))
            delta_um = None
            if cand.get("hpwl_final") is not None and neg.get("hpwl_final") is not None:
                delta_um = cand["hpwl_final"] - neg["hpwl_final"]
            runtime_ratio = None
            if cand.get("runtime") is not None and neg.get("runtime") not in (None, 0):
                runtime_ratio = cand["runtime"] / neg["runtime"]
            rows.append(
                {
                    "case": case_id,
                    "flow_variant": flow_variant,
                    "status": row["status"],
                    "neg": neg,
                    "cand": cand,
                    "delta_um": delta_um,
                    "delta_pct": pct_delta(cand.get("hpwl_final"), neg.get("hpwl_final")),
                    "runtime_ratio": runtime_ratio,
                    "candidate_log": str(candidate_log),
                    "candidate_log_evidence": log_evidence(candidate_log),
                    "negotiation_log": str(neg_log),
                    "negotiation_log_evidence": log_evidence(neg_log),
                }
            )

    report_dir = WB_ROOT / "reports"
    report_dir.mkdir(parents=True, exist_ok=True)
    md_path = report_dir / f"{matrix_id}_vs_negotiation.md"
    tsv_path = report_dir / f"{matrix_id}_vs_negotiation.tsv"

    headers = [
        "case",
        "status",
        "neg_legality",
        "cand_legality",
        "neg_HPWLg",
        "cand_HPWLg",
        "neg_HPWLlg",
        "cand_HPWLlg",
        "neg_HPWLimprove",
        "cand_HPWLimprove",
        "neg_HPWLfinal",
        "cand_HPWLfinal",
        "cand_minus_neg_um",
        "cand_minus_neg_pct",
        "neg_runtime",
        "cand_runtime",
        "runtime_ratio",
        "cand_avg_disp",
        "cand_max_disp",
        "candidate_log_evidence",
        "candidate_log",
        "candidate_metrics",
    ]
    with tsv_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f, delimiter="\t")
        writer.writerow(headers)
        for row in rows:
            neg = row["neg"]
            cand = row["cand"]
            writer.writerow(
                [
                    row["case"],
                    row["status"],
                    neg.get("legality", "missing"),
                    cand.get("legality", "missing"),
                    fmt(neg.get("hpwlg")),
                    fmt(cand.get("hpwlg")),
                    fmt(neg.get("hpwl_legalized")),
                    fmt(cand.get("hpwl_legalized")),
                    fmt(neg.get("hpwl_after_improve")),
                    fmt(cand.get("hpwl_after_improve")),
                    fmt(neg.get("hpwl_final")),
                    fmt(cand.get("hpwl_final")),
                    fmt(row["delta_um"]),
                    fmt(row["delta_pct"], 3),
                    fmt(neg.get("runtime"), 3),
                    fmt(cand.get("runtime"), 3),
                    fmt(row["runtime_ratio"], 3),
                    fmt(cand.get("avg_disp"), 3),
                    fmt(cand.get("max_disp"), 3),
                    row["candidate_log_evidence"],
                    row["candidate_log"],
                    cand.get("path", ""),
                ]
            )

    lines = [
        f"# Negotiation Repair Candidate vs OpenROAD Negotiation",
        "",
        f"- matrix_id: `{matrix_id}`",
        f"- results: `{results_tsv}`",
        f"- tsv: `{tsv_path}`",
        "",
        "| case | status | cand legality | neg final | cand final | cand-neg | delta % | neg rt | cand rt | rt ratio | log evidence |",
        "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for row in rows:
        neg = row["neg"]
        cand = row["cand"]
        lines.append(
            "| {case} | {status} | {legality} | {negf} | {candf} | {du} | {dp} | {negrt} | {candrt} | {rr} | {evidence} |".format(
                case=row["case"],
                status=row["status"],
                legality=cand.get("legality", "missing"),
                negf=fmt(neg.get("hpwl_final")),
                candf=fmt(cand.get("hpwl_final")),
                du=fmt(row["delta_um"]),
                dp=fmt(row["delta_pct"], 3),
                negrt=fmt(neg.get("runtime"), 3),
                candrt=fmt(cand.get("runtime"), 3),
                rr=fmt(row["runtime_ratio"], 3),
                evidence=row["candidate_log_evidence"],
            )
        )
    lines.extend(["", "## Candidate Evidence Paths", ""])
    for row in rows:
        lines.append(f"- `{row['case']}` metrics: `{row['cand'].get('path', '')}`")
        lines.append(f"- `{row['case']}` log: `{row['candidate_log']}`")
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(md_path.read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
EOF

python3 - \
  "${WORKBENCH_ROOT}/scripts/30_report_vs_negotiation.py" \
  "${WORKBENCH_ROOT}" \
  "${DPL_EVOLVE_AGENT_ROOT}" \
  "${ORFS_ROOT}" \
  "${DPL_EVOLVE_STATE_ROOT}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
replacements = {
    "__WORKBENCH_ROOT__": sys.argv[2],
    "__DPL_EVOLVE_AGENT_ROOT__": sys.argv[3],
    "__ORFS_ROOT__": sys.argv[4],
    "__DPL_EVOLVE_STATE_ROOT__": sys.argv[5],
}
text = path.read_text(encoding="utf-8")
for old, new in replacements.items():
    text = text.replace(old, new)
path.write_text(text, encoding="utf-8")
PY

cat > "${WORKBENCH_ROOT}/scripts/40_export_candidate_patches.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail

WB_ROOT="${WORKBENCH_ROOT}"
AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT}"
CANDIDATE="\${WB_ROOT}/source/evolved_negotiation_candidate"
OUTPUT="\${WB_ROOT}/output"
BASE_COMMIT_FILE="\${WB_ROOT}/source/framework_base_commit.txt"

mkdir -p "\${OUTPUT}"
if [[ ! -f "\${BASE_COMMIT_FILE}" ]]; then
  echo "[ERROR] Missing framework base commit file. Run 00_prepare_source.sh first." >&2
  exit 1
fi
BASE_COMMIT="\$(cat "\${BASE_COMMIT_FILE}")"
DELTA="\${OUTPUT}/openroad_dpl_evolve_negotiation_iter30_framework_delta.patch"
FROM_CLEAN="\${OUTPUT}/openroad_dpl_evolve_negotiation_iter30_from_clean.patch"

git -C "\${CANDIDATE}" diff --binary \\
  --src-prefix=a/src/dpl_evolve/ \\
  --dst-prefix=b/src/dpl_evolve/ \\
  "\${BASE_COMMIT}" -- . > "\${DELTA}"

cat \\
  "\${AGENT_ROOT}/patches/openroad_dpl_evolve_base.patch" \\
  "\${AGENT_ROOT}/patches/openroad_dpl_evolve_framework.patch" \\
  "\${DELTA}" > "\${FROM_CLEAN}"

sha256sum "\${DELTA}" "\${FROM_CLEAN}" > "\${OUTPUT}/sha256sums.txt"
git -C "\${CANDIDATE}" status --short > "\${OUTPUT}/candidate_git_status.txt"
git -C "\${CANDIDATE}" log --oneline --decorate -20 > "\${OUTPUT}/candidate_git_log.txt"

echo "[INFO] Exported:"
echo "  \${DELTA}"
echo "  \${FROM_CLEAN}"
echo "  \${OUTPUT}/sha256sums.txt"
EOF

cat > "${WORKBENCH_ROOT}/scripts/50_validate_exported_patches.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail

WB_ROOT="${WORKBENCH_ROOT}"
AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT}"
ORFS_ROOT="${ORFS_ROOT}"
OUTPUT="\${WB_ROOT}/output"
FRAMEWORK_SEED="${FRAMEWORK_SEED}"
DELTA="\${OUTPUT}/openroad_dpl_evolve_negotiation_iter30_framework_delta.patch"
FROM_CLEAN="\${OUTPUT}/openroad_dpl_evolve_negotiation_iter30_from_clean.patch"
VALIDATION="\${WB_ROOT}/validation"
REPORT="\${OUTPUT}/patch_validation.log"

mkdir -p "\${VALIDATION}" "\${OUTPUT}"
: > "\${REPORT}"

log() {
  printf '%s\\n' "\$*" | tee -a "\${REPORT}"
}

if [[ ! -s "\${DELTA}" || ! -s "\${FROM_CLEAN}" ]]; then
  echo "[ERROR] Missing exported patches. Run 40_export_candidate_patches.sh first." >&2
  exit 1
fi

FRAMEWORK_APPLY="\${VALIDATION}/framework_apply_check"
rm -rf "\${FRAMEWORK_APPLY}"
rsync -a --delete --exclude '.git/' "\${FRAMEWORK_SEED}/" "\${FRAMEWORK_APPLY}/"
git init "\${FRAMEWORK_APPLY}" >/dev/null
git -C "\${FRAMEWORK_APPLY}" config user.email "dpl-evolve-agent@example.invalid"
git -C "\${FRAMEWORK_APPLY}" config user.name "dpl-evolve-agent"
git -C "\${FRAMEWORK_APPLY}" add -A
git -C "\${FRAMEWORK_APPLY}" commit -m "Seed framework for patch validation" >/dev/null
git -C "\${FRAMEWORK_APPLY}" apply --whitespace=nowarn --check -p3 "\${DELTA}"
git -C "\${FRAMEWORK_APPLY}" apply --whitespace=nowarn -p3 "\${DELTA}"
log "[OK] framework-delta applies with -p3 to framework dpl_evolve seed"

if ! rg -n "negotiation_primary|runEvolveNegotiationRepair|NegotiationLegalizer" "\${FRAMEWORK_APPLY}/src/StudentAlgorithm.cpp" >> "\${REPORT}"; then
  log "[WARN] Could not find expected negotiation-primary signals in StudentAlgorithm.cpp"
fi

OPENROAD_REPO="\${ORFS_ROOT}/tools/OpenROAD"
ANCHOR="\$(python3 - <<'PY' "\${AGENT_ROOT}/metadata/anchors.json"
import json, sys
print(json.load(open(sys.argv[1], encoding="utf-8"))["workspace_model"]["openroad_base_commit"])
PY
)"
OPENROAD_CHECK="\${VALIDATION}/openroad_clean_anchor"
if [[ -d "\${OPENROAD_CHECK}/.git" || -f "\${OPENROAD_CHECK}/.git" ]]; then
  git -C "\${OPENROAD_REPO}" worktree remove --force "\${OPENROAD_CHECK}" >/dev/null 2>&1 || rm -rf "\${OPENROAD_CHECK}"
else
  rm -rf "\${OPENROAD_CHECK}"
fi
git -C "\${OPENROAD_REPO}" worktree add --detach "\${OPENROAD_CHECK}" "\${ANCHOR}" >/dev/null
git -C "\${OPENROAD_CHECK}" apply --whitespace=nowarn --check "\${FROM_CLEAN}"
git -C "\${OPENROAD_CHECK}" apply --whitespace=nowarn "\${FROM_CLEAN}"
log "[OK] from-clean patch applies to OpenROAD anchor \${ANCHOR}"

if ! rg -n "negotiation_primary|runEvolveNegotiationRepair|NegotiationLegalizer" "\${OPENROAD_CHECK}/src/dpl_evolve/src/StudentAlgorithm.cpp" >> "\${REPORT}"; then
  log "[WARN] Could not find expected negotiation-primary signals in clean-applied StudentAlgorithm.cpp"
fi

log "[INFO] validation report: \${REPORT}"
EOF

cat > "${WORKBENCH_ROOT}/scripts/run_full_check.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
"\${SCRIPT_DIR}/00_prepare_source.sh" --force
"\${SCRIPT_DIR}/20_run_3case_eval.sh" --threads 8 --max-parallel 3
"\${SCRIPT_DIR}/30_report_vs_negotiation.py"
"\${SCRIPT_DIR}/40_export_candidate_patches.sh"
"\${SCRIPT_DIR}/50_validate_exported_patches.sh"
EOF

chmod +x "${WORKBENCH_ROOT}/scripts/"*.sh "${WORKBENCH_ROOT}/scripts/30_report_vs_negotiation.py"

if [[ "${FORCE}" -eq 1 ]]; then
  "${WORKBENCH_ROOT}/scripts/00_prepare_source.sh" --force
fi

echo "[INFO] negotiation patch repair workbench ready: ${WORKBENCH_ROOT}"
echo "[INFO] worker task: ${WORKBENCH_ROOT}/WORKER_TASK.md"
echo "[INFO] prepare source: ${WORKBENCH_ROOT}/scripts/00_prepare_source.sh --force"
