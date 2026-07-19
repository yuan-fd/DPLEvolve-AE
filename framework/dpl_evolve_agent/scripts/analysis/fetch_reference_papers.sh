#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: ./scripts/analysis/fetch_reference_papers.sh [--include-personal-use]

Fetches redistribution-sensitive papers into a local ignored cache only.
The cache is for local reading and research-agent context, not for Git commits.

Options:
  --include-personal-use  Also fetch author-version PDFs marked personal use.
  -h, --help              Show this help.
USAGE
}

include_personal_use=0
for arg in "$@"; do
  case "$arg" in
    --include-personal-use)
      include_personal_use=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg" >&2
      usage >&2
      exit 2
      ;;
  esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${script_dir}/../.." && pwd)}"
repo_root="$(realpath -m "${repo_root}")"
paper_dir="${repo_root}/knowledge/reference/papers"
cache_dir="${DPL_EVOLVE_PAPER_CACHE_DIR:-${paper_dir}/pdf_cache}"

mkdir -p "${cache_dir}"

if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required to fetch reference papers." >&2
  exit 1
fi

download_pdf() {
  local name="$1"
  local url="$2"
  local out="${cache_dir}/${name}"
  local tmp="${out}.tmp"

  if [[ -s "${out}" ]]; then
    echo "[skip] ${name} already exists"
    return
  fi

  echo "[fetch] ${name}"
  curl -L --fail --retry 3 --connect-timeout 20 -o "${tmp}" "${url}"
  mv "${tmp}" "${out}"
}

download_pdf \
  "DREAMPlace_2019_Lin.pdf" \
  "https://research.nvidia.com/sites/default/files/pubs/2019-06_DREAMPlace%3A-Deep-Learning/54_1_Lin_DREAMPLACE.pdf"

if [[ "${include_personal_use}" == "1" ]]; then
  download_pdf \
    "LEGALM_ISPD2025_Mai_PERSONAL_USE_ONLY.pdf" \
    "https://yibolin.com/publications/papers/PLACE_ISPD2025_Mai.pdf"
else
  echo "[info] LEGALM ISPD 2025 author PDF is personal-use/not-for-redistribution."
  echo "[info] Re-run with --include-personal-use to cache it locally."
fi

cat <<EOF

Cache directory:
  ${cache_dir}

Tracked reference already in repo:
  knowledge/reference/OpenDP.pdf

Link-only or license-check-needed references:
  NBLG exact-title lookup
  LEGALM 2.0 exact-title lookup
  Abacus DOI: https://doi.org/10.1145/1353629.1353640

Do not commit files under pdf_cache/.
EOF
