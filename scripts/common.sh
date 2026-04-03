#!/bin/sh

set -eu

if [ -z "${SCRIPT_DIR:-}" ]; then
    SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fi

if [ -z "${PROJECT_DIR:-}" ]; then
    PROJECT_DIR=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)
fi

: "${BUILD_DIR:=${PROJECT_DIR}/build}"
: "${ESP_TARGET:=esp32}"
: "${IDF_PY:=idf.py}"
: "${IDF_BAUD:=460800}"

script_log() {
    printf '[sparkplug-scripts] %s\n' "$*"
}

script_print_idf_hint() {
    script_log "Install or activate ESP-IDF v6.0, then rerun the command."
    script_log "Docs: https://idf.espressif.com/"
    script_log "Expected shell action: source \"/path/to/esp-idf/export.sh\""
    script_log "Optional convenience loader: ~/.esp_idf_env"
}

script_os_name() {
    uname -s 2>/dev/null || printf 'Unknown'
}

script_require_supported_host() {
    case "$(script_os_name)" in
        Linux|Darwin)
            return 0
            ;;
        *)
            script_log "ERROR: unsupported host. Only Linux and macOS are supported."
            return 1
            ;;
    esac
}

script_source_if_present() {
    _script_source_path=$1

    if [ ! -f "${_script_source_path}" ]; then
        return 1
    fi

    # shellcheck source=/dev/null
    . "${_script_source_path}"
    return 0
}

script_prepend_path_once() {
    _script_path_dir=$1

    if [ -z "${_script_path_dir}" ] || [ ! -d "${_script_path_dir}" ]; then
        return 1
    fi

    case ":${PATH}:" in
        *":${_script_path_dir}:"*)
            return 0
            ;;
    esac

    PATH="${_script_path_dir}:${PATH}"
    export PATH
    return 0
}

script_add_managed_tool_path_if_present() {
    _script_tool_name=$1

    if command -v "${_script_tool_name}" >/dev/null 2>&1; then
        return 0
    fi

    case "${_script_tool_name}" in
        ninja)
            for _script_candidate in "${HOME}"/.espressif/tools/ninja/*/ninja; do
                if [ -x "${_script_candidate}" ]; then
                    script_prepend_path_once "$(dirname "${_script_candidate}")"
                    command -v "${_script_tool_name}" >/dev/null 2>&1 && return 0
                fi
            done
            ;;
        cmake)
            for _script_candidate in "${HOME}"/.espressif/tools/cmake/*/CMake.app/Contents/bin/cmake; do
                if [ -x "${_script_candidate}" ]; then
                    script_prepend_path_once "$(dirname "${_script_candidate}")"
                    command -v "${_script_tool_name}" >/dev/null 2>&1 && return 0
                fi
            done
            ;;
    esac

    return 1
}

script_try_source_idf_env() {
    if command -v "${IDF_PY}" >/dev/null 2>&1; then
        return 0
    fi

    if [ -n "${IDF_PATH:-}" ] && script_source_if_present "${IDF_PATH}/export.sh"; then
        command -v "${IDF_PY}" >/dev/null 2>&1 && return 0
    fi

    if script_source_if_present "${HOME}/esp/esp-idf/export.sh"; then
        command -v "${IDF_PY}" >/dev/null 2>&1 && return 0
    fi

    # shellcheck disable=SC2086
    for _script_glob in ${HOME}/.espressif/frameworks/esp-idf-v*/export.sh; do
        if [ -f "${_script_glob}" ] && script_source_if_present "${_script_glob}"; then
            command -v "${IDF_PY}" >/dev/null 2>&1 && return 0
        fi
    done

    if script_source_if_present "${HOME}/.esp_idf_env"; then
        command -v "${IDF_PY}" >/dev/null 2>&1 && return 0
    fi

    return 1
}

script_require_idf() {
    if [ "${SCRIPT_IDF_PREFLIGHT_DONE:-0}" = "1" ]; then
        return 0
    fi

    script_require_supported_host

    if ! script_try_source_idf_env; then
        script_log "ERROR: idf.py is not available in this shell."
        script_print_idf_hint
        return 1
    fi

    if ! command -v "${IDF_PY}" >/dev/null 2>&1; then
        script_log "ERROR: idf.py is still not available after environment setup."
        script_print_idf_hint
        return 1
    fi

    if ! SCRIPT_IDF_VERSION=$("${IDF_PY}" --version 2>/dev/null); then
        script_log "ERROR: idf.py exists but failed to run."
        script_print_idf_hint
        return 1
    fi

    script_add_managed_tool_path_if_present cmake || true
    script_add_managed_tool_path_if_present ninja || true

    script_log "ESP-IDF ready: ${SCRIPT_IDF_VERSION}"
    SCRIPT_IDF_PREFLIGHT_DONE=1
    export SCRIPT_IDF_PREFLIGHT_DONE
    return 0
}

script_read_config_target() {
    _script_target_file=$1

    if [ ! -f "${_script_target_file}" ]; then
        return 0
    fi

    sed -n 's/^CONFIG_IDF_TARGET="\([^"]*\)"/\1/p' "${_script_target_file}" | head -n 1
}

script_ensure_target() {
    _script_current_target=$(script_read_config_target "${PROJECT_DIR}/sdkconfig")

    if [ -z "${_script_current_target}" ]; then
        _script_current_target=$(script_read_config_target "${PROJECT_DIR}/sdkconfig.defaults")
    fi

    if [ -n "${_script_current_target}" ] && [ "${_script_current_target}" != "${ESP_TARGET}" ]; then
        script_log "ERROR: configured target is ${_script_current_target}, expected ${ESP_TARGET}."
        return 1
    fi

    return 0
}

script_detect_port() {
    if [ -n "${PORT:-}" ]; then
        if [ ! -e "${PORT}" ]; then
            script_log "WARNING: PORT=${PORT} does not exist on this host."
        fi
        printf '%s\n' "${PORT}"
        return 0
    fi

    case "$(script_os_name)" in
        Darwin)
            _script_port_patterns='/dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial*'
            ;;
        Linux)
            _script_port_patterns='/dev/serial/by-id/* /dev/ttyACM* /dev/ttyUSB*'
            ;;
        *)
            _script_port_patterns='/dev/ttyACM* /dev/ttyUSB* /dev/cu.usbmodem* /dev/cu.usbserial*'
            ;;
    esac

    _script_first_port=''
    _script_port_count=0

    for _script_pattern in ${_script_port_patterns}; do
        # shellcheck disable=SC2086
        for _script_candidate in ${_script_pattern}; do
            [ -e "${_script_candidate}" ] || continue

            _script_port_count=$((_script_port_count + 1))
            if [ "${_script_port_count}" -eq 1 ]; then
                _script_first_port=${_script_candidate}
            else
                script_log "Detected additional serial port: ${_script_candidate}"
            fi
        done
    done

    if [ "${_script_port_count}" -eq 0 ]; then
        script_log "ERROR: no serial port detected."
        script_log "Set PORT=/dev/ttyUSB0 on Linux or PORT=/dev/cu.usbmodemXXXX on macOS."
        return 1
    fi

    if [ "${_script_port_count}" -gt 1 ]; then
        script_log "ERROR: multiple serial ports detected. Set PORT explicitly."
        return 1
    fi

    printf '%s\n' "${_script_first_port}"
    return 0
}
