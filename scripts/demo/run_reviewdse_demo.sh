#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="${AE_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
# shellcheck source=../reproduce/common.sh
source "${AE_ROOT}/scripts/reproduce/common.sh"

CASE_ID="ariane133_nangate45"
STUDENTS="4"
ITERATIONS="2"
THREAD_COUNT="${THREADS:-10}"
FLOW_VARIANT="${FLOW_VARIANT:-paper9_place}"
TEACHER="gpt-5.6-sol"
TEACHER_EFFORT="xhigh"
STUDENT="gpt-5.6-terra"
STUDENT_EFFORT="high"
RUN_PREFIX="${DSE_RUN_PREFIX:-}"
REFRESH_SECONDS="2"
DRY_RUN=0
CHECK_MODELS_ONLY=0

usage() {
  cat <<'EOF'
Usage: run_reviewdse_demo.sh [options]

Launch one real ReviewDSE closed loop and render its observable state in a
recording-friendly terminal dashboard.

Options:
  --case ID                       Target. Default: ariane133_nangate45.
  --students N                    Parallel Students. Default: 4.
  --iterations N                  Teacher/Student iterations. Default: 2.
  --threads N                     OpenROAD threads per evaluation. Default: 10.
  --flow-variant NAME             Prepared input variant. Default: paper9_place.
  --teacher-model NAME            Default: gpt-5.6-sol.
  --teacher-reasoning-effort E    Default: xhigh.
  --student-model NAME            Default: gpt-5.6-terra.
  --student-reasoning-effort E    Default: high.
  --run-prefix NAME               Stable output prefix. Default: timestamped.
  --refresh-seconds N             Dashboard refresh interval. Default: 2.
  --check-models-only             Probe Student then Teacher; do not start DSE.
  --dry-run                       Print the real launch command only.
  --help                          Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case) CASE_ID="$2"; shift 2 ;;
    --students) STUDENTS="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --threads) THREAD_COUNT="$2"; shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --teacher-model) TEACHER="$2"; shift 2 ;;
    --teacher-reasoning-effort) TEACHER_EFFORT="$2"; shift 2 ;;
    --student-model) STUDENT="$2"; shift 2 ;;
    --student-reasoning-effort) STUDENT_EFFORT="$2"; shift 2 ;;
    --run-prefix) RUN_PREFIX="$2"; shift 2 ;;
    --refresh-seconds) REFRESH_SECONDS="$2"; shift 2 ;;
    --check-models-only) CHECK_MODELS_ONLY=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done

repro_positive_integer students "${STUDENTS}"
repro_positive_integer iterations "${ITERATIONS}"
repro_positive_integer threads "${THREAD_COUNT}"
repro_positive_integer refresh-seconds "${REFRESH_SECONDS}"
[[ -n "${TEACHER}" && -n "${STUDENT}" ]] || repro_die "model names must not be empty"
[[ "${CASE_ID}" =~ ^[A-Za-z0-9_.-]+$ ]] || repro_die "unsafe case id: ${CASE_ID}"

if [[ -z "${RUN_PREFIX}" ]]; then
  RUN_PREFIX="ae_live_demo_$(date +%Y%m%d_%H%M%S)"
fi
[[ "${RUN_PREFIX}" =~ ^[A-Za-z0-9_.-]+$ ]] || repro_die "unsafe run prefix: ${RUN_PREFIX}"

launch=(
  bash "${AE_ROOT}/scripts/reproduce/run_dse.sh"
  --profile small
  --case "${CASE_ID}"
  --children "${STUDENTS}"
  --iterations "${ITERATIONS}"
  --threads "${THREAD_COUNT}"
  --flow-variant "${FLOW_VARIANT}"
  --teacher-model "${TEACHER}"
  --teacher-reasoning-effort "${TEACHER_EFFORT}"
  --student-model "${STUDENT}"
  --student-reasoning-effort "${STUDENT_EFFORT}"
  --run-prefix "${RUN_PREFIX}"
)

if [[ "${DRY_RUN}" -eq 1 ]]; then
  printf 'DPLEvolve — Live ReviewDSE Demo (dry run)\n'
  printf '  case       : %s\n' "${CASE_ID}"
  printf '  loop       : %s Students x %s iterations\n' "${STUDENTS}" "${ITERATIONS}"
  printf '  Teacher    : %s / %s\n' "${TEACHER}" "${TEACHER_EFFORT}"
  printf '  Students   : %s / %s\n' "${STUDENT}" "${STUDENT_EFFORT}"
  printf '  run prefix : %s\n\n' "${RUN_PREFIX}"
  "${launch[@]}" --dry-run
  exit 0
fi

repro_require_runtime
batch_root="${DPL_EVOLVE_STATE_ROOT}/experiment_batches/${RUN_PREFIX}_${FLOW_VARIANT}"
if [[ "${CHECK_MODELS_ONLY}" -eq 0 && -e "${batch_root}" ]]; then
  repro_die "run prefix '${RUN_PREFIX}' already has batch state: ${batch_root}; choose a fresh DSE_RUN_PREFIX so old and new evidence cannot mix"
fi

"${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/check_reviewdse_models.py" \
  --cwd "${AE_ROOT}" \
  --student-model "${STUDENT}" \
  --student-effort "${STUDENT_EFFORT}" \
  --teacher-model "${TEACHER}" \
  --teacher-effort "${TEACHER_EFFORT}"

if [[ "${CHECK_MODELS_ONLY}" -eq 1 ]]; then
  exit 0
fi

command -v setsid >/dev/null 2>&1 || repro_die "setsid is required to manage the demo process group"
mkdir -p "${batch_root}"
launcher_log="${batch_root}/demo-launcher.log"

printf 'Starting the real ReviewDSE loop. The dashboard will replace this screen.\n'
printf 'Raw launcher output: %s\n' "${launcher_log}"

launcher_pid=""
cleanup() {
  local signal="${1:-TERM}"
  if [[ -n "${launcher_pid}" ]] && kill -0 "${launcher_pid}" 2>/dev/null; then
    printf '\nStopping ReviewDSE process group (%s)...\n' "${launcher_pid}" >&2
    kill "-${signal}" -- "-${launcher_pid}" 2>/dev/null || true
  fi
}
trap 'cleanup TERM' INT TERM EXIT

setsid "${launch[@]}" >"${launcher_log}" 2>&1 &
launcher_pid=$!

set +e
"${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/reviewdse_demo_dashboard.py" \
  --state-root "${DPL_EVOLVE_STATE_ROOT}" \
  --batch-root "${batch_root}" \
  --case "${CASE_ID}" \
  --students "${STUDENTS}" \
  --iterations "${ITERATIONS}" \
  --teacher-model "${TEACHER}" \
  --student-model "${STUDENT}" \
  --launcher-pid "${launcher_pid}" \
  --launcher-log "${launcher_log}" \
  --refresh-seconds "${REFRESH_SECONDS}" \
  --watch
dashboard_rc=$?

wait "${launcher_pid}"
launcher_rc=$?
set -e
launcher_pid=""
trap - INT TERM EXIT

if [[ "${dashboard_rc}" -ne 0 ]]; then
  printf '[WARN] dashboard exited with status %s; the ReviewDSE result is unchanged.\n' "${dashboard_rc}" >&2
fi
if [[ "${launcher_rc}" -ne 0 ]]; then
  printf '[ERROR] ReviewDSE failed with status %s. Raw output: %s\n' "${launcher_rc}" "${launcher_log}" >&2
fi
exit "${launcher_rc}"
