#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
failures=0

pass() { printf '[PASS] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1" >&2; failures=$((failures + 1)); }

required_root=(README.md LICENSE CITATION.cff Makefile artifacts configs docs images agent scripts src schemas provenance paper)
for path in "${required_root[@]}"; do
  [[ -e "${ROOT}/${path}" ]] && pass "root path: ${path}" || fail "missing root path: ${path}"
done

bundles=(
  artifacts/01-table4-qor
  artifacts/02-table5-composability
  artifacts/03-table6-cutrow
  artifacts/04-aes-smoke
)
for bundle in "${bundles[@]}"; do
  for path in README.md run.sh inputs expected output; do
    [[ -e "${ROOT}/${bundle}/${path}" ]] \
      && pass "${bundle}/${path}" \
      || fail "missing ${bundle}/${path}"
  done
  [[ -x "${ROOT}/${bundle}/run.sh" ]] \
    && pass "executable: ${bundle}/run.sh" \
    || fail "not executable: ${bundle}/run.sh"
done

required_files=(
  artifacts/01-table4-qor/verify.py
  artifacts/01-table4-qor/config/baseline_9case.yaml
  artifacts/01-table4-qor/inputs/bo_paper/aes_asap7.trials.tsv
  artifacts/01-table4-qor/selected-programs/manifest.json
  artifacts/01-table4-qor/selected-programs/run.sh
  artifacts/02-table5-composability/verify.py
  artifacts/02-table5-composability/inputs/counterexamples.tsv
  artifacts/03-table6-cutrow/verify.py
  artifacts/03-table6-cutrow/inputs/reviewdse.tsv
  artifacts/04-aes-smoke/check.sh
  artifacts/04-aes-smoke/config/aes_nangate45.yaml
  artifacts/04-aes-smoke/expected/ae_reproduction_lock.json
  scripts/agent/run_artifact.sh
  scripts/shared/env_vars.sh
  scripts/shared/validate_config.py
  scripts/maintenance/prepare_zenodo.sh
  schemas/experiment_config.schema.json
  agent/README.md
  agent/schemas/run-manifest.schema.json
  configs/reproduction/paper-experiments.json
  configs/reproduction/paper9.tsv
  scripts/reproduce/prepare_paper_inputs.sh
  scripts/reproduce/run_baselines.sh
  scripts/reproduce/run_bo.sh
  scripts/reproduce/run_level1.sh
  scripts/reproduce/run_dse.sh
  scripts/reproduce/replay_selected.sh
  scripts/reproduce/reproduce_table5.sh
  scripts/reproduce/reproduce_table6.sh
  scripts/reproduce/record_table4_inputs.py
  scripts/reproduce/summarize_table4.py
  scripts/reproduce/summarize_table5.py
  scripts/reproduce/summarize_table6.py
  scripts/reproduce/verify_data_manifest.py
  images/dplevolve-architecture.png
)
for path in "${required_files[@]}"; do
  [[ -f "${ROOT}/${path}" ]] && pass "file: ${path}" || fail "missing file: ${path}"
done

for path in "${ROOT}"/scripts/reproduce/*.sh "${ROOT}"/scripts/reproduce/*.py; do
  [[ -x "${path}" ]] \
    && pass "executable: ${path#${ROOT}/}" \
    || fail "not executable: ${path#${ROOT}/}"
done

for path in \
  experiments results scripts/internal scripts/lib; do
  [[ ! -e "${ROOT}/${path}" ]] \
    && pass "not public: ${path}" \
    || fail "legacy or local-only path is visible: ${path}"
done

if find "${ROOT}" -path "${ROOT}/.git" -prune -o -type f -iname '*.pdf' -print -quit | grep -q .; then
  fail "generated/reference PDF is tracked in the repository tree"
else
  pass "repository tree contains no PDF copies"
fi

if grep -Fxq 'extras/unsupported/' "${ROOT}/.gitignore"; then
  pass "local unsupported material is ignored"
else
  fail "extras/unsupported is not excluded from Git"
fi

if rg -nP '\p{Han}' \
    "${ROOT}/README.md" "${ROOT}/artifacts" "${ROOT}/docs" "${ROOT}/agent" \
    -g '*.md' -g '*.txt' -g '*.json' -g '*.yaml' >/dev/null; then
  fail "public documentation or data contains Chinese characters"
else
  pass "public text is English-only"
fi

if rg -n '鈥|鈹|碌m|AUTHOR (LIST|INFORMATION) REQUIRED' \
    "${ROOT}/README.md" "${ROOT}/artifacts" "${ROOT}/docs" "${ROOT}/agent" >/dev/null; then
  fail "public text contains mojibake or release placeholders"
else
  pass "public text has no known mojibake or document placeholders"
fi

if [[ "${failures}" -ne 0 ]]; then
  printf '\n[FAIL] %d structure check(s) failed\n' "${failures}" >&2
  exit 1
fi
printf '\n[PASS] Repository structure is reviewer-ready\n'
