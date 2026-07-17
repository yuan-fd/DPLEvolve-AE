#!/usr/bin/env bash
# DPLEvolve AE — Zenodo Archive Preparation Script
# Creates a self-contained tarball for Zenodo upload.
#
# Usage: bash scripts/internal/prepare_zenodo.sh
# Output: DPLEvolve-AE-zenodo-YYYYMMDD.tar.gz

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECTS_ROOT="$(cd "${AE_ROOT}/.." && pwd)"

STAMP="$(date +%Y%m%d)"
ZENODO_DIR="/tmp/DPLEvolve-AE-zenodo-${STAMP}"
ARCHIVE_NAME="DPLEvolve-AE-zenodo-${STAMP}.tar.gz"

echo "============================================"
echo " DPLEvolve AE — Zenodo Archive Preparation"
echo "============================================"
echo ""

# Clean up any previous
rm -rf "${ZENODO_DIR}"
mkdir -p "${ZENODO_DIR}"

# 1. Copy AE repo (excluding generated files)
echo "[1/5] Copying DPLEvolve-AE..."
rsync -a --exclude='.git' --exclude='results/reproduced/*' --exclude='results/tables/*' \
  --exclude='provenance/current-machine.json' --exclude='__pycache__' \
  --exclude='*.pyc' --exclude='containers/.gitkeep' \
  "${AE_ROOT}/" "${ZENODO_DIR}/DPLEvolve-AE/"

# 2. Copy core framework at pinned commit
echo "[2/5] Copying dpl_evolve_agent at pinned commit..."
AGENT_DIR="${PROJECTS_ROOT}/dpl_evolve_agent"
if [[ -d "${AGENT_DIR}" ]]; then
  rsync -a --exclude='.git' --exclude='__pycache__' --exclude='*.pyc' \
    --exclude='.dpl_evolve_state' --exclude='Agenticflow' \
    "${AGENT_DIR}/" "${ZENODO_DIR}/dpl_evolve_agent/"
fi

# 3. Copy provenance and version data
echo "[3/5] Copying provenance..."
mkdir -p "${ZENODO_DIR}/provenance"
cp "${AE_ROOT}/provenance/source-commits.json" "${ZENODO_DIR}/provenance/"
cp "${AE_ROOT}/provenance/original-artifact-checksums.txt" "${ZENODO_DIR}/provenance/"
# Generate fresh provenance
if [[ -f "${AE_ROOT}/scripts/internal/record_provenance.sh" ]]; then
  bash "${AE_ROOT}/scripts/internal/record_provenance.sh" "${ZENODO_DIR}/provenance/current-machine.json" 2>/dev/null || true
fi

# 4. Copy audit reports
echo "[4/5] Copying audit reports..."
mkdir -p "${ZENODO_DIR}/audit"
if [[ -d "${PROJECTS_ROOT}/audit" ]]; then
  cp "${PROJECTS_ROOT}/audit/"*.md "${ZENODO_DIR}/audit/" 2>/dev/null || true
fi

# 5. Write README for Zenodo
echo "[5/5] Writing README-ZENODO.md..."
cat > "${ZENODO_DIR}/README-ZENODO.md" << 'ZENODOEOF'
# DPLEvolve Artifact Evaluation — Zenodo Archive

This archive contains everything needed to reproduce the key results from:

> **From Tool Invocation to Source-Mechanism Exploration: Protected White-Box DSE for Open-Source EDA**
> MLCAD 2026 (Short Paper, Paper ID 150)

## Contents

```
├── DPLEvolve-AE/          ← Artifact Evaluation repository (entry point)
├── dpl_evolve_agent/      ← Core ReviewDSE framework
├── provenance/            ← Pinned source commits + checksums
├── audit/                 ← Phase 1 & 2 environment audit reports
├── README-ZENODO.md       ← You are reading this
└── QUICKSTART.md          ← 3-step quick start
```

## Quick Start

```bash
# 1. Extract
tar xzf DPLEvolve-AE-zenodo-*.tar.gz
cd DPLEvolve-AE-zenodo-*/DPLEvolve-AE/

# 2. Check environment
make check

# 3. Build dependencies (requires internet for submodules)
make setup

# 4. Run smoke test (no API required)
make smoke
```

## Requirements

- Linux x86-64 (tested on RHEL 8)
- GCC >= 9, CMake >= 3.20
- Python >= 3.11
- ~10 GB disk space
- Internet (for initial Git submodule fetch)

## Sibling Repositories

This archive includes `dpl_evolve_agent/` at its pinned commit.
You also need `OpenROAD-flow-scripts/` at commit `9e2467a6`, which
will be cloned by `make setup` if not found.

## What Can Be Validated Without LLM API

| Command | Description | Time |
|---|---|---|
| `make check` | Environment validation | < 1 min |
| `make setup` | Build Yosys + OpenROAD | ~30 min |
| `make smoke` | AES smoke test (verify pipeline) | ~5 min |
| `make reproduce-baseline` | All paper baselines | ~30 min |
| `make reproduce-bo` | BO-DSE (Optuna TPE, no LLM!) | ~4.5 hours |

## What Requires LLM API

| Command | API Required | Token Cost |
|---|---|---|
| `make reproduce-main` | Yes (GPT-5.5/5.4) | ~2.15B logged tokens/design |

## Provenance

All source revisions are pinned in `provenance/source-commits.json`.
Binary checksums in `provenance/original-artifact-checksums.txt`.

## Contact

See the paper for author contact information.
ZENODOEOF

# Create tarball
echo ""
echo "Creating archive: ${ARCHIVE_NAME}..."
cd /tmp
tar czf "${ARCHIVE_NAME}" "DPLEvolve-AE-zenodo-${STAMP}"

SIZE=$(du -h "${ARCHIVE_NAME}" | cut -f1)
echo ""
echo "============================================"
echo " Zenodo archive ready!"
echo " File:    /tmp/${ARCHIVE_NAME}"
echo " Size:    ${SIZE}"
echo "============================================"
echo ""
echo "Next steps:"
echo "  1. Verify: tar tzf /tmp/${ARCHIVE_NAME} | head -20"
echo "  2. Upload to: https://zenodo.org/deposit/new"
echo "  3. Get DOI and add to paper's Artifact Appendix"
