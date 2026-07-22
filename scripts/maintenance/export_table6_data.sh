#!/usr/bin/env bash
# Maintainer-only exporter for the retained paper Table 6 inputs.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SOURCE_ROOT=""
OUTPUT_ROOT=""
ARCHIVE=""

usage() {
  cat <<'EOF'
Usage: export_table6_data.sh --source-root PATH --output PATH [--archive FILE]

SOURCE_ROOT is the original Agenticflow repository containing cut_rows/.
The exporter writes a self-contained paper-data/table6 tree with deterministic
gzip files, the single evolved-negotiation source, and a complete SHA-256
manifest.  It never exports the deleted multi-terabyte ODB collection.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source-root) SOURCE_ROOT="$(realpath -m "$2")"; shift 2 ;;
    --output) OUTPUT_ROOT="$(realpath -m "$2")"; shift 2 ;;
    --archive) ARCHIVE="$(realpath -m "$2")"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done
[[ -n "${SOURCE_ROOT}" && -n "${OUTPUT_ROOT}" ]] || { usage >&2; exit 2; }
for command in gzip rsync sha256sum sort find; do
  command -v "${command}" >/dev/null || { echo "[ERROR] missing command: ${command}" >&2; exit 1; }
done
if [[ -e "${OUTPUT_ROOT}/table6" ]]; then
  echo "[ERROR] output already exists; choose a new empty output root: ${OUTPUT_ROOT}/table6" >&2
  exit 2
fi

RESULTS="${SOURCE_ROOT}/cut_rows/results"
EXPERIMENT="${SOURCE_ROOT}/cut_rows/experiments/article_cutrow_fail_search_20260509"
EVOLVED_SOURCE="${EXPERIMENT}/candidate_runs/evolved_negotiation_selected/variant/dpl_evolve"
[[ -f "${EVOLVED_SOURCE}/CMakeLists.txt" ]] || { echo "[ERROR] evolved source missing: ${EVOLVED_SOURCE}" >&2; exit 1; }
mkdir -p "${OUTPUT_ROOT}/table6/cases" "${OUTPUT_ROOT}/table6/programs/evolved_negotiation"

inventory="${OUTPUT_ROOT}/table6/source-inventory.tsv"
printf 'kind\tcase\tpattern\tfilename\tuncompressed_bytes\tuncompressed_sha256\n' > "${inventory}"

export_case() {
  local case_id="$1" pattern="$2" filename source destination bytes digest
  mkdir -p "${OUTPUT_ROOT}/table6/cases/${case_id}/${pattern}"
  for filename in cutrows.def cutrows.v; do
    source="${RESULTS}/${case_id}/${pattern}/innovus_cutrow/${filename}"
    destination="${OUTPUT_ROOT}/table6/cases/${case_id}/${pattern}/${filename}.gz"
    [[ -f "${source}" ]] || { echo "[ERROR] retained input missing: ${source}" >&2; exit 1; }
    gzip -n -1 -c "${source}" > "${destination}"
    bytes="$(stat -c %s "${source}")"
    digest="$(sha256sum "${source}" | awk '{print $1}')"
    printf 'cutrow\t%s\t%s\t%s\t%s\t%s\n' "${case_id}" "${pattern}" "${filename}" "${bytes}" "${digest}" >> "${inventory}"
  done
}

for case_id in ariane133_placebatch swerv_wrapper_dense2 bp_quad_placebatch; do
  mkdir -p "${OUTPUT_ROOT}/table6/cases/${case_id}"
  sdc="${RESULTS}/${case_id}/_handoff/handoff.sdc"
  [[ -f "${sdc}" ]] || { echo "[ERROR] retained SDC missing: ${sdc}" >&2; exit 1; }
  install -m 0644 "${sdc}" "${OUTPUT_ROOT}/table6/cases/${case_id}/handoff.sdc"
  printf 'constraint\t%s\t\thandoff.sdc\t%s\t%s\n' \
    "${case_id}" "$(stat -c %s "${sdc}")" "$(sha256sum "${sdc}" | awk '{print $1}')" >> "${inventory}"
done

export_case ariane133_placebatch center_band_8
export_case ariane133_placebatch center_band_9
export_case ariane133_placebatch center_band_10
export_case swerv_wrapper_dense2 center_band_5p25
export_case swerv_wrapper_dense2 center_band_5p5
export_case swerv_wrapper_dense2 center_band_6
export_case bp_quad_placebatch center_band_5
export_case bp_quad_placebatch center_band_6
export_case bp_quad_placebatch center_band_8

rsync -a --delete --exclude='.git/' "${EVOLVED_SOURCE}/" \
  "${OUTPUT_ROOT}/table6/programs/evolved_negotiation/dpl_evolve/"

"${AE_ROOT}/scripts/reproduce/verify_data_manifest.py" --help >/dev/null
(
  cd "${OUTPUT_ROOT}"
  find table6 -type f ! -name MANIFEST.sha256 -print0 \
    | sort -z \
    | xargs -0 sha256sum > table6/MANIFEST.sha256
)
"${AE_ROOT}/scripts/reproduce/verify_data_manifest.py" --root "${OUTPUT_ROOT}" --scope table6

size="$(du -sh "${OUTPUT_ROOT}/table6" | cut -f1)"
echo "[PASS] Table 6 paper-data exported: ${OUTPUT_ROOT}/table6 (${size})"
if [[ -n "${ARCHIVE}" ]]; then
  mkdir -p "$(dirname "${ARCHIVE}")"
  tar -C "${OUTPUT_ROOT}" -czf "${ARCHIVE}" table6
  echo "[PASS] archive=${ARCHIVE} sha256=$(sha256sum "${ARCHIVE}" | awk '{print $1}')"
fi
