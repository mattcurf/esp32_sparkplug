[![Build](https://github.com/mattcurf/esp32_sparkplug/actions/workflows/build.yml/badge.svg)](https://github.com/mattcurf/esp32_sparkplug/actions/workflows/build.yml)

# Sparkplug B ESP32 Temperature Node
ESP-IDF firmware for a classic ESP32 Sparkplug B node that reads a TMP36 on `GPIO32` / `ADC1_CHANNEL_4`, converts the reading to Celsius, and publishes Sparkplug B protobuf payloads over MQTT alongside a synthetic sinewave metric.

For more information about Eclipse Sparkplug B, see https://github.com/eclipse-sparkplug/sparkplug  (Sparkplug®, Sparkplug Compatible, and the Sparkplug Logo are trademarks of the Eclipse Foundation.)

## Project Notes
This repo is a quick, fun embedded project of mine, not a polished production firmware baseline!
- expect some shortcuts and simplifications that keep the code easy to read and easy to hack on
- do not treat it as a definitive reference for ESP32 architecture, security hardening, fleet provisioning, or long-term maintainability best practices
- if you want to build something production-facing from it, plan to review the configuration, credential handling, dependency licensing, observability, testing, and operational failure paths carefully

## License Notes
The original project code in this repository is licensed under Apache 2.0. This repository also includes third-party code under other licenses, so the repo should be treated as mixed-license rather than "everything is Apache-2.0".

- see `LICENSE.txt` for the project license covering the original code in this repository
- see `THIRD_PARTY_NOTICES.md` for vendored and managed dependency license notes
- do not assume the vendored Sparkplug schema or nanopb runtime files are covered by Apache 2.0

## Implemented Scope
- `app_config`: fixed compile-time constants for Wi-Fi, MQTT, Sparkplug, ADC, and publish policy
- `sensor_tmp36`: ADC oneshot init, calibration, 32-sample averaging, millivolt conversion, Celsius conversion, and cached reading snapshots
- `wifi_manager`: station init/start, reconnect handling, and status queries
- `time_sync`: SNTP startup, valid-time gating, and time status queries
- `sparkplug_node`: vendored Sparkplug schema + nanopb runtime, topic building, payload encode/decode, MQTT session ownership, birth/death lifecycle, rebirth handling, publish policy, optional Primary Host Application (`PHID`) wait, and periodic disconnect simulation for Primary host death handling
- `app_console`: UART REPL with runtime diagnostics and manual publish/rebirth actions
- `app_main`: startup ordering, top-level wiring, and MQTT connection status LED updates

## Deliberate Out-Of-Scope Items
- No Device support (`DBIRTH` / `DDATA` / `DDEATH` / `DCMD`). This firmware is intentionally node-only, and Sparkplug devices are optional.
- No multi-MQTT-server support. This is optional per the spec and only relevant when a Primary Host is configured to coordinate multiple brokers.

## Fixed Runtime Inputs
- Target: `esp32`
- Sensor: `TMP36` on `GPIO32` / `ADC1_CHANNEL_4`
- MQTT status LED default: `GPIO2`, active-high
- Broker default: `mqtt://broker.example.com:1883`
- Sparkplug group ID: `home`
- Sparkplug node ID: `sensor`
- Publish unit: `Celsius`

## Local Configuration
Before flashing real hardware, update the placeholder connection settings in [components/app_config/app_config.c](components/app_config/app_config.c):

- set `.wifi.ssid` to your Wi-Fi network name
- set `.wifi.password` to your Wi-Fi password
- set `.sparkplug.broker_uri` to your MQTT broker URI, for example `mqtt://broker-host-or-ip:1883`
- if your broker requires authentication, set `.sparkplug.username` and `.sparkplug.password`
- if your broker does not require authentication, leave `.sparkplug.username` and `.sparkplug.password` as `NULL`
- if your board uses a different LED pin or LED polarity, update `.status_led.gpio_num` and `.status_led.active_high`
- to enable Primary Host Application wait, set `.sparkplug.primary_host_id` to the host application ID (for example `"scada_host"`); leave it as `NULL` to publish NBIRTH immediately without waiting for a host

For TLS brokers, point `.sparkplug.broker_uri` at an `mqtts://` endpoint such as `mqtts://broker-host-or-ip:8883` and replace the embedded CA chain in [components/sparkplug_node/certs/ca-chain.cert.pem](components/sparkplug_node/certs/ca-chain.cert.pem) with the PEM chain that signs your broker certificate.

The repository intentionally ships with placeholders for Wi-Fi and MQTT connection details so it can be published safely as open source.

The status LED is driven from the connection state:

- off while booting or whenever Wi-Fi does not yet have an IP address
- brief pulse every `0.5 s` while Wi-Fi is up but MQTT is not yet connected
- slow blink (`1 s` on / `1 s` off) while MQTT is connected but waiting for a configured Primary Host to come online
- solid on while the node is connected to the MQTT broker and the Primary Host is online (or no Primary Host is configured)

## Published Metrics
The node publishes these Sparkplug metrics:

- `bdSeq`
- `Node Control/Rebirth` with alias `1`
- `temperature_c` with alias `2`
- `synthetic_sinewave` with alias `3`

`synthetic_sinewave` is a generated float in the range `0..100`. It is sampled and published once per second using a 20-second sine period, so normal steady-state `NDATA` traffic occurs at `1 Hz`.

## Sparkplug Topics
The firmware uses these exact node-level topics:

- `spBv1.0/home/NBIRTH/sensor`
- `spBv1.0/home/NDATA/sensor`
- `spBv1.0/home/NDEATH/sensor`
- subscribes to `spBv1.0/home/NCMD/sensor`
- subscribes to `STATE/<primary_host_id>` when a Primary Host is configured

Implemented message types:

- `NBIRTH`
- `NDATA`
- `NDEATH`
- `NCMD` rebirth decode

## Primary Host Application (PHID)

When `.sparkplug.primary_host_id` is set to a non-`NULL` string, the edge node subscribes to the `STATE/<primary_host_id>` topic after connecting to the MQTT broker and gates its `NBIRTH` until the Primary Host publishes a `STATE` message with `{"online":true, "timestamp":...}`. Per the Sparkplug 3.0 specification:

- the node validates the `online` boolean and `timestamp` fields in each `STATE` payload
- stale `STATE` messages (timestamp less than the previously received value) are ignored
- when the Primary Host goes offline (valid `STATE` with `online` set to `false`), the node publishes `NDEATH`, disconnects, and reconnects to wait for the host again
- when `primary_host_id` is `NULL` (the default), the node publishes `NBIRTH` immediately after subscribing to `NCMD`, preserving the original behavior

## Disconnect Simulation

By default, the node periodically simulates an ungraceful network loss to exercise Sparkplug B Primary host death handling:

- enabled by default at boot
- disconnects every `3 minutes`
- each disconnect lasts `90 seconds`
- uses a temporary Wi-Fi drop so the MQTT session ends ungracefully and the broker can publish the node death certificate / last will behavior
- after Wi-Fi returns and the station has an IP again, the node reconnects, republishes `NBIRTH`, and resumes normal `NDATA`

This behavior is intended for validation and demo use, not for long-running stable operation.

## Console Commands

The UART console exposes:

- `status`
- `sensor`
- `wifi`
- `time`
- `mqtt`
- `sparkplug`
- `disconnect_sim [status|on|off]`
- `publish`
- `rebirth`
- `restart`

`disconnect_sim` control notes:

- `disconnect_sim` or `disconnect_sim status` shows whether the simulator is enabled and whether a simulated outage is active right now
- `disconnect_sim off` disables future simulated disconnects
- `disconnect_sim on` re-enables the default periodic disconnect behavior

## Pinned ESP-IDF Version

This repo is currently pinned to `ESP-IDF v6.0` for the documented host workflow and GitHub Actions builds.

## Supported Hosts

- Linux
- macOS

## Prerequisites

Install one local ESP-IDF environment and activate it in each shell before building. The recommended path is Espressif Installation Manager CLI, as described in the ESP-IDF setup docs:

- https://idf.espressif.com/
- https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/get-started/

The build uses the ESP-IDF component manager to pull `espressif/mqtt`, which is required on ESP-IDF 6 because MQTT is no longer provided as a built-in component.

Host tools expected by the wrapper scripts:

- `python3`
- `cmake`
- `ninja`
- `git`
- `idf.py`

Optional but recommended:

- `shellcheck`

If you keep a reusable shell loader such as `~/.esp_idf_env`, the wrapper scripts will try that path automatically when `idf.py` is not already on `PATH`.

## Bootstrap And Preflight

Run the bootstrap wrapper first:

```sh
./scripts/bootstrap.sh
```

The bootstrap script:

- checks that the host is Linux or macOS
- tries to locate or source ESP-IDF from common install paths
- verifies the common build tools are present after the ESP-IDF environment is available
- confirms `idf.py --version` runs successfully
- reports whether `shellcheck` is available

If preflight fails, activate the ESP-IDF environment in the current shell and rerun the script.

If ESP-IDF activation fails with missing tool errors such as `xtensa-esp-elf`, `openocd-esp32`, or `esp32ulp-elf`, install the managed ESP-IDF tools first:

```sh
"$HOME/.espressif/tools/python/v6.0/venv/bin/python" \
  "$HOME/.espressif/v6.0/esp-idf/tools/idf_tools.py" install
```

Then rerun:

```sh
./scripts/bootstrap.sh
```

## Build

Build the firmware with:

```sh
./scripts/build.sh
```

The wrapper builds from the repository root and uses the repo-local build directory at `build/`.

If you need to pass extra global `idf.py` arguments, place them after the wrapper name. Example:

```sh
./scripts/build.sh -v
```

## Flash

Flash the board with:

```sh
./scripts/flash.sh
```

The flash wrapper:

- uses `PORT` when explicitly provided
- otherwise tries to auto-detect a serial device on Linux or macOS
- uses `IDF_BAUD` if set, otherwise defaults to `460800`

Examples:

```sh
PORT=/dev/ttyUSB0 ./scripts/flash.sh
PORT=/dev/cu.usbmodem1101 IDF_BAUD=921600 ./scripts/flash.sh
```

Additional global `idf.py` arguments can also be forwarded:

```sh
PORT=/dev/ttyUSB0 ./scripts/flash.sh -v
```

## Monitor

Open the serial monitor with:

```sh
./scripts/monitor.sh
```

Examples:

```sh
PORT=/dev/ttyUSB0 ./scripts/monitor.sh
PORT=/dev/cu.usbmodem1101 ./scripts/monitor.sh
```

Exit the ESP-IDF monitor with `Ctrl-]`.

## CI

GitHub Actions runs the build on both:

- `ubuntu-latest`
- `macos-latest`

The workflow installs `ESP-IDF v6.0`, runs the repo bootstrap script, and then runs the repo build wrapper so local and CI build paths stay aligned.

## Validation Status

Validated locally in an ESP-IDF 6.0 environment with:

- `./scripts/bootstrap.sh`
- `./scripts/build.sh`
- `sh -n scripts/bootstrap.sh scripts/build.sh scripts/flash.sh scripts/monitor.sh scripts/common.sh`
