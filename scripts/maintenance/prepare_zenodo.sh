#!/usr/bin/env bash
# Build and validate the public Zenodo archive.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ALLOW_PLACEHOLDER_AUTHORS=0

if [[ "${1:-}" == "--allow-placeholder-authors" ]]; then
  ALLOW_PLACEHOLDER_AUTHORS=1
elif [[ $# -gt 0 ]]; then
  echo "Usage: $0 [--allow-placeholder-authors]" >&2
  exit 2
fi

for command in rg rsync tar sha256sum; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    echo "[ERROR] Required command not found: ${command}" >&2
    exit 1
  fi
done

required_paths=(
  README.md
  CITATION.cff
  .zenodo.json
  paper/OpenROAD_Evolve.pdf
  artifacts/01-table4-qor/inputs/bo_paper
  artifacts/01-table4-qor/selected-programs/manifest.json
  artifacts/02-table5-composability/inputs/provenance.json
  artifacts/03-table6-cutrow/inputs/provenance.json
  artifacts/04-aes-smoke/expected/ae_reproduction_lock.json
  src/dpl_evolve_agent
  scripts/agent/run_artifact.sh
)
for path in "${required_paths[@]}"; do
  if [[ ! -e "${AE_ROOT}/${path}" ]]; then
    echo "[ERROR] Required release content is missing: ${path}" >&2
    exit 1
  fi
done

metadata_has_placeholders=0
if ! grep -q '^authors:' "${AE_ROOT}/CITATION.cff"; then
  metadata_has_placeholders=1
fi
if grep -q 'DPLEvolve Contributors\|AUTHOR INFORMATION REQUIRED\|AUTHOR LIST REQUIRED\|See paper for author' \
  "${AE_ROOT}/.zenodo.json" "${AE_ROOT}/CITATION.cff" "${AE_ROOT}/README.md"; then
  metadata_has_placeholders=1
fi
if [[ "${metadata_has_placeholders}" -ne 0 && "${ALLOW_PLACEHOLDER_AUTHORS}" -ne 1 ]]; then
  echo "[ERROR] Release metadata still has missing/placeholder authors." >&2
  echo "        Update CITATION.cff and .zenodo.json, plus any rendered citation text." >&2
  echo "        Use --allow-placeholder-authors only to audit archive contents." >&2
  exit 2
fi

echo "============================================"
echo " DPLEvolve AE - Zenodo Archive Preparation"
echo "============================================"
echo ""
echo "[1/5] Running no-LLM evidence gates..."
make -C "${AE_ROOT}" evidence

STAMP="$(date +%Y%m%d-%H%M%S)"
STAGING="$(mktemp -d /tmp/dplevolve-zenodo.XXXXXX)"
ARCHIVE_ROOT="DPLEvolve-AE-zenodo-${STAMP}"
ARCHIVE_NAME="${ARCHIVE_ROOT}.tar.gz"
ARCHIVE_PATH="/tmp/${ARCHIVE_NAME}"
trap 'rm -rf "${STAGING}"' EXIT
mkdir -p "${STAGING}/${ARCHIVE_ROOT}/DPLEvolve-AE"

echo "[2/5] Copying release artifact content..."
rsync -a \
  --exclude='.git/' \
  --exclude='__pycache__/' \
  --exclude='*.pyc' \
  --exclude='*.tar.gz' \
  --exclude='*.zip' \
  --exclude='env.local.sh' \
  --exclude='**/environment.local.sh' \
  --exclude='provenance/current-machine.json' \
  --exclude='extras/' \
  --exclude='artifacts/*/output/*' \
  --exclude='artifacts/*/selected-programs/output/*' \
  --exclude='src/dpl_evolve_agent/.dpl_evolve_state/' \
  --exclude='src/dpl_evolve_agent/.venv_raytune/' \
  --exclude='src/dpl_evolve_agent/.runtime_aliases/' \
  --exclude='src/dpl_evolve_agent/local_backups/' \
  "${AE_ROOT}/" "${STAGING}/${ARCHIVE_ROOT}/DPLEvolve-AE/"

release_path_pattern='/'"home/"'[A-Za-z0-9._-]+/|/'"Users/"'[A-Za-z0-9._-]+/|/'"root/"
mapfile -t leaked_paths < <(
  rg -l --hidden "${release_path_pattern}" \
    "${STAGING}/${ARCHIVE_ROOT}/DPLEvolve-AE" || true
)
if [[ ${#leaked_paths[@]} -ne 0 ]]; then
  echo "[ERROR] Release content contains developer-specific absolute paths:" >&2
  printf '  %s\n' "${leaked_paths[@]:0:20}" >&2
  if [[ ${#leaked_paths[@]} -gt 20 ]]; then
    echo "  ... and $((${#leaked_paths[@]} - 20)) more" >&2
  fi
  exit 1
fi

cp "${AE_ROOT}/docs/quickstart.md" "${STAGING}/${ARCHIVE_ROOT}/QUICKSTART.md"

echo "[3/5] Writing archive README..."
cat > "${STAGING}/${ARCHIVE_ROOT}/README-ZENODO.md" <<'ZENODOEOF'
# DPLEvolve Artifact Evaluation - Zenodo Archive

Artifact for the MLCAD 2026 short paper:

> **From Tool Invocation to Source-Mechanism Exploration: Protected White-Box DSE for Open-Source EDA**

## Recommended Review

```bash
cd DPLEvolve-AE
make evidence
```

This regenerates the Table 4 BO summary from normalized trial records,
regenerates the two ReviewDSE columns from selected-candidate records, verifies
the compact archived summaries behind Tables 5 and 6, and integrity-checks 18
selected source programs. It takes seconds and makes no EDA or LLM call.

For the optional real OpenROAD smoke flow on a clean machine:

```bash
make bootstrap
make setup
make smoke
```

## Contents

- `DPLEvolve-AE/`: complete AE repository and bundled ReviewDSE framework
- `DPLEvolve-AE/paper/`: reviewed paper PDF
- `QUICKSTART.md`: detailed clean-machine instructions
- `MANIFEST.sha256`: archive file-integrity manifest

## Known Limitation

The original nine Table 4 ODB inputs were not retained, so exact nine-case
frozen-program numerical replay is unavailable. Archived-record verification,
source integrity checking, and the AES OpenROAD smoke flow remain executable.

All versions are pinned in `DPLEvolve-AE/provenance/source-commits.json`.
ZENODOEOF

echo "[4/5] Generating integrity manifest and archive..."
(
  cd "${STAGING}/${ARCHIVE_ROOT}"
  find . -type f ! -name MANIFEST.sha256 -print0 \
    | sort -z \
    | xargs -0 sha256sum > MANIFEST.sha256
)
tar -C "${STAGING}" -czf "${ARCHIVE_PATH}" "${ARCHIVE_ROOT}"

echo "[5/5] Validating archive contents..."
LISTING="${STAGING}/archive-list.txt"
tar -tzf "${ARCHIVE_PATH}" > "${LISTING}"
for required in \
  "${ARCHIVE_ROOT}/DPLEvolve-AE/README.md" \
  "${ARCHIVE_ROOT}/DPLEvolve-AE/paper/OpenROAD_Evolve.pdf" \
  "${ARCHIVE_ROOT}/DPLEvolve-AE/artifacts/01-table4-qor/inputs/bo_paper/aes_asap7.trials.tsv" \
  "${ARCHIVE_ROOT}/DPLEvolve-AE/artifacts/01-table4-qor/selected-programs/manifest.json" \
  "${ARCHIVE_ROOT}/DPLEvolve-AE/artifacts/02-table5-composability/inputs/counterexamples.tsv" \
  "${ARCHIVE_ROOT}/DPLEvolve-AE/artifacts/03-table6-cutrow/inputs/reviewdse.tsv" \
  "${ARCHIVE_ROOT}/DPLEvolve-AE/artifacts/04-aes-smoke/expected/ae_reproduction_lock.json" \
  "${ARCHIVE_ROOT}/DPLEvolve-AE/src/dpl_evolve_agent/baseline/run_baseline.sh" \
  "${ARCHIVE_ROOT}/MANIFEST.sha256"; do
  if ! grep -Fxq "${required}" "${LISTING}"; then
    echo "[ERROR] Archive is missing: ${required}" >&2
    exit 1
  fi
done
if grep -Eq '/\.git/|/__pycache__/|/\.dpl_evolve_state/|/extras/|/output/[^/]+$' "${LISTING}"; then
  echo "[ERROR] Archive contains excluded generated or repository state." >&2
  exit 1
fi

SIZE="$(du -h "${ARCHIVE_PATH}" | cut -f1)"
SHA256="$(sha256sum "${ARCHIVE_PATH}" | cut -d' ' -f1)"
echo ""
echo "[PASS] Zenodo archive content validation passed"
echo "Archive: ${ARCHIVE_PATH}"
echo "Size:    ${SIZE}"
echo "SHA-256: ${SHA256}"
if [[ "${metadata_has_placeholders}" -ne 0 ]]; then
  echo "[WARN] Audit-only archive: author metadata is still incomplete."
fi
