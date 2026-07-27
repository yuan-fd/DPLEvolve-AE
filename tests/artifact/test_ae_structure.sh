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
  tests/toolchain/aes-smoke
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

experiment_bundles=(
  artifacts/01-table4-qor
  artifacts/02-table5-composability
  artifacts/03-table6-cutrow
  artifacts/04-figures
  artifacts/05-reviewdse-search
  artifacts/06-ariane-diagnostic
)
for bundle in "${experiment_bundles[@]}"; do
  for path in README.md reproduce.sh inputs expected output; do
    [[ -e "${ROOT}/${bundle}/${path}" ]] \
      && pass "experiment path: ${bundle}/${path}" \
      || fail "missing experiment path: ${bundle}/${path}"
  done
  [[ -x "${ROOT}/${bundle}/reproduce.sh" ]] \
    && pass "executable: ${bundle}/reproduce.sh" \
    || fail "not executable: ${bundle}/reproduce.sh"
done

required_files=(
  artifacts/01-table4-qor/config/baseline_9case.yaml
  artifacts/01-table4-qor/inputs/bo_paper/aes_asap7.trials.tsv
  artifacts/01-table4-qor/selected-programs/manifest.json
  artifacts/01-table4-qor/selected-programs/run.sh
  artifacts/02-table5-composability/inputs/counterexamples.tsv
  artifacts/02-table5-composability/programs/MANIFEST.sha256
  artifacts/02-table5-composability/programs/legalm/dpl_evolve/CMakeLists.txt
  artifacts/02-table5-composability/programs/diamond/dpl_evolve/CMakeLists.txt
  artifacts/02-table5-composability/programs/negotiation/dpl_evolve/CMakeLists.txt
  artifacts/03-table6-cutrow/inputs/reviewdse.tsv
  paper/artifact_evaluation.pdf
  tests/toolchain/aes-smoke/check.sh
  tests/toolchain/aes-smoke/config/aes_nangate45.yaml
  tests/toolchain/aes-smoke/expected/ae_reproduction_lock.json
  scripts/agent/run_artifact.sh
  scripts/README.md
  scripts/shared/env_vars.sh
  scripts/shared/validate_config.py
  scripts/maintenance/prepare_zenodo.sh
  schemas/experiment_config.schema.json
  agent/README.md
  agent/schemas/run-manifest.schema.json
  configs/reproduction/paper-experiments.json
  configs/reproduction/paper9.tsv
  configs/reproduction/table5-inputs.tsv
  configs/reproduction/table5-sources.tsv
  configs/reproduction/table6-replay.tsv
  configs/reproduction/ariane-diagnostic.tsv
  scripts/reproduce/prepare_paper_inputs.sh
  scripts/reproduce/fetch_table6_data.sh
  scripts/reproduce/prepare_table5_inputs.sh
  scripts/reproduce/run_baselines.sh
  scripts/reproduce/run_bo.sh
  scripts/reproduce/run_level1.sh
  scripts/reproduce/verify_level1.py
  scripts/reproduce/run_dse.sh
  scripts/reproduce/replay_selected.sh
  scripts/reproduce/reproduce_table5.sh
  scripts/reproduce/reproduce_table6.sh
  scripts/reproduce/reproduce_figures.py
  scripts/reproduce/reproduce_ariane_diagnostic.sh
  scripts/reproduce/summarize_ariane_diagnostic.py
  scripts/reproduce/record_table4_inputs.py
  scripts/reproduce/summarize_table4.py
  scripts/reproduce/summarize_table5.py
  scripts/reproduce/summarize_table6.py
  scripts/reproduce/verify_data_manifest.py
  scripts/reproduce/openroad_legalize_cutrow.tcl
  scripts/maintenance/export_table6_data.sh
  scripts/agent/README.md
  artifacts/01-table4-qor/inputs/figures/MANIFEST.sha256
  artifacts/01-table4-qor/inputs/diagnostics/MANIFEST.sha256
  artifacts/01-table4-qor/diagnostics/ariane-warmstart/README.md
  images/dplevolve-architecture.png
  docs/reviewer-walkthrough.md
  docs/README.md
  docs/table5-status.md
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
  experiments results scripts/internal scripts/lib \
  artifacts/04-aes-smoke artifacts/05-figures \
  artifacts/06-reviewdse-search artifacts/07-ariane-diagnostic; do
  [[ ! -e "${ROOT}/${path}" ]] \
    && pass "not public: ${path}" \
    || fail "legacy or local-only path is visible: ${path}"
done

if head -1 "${ROOT}/README.md" | grep -Fxq \
  '# From Tool Invocation to Source-Mechanism Exploration: Protected White-Box DSE for Open-Source EDA'; then
  pass "README title is the paper title"
else
  fail "README title is not the paper title"
fi
if grep -Fq '[Paper link](https://arxiv.org/abs/2607.11294)' "${ROOT}/README.md"; then
  pass "README contains the public paper link"
else
  fail "README paper link is missing"
fi
if grep -En 'Optional ReviewDSE|## More information|The released environment was tested' \
    "${ROOT}/README.md" >/dev/null; then
  fail "README contains deprecated guide prose"
else
  pass "README is limited to the operational reproduction flow"
fi

if find "${ROOT}" -path "${ROOT}/.git" -prune -o -type f -iname '*.pdf' \
    ! -path "${ROOT}/paper/artifact_evaluation.pdf" -print -quit | grep -q .; then
  fail "unexpected generated/reference PDF is tracked in the repository tree"
else
  pass "paper/artifact_evaluation.pdf is the only tracked PDF copy"
fi

if grep -Fxq 'extras/unsupported/' "${ROOT}/.gitignore"; then
  pass "local unsupported material is ignored"
else
  fail "extras/unsupported is not excluded from Git"
fi

if grep -RInP --include='*.md' --include='*.txt' --include='*.json' --include='*.yaml' \
    '\p{Han}' "${ROOT}/README.md" "${ROOT}/artifacts" "${ROOT}/docs" \
    "${ROOT}/agent" >/dev/null; then
  fail "public documentation or data contains Chinese characters"
else
  pass "public text is English-only"
fi

if grep -RInE --include='*.md' --include='*.txt' --include='*.json' --include='*.yaml' \
    '鈥|鈹|碌m|AUTHOR (LIST|INFORMATION) REQUIRED' \
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
