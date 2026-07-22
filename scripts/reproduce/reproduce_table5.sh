#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

THREADS="${THREADS:-10}"
CHECK_ONLY=0
ROWS=(aes_dense_n45 jpeg_dense_n45 swerv_dense_n45)

usage() {
  cat <<'EOF'
Usage: reproduce_table5.sh [--threads N] [--check-inputs] [--dry-run]

Freshly replay both complete source candidates for every Table 5 row: the
legalization-HPWL-selected candidate and its full-flow reference. The script
then summarizes H_g, H_lg, H_ip, and H_f from new metrics.json files.

Exact ODBs and source candidates are expected under PAPER_DATA_ROOT. See
docs/paper-data-layout.md. Missing inputs are a hard error, not replaced with
the archived Table 5 transcription.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads) THREADS="$2"; shift 2 ;;
    --check-inputs) CHECK_ONLY=1; shift ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done
repro_positive_integer threads "${THREADS}"

missing=0
if [[ ! -f "${PAPER_DATA_ROOT}/table5/MANIFEST.sha256" ]]; then
  printf '[MISSING] %s\n' "${PAPER_DATA_ROOT}/table5/MANIFEST.sha256" >&2
  missing=$((missing + 1))
fi
for row_id in "${ROWS[@]}"; do
  for path in \
    "${PAPER_DATA_ROOT}/table5/${row_id}/input/3_4_place_resized.odb" \
    "${PAPER_DATA_ROOT}/table5/${row_id}/input/2_floorplan.sdc" \
    "${PAPER_DATA_ROOT}/table5/${row_id}/programs/selected/dpl_evolve/CMakeLists.txt" \
    "${PAPER_DATA_ROOT}/table5/${row_id}/programs/reference/dpl_evolve/CMakeLists.txt"; do
    if [[ ! -f "${path}" ]]; then
      printf '[MISSING] %s\n' "${path}" >&2
      missing=$((missing + 1))
    fi
  done
done
if [[ "${missing}" -gt 0 ]]; then
  printf '[BLOCKED] Table 5 needs %s exact input/source files; see docs/paper-data-layout.md\n' "${missing}" >&2
  if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then exit 3; fi
fi
if [[ "${missing}" -eq 0 ]]; then
  if ! "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/verify_data_manifest.py" \
    --root "${PAPER_DATA_ROOT}" --scope table5; then
    echo "[BLOCKED] Table 5 paper-data checksum verification failed" >&2
    exit 3
  fi
fi
if [[ "${CHECK_ONLY}" -eq 1 ]]; then
  [[ "${missing}" -eq 0 ]] || exit 3
  echo "[PASS] all Table 5 replay inputs are present"
  exit 0
fi
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then repro_require_runtime; fi

RUN_ID="table5_$(date +%Y%m%d_%H%M%S)"
RUN_ROOT="${REPRO_OUTPUT_ROOT}/${RUN_ID}"
MANIFEST="${RUN_ROOT}/runs.tsv"
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  mkdir -p "${RUN_ROOT}"
  printf 'experiment\trow_id\trole\tcase\tpattern\tresults_tsv\n' > "${MANIFEST}"
fi

case_for_row() {
  case "$1" in
    aes_dense_n45) echo aes_dense_nangate45 ;;
    jpeg_dense_n45) echo jpeg_util90_nangate45 ;;
    swerv_dense_n45) echo swerv_wrapper_nangate45 ;;
  esac
}

for row_id in "${ROWS[@]}"; do
  case_id="$(case_for_row "${row_id}")"
  platform="$(${DPL_EVOLVE_PYTHON} "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field platform 2>/dev/null || echo nangate45)"
  design="$(${DPL_EVOLVE_PYTHON} "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field design 2>/dev/null || true)"
  flow_variant="paper_table5_${row_id}"
  staged_odb="${ORFS_ROOT}/flow/results/${platform}/${design}/${flow_variant}/3_4_place_resized.odb"
  staged_sdc="${ORFS_ROOT}/flow/results/${platform}/${design}/${flow_variant}/2_floorplan.sdc"
  repro_run install -D -m 0644 "${PAPER_DATA_ROOT}/table5/${row_id}/input/3_4_place_resized.odb" "${staged_odb}"
  repro_run install -D -m 0644 "${PAPER_DATA_ROOT}/table5/${row_id}/input/2_floorplan.sdc" "${staged_sdc}"

  plan="${RUN_ROOT}/${row_id}.tsv"
  if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
    printf 'enabled\tcase\tcore_utilization\tflow_variant\tround_id\tstart_kind\tnotes\n1\t%s\tdefault\t%s\t%s\tfrozen\tTable 5 exact replay\n' \
      "${case_id}" "${flow_variant}" "${RUN_ID}" > "${plan}"
  else
    repro_note "would write one-row plan ${plan}: case=${case_id} flow_variant=${flow_variant}"
  fi

  for role in selected reference; do
    matrix_id="${row_id}_${role}"
    output_root="${RUN_ROOT}/matrices"
    results="${output_root}/${matrix_id}/results.tsv"
    repro_run "${DPL_EVOLVE_AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh" \
      --matrix-id "${matrix_id}" \
      --output-root "${output_root}" \
      --candidate-src "${PAPER_DATA_ROOT}/table5/${row_id}/programs/${role}/dpl_evolve" \
      --candidate-label "${role}" \
      --plan "${plan}" \
      --threads "${THREADS}" \
      --max-parallel 1 \
      --skip-place \
      --skip-baseline
    if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
      printf 'table5\t%s\t%s\t%s\t\t%s\n' "${row_id}" "${role}" "${case_id}" "${results}" >> "${MANIFEST}"
    fi
  done
done

if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  fresh_runs="${RUN_ROOT}/table5-fresh-runs.tsv"
  repro_run "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/summarize_replays.py" \
    --manifest "${MANIFEST}" --output "${fresh_runs}"
  repro_run "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/summarize_table5.py" \
    --fresh-runs "${fresh_runs}" --output "${RUN_ROOT}/table5-fresh.tsv"
fi
