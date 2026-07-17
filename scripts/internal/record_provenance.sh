#!/usr/bin/env bash
# DPLEvolve AE — Record Machine Provenance (via Python for safe JSON)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/env_vars.sh"
dpl_ae_resolve_env

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/utils.sh"

OUTPUT="${1:-${AE_ROOT}/provenance/current-machine.json}"

dpl_ae_info "Recording machine provenance..."

YOSYS_BIN="${YOSYS_EXE:-unknown}"
OR_BIN="${OPENROAD_EXE:-unknown}"

"${DPL_EVOLVE_PYTHON}" -c "
import json, os, subprocess, hashlib, sys, datetime

def sh(cmd, default='unknown'):
    try:
        return subprocess.check_output(cmd, shell=True, stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return default

def sha256(path):
    if not path or not os.path.isfile(path):
        return 'unknown'
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1048576), b''):
            h.update(chunk)
    return h.hexdigest()

data = {
    'generated_at': datetime.datetime.now().isoformat(),
    'generated_by': 'record_provenance.sh',
    'machine': {
        'hostname': sh('hostname'),
        'os': sh('cat /etc/os-release 2>/dev/null | head -1 | cut -d\\\" -f2', 'unknown'),
        'kernel': sh('uname -r'),
        'arch': sh('uname -m'),
        'cpu_cores': int(sh('nproc', '0')),
        'memory_gb': sh(\"free -g 2>/dev/null | awk '/^Mem:/{print \$2}'\"),
        'disk_gb_available': sh(\"df -BG '${AE_ROOT}' 2>/dev/null | awk 'NR==2{print \$4}' | sed 's/G//'\"),
    },
    'toolchain': {
        'gcc': sh('gcc --version 2>/dev/null | head -1', 'unknown'),
        'cmake': sh('cmake --version 2>/dev/null | head -1', 'unknown'),
        'make': sh('make --version 2>/dev/null | head -1', 'unknown'),
        'python': sh('${DPL_EVOLVE_PYTHON} --version 2>&1', 'unknown'),
    },
    'binaries': {
        'yosys': {
            'path': '${YOSYS_BIN}',
            'sha256': sha256('${YOSYS_BIN}'),
        },
        'openroad': {
            'path': '${OR_BIN}',
            'sha256': sha256('${OR_BIN}'),
        },
    },
    'ae_root': '${AE_ROOT}',
    'agent_root': '${DPL_EVOLVE_AGENT_ROOT:-unset}',
    'orfs_root': '${ORFS_ROOT:-unset}',
    'state_root': '${DPL_EVOLVE_STATE_ROOT:-unset}',
}

with open('${OUTPUT}', 'w') as f:
    json.dump(data, f, indent=2, ensure_ascii=False)

print('OK')
"

dpl_ae_ok "Provenance recorded to: ${OUTPUT}"
