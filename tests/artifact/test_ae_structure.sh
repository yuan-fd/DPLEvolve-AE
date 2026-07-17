#!/usr/bin/env bash
# DPLEvolve AE — Structural Self-Test
# Verifies that the AE repository has all required files and directories.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

errors=0

echo "=== DPLEvolve AE Structure Self-Test ==="
echo ""

# Required directories
required_dirs=(
  "docs"
  "agent/context"
  "agent/tasks"
  "agent/schemas"
  "scripts/human"
  "scripts/agent"
  "scripts/internal"
  "scripts/lib"
  "configs/smoke"
  "configs/paper"
  "configs/ablation"
  "configs/schema"
  "provenance"
  "results/reference"
  "results/reproduced"
  "results/tables"
  "env"
  "benchmarks"
  "third_party"
  "experiments/baseline"
  "experiments/main"
  "experiments/ablation"
)

for dir in "${required_dirs[@]}"; do
  if [[ -d "${AE_ROOT}/${dir}" ]]; then
    echo "  [OK] ${dir}/"
  else
    echo "  [MISSING] ${dir}/"
    errors=$((errors + 1))
  fi
done
echo ""

# Required files
required_files=(
  "README.md"
  "LICENSE"
  "CITATION.cff"
  "SECURITY.md"
  "CHANGELOG.md"
  "Makefile"
  ".gitignore"
  "docs/artifact-overview.md"
  "docs/quickstart.md"
  "docs/environment.md"
  "docs/experiments.md"
  "docs/expected-results.md"
  "docs/troubleshooting.md"
  "docs/claims-to-artifacts.md"
  "docs/artifact-appendix.md"
  "agent/AGENTS.md"
  "agent/context/project-map.md"
  "agent/context/experiment-semantics.md"
  "agent/context/invariants.md"
  "agent/tasks/reproduce-baseline.md"
  "agent/tasks/reproduce-main-results.md"
  "agent/tasks/validate-artifact.md"
  "agent/schemas/run-manifest.schema.json"
  "agent/schemas/result.schema.json"
  "configs/smoke/aes_nangate45.yaml"
  "configs/schema/experiment_config.schema.json"
  "provenance/source-commits.json"
  "provenance/original-artifact-checksums.txt"
  "env/versions.lock"
  "env/requirements.txt"
  "env/modules.sh"
)

for file in "${required_files[@]}"; do
  if [[ -f "${AE_ROOT}/${file}" ]]; then
    echo "  [OK] ${file}"
  else
    echo "  [MISSING] ${file}"
    errors=$((errors + 1))
  fi
done
echo ""

# Required scripts (executable)
required_scripts=(
  "scripts/human/check_environment.sh"
  "scripts/human/setup.sh"
  "scripts/human/smoke_test.sh"
  "scripts/human/reproduce_baseline.sh"
  "scripts/human/reproduce_main.sh"
  "scripts/human/generate_tables.sh"
  "scripts/agent/inspect_environment.sh"
  "scripts/agent/execute_experiment.sh"
  "scripts/agent/validate_run.sh"
  "scripts/agent/summarize_results.py"
  "scripts/internal/runtime_env.sh"
  "scripts/internal/record_provenance.sh"
  "scripts/lib/env_vars.sh"
  "scripts/lib/utils.sh"
)

for script in "${required_scripts[@]}"; do
  if [[ -x "${AE_ROOT}/${script}" ]]; then
    echo "  [OK] ${script} (executable)"
  elif [[ -f "${AE_ROOT}/${script}" ]]; then
    echo "  [FIX] ${script} (not executable)"
    chmod +x "${AE_ROOT}/${script}"
    echo "        -> fixed"
  else
    echo "  [MISSING] ${script}"
    errors=$((errors + 1))
  fi
done
echo ""

if [[ "${errors}" -eq 0 ]]; then
  echo "=== All checks passed! ==="
  exit 0
else
  echo "=== ${errors} error(s) found ==="
  exit 1
fi
