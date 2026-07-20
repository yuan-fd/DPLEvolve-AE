#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "monitor_evolve_heartbeat.sh"

INTERVAL=600
ONCE=0
SHOW_PROCESSES=1
DETAIL_TAIL=20
BATCH_ROOT=""
RUN_PREFIX=""
DETAIL_STAGE=0

usage() {
  cat <<'EOF'
Usage: monitor_evolve_heartbeat.sh [options]

Print a compact heartbeat for a DPL-evolve experiment batch. By default it
auto-selects the most recently modified directory under
.dpl_evolve_state/experiment_batches and repeats every 600 seconds.

Options:
  --batch-root PATH       Exact experiment_batches/<batch> directory.
  --run-prefix PREFIX     Select the newest experiment batch whose name starts
                          with PREFIX.
  --interval SECONDS      Heartbeat interval. Default: 600.
  --once                  Print one heartbeat and exit.
  --no-processes          Do not include active process summary.
  --detail-stage          Also print report_stage_metrics.py for active rounds.
  --detail-tail N         Tail rows for report_experiment_status.py details.
                          Default: 20.
  --help                  Show this message.

Examples:
  scripts/orchestration/monitor_evolve_heartbeat.sh --once
  scripts/orchestration/monitor_evolve_heartbeat.sh --run-prefix evolve_9case_t55 --interval 600
  scripts/orchestration/monitor_evolve_heartbeat.sh --batch-root .dpl_evolve_state/experiment_batches/<batch>
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --batch-root) BATCH_ROOT="$2"; shift 2 ;;
    --run-prefix) RUN_PREFIX="$2"; shift 2 ;;
    --interval) INTERVAL="$2"; shift 2 ;;
    --once) ONCE=1; shift ;;
    --no-processes) SHOW_PROCESSES=0; shift ;;
    --detail-stage) DETAIL_STAGE=1; shift ;;
    --detail-tail) DETAIL_TAIL="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if ! [[ "${INTERVAL}" =~ ^[0-9]+$ ]] || [[ "${INTERVAL}" -lt 1 ]]; then
  echo "[ERROR] --interval must be a positive integer: ${INTERVAL}" >&2
  exit 2
fi
if ! [[ "${DETAIL_TAIL}" =~ ^[0-9]+$ ]]; then
  echo "[ERROR] --detail-tail must be a non-negative integer: ${DETAIL_TAIL}" >&2
  exit 2
fi

find_batch_root() {
  local root="${DPL_EVOLVE_STATE_ROOT}/experiment_batches"
  local prefix="$1"
  if [[ -n "${BATCH_ROOT}" ]]; then
    if [[ -d "${BATCH_ROOT}" ]]; then
      printf '%s\n' "${BATCH_ROOT}"
      return 0
    fi
    echo "[ERROR] --batch-root does not exist: ${BATCH_ROOT}" >&2
    return 1
  fi
  if [[ ! -d "${root}" ]]; then
    echo "[ERROR] Missing experiment batch root: ${root}" >&2
    return 1
  fi
  if [[ -n "${prefix}" ]]; then
    find "${root}" -maxdepth 1 -mindepth 1 -type d -name "${prefix}*" -printf '%T@ %p\n' \
      | sort -nr \
      | awk 'NR == 1 {print $2}'
  else
    find "${root}" -maxdepth 1 -mindepth 1 -type d -printf '%T@ %p\n' \
      | sort -nr \
      | awk 'NR == 1 {print $2}'
  fi
}

round_ids_from_batch() {
  local batch="$1"
  local experiments="${batch}/experiments.tsv"
  if [[ ! -f "${experiments}" ]]; then
    return 0
  fi
  awk -F'\t' 'NR > 1 && $3 != "" {print $3}' "${experiments}" | sort -u
}

print_process_lines() {
  local regex="$1"
  ps -u "${USER}" -o pid,etimes,stat,pcpu,pmem,cmd \
    | rg "${regex}|run_evolve_9case_place_batch|optimize_case_with_codex.py|run_codex_exec.py|codex exec|openroad -exit|ninja|cmake" \
    | rg -v 'rg |monitor_evolve_heartbeat' \
    | sed -n '1,100p' || true
}

heartbeat() {
  local batch batch_name process_regex status_file experiments_file
  batch="$(find_batch_root "${RUN_PREFIX}")"
  if [[ -z "${batch}" ]]; then
    echo "[ERROR] No experiment batch found" >&2
    return 1
  fi
  batch_name="$(basename "${batch}")"
  experiments_file="${batch}/experiments.tsv"
  status_file="${batch}/status.tsv"
  process_regex="${RUN_PREFIX:-${batch_name}}"

  echo "===== DPL evolve heartbeat $(date '+%F %T %Z') ====="
  echo "[batch] ${batch}"

  if [[ "${SHOW_PROCESSES}" -eq 1 ]]; then
    echo
    echo "[process]"
    print_process_lines "${process_regex}"
  fi

  echo
  echo "[experiments]"
  if [[ -f "${experiments_file}" ]]; then
    cat "${experiments_file}"
  else
    echo "missing ${experiments_file}"
  fi

  echo
  echo "[batch status]"
  if [[ -f "${status_file}" ]]; then
    cat "${status_file}"
  else
    echo "missing ${status_file}"
  fi

  mapfile -t rounds < <(round_ids_from_batch "${batch}")
  echo
  echo "[round count] ${#rounds[@]}"
  if (( ${#rounds[@]} == 0 )); then
    echo "No launched rounds recorded yet."
    echo "===== end heartbeat ====="
    return 0
  fi

  local report_args=()
  local round_id
  for round_id in "${rounds[@]}"; do
    report_args+=(--round-id "${round_id}")
  done

  echo
  echo "[round summary]"
  "${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/analysis/report_experiment_quick_status.py" \
    --format markdown \
    --batch-root "${batch}" \
    "${report_args[@]}"

  if [[ "${DETAIL_STAGE}" -eq 1 ]]; then
    echo
    echo "[stage metrics]"
    "${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/evaluator/report_stage_metrics.py" \
      --format markdown \
      "${report_args[@]}" \
      --no-expected
  fi

  echo "===== end heartbeat ====="
}

while true; do
  heartbeat
  if [[ "${ONCE}" -eq 1 ]]; then
    exit 0
  fi
  sleep "${INTERVAL}"
done
