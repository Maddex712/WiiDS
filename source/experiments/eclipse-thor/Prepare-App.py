"""Prepare a separate Eclipse host with Claude's Sunshine companion design."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'reference/sms-launcher-thor'
TARGET = ROOT / 'reference/eclipse-launcher-thor'


def replace(path, before, after):
    text = path.read_text(encoding='utf-8')
    if before not in text:
        raise ValueError(f'Expected source changed: {path}: {before[:65]}')
    path.write_text(text.replace(before, after), encoding='utf-8', newline='\n')



def adapt_companion(path):
    """Eclipse-only phase/area compatibility; preserve the original drawing helpers."""
    # Since 2026-10-20 the shared companion handles Eclipse itself (package-name theme, stages
    # 61+, BSE/Eclipse contexts): it is used unchanged.
    if 'eclipse = activity.getPackageName().contains("eclipse")' in path.read_text(encoding='utf-8'):
        return
    replace(path,
            'private static final int APP_GAMEPLAY = 5, APP_TITLE = 8, APP_MENU = 9;',
            'private static final int APP_GAMEPLAY = 5, APP_TITLE = 8, APP_MENU = 9;\n'
            '    // BSE application.hxx / Eclipse menu/character_select.hxx, at the pinned mod revisions.\n'
            '    private static final int APP_SETTINGS = 10, APP_CHARACTER_SELECT = 11;')
    replace(path, '        static Area of(String scene) {', '''        static Area from(Snapshot state) {
            if (state == null) return null;
            // Eclipse stage.hxx adds stages 61-92. They must never borrow a retail postcard/plate.
            if (state.stage < 61) {
                Area retail = of(state.scene);
                if (retail != null) return retail;
            }
            if ((state.scene.isEmpty() && state.areaName.isEmpty()) || "option".equals(state.scene)) return null;
            String name = state.areaName.isEmpty() ? state.scene.replace('_', ' ') : state.areaName;
            return new Area(null, name, false, false);
        }
        static Area of(String scene) {''')
    replace(path, 'Area area = s == null ? null : Area.of(s.scene);', 'Area area = Area.from(s);')
    replace(path, 'int stage = s.shineStage >= 0 ? s.shineStage : -1;',
            'int stage = s.stage < 61 && s.shineStage >= 0 ? s.shineStage : -1;')
    replace(path, 'boolean slots = s.tracked();', 'boolean slots = s.stage < 61 && s.tracked();')
    replace(path, 'boolean sky = playing || state == APP_TITLE || state == APP_MENU;',
            'boolean eclipseMenu = state == APP_SETTINGS || state == APP_CHARACTER_SELECT;\n'
            '            boolean sky = playing || state == APP_TITLE || state == APP_MENU || eclipseMenu;')
    replace(path, '                } else if (fileSelect) {',
            '                } else if (eclipseMenu) {\n'
            '                    eclipseMenu(c, state);\n'
            '                } else if (fileSelect) {')
    replace(path, '        private void title(Canvas c, float t) {', '''        /** Same Sunshine backdrop and message-window styling for Eclipse's additional contexts. */
        private void eclipseMenu(Canvas c, int state) {
            image(c, "game_6-shine_icon", W / 2, 270, 140);
            window(c, 160, 390, W - 160, 650);
            label(c, "Super Mario Eclipse", W / 2, 490, 64, 0xffffffff, Paint.Align.CENTER, W - 400);
            label(c, state == APP_SETTINGS ? "Game settings" : "Character select",
                W / 2, 590, 46, 0xffffe27a, Paint.Align.CENTER, W - 400);
        }

        private void title(Canvas c, float t) {''')
    replace(path, '''            if (cache.containsKey(key)) return cache.get(key);
            Bitmap b = build.get();''', '''            if (cache.containsKey(key)) return cache.get(key);
            long began = BuildConfig.DEBUG ? SystemClock.elapsedRealtimeNanos() : 0;
            if (BuildConfig.DEBUG) Log.i("EclipseUiWatch", "cache build begin key=" + key);
            Bitmap b = build.get();
            if (BuildConfig.DEBUG) Log.i("EclipseUiWatch", "cache build end key=" + key
                + " duration_ms=" + ((SystemClock.elapsedRealtimeNanos() - began) / 1000000.0)
                + " bitmap=" + (b == null ? "none" : b.getWidth() + "x" + b.getHeight()));''')
    # A presentation-only renderer comparison; the top SDL/GLES path stays untouched.
    replace(path, '        Screen(Display display) { super(activity, display); view = new Board(getContext()); }', '''        Screen(Display display) {
            super(activity, display);
            view = new Board(getContext());
            // Eclipse-only comparison: rasterize the unchanged lower artwork in software.
            // The top SDL window and its native GLES renderer are unchanged.
            view.setLayerType(View.LAYER_TYPE_SOFTWARE, null);
            Window lower = getWindow();
            if (lower != null) lower.clearFlags(WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED);
        }''')
    replace(path, '        private int cachedArt = -1;',
            '        private int cachedArt = -1;\n        private boolean rendererReported;')
    replace(path, '        @Override protected void onDraw(Canvas c) {', '''        @Override protected void onDraw(Canvas c) {
            if (BuildConfig.DEBUG && !rendererReported) {
                rendererReported = true;
                Log.i("EclipseUiWatch", "lower first draw canvasHardware=" + c.isHardwareAccelerated()
                    + " windowHardware=" + isHardwareAccelerated() + " layerType=" + getLayerType());
            }''')


def main():
    if TARGET.exists():
        raise ValueError('Prepared host exists; review it instead of overwriting local edits')
    TARGET.mkdir()
    for name in ('package.json', 'changelog.json', 'LICENSE'):
        p = SOURCE / name
        if p.is_file(): shutil.copy2(p, TARGET / name)
    android = TARGET / 'android'
    android.mkdir()
    for p in (SOURCE / 'android').iterdir():
        if p.is_file() and p.name != 'local.properties': shutil.copy2(p, android / p.name)
    shutil.copytree(SOURCE / 'android/gradle', android / 'gradle')
    shutil.copytree(SOURCE / 'android/app/src', android / 'app/src')
    shutil.copy2(SOURCE / 'android/app/build.gradle', android / 'app/build.gradle')
    gradle = android / 'app/build.gradle'
    replace(gradle, "applicationId 'com.wither.sms.thor'", "applicationId 'com.wither.sms.eclipse.thor'")
    replace(gradle, '../../../../build/sunshine-thor/jniLibs', '../../../../build/eclipse-thor/jniLibs')
    replace(gradle, '        debug {}', '''        debug {
            signingConfig wiidsKeystore != null ? signingConfigs.wiids : signingConfigs.debug
        }''')
    java = android / 'app/src/main/java'
    manifest = android / 'app/src/main/AndroidManifest.xml'
    replace(manifest, '<queries><package android:name="com.driftdroid.android.thor" /></queries>',
            '<queries><package android:name="com.driftdroid.android.thor" /><package android:name="io.github.maddex712.wiids" /></queries>')
    replace(manifest, 'Super Mario Sunshine - Thor', 'Super Mario Eclipse - Thor')
    entry = java / 'com/wither/sms/ThorLaunchActivity.java'
    replace(entry, '!"sunshine".equals', '!"eclipse".equals')
    replace(entry, 'Sunshine data is not ready', 'Eclipse data is not ready')
    replace(entry, '        try {\n            ThorStorage.validateDisc(this);',
            '        ThorStorage.setLibrary(this, request.getStringExtra("thor_launcher_package"));\n        try {\n            ThorStorage.validateDisc(this);')
    storage = java / 'dev/chasem/smslauncher/ThorStorage.java'
    replace(storage, '"GMSE01"', '"GMSE04"')
    replace(storage, 'Import the verified US revision 0 Sunshine disc first.', 'Import the verified Super Mario Eclipse 1.1.0 disc first.')
    replace(storage, 'Sunshine requires a US revision 0 GameCube disc.', 'Eclipse requires its patched GMSE04 disc.')
    replace(storage, 'Sunshine storage is unavailable.', 'Eclipse storage is unavailable.')
    replace(storage, '    public static boolean returnToLibrary(Context context) {', '''    public static void setLibrary(Context context, String name) {
        if (!"io.github.maddex712.wiids".equals(name) && !"com.driftdroid.android.thor".equals(name))
            name = "io.github.maddex712.wiids";
        context.getSharedPreferences("transport", Context.MODE_PRIVATE).edit().putString("library", name).apply();
    }
    public static boolean returnToLibrary(Context context) {''')
    replace(storage, '"com.driftdroid.android.thor", "com.driftdroid.android.ModePickerActivity"',
            'context.getSharedPreferences("transport", Context.MODE_PRIVATE).getString("library", "io.github.maddex712.wiids"), "com.driftdroid.android.ModePickerActivity"')
    launcher = java / 'dev/chasem/smslauncher/LauncherActivity.java'
    replace(launcher, 'Intent launch = new Intent(this, GameActivity.class);',
            'Intent launch = new Intent(this, GameActivity.class).putExtra("thor_library", true);')
    replace(launcher, 'Super Mario Sunshine', 'Super Mario Eclipse')
    replace(launcher, "Import a US revision 0 disc image, then play", "Import your patched Eclipse 1.1.0 disc image, then play")
    replace(launcher, 'sunshine-save.zip', 'eclipse-save.zip')
    replace(launcher, 'sunshine-game.log', 'eclipse-game.log')
    replace(launcher, '            ThorStorage.validateDisc(temp);', '''            ThorStorage.validateDisc(temp);
            try {
                java.security.MessageDigest digest = java.security.MessageDigest.getInstance("MD5");
                try (InputStream checked = new FileInputStream(temp)) {
                    byte[] buffer = new byte[1048576]; int n;
                    while ((n = checked.read(buffer)) != -1) digest.update(buffer, 0, n);
                }
                StringBuilder actual = new StringBuilder();
                for (byte b : digest.digest()) actual.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
                if (!"caa546309e0443f7b47632b040a250d7".equals(actual.toString()))
                    throw new IOException("This is not the verified Eclipse 1.1.0 disc.");
            } catch (java.security.NoSuchAlgorithmException impossible) { throw new IOException(impossible); }''')
    replace(launcher, '!new File(staged, "super_mario_sunshine.dat").isFile()', '!new File(staged, "GMSE04_super_mario_sunshine.dat").isFile()')
    replace(launcher, '!new File(staged, "super_mario_sunshine.stat").isFile()', '!new File(staged, "GMSE04_super_mario_sunshine.stat").isFile()')
    replace(launcher, 'Backup is missing Sunshine card files.', 'Backup is missing Eclipse card files.')
    hud = 'android/app/src/main/java/dev/chasem/smslauncher/ThorSunshineScreen.java'
    original = hashlib.sha256((SOURCE / hud).read_bytes()).hexdigest()
    assert hashlib.sha256((TARGET / hud).read_bytes()).hexdigest() == original
    adapt_companion(TARGET / hud)
    adapted = hashlib.sha256((TARGET / hud).read_bytes()).hexdigest()
    out = ROOT / 'build/eclipse-thor'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'host-preparation.json').write_text(json.dumps({
        'package': 'com.wither.sms.eclipse.thor', 'source': str(SOURCE),
        'host': str(TARGET), 'companion_source_sha256': original, 'companion_sha256': adapted,
        'companion_copied_verbatim': False, 'companion_design_preserved': True,
        'companion_eclipse_compatibility': ['settings-context-10', 'character-select-context-11', 'custom-scene-fallback-no-retail-art'], 'regular_sunshine_modified': False,
        'owner_disc_or_saves_copied': False,
    }, indent=2) + '\n')
    print(TARGET)


if __name__ == '__main__': main()
