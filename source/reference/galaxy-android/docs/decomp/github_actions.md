---
title: Decompilation CI with GitHub Actions
description: Setting up continuous integration, build container registries, and Frogress progress tracking for decompilation repositories.
tags:
  - decompilation
  - github-actions
  - ci
  - frogress
  - petari
---

# Decompilation CI with GitHub Actions

> **Acknowledgements:**
> Sourced from the [Petari Decompilation Project](https://github.com/SMGCommunity/Petari). All credits belong to the SMGCommunity reverse-engineering team.

This guide explains how CI workflows are configured for automated decompilation matching checks and progress reporting.

- [Build Repository](#build-repository)
- [Progress Tracking](#progress)
- [Workflow Configuration](#workflow)

---

## Build Repository

A companion repository can be used to build and store the CI container:

> [!CAUTION]
> This container repository should remain **private** to avoid exposing proprietary game assets.

1. Create a repository for the build container.
2. Add necessary verification assets (`sys/main.dol`, `.rel` files).
3. Under package settings, grant read access to the main decompilation repository.

---

## Progress Tracking

1. In the [GC/Wii Decompilation Discord](https://discord.gg/hKx3FJJgrV), visit `#frogress` to request an API key for your project.
2. Under repository secrets, register `PROGRESS_API_KEY`.

---

## Workflow Configuration

1. Use `.github/workflows/build.yml` for automated Ninja matching builds.
2. Set `container:` to point to your build image.
3. Configure `PROGRESS_SLUG` to track matching percentages on every push to `main`.

---

## References
- [Getting Started](getting_started.md)
- [Petari Repository](https://github.com/SMGCommunity/Petari)
