#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)

# shellcheck source=/dev/null
. "${SCRIPT_DIR}/common.sh"

script_require_idf
script_ensure_target

mkdir -p "${BUILD_DIR}"
script_log "Building ${PROJECT_DIR} for target ${ESP_TARGET}"
exec "${IDF_PY}" -C "${PROJECT_DIR}" -B "${BUILD_DIR}" "$@" build
