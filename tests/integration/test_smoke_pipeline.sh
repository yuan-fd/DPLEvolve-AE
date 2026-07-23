#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

digest_inputs() {
  find artifacts -type f \( -path '*/inputs/*' -o -path '*/expected/*' \) -print0 \
    | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1
}

before="$(digest_inputs)"

bash artifacts/01-table4-qor/run.sh
bash artifacts/02-table5-composability/run.sh
bash artifacts/03-table6-cutrow/run.sh

after="$(digest_inputs)"
if [[ "${before}" != "${after}" ]]; then
  echo '[FAIL] An evidence command modified packaged inputs or expected values.' >&2
  exit 1
fi

for output in \
  artifacts/01-table4-qor/output/summary.json \
  artifacts/02-table5-composability/output/summary.json \
  artifacts/03-table6-cutrow/output/summary.json; do
  [[ -s "${output}" ]] || { echo "[FAIL] Missing generated report: ${output}" >&2; exit 1; }
done

bash scripts/agent/run_artifact.sh --artifact table4 --dry-run | grep -F 'artifacts/01-table4-qor/run.sh' >/dev/null
bash scripts/agent/run_artifact.sh --artifact smoke --dry-run | grep -F -- '--check-only' >/dev/null

missing_orfs="$(mktemp -d)/OpenROAD-flow-scripts"
smoke_check_output="$(ORFS_ROOT="${missing_orfs}" bash artifacts/04-aes-smoke/run.sh --check-only)"
grep -F '[SKIP] Optional prepared AES smoke result is not available.' <<<"${smoke_check_output}" >/dev/null
ORFS_ROOT="${missing_orfs}" bash artifacts/04-aes-smoke/run.sh --help | grep -F 'Usage:' >/dev/null
if ORFS_ROOT="${missing_orfs}" bash artifacts/04-aes-smoke/run.sh --run >/dev/null 2>&1; then
  echo '[FAIL] A fresh smoke run accepted a missing ORFS workspace.' >&2
  exit 1
fi

# A setup file retained from another checkout must not override paths supplied
# by this invocation. This is especially important on reviewer machines that
# reuse a state directory across a fresh clone.
stale_root="$(mktemp -d)"
mkdir -p "${stale_root}/state/ae" "${stale_root}/wrong-orfs/flow"
printf '%s\n' \
  "export AE_ROOT=${stale_root}/wrong-ae" \
  "export DPL_EVOLVE_AGENT_ROOT=${ROOT}/src/dpl_evolve_agent" \
  "export ORFS_ROOT=${stale_root}/wrong-orfs" \
  "export DPL_EVOLVE_STATE_ROOT=${stale_root}/wrong-state" \
  "export DPL_EVOLVE_PYTHON=$(command -v python3)" \
  > "${stale_root}/state/ae/environment.sh"
requested_missing="${stale_root}/requested-missing-orfs"
if AE_ROOT="${ROOT}" \
   DPL_EVOLVE_STATE_ROOT="${stale_root}/state" \
   ORFS_ROOT="${requested_missing}" \
   bash -c 'source scripts/shared/env_vars.sh; dpl_ae_resolve_env' \
   >"${stale_root}/resolution.log" 2>&1; then
  echo '[FAIL] A stale machine environment replaced the requested ORFS_ROOT.' >&2
  exit 1
fi
grep -F "${requested_missing}" "${stale_root}/resolution.log" >/dev/null

if bash scripts/agent/run_artifact.sh --artifact unsupported --dry-run >/dev/null 2>&1; then
  echo '[FAIL] Agent dispatcher accepted an unsupported artifact.' >&2
  exit 1
fi

echo '[PASS] Independent evidence bundles and machine dispatcher work as expected.'
