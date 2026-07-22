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

if bash scripts/agent/run_artifact.sh --artifact unsupported --dry-run >/dev/null 2>&1; then
  echo '[FAIL] Agent dispatcher accepted an unsupported artifact.' >&2
  exit 1
fi

echo '[PASS] Independent evidence bundles and machine dispatcher work as expected.'
