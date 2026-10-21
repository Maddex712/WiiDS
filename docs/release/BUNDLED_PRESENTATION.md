# Bundled WiiDS presentation

The owner's approved current presentation assets are curated into
`assets/presentation/`, separately from disc imports and game profiles. The
manifest is `schema: 1`, `kind: wiids-bundled-presentation`, with a stable
`revision` and a `files` array. Each entry declares:

```json
{
  "asset": "presentation/files/launcher/original-cover.png",
  "target": "launcher/original-cover.png",
  "sha256": "64 lowercase hex digits",
  "bytes": 12345
}
```

The Android loader accepts only seven target groups:

| Group | Destination |
| --- | --- |
| `launcher` | Private completed bundle revision, `ThorMap/launcher` |
| `launcher-audio` | Private completed bundle revision, `ThorMap/launcher-audio` |
| `thor-map` | Internal `WiiCompiled/ThorMap`, missing files only |
| `nsmbw-hud` | External app files `nsmbw/ThorHud`, missing files only |
| `msmbwii-hud` | External app files `msmbwii/ThorHud`, missing files only |
| `sms-hud` | External app files `sms/ThorHud`, missing files only |
| `eclipse-hud` | External app files `eclipse/ThorHud`, missing files only |

The loader rejects unsupported roots/extensions, traversal, absolute paths,
backslashes, duplicate paths, symbolic-link chains, oversized entries and bad
checksums. Limits are a 1 MiB manifest, 8192 files, 64 MiB per file and 768 MiB
total. The extension allowlist is PNG/JPG/JPEG, WAV, JSON, TSV and RGBA. These
paths cannot address discs, memory cards, reserve sidecars or game settings.

Files are copied to temporary paths, hashed and flushed before atomic rename.
Case/music bundles become active only after their complete revision is ready.
Existing external `custom/launcher` and `custom/launcher-audio` manifests keep
priority; otherwise the packaged sets take priority over generated defaults.
Existing HUD/menu/map files are preserved, including user modifications. Missing
ones are restored on the next bootstrap. An incomplete packaged revision is
reconstructed from its APK payload.

`ModePickerActivity` runs bootstrap on a worker before constructing the library,
with a visible progress message and a lifecycle guard. It does not take device
input, import discs or start games as part of asset installation.

Verification: `test_bundled_presentation.py` compiles the actual Kotlin loader
and actual launcher-folder precedence component with small Android filesystem
stubs. It checks the entire staged payload and negative/preservation fixtures.
This is host filesystem evidence, not an Android device launch claim.
