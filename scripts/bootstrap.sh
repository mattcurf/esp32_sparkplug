#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)

# shellcheck source=/dev/null
. "${SCRIPT_DIR}/common.sh"

BOOTSTRAP_FAILED=0

check_required_command() {
    _bootstrap_cmd=$1

    if command -v "${_bootstrap_cmd}" >/dev/null 2>&1; then
        script_log "Found ${_bootstrap_cmd}: $(command -v "${_bootstrap_cmd}")"
        return 0
    fi

    script_log "ERROR: missing required command: ${_bootstrap_cmd}"
    BOOTSTRAP_FAILED=1
    return 0
}

check_optional_command() {
    _bootstrap_cmd=$1

    if command -v "${_bootstrap_cmd}" >/dev/null 2>&1; then
        script_log "Found optional tool ${_bootstrap_cmd}: $(command -v "${_bootstrap_cmd}")"
    else
        script_log "Optional tool not found: ${_bootstrap_cmd}"
    fi
}

script_log "Project directory: ${PROJECT_DIR}"
script_log "Host OS: $(script_os_name)"

if ! script_require_supported_host; then
    exit 1
fi

check_required_command python3
check_required_command git
check_optional_command shellcheck

if script_require_idf; then
    :
else
    BOOTSTRAP_FAILED=1
fi

check_required_command cmake
check_required_command ninja

if [ "${BOOTSTRAP_FAILED}" -ne 0 ]; then
    script_log "Bootstrap preflight failed."
    exit 1
fi

script_log "Bootstrap preflight passed."
