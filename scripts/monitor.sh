#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)

# shellcheck source=/dev/null
. "${SCRIPT_DIR}/common.sh"

script_require_idf
script_ensure_target

MONITOR_PORT=$(script_detect_port)
script_log "Opening monitor on ${MONITOR_PORT}"
exec "${IDF_PY}" -C "${PROJECT_DIR}" -B "${BUILD_DIR}" -p "${MONITOR_PORT}" "$@" monitor
