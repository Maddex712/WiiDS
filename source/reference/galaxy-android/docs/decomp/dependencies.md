---
title: SMG Decompilation - Dependencies
description: Host build dependencies and toolchain requirements for compiling and analyzing the decompilation codebase.
tags:
  - decompilation
  - dependencies
  - petari
  - build-system
  - ninja
  - python
---

# Decompilation Dependencies

> **Acknowledgements:**
> Sourced from the [Petari Decompilation Project](https://github.com/SMGCommunity/Petari). All credits belong to the SMGCommunity reverse-engineering team.

## Windows
On Windows, it is highly recommended to use native tooling. WSL or MSYS2 are not required.
- Install [Python](https://www.python.org/downloads/) (3.8+) and ensure it is on `%PATH%`.
- Download [Ninja](https://github.com/ninja-build/ninja/releases) or install via pip: `pip install ninja`.

## macOS
- Install Ninja:
  ```sh
  brew install ninja
  ```
- Install Wine Crossover:
  ```sh
  brew install --cask --no-quarantine gcenx/wine/wine-crossover
  ```

## Linux
- Install Ninja from your distribution's package manager (`apt install ninja-build` / `pacman -S ninja`).
- For non-x86_64 architectures: Install Wine from your package manager. For x86_64, [wibo](https://github.com/decompals/wibo) is automatically downloaded.

## References
- [Getting Started](getting_started.md)
- [Petari Repository](https://github.com/SMGCommunity/Petari)
