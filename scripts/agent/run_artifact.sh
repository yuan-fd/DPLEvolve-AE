#!/usr/bin/env bash
# Fixed machine-facing dispatcher for fresh experiment execution.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DPL_EVOLVE_STATE_ROOT="${DPL_EVOLVE_STATE_ROOT:-${AE_ROOT}/../dpl_evolve_state}"

artifact=""
run_smoke=0
dry_run=0

usage() {
  cat <<'EOF'
Usage: run_artifact.sh --artifact ID [--dry-run]

IDs: prepare, table4, table5, table6, figures, search, ariane, smoke

Each ID invokes the experiment's fresh reproduce.sh wrapper. Search prints the
Level 1/2 plans and never starts paid model calls through this dispatcher.
Smoke checks for an existing diagnostic result unless --run-smoke is supplied.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --artifact) artifact="${2:?--artifact requires a value}"; shift 2 ;;
    --run-smoke) run_smoke=1; shift ;;
    --dry-run) dry_run=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "${artifact}" in
  prepare) command=(make -C "${AE_ROOT}" bootstrap build-tools prepare-paper-inputs "THREADS=${DPL_EVOLVE_THREADS:-10}") ;;
  table4) command=(bash "${AE_ROOT}/artifacts/01-table4-qor/reproduce.sh") ;;
  table5) command=(bash "${AE_ROOT}/artifacts/02-table5-composability/reproduce.sh") ;;
  table6) command=(bash "${AE_ROOT}/artifacts/03-table6-cutrow/reproduce.sh" --fetch) ;;
  figures) command=(bash "${AE_ROOT}/artifacts/04-figures/reproduce.sh") ;;
  search) command=(bash "${AE_ROOT}/artifacts/05-reviewdse-search/reproduce.sh" --plan) ;;
  ariane) command=(bash "${AE_ROOT}/artifacts/06-ariane-diagnostic/reproduce.sh") ;;
  smoke)
    if [[ "${run_smoke}" -eq 1 ]]; then
      command=(bash "${AE_ROOT}/tests/toolchain/aes-smoke/run.sh" --run --threads "${DPL_EVOLVE_THREADS:-4}")
    else
      command=(bash "${AE_ROOT}/tests/toolchain/aes-smoke/run.sh" --check-only)
    fi
    ;;
  *)
    echo "[ERROR] unsupported artifact '${artifact}'" >&2
    usage >&2
    exit 2
    ;;
esac

if [[ "${dry_run}" -eq 1 ]]; then
  printf 'command='; printf '%q ' "${command[@]}"; printf '\n'
  exit 0
fi

manifest_dir="${DPL_EVOLVE_STATE_ROOT}/agent_runs"
mkdir -p "${manifest_dir}"
stamp="$(date +%Y%m%d_%H%M%S)"
manifest="${manifest_dir}/${artifact}_${stamp}.json"
started="$(date -Iseconds)"
if "${command[@]}"; then
  exit_code=0
  status=PASS
else
  exit_code=$?
  if [[ "${exit_code}" -eq 3 ]]; then status=BLOCKED; else status=FAIL; fi
fi
finished="$(date -Iseconds)"

printf '{\n  "artifact": "%s",\n  "status": "%s",\n  "exit_code": %d,\n  "started_at": "%s",\n  "finished_at": "%s"\n}\n' \
  "${artifact}" "${status}" "${exit_code}" "${started}" "${finished}" > "${manifest}"
echo "manifest=${manifest}"
exit "${exit_code}"
