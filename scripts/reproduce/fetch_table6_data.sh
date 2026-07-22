#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

ARCHIVE_NAME="dplevolve-table6-paper-data-20260722.tar.gz"
ARCHIVE_URL="https://github.com/yuan-fd/DPLEvolve-AE/releases/download/paper-data-v1/${ARCHIVE_NAME}"
ARCHIVE_SHA256="c73f84c6008ddf578bce9c2708dbe1eff55b2a8e96dada95376369afe9008b63"

usage() {
  cat <<EOF
Usage: fetch_table6_data.sh

Download the retained Table 6 DEF/Verilog/SDC and evolved-source package,
verify the published archive SHA-256 and its complete internal manifest, then
install it as:

  ${PAPER_DATA_ROOT}/table6

Set PAPER_DATA_ROOT before running this command to choose another location.
An existing table6 directory is verified but never overwritten.
EOF
}

case "${1:-}" in
  --help|-h) usage; exit 0 ;;
  "") ;;
  *) repro_die "unknown argument: $1" ;;
esac

for command in curl sha256sum tar mktemp; do
  command -v "${command}" >/dev/null 2>&1 || repro_die "required command not found: ${command}"
done

if [[ -e "${PAPER_DATA_ROOT}/table6" ]]; then
  repro_note "existing Table 6 directory found; verifying without overwriting it"
  "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/verify_data_manifest.py" \
    --root "${PAPER_DATA_ROOT}" --scope table6
  exit 0
fi

temporary="$(mktemp -d "${TMPDIR:-/tmp}/dplevolve-table6-data.XXXXXX")"
trap 'rm -rf "${temporary}"' EXIT
archive="${temporary}/${ARCHIVE_NAME}"

repro_note "downloading ${ARCHIVE_URL}"
curl -fL --retry 3 --output "${archive}" "${ARCHIVE_URL}"
printf '%s  %s\n' "${ARCHIVE_SHA256}" "${archive}" | sha256sum -c -

if tar -tzf "${archive}" | grep -Ev '^table6(/|$)' >/dev/null; then
  repro_die "archive contains a path outside table6/"
fi
tar --no-same-owner --no-same-permissions -xzf "${archive}" -C "${temporary}"
"${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/verify_data_manifest.py" \
  --root "${temporary}" --scope table6

mkdir -p "${PAPER_DATA_ROOT}"
mv "${temporary}/table6" "${PAPER_DATA_ROOT}/table6"
repro_note "installed verified Table 6 data at ${PAPER_DATA_ROOT}/table6"
