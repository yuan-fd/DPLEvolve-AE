#!/usr/bin/env bash
# Build and validate the public Zenodo archive.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ALLOW_PLACEHOLDER_AUTHORS=0
ALLOW_INCOMPLETE_PAPER_DATA=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --allow-placeholder-authors) ALLOW_PLACEHOLDER_AUTHORS=1 ;;
    --allow-incomplete-paper-data) ALLOW_INCOMPLETE_PAPER_DATA=1 ;;
    *)
      echo "Usage: $0 [--allow-placeholder-authors] [--allow-incomplete-paper-data]" >&2
      exit 2
      ;;
  esac
  shift
done

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
  configs/reproduction/paper-experiments.json
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

table6_data_complete=1
if ! make -C "${AE_ROOT}" check-table6-data; then
  table6_data_complete=0
  if [[ "${ALLOW_INCOMPLETE_PAPER_DATA}" -ne 1 ]]; then
    echo "[ERROR] Formal release refused: retained Table 6 replay data is unavailable or invalid." >&2
    echo "        Run 'make fetch-table6-data' and see docs/paper-data-layout.md." >&2
    echo "        Use --allow-incomplete-paper-data only for an audit-only package." >&2
    exit 3
  fi
fi

echo "============================================"
echo " DPLEvolve AE - Zenodo Archive Preparation"
echo "============================================"
echo ""
echo "[1/5] Running archive-audit gates..."
make -C "${AE_ROOT}" audit-archive

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

## Recommended Fresh Review

```bash
cd DPLEvolve-AE
make bootstrap
make build-tools
make prepare-paper-inputs CASE=aes_nangate45
make validate-evaluator CASE=aes_nangate45
```

This builds the pinned environment, generates one paper input, and runs the
actual protected detailed-placement evaluator. Use the root README for the
nine-case default, BO, selected-source replay, and cost-gated DSE commands.

For the optional compact archived-record audit:

```bash
make audit-archive
```

## Contents

- `DPLEvolve-AE/`: complete AE repository and bundled ReviewDSE framework
- `DPLEvolve-AE/configs/reproduction/`: paper experiment contract
- `QUICKSTART.md`: detailed clean-machine instructions
- `MANIFEST.sha256`: archive file-integrity manifest

## Known Limitation

Table 4 ODBs can be regenerated, but only AES Nangate45 currently has an
archived input checksum. Table 6 is replayable after `make fetch-table6-data`.
Table 5 is incomplete: the untracked SWERV DENSE_2 input config and six exact
candidate source trees were not retained, so its replay command reports
`BLOCKED`. See `docs/paper-data-layout.md`.

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
  "${ARCHIVE_ROOT}/DPLEvolve-AE/configs/reproduction/paper-experiments.json" \
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
if [[ "${table6_data_complete}" -ne 1 ]]; then
  echo "[WARN] Audit-only archive: retained Table 6 replay data was not installed."
fi
