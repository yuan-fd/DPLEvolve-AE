#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "run_evolve_9case_place_batch.sh"

CASE_SET="${EVOLVE_CASE_SET:-evolve_9case}"
FLOW_VARIANT="${EVOLVE_FLOW_VARIANT:-place_batch_20260421_220319}"
RUN_STAMP="$(date +%Y%m%d_%H%M%S)"
RUN_PREFIX="${EVOLVE_RUN_PREFIX:-evolve_9case_place_batch_${RUN_STAMP}}"
START_KIND="${EVOLVE_START_KIND:-framework}"
CHILDREN="${EVOLVE_CHILDREN:-4}"
ITERATIONS="${EVOLVE_ITERATIONS:-10}"
MAX_PARALLEL="${EVOLVE_MAX_PARALLEL:-4}"
THREADS="${EVOLVE_THREADS:-10}"
MAX_CONCURRENT_CASES="${EVOLVE_MAX_CONCURRENT_CASES:-3}"
TEACHER_MODEL="${EVOLVE_TEACHER_MODEL:-gpt-5.5}"
TEACHER_REASONING_EFFORT="${EVOLVE_TEACHER_REASONING_EFFORT:-xhigh}"
STUDENT_MODEL="${EVOLVE_STUDENT_MODEL:-gpt-5.4}"
STUDENT_REASONING_EFFORT="${EVOLVE_STUDENT_REASONING_EFFORT:-xhigh}"
STUDENT_RUNTIME_MULTIPLIER="${EVOLVE_STUDENT_RUNTIME_MULTIPLIER:-2.0}"
PREPARE_WORKSPACE=0
PREPARE_FORCE=0
SKIP_CORE_BUILD=0
REFRESH_BASELINES=0
TEACHER_ONLY=0
DRY_RUN=0
LEVEL1_EVIDENCE="${EVOLVE_LEVEL1_EVIDENCE:-}"
CASES=()

usage() {
  cat <<'EOF'
Usage: run_evolve_9case_place_batch.sh [options]

Launch the 9-case Teacher/Student white-box evolve line from shared
place_batch_20260421_220319 snapshots.

Defaults:
  case set:        evolve_9case
  children:        4
  iterations:      10
  teacher:         gpt-5.5 / xhigh
  student:         gpt-5.4 / xhigh
  start kind:      framework
  case concurrency 3

Options:
  --case-set NAME              Case set from problems/case_sets.json.
  --case ID                    Run one case. May be repeated. Overrides case set.
  --flow-variant NAME          Default: place_batch_20260421_220319.
  --run-prefix NAME            Round-id prefix. Default includes launch stamp.
  --start-kind KIND            framework, diamond, default_negotiation,
                               source_topk_diamond, evolved_diamond,
                               evolved_negotiation, prepared.
  --children N                 Student count per iteration. Default: 4.
  --iterations N               Iterations per case. Default: 10.
  --max-parallel N             Parallel students inside one case. Default: 4.
  --threads N                  OpenROAD threads. Default: 10.
  --max-concurrent-cases N     Concurrent Teacher rounds. Default: 3.
  --teacher-model NAME         Default: gpt-5.5.
  --teacher-reasoning-effort E Default: xhigh.
  --student-model NAME         Default: gpt-5.4.
  --student-reasoning-effort E Default: xhigh.
  --runtime-multiplier X       Student flow timeout multiplier. Default: 2.0.
  --level1-evidence PATH       Frozen paper-level calibration evidence packet.
  --prepare-workspace          Run prepare_workspace.sh before launch.
  --prepare-force              Pass --force to prepare_workspace.sh.
  --skip-core-build            Do not configure/build common OpenROAD core.
  --refresh-baselines          Refresh canonical 3-line baselines per case.
  --teacher-only               Launch only Teacher planning; skip Student and review.
  --dry-run                    Print optimize commands without launching agents.
  --help                       Show this message.

The script refuses to start if any requested case is missing
FLOW_VARIANT/3_4_place_resized.odb.  It always passes
--reuse-baseline-preflight to the Teacher loop: existing complete canonical
baseline suites are reused, while missing rows are filled by the loop.  Use
--refresh-baselines when the baseline suite should be recomputed explicitly
before the evolve rounds.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case-set) CASE_SET="$2"; shift 2 ;;
    --case) CASES+=("$2"); shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --run-prefix) RUN_PREFIX="$2"; shift 2 ;;
    --start-kind) START_KIND="$2"; shift 2 ;;
    --children) CHILDREN="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --max-concurrent-cases) MAX_CONCURRENT_CASES="$2"; shift 2 ;;
    --teacher-model) TEACHER_MODEL="$2"; shift 2 ;;
    --teacher-reasoning-effort) TEACHER_REASONING_EFFORT="$2"; shift 2 ;;
    --student-model) STUDENT_MODEL="$2"; shift 2 ;;
    --student-reasoning-effort) STUDENT_REASONING_EFFORT="$2"; shift 2 ;;
    --runtime-multiplier) STUDENT_RUNTIME_MULTIPLIER="$2"; shift 2 ;;
    --level1-evidence) LEVEL1_EVIDENCE="$2"; shift 2 ;;
    --prepare-workspace) PREPARE_WORKSPACE=1; shift ;;
    --prepare-force) PREPARE_FORCE=1; shift ;;
    --skip-core-build) SKIP_CORE_BUILD=1; shift ;;
    --refresh-baselines) REFRESH_BASELINES=1; shift ;;
    --teacher-only) TEACHER_ONLY=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

