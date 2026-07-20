#!/usr/bin/env bash
# Machine-facing dispatcher for the four reviewer artifact bundles.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

artifact=""
run_smoke=0
dry_run=0

usage() {
  cat <<'EOF'
Usage: run_artifact.sh --artifact table4|table5|table6|smoke [--run-smoke] [--dry-run]

The smoke artifact defaults to check-only mode. Pass --run-smoke to create a
fresh AES OpenROAD run. A machine-readable run manifest is written to the
selected artifact's output directory.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --artifact)
      [[ $# -ge 2 ]] || { echo "[ERROR] --artifact requires a value" >&2; exit 2; }
      artifact="$2"
      shift 2
      ;;
    --run-smoke) run_smoke=1; shift ;;
    --dry-run) dry_run=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "${artifact}" in
  table4)
    bundle="${AE_ROOT}/artifacts/01-table4-qor"
    command=(bash "${bundle}/run.sh")
    ;;
  table5)
    bundle="${AE_ROOT}/artifacts/02-table5-composability"
    command=(bash "${bundle}/run.sh")
    ;;
  table6)
    bundle="${AE_ROOT}/artifacts/03-table6-cutrow"
    command=(bash "${bundle}/run.sh")
    ;;
  smoke)
    bundle="${AE_ROOT}/artifacts/04-aes-smoke"
    if [[ "${run_smoke}" -eq 1 ]]; then
      command=(bash "${bundle}/run.sh" --run --threads "${DPL_EVOLVE_THREADS:-4}")
    else
      command=(bash "${bundle}/run.sh" --check-only)
    fi
    ;;
  *)
    echo "[ERROR] --artifact must be table4, table5, table6, or smoke" >&2
    exit 2
    ;;
esac

mkdir -p "${bundle}/output"
stamp="$(date +%Y%m%d_%H%M%S)"
manifest="${bundle}/output/agent_run_${stamp}.json"

if [[ "${dry_run}" -eq 1 ]]; then
  printf 'command='; printf '%q ' "${command[@]}"; printf '\n'
  exit 0
fi

started="$(date -Iseconds)"
if "${command[@]}"; then
  exit_code=0
  status="PASS"
else
  exit_code=$?
  status="FAIL"
fi
finished="$(date -Iseconds)"

printf '{\n  "artifact": "%s",\n  "status": "%s",\n  "exit_code": %d,\n  "started_at": "%s",\n  "finished_at": "%s"\n}\n' \
  "${artifact}" "${status}" "${exit_code}" "${started}" "${finished}" > "${manifest}"

echo "manifest=${manifest}"
exit "${exit_code}"
