# Galaxy inside WiiDS 1.0

Galaxy 1 now runs inside the single non-debuggable WiiDS APK. Read
[setup, migration, limits and rebuild](../../docs/release/GALAXY-SETUP.md).
The current snapshot is based on OpenPort-Labs/Galaxy-Android
`1abcb975927ecb4189949bd51f81e73a04d97566` (GPL-3.0), including Petari CC0
sources and local platform/game changes. Retain GalaxyQuest and dependency credits.
Galaxy 2 is not included. Earlier logs/handoffs describe historical helper builds.

Build order: Build-Native.ps1, Build-Importer.py, Stage-Integrated.py, then
scripts/Package-PublicRelease.ps1. No standalone-helper packaging is required.
LowerAssets.py generates private art/fonts/maps/planets during import.
Test-Transactions.py exercises actual transactional install/migration code.
The owner device was unavailable for integrated 1.0; desktop/Android emulator
coverage and residual limits are recorded in the release inventory.
