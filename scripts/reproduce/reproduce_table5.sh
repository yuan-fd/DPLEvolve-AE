#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

THREADS="${THREADS:-10}"
CHECK_MODE=""
SKIP_PREPARE=0
ROWS=(aes_dense_n45 jpeg_dense_n45 swerv_dense_n45)
TABLE5_ARTIFACT_ROOT="${AE_ROOT}/artifacts/02-table5-composability"
TABLE5_PROGRAM_ROOT="${TABLE5_ARTIFACT_ROOT}/programs"
LEGALM_PROGRAM="${TABLE5_ARTIFACT_ROOT}/programs/legalm/dpl_evolve"
DIAMOND_PROGRAM="${TABLE5_ARTIFACT_ROOT}/programs/diamond/dpl_evolve"
NEGOTIATION_PROGRAM="${TABLE5_ARTIFACT_ROOT}/programs/negotiation/dpl_evolve"

usage() {
  cat <<'EOF'
Usage: reproduce_table5.sh [options]

Prepare the three dense placement inputs, map each selected/reference role to
one of the three retained program snapshots, and run both roles through the
same downstream DPO/final flow. The output is derived from new metrics.json
files, not the archived Table 5 transcription.

Options:
  --threads N          Build/evaluation threads. Default: 10.
  --skip-prepare       Reuse existing DENSE/DENSE_2 ODBs.
  --check-paper-data   Verify the three checksummed program snapshots.
  --check-inputs       Also check regenerated ORFS ODB/SDC inputs.
  --dry-run            Print commands without executing them.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads) THREADS="$2"; shift 2 ;;
    --skip-prepare) SKIP_PREPARE=1; shift ;;
    --check-paper-data) CHECK_MODE=paper-data; shift ;;
    --check-inputs) CHECK_MODE=inputs; shift ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done
repro_positive_integer threads "${THREADS}"

case_for_row() {
  case "$1" in
    aes_dense_n45) echo aes_dense_nangate45 ;;
    jpeg_dense_n45) echo jpeg_util90_nangate45 ;;
    swerv_dense_n45) echo swerv_wrapper_nangate45 ;;
    *) repro_die "unknown Table 5 row: $1" ;;
  esac
}

variant_for_row() {
  case "$1" in
    aes_dense_n45|jpeg_dense_n45) echo DENSE ;;
    swerv_dense_n45) echo DENSE_2 ;;
  esac
}

program_for_row_role() {
  case "$1:$2" in
    aes_dense_n45:selected) echo legalm ;;
    aes_dense_n45:reference) echo diamond ;;
    jpeg_dense_n45:selected|jpeg_dense_n45:reference) echo negotiation ;;
    swerv_dense_n45:selected) echo diamond ;;
    swerv_dense_n45:reference) echo negotiation ;;
    *) repro_die "unknown Table 5 row/role: $1/$2" ;;
  esac
}

program_path() {
  case "$1" in
    legalm) echo "${LEGALM_PROGRAM}" ;;
    diamond) echo "${DIAMOND_PROGRAM}" ;;
    negotiation) echo "${NEGOTIATION_PROGRAM}" ;;
    *) repro_die "unknown Table 5 program snapshot: $1" ;;
  esac
}

if ! "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/verify_data_manifest.py" \
  --root "${TABLE5_ARTIFACT_ROOT}" --scope programs; then
  echo "[BLOCKED] Table 5 program-snapshot checksum verification failed" >&2
  exit 3
fi
if [[ "${CHECK_MODE}" == paper-data ]]; then
  echo "[PASS] Table 5 has three available checksummed snapshots under ${TABLE5_PROGRAM_ROOT}"
  exit 0
fi

if [[ "${CHECK_MODE}" == inputs ]]; then
  missing=0
  for row_id in "${ROWS[@]}"; do
    case_id="$(case_for_row "${row_id}")"
    flow_variant="$(variant_for_row "${row_id}")"
    platform="$(${DPL_EVOLVE_PYTHON} "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field platform)"
    design="$(${DPL_EVOLVE_PYTHON} "${DPL_EVOLVE_AGENT_ROOT}/scripts/repo/case_registry.py" --case "${case_id}" --field design)"
    for filename in 3_4_place_resized.odb 2_floorplan.sdc; do
      path="${ORFS_ROOT}/flow/results/${platform}/${design}/${flow_variant}/${filename}"
      if [[ ! -f "${path}" ]]; then printf '[MISSING] %s\n' "${path}" >&2; missing=$((missing + 1)); fi
    done
  done
  if [[ "${missing}" -gt 0 ]]; then
    echo "[BLOCKED] Regenerate Table 5 inputs with 'make prepare-table5-inputs'." >&2
    exit 3
  fi
  echo "[PASS] Table 5 sources and regenerated dense inputs are present"
  exit 0
fi

if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then repro_require_runtime; fi
if [[ "${SKIP_PREPARE}" -eq 0 ]]; then
  prepare_args=(bash "${SCRIPT_DIR}/prepare_table5_inputs.sh" --threads "${THREADS}")
  if [[ "${REPRO_DRY_RUN}" -eq 1 ]]; then prepare_args+=(--dry-run); fi
  "${prepare_args[@]}"
fi

RUN_ID="table5_$(date +%Y%m%d_%H%M%S)"
RUN_ROOT="${REPRO_OUTPUT_ROOT}/${RUN_ID}"
MANIFEST="${RUN_ROOT}/runs.tsv"
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  mkdir -p "${RUN_ROOT}"
  printf 'experiment\trow_id\trole\tcase\tpattern\tresults_tsv\n' > "${MANIFEST}"
fi

for row_id in "${ROWS[@]}"; do
  case_id="$(case_for_row "${row_id}")"
  flow_variant="$(variant_for_row "${row_id}")"
  plan="${RUN_ROOT}/${row_id}.tsv"
  if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
    printf 'enabled\tcase\tcore_utilization\tflow_variant\tround_id\tstart_kind\tnotes\n1\t%s\tdefault\t%s\t%s\tfrozen\tTable 5 exact source replay\n' \
      "${case_id}" "${flow_variant}" "${RUN_ID}" > "${plan}"
  else
    repro_note "would write one-row plan ${plan}: case=${case_id} flow_variant=${flow_variant}"
  fi

  for role in selected reference; do
    program="$(program_for_row_role "${row_id}" "${role}")"
    candidate_src="$(program_path "${program}")"
    matrix_id="${row_id}_${role}"
    output_root="${RUN_ROOT}/matrices"
    results="${output_root}/${matrix_id}/results.tsv"
    repro_run "${DPL_EVOLVE_AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh" \
      --matrix-id "${matrix_id}" \
      --output-root "${output_root}" \
      --candidate-src "${candidate_src}" \
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
