# Third-Party Notices

This repository includes or depends on third-party software.

## Mixed-License Note

The repository's own original code is licensed under Apache 2.0, but some vendored files and dependencies are provided under different upstream licenses. These third-party components keep their own licenses; they are not relicensed to Apache 2.0 by inclusion in this repository.

## Vendored Code

### Eclipse Tahu Sparkplug B Schema

- Path: `components/sparkplug_node/vendor/eclipse_tahu/`
- Relevant files:
  - `sparkplug_b.proto`
  - generated bindings checked into the same directory
- Upstream project: Eclipse Tahu
- Upstream repository: `https://github.com/eclipse/tahu`
- License signal in vendored source: `EPL-2.0`

The vendored `sparkplug_b.proto` file includes the following header notice:

> Copyright (c) 2015, 2018 Cirrus Link Solutions and others
>
> This program and the accompanying materials are made available under the terms of the Eclipse Public License 2.0.
>
> SPDX-License-Identifier: EPL-2.0

## Vendored Nanopb Runtime

- Path: `components/sparkplug_node/vendor/nanopb/`
- Relevant files:
  - `pb.h`
  - `pb_common.c`
  - `pb_common.h`
  - `pb_decode.c`
  - `pb_decode.h`
  - `pb_encode.c`
  - `pb_encode.h`
- Upstream project: Nanopb
- Upstream repository: `https://github.com/nanopb/nanopb`
- Vendored version signal: `nanopb-0.4.9.1` in `pb.h`
- Upstream license: `Zlib`

The vendored nanopb files in this repository do not currently include the upstream `LICENSE.txt` file. The upstream project states that Nanopb is distributed under the Zlib license.

## Build-Time Managed Dependencies

This project also pulls dependencies at build time through the ESP-IDF component manager. At the time of writing, the direct managed dependency recorded in `dependencies.lock` is:

- `espressif/mqtt`

Because this dependency is downloaded into the local build environment instead of being vendored into this repository, its licensing terms should be reviewed from the upstream component source and registry metadata as part of release or redistribution review.
