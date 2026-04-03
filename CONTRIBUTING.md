# Contributing

Thanks for taking a look.

This is a small hobby ESP32 project, so contributions are welcome, but the goal is to keep things simple and fun rather than turning the repo into a large framework.

## Ground Rules

- Keep changes small and focused.
- Prefer straightforward ESP-IDF patterns over heavy abstraction.
- Do not commit real Wi-Fi credentials, broker URIs, usernames, passwords, API keys, or other secrets.
- Update `README.md` when behavior, setup, or operator-facing commands change.
- If you vendor or update third-party code, update `THIRD_PARTY_NOTICES.md` too.

## Setup

1. Install ESP-IDF `v6.0`.
2. Activate the ESP-IDF environment in your shell.
3. Run `./scripts/bootstrap.sh`.

## Build

Run:

```sh
./scripts/build.sh
```

If the build directory was created under a different ESP-IDF Python environment, rerun with:

```sh
./scripts/build.sh fullclean
```

## Before Opening A PR

Please do the following when possible:

1. Run `./scripts/build.sh` successfully.
2. If you changed shell scripts, run `sh -n scripts/*.sh` and `shellcheck` if available.
3. If your change affects runtime behavior, describe what you tested on hardware and what remains unverified.
4. Double-check that no private credentials or machine-specific values were introduced.

## Scope Notes

Useful contributions include:

- bug fixes
- build or tooling improvements
- documentation cleanup
- Sparkplug payload correctness fixes
- console usability improvements
- hardware validation notes

Changes that significantly expand scope may be better discussed first.
