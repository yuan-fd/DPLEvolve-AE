#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
AGENT_ROOT="$(realpath -m "${AGENT_ROOT}")"
export DPL_EVOLVE_AGENT_ROOT="${AGENT_ROOT}"
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_source_local_env_preserving_overrides "${AGENT_ROOT}"
export DPL_EVOLVE_AGENT_ROOT="${AGENT_ROOT}"
export DPL_EVOLVE_STATE_ROOT="${DPL_EVOLVE_STATE_ROOT:-$(realpath -m "${AGENT_ROOT}/../dpl_evolve_state")}"
export PYTHONPYCACHEPREFIX="${PYTHONPYCACHEPREFIX:-${DPL_EVOLVE_STATE_ROOT}/pycache/release_readiness}"
mkdir -p "${PYTHONPYCACHEPREFIX}"
dpl_resolve_python yaml
PYTHON_BIN="${DPL_EVOLVE_PYTHON}"

ROUND_ID="release_readiness_dryrun"
FLOW_VARIANT="release_readiness_dryrun"
CASE_ID="jpeg_nangate45"
RUN_TEACHER_DRY_RUN=1

usage() {
  cat <<'EOF'
Usage: check_release_readiness.sh [options]

Run the repo-level release-readiness gate:
  1. Python compile check.
  2. Shell syntax check.
  3. Knowledge index validation.
  4. Repo hygiene audit.
  5. Teacher dry-run prompt audit and generated Student script syntax check.

Options:
  --case ID                 Dry-run case. Default: jpeg_nangate45.
  --round-id ID             Dry-run round id. Default: release_readiness_dryrun.
  --flow-variant NAME       Dry-run flow variant. Default: release_readiness_dryrun.
  --skip-teacher-dry-run    Skip the Teacher dry-run gate.
  --help                    Show this message.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case)
      CASE_ID="$2"
      shift 2
      ;;
    --round-id)
      ROUND_ID="$2"
      shift 2
      ;;
    --flow-variant)
      FLOW_VARIANT="$2"
      shift 2
      ;;
    --skip-teacher-dry-run)
      RUN_TEACHER_DRY_RUN=0
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[ERROR] Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

cd "${AGENT_ROOT}"

log() {
  printf '[release_readiness] %s\n' "$*"
}

log "agent_root=${AGENT_ROOT}"
log "python=${PYTHON_BIN}"

log "python compile"
"${PYTHON_BIN}" -m compileall -q runtime_paths.py scripts baseline validation database memory learning adapters

log "shell syntax"
find scripts baseline experiments -type f -name '*.sh' -print0 | xargs -0 -r bash -n

log "knowledge index"
"${PYTHON_BIN}" scripts/repo/query_knowledge.py --validate

log "repo hygiene"
"${PYTHON_BIN}" scripts/repo/audit_repo_hygiene.py

if [[ "${RUN_TEACHER_DRY_RUN}" -eq 1 ]]; then
  log "teacher dry-run case=${CASE_ID} round_id=${ROUND_ID} flow_variant=${FLOW_VARIANT}"
  "${PYTHON_BIN}" scripts/optimize_case_with_codex.py \
    --case "${CASE_ID}" \
    --flow-variant "${FLOW_VARIANT}" \
    --round-id "${ROUND_ID}" \
    --iterations 1 \
    --children 1 \
    --skip-baseline-preflight \
    --dry-run \
    --audit-prompts

  generated_root="${DPL_EVOLVE_STATE_ROOT:-$(realpath -m "${AGENT_ROOT}/../dpl_evolve_state")}/${ROUND_ID}/teacher_rounds/students"
  if [[ -d "${generated_root}" ]]; then
    log "generated Student shell syntax"
    find "${generated_root}" -type f -name '*.sh' -print0 | xargs -0 -r bash -n
  else
    echo "[ERROR] Missing generated Student script root: ${generated_root}" >&2
    exit 1
  fi
else
  log "teacher dry-run skipped"
fi

log "OK"