case "${START_KIND}" in
  framework|diamond|source_topk_diamond|default_negotiation|evolved_diamond|evolved_negotiation|prepared) ;;
  *) echo "[ERROR] Unsupported --start-kind: ${START_KIND}" >&2; exit 1 ;;
esac

case "${TEACHER_REASONING_EFFORT}" in low|medium|high|xhigh) ;;
  *) echo "[ERROR] Bad teacher reasoning effort: ${TEACHER_REASONING_EFFORT}" >&2; exit 1 ;;
esac
case "${STUDENT_REASONING_EFFORT}" in low|medium|high|xhigh) ;;
  *) echo "[ERROR] Bad student reasoning effort: ${STUDENT_REASONING_EFFORT}" >&2; exit 1 ;;
esac

for value_name in CHILDREN ITERATIONS MAX_PARALLEL THREADS MAX_CONCURRENT_CASES; do
  value="${!value_name}"
  if ! [[ "${value}" =~ ^[0-9]+$ ]] || [[ "${value}" -lt 1 ]]; then
    echo "[ERROR] ${value_name} must be a positive integer, got '${value}'" >&2
    exit 2
  fi
done

if [[ "${#CASES[@]}" -eq 0 ]]; then
  mapfile -t CASES < <("${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case-set "${CASE_SET}" --field case)
fi

case_field() {
  "${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case "$1" --field "$2"
}

snapshot_path() {
  local case_id="$1"
  local design platform
  design="$(case_field "${case_id}" design)"
  platform="$(case_field "${case_id}" platform)"
  printf '%s\n' "${ORFS_ROOT}/flow/results/${platform}/${design}/${FLOW_VARIANT}/3_4_place_resized.odb"
}

require_snapshots() {
  local missing=0
  local case_id snapshot
  for case_id in "${CASES[@]}"; do
    snapshot="$(snapshot_path "${case_id}")"
    if [[ ! -f "${snapshot}" ]]; then
      echo "[ERROR] Missing input snapshot for ${case_id}: ${snapshot}" >&2
      missing=1
    fi
  done
  return "${missing}"
}

run_cmd() {
  printf '[evolve_9case] +'
  printf ' %q' "$@"
  printf '\n'
  if [[ "${DRY_RUN}" -eq 0 ]]; then
    "$@"
  fi
}

round_model_tag() {
  local model="$1"
  local effort="$2"
  local normalized
  normalized="${model//./}"
  normalized="${normalized//-/}"
  printf '%s_%s' "${normalized}" "${effort}"
}

BATCH_ROOT="${DPL_EVOLVE_STATE_ROOT}/experiment_batches/${RUN_PREFIX}_${FLOW_VARIANT}"
LOG_DIR="${BATCH_ROOT}/logs"
STATUS_FILE="${BATCH_ROOT}/status.tsv"
EXPERIMENTS_FILE="${BATCH_ROOT}/experiments.tsv"
if [[ "${DRY_RUN}" -eq 0 ]]; then
  mkdir -p "${LOG_DIR}"
  printf "case\tstatus\tstart\tend\tlog\tround_id\n" > "${STATUS_FILE}"
  printf "case\tflow_variant\tround_id\tstart_kind\tchildren\titerations\tteacher\tstudent\n" > "${EXPERIMENTS_FILE}"
  require_snapshots
fi

if [[ "${PREPARE_WORKSPACE}" -eq 1 ]]; then
  prepare_cmd=("${AGENT_ROOT}/scripts/workspace/prepare_workspace.sh" --workspace-root "${ORFS_ROOT}")
  if [[ "${PREPARE_FORCE}" -eq 1 ]]; then
    prepare_cmd+=(--force)
  fi
  run_cmd "${prepare_cmd[@]}"
fi

if [[ "${SKIP_CORE_BUILD}" -ne 1 ]]; then
  run_cmd "${AGENT_ROOT}/scripts/workspace/configure_openroad_core.sh"
  run_cmd "${AGENT_ROOT}/scripts/workspace/build_openroad_core.sh" --threads "${THREADS}"
fi

if [[ "${REFRESH_BASELINES}" -eq 1 ]]; then
  for case_id in "${CASES[@]}"; do
    run_cmd "${AGENT_ROOT}/baseline/run_baseline_suite.sh" \
      --case "${case_id}" \
      --flow-variant "${FLOW_VARIANT}" \
      --threads "${THREADS}" \
      --tag-prefix "${RUN_PREFIX}_${case_id}_baseline_probe"
  done
fi

run_case() {
  local case_id="$1"
  local round_id round_tag log start_ts end_ts rc
  local -a cmd
  round_tag="t$(round_model_tag "${TEACHER_MODEL}" "${TEACHER_REASONING_EFFORT}")_s$(round_model_tag "${STUDENT_MODEL}" "${STUDENT_REASONING_EFFORT}")"
  round_id="${RUN_PREFIX}_${FLOW_VARIANT}_${case_id}_${CHILDREN}x${ITERATIONS}_${round_tag}"
  log="${LOG_DIR}/${case_id}.log"
  start_ts="$(date '+%F %T')"
  cmd=(
    "${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/optimize_case_with_codex.py"
    --case "${case_id}"
    --flow-variant "${FLOW_VARIANT}"
    --round-id "${round_id}"
    --start-kind "${START_KIND}"
    --children "${CHILDREN}"
    --iterations "${ITERATIONS}"
    --max-parallel "${MAX_PARALLEL}"
    --threads "${THREADS}"
    --student-runtime-multiplier "${STUDENT_RUNTIME_MULTIPLIER}"
    --teacher-model "${TEACHER_MODEL}"
    --teacher-reasoning-effort "${TEACHER_REASONING_EFFORT}"
    --student-model "${STUDENT_MODEL}"
    --student-reasoning-effort "${STUDENT_REASONING_EFFORT}"
    --audit-prompts
    --reuse-baseline-preflight
    --skip-core-build
    --launch
  )
  if [[ -n "${LEVEL1_EVIDENCE}" ]]; then
    cmd+=(--level1-evidence "${LEVEL1_EVIDENCE}")
  fi
  if [[ "${TEACHER_ONLY}" -eq 1 ]]; then
    cmd+=(--teacher-only)
  fi
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    cmd+=(--dry-run)
    printf '[DRY-RUN] case=%s command:' "${case_id}"
    printf ' %q' "${cmd[@]}"
    printf '\n'
    return 0
  fi

  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s/%s\t%s/%s\n" \
    "${case_id}" "${FLOW_VARIANT}" "${round_id}" "${START_KIND}" "${CHILDREN}" "${ITERATIONS}" \
    "${TEACHER_MODEL}" "${TEACHER_REASONING_EFFORT}" "${STUDENT_MODEL}" "${STUDENT_REASONING_EFFORT}" \
    >> "${EXPERIMENTS_FILE}"

  set +e
  {
    printf "[START] %s\n" "${start_ts}"
    printf "[INFO] case=%s round_id=%s\n" "${case_id}" "${round_id}"
    printf "[INFO] command:"
    printf " %q" "${cmd[@]}"
    printf "\n"
    "${cmd[@]}"
  } > "${log}" 2>&1
  rc=$?
  set -e
  end_ts="$(date '+%F %T')"
  if [[ "${rc}" -eq 0 ]]; then
    printf "%s\tPASS\t%s\t%s\t%s\t%s\n" "${case_id}" "${start_ts}" "${end_ts}" "${log}" "${round_id}" >> "${STATUS_FILE}"
  else
    printf "%s\tFAIL(%s)\t%s\t%s\t%s\t%s\n" "${case_id}" "${rc}" "${start_ts}" "${end_ts}" "${log}" "${round_id}" >> "${STATUS_FILE}"
  fi
  return "${rc}"
}

echo "9-case evolve launch"
echo "  case_set             : ${CASE_SET}"
echo "  cases                : ${CASES[*]}"
echo "  flow_variant         : ${FLOW_VARIANT}"
echo "  run_prefix           : ${RUN_PREFIX}"
echo "  start_kind           : ${START_KIND}"
echo "  children x iterations: ${CHILDREN} x ${ITERATIONS}"
echo "  teacher              : ${TEACHER_MODEL} / ${TEACHER_REASONING_EFFORT}"
echo "  student              : ${STUDENT_MODEL} / ${STUDENT_REASONING_EFFORT}"
echo "  max_concurrent_cases : ${MAX_CONCURRENT_CASES}"
echo "  teacher_only         : ${TEACHER_ONLY}"
echo "  batch_root           : ${BATCH_ROOT}"
echo

declare -a pids=()
overall_rc=0
for case_id in "${CASES[@]}"; do
  while (( $(jobs -pr | wc -l) >= MAX_CONCURRENT_CASES )); do
    if ! wait -n; then
      overall_rc=1
    fi
  done
  echo "[INFO] launch case=${case_id}"
  run_case "${case_id}" &
  pids+=("$!")
done

for pid in "${pids[@]}"; do
  if ! wait "${pid}"; then
    overall_rc=1
  fi
done

echo
echo "Evolve launch finished. Status:"
if [[ "${DRY_RUN}" -eq 1 ]]; then
  echo "Dry run only; no snapshots, API calls, or output directories were required."
else
  cat "${STATUS_FILE}"
fi
exit "${overall_rc}"
