# Retired synthetic crop diagnostic; retained as an explanatory entrypoint.
param(
    [string]$AndroidJar,
    [string]$BuildTools
)
throw 'ScreenCheck is retired: its crop-controller API was replaced by the independent native-state map. See diagnostics/README.md and docs/HANDOFF.md. No diagnostic APK was built.'
