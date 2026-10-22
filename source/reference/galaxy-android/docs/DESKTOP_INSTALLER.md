---
title: Galaxy Definitive - Desktop Installer Pipeline
description: Overview of the automated PC desktop extraction, conversion, and packaging pipeline for Galaxy Definitive.
tags:
  - installer
  - desktop
  - python
  - pipeline
  - galaxy-definitive
---

# Desktop Installer Pipeline

> For general installation instructions, please refer to the main [Installation Guide](INSTALLATION.md).

The desktop pipeline combines disc extraction, game conversion, APK installation, and data transfer into automated scripts:

- **1-Click Game Transfer:** [`transfer_game.bat`](../transfer_game.bat)
- **1-Click Phone Deployment:** [`deploy_to_phone.bat`](../deploy_to_phone.bat)
- **Cooker Script:** [`tools/cook/cook.py`](../tools/cook/cook.py)
- **Data Pushing Tool:** [`tools/push_data.py`](../tools/push_data.py)

## References
- Built upon toolchains from [bigmak94/GalaxyQuest](https://github.com/bigmak94/GalaxyQuest) and [SMGCommunity/Petari](https://github.com/SMGCommunity/Petari).
