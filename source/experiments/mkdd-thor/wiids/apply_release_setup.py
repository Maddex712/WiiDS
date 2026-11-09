"""Add portable Double Dash/Plus setup to the verified integrated private Android tree.

Only source files change. No binaries, disc data, APK build/install or publication.
"""
from pathlib import Path
import argparse
import shutil

HERE = Path(__file__).resolve().parent


def replace(path, before, after):
    text = path.read_text(encoding='utf-8')
    if after in text:
        return
    if text.count(before) != 1:
        raise ValueError(f'{path}: expected exactly one setup anchor: {before[:80]}')
    path.write_text(text.replace(before, after), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('android', type=Path)
    a = parser.parse_args()
    main = a.android / 'app/src/main'
    kotlin = main / 'kotlin/com/driftdroid/android'
    rom = kotlin / 'rom'
    for name in ['MkddArt', 'MkddOverview', 'MkddVcdiff', 'DuoArt', 'MkddGameActivity']:
        shutil.copyfile(HERE.parent / f'android/dev/thor/mkdd/{name}.java', main / f'java/dev/thor/mkdd/{name}.java')
    shutil.copyfile(HERE / 'dev/thor/mkdd/MkddProfiles.java', main / 'java/dev/thor/mkdd/MkddProfiles.java')
    shutil.copyfile(HERE / 'MkddPlusMod.kt', rom / 'MkddPlusMod.kt')
    replace(rom / 'GameInstaller.kt', 'val running get() = state.running',
            'val running get() = state.running || MkddPlusMod.state.running')
    replace(rom / 'GameInstaller.kt', '|| state.running || EclipseMod.state.running ||',
            '|| running || EclipseMod.state.running ||')
    replace(rom / 'GameInstaller.kt', 'if (state.running || com.driftdroid.android.GameStorage.blocker(app) != null)',
            'if (running || com.driftdroid.android.GameStorage.blocker(app) != null)')
    replace(rom / 'GameInstaller.kt', 'val app = context.applicationContext\n        Thread {\n            for (disc',
            'val app = context.applicationContext\n        Thread {\n            if (runCatching { com.driftdroid.android.SaveManagerProfiles.requireStopped(app) }.isFailure) return@Thread\n            for (disc')
    replace(rom / 'WiiDsArt.kt',
            '        if (com.driftdroid.android.GameStorage.blocker(context) != null) return "Manage games is busy."\n'
            '        building = disc',
            '        synchronized(GameInstaller) {\n'
            '            if (com.driftdroid.android.SaveManagerProfiles.blocker(context) != null) return "Game setup is busy."\n'
            '            if (runCatching { com.driftdroid.android.SaveManagerProfiles.requireStopped(context) }.isFailure) return "Close the running game before building art."\n'
            '            building = disc\n        }')
    replace(rom / 'WiiDsArt.kt',
            '                if (com.driftdroid.android.GameStorage.blocker(app) != null) return@Thread\n'
            '                building = GameCatalog.Disc.MARIO_KART_PAL',
            '                synchronized(GameInstaller) {\n'
            '                    if (com.driftdroid.android.SaveManagerProfiles.blocker(app) != null ||\n'
            '                        runCatching { com.driftdroid.android.SaveManagerProfiles.requireStopped(app) }.isFailure) return@Thread\n'
            '                    building = GameCatalog.Disc.MARIO_KART_PAL\n                }')
    replace(kotlin / 'SaveManagerProfiles.kt', 'busy.get() -> "Wait for the save operation to finish."',
            'busy.get() -> "Wait for the save operation to finish."\n'
            '        com.driftdroid.android.rom.GameInstaller.running || com.driftdroid.android.rom.EclipseMod.state.running ||\n'
            '            com.driftdroid.android.rom.MsmbwiiMod.state.running -> "Finish or cancel game setup first."\n'
            '        com.driftdroid.android.rom.WiiDsArt.building != null -> "Wait for the game artwork to finish."')
    replace(kotlin / 'GameStorage.kt', 'if (SaveManagerProfiles.busy.get() || WiiDsUpdater.installing.get() ||',
            'if (WiiDsArt.building != null || SaveManagerProfiles.busy.get() || WiiDsUpdater.installing.get() ||')
    replace(rom / 'GameInstaller.kt',
            '            // Before Cancel is shown: a tap from here on must reach the extraction.',
            '            if (com.driftdroid.android.SaveManagerProfiles.blocker(context) != null ||\n'
            '                runCatching { com.driftdroid.android.SaveManagerProfiles.requireStopped(context) }.isFailure) return false\n'
            '            // Before Cancel is shown: a tap from here on must reach the extraction.')
    replace(kotlin / 'SaveManagerActivity.kt',
            '        if (!SaveManagerProfiles.busy.compareAndSet(false, true)) return',
            '        synchronized(com.driftdroid.android.rom.GameInstaller) {\n'
            '            if (com.driftdroid.android.rom.GameInstaller.running ||\n'
            '                com.driftdroid.android.rom.EclipseMod.state.running ||\n'
            '                com.driftdroid.android.rom.MsmbwiiMod.state.running ||\n'
            '                com.driftdroid.android.rom.WiiDsArt.building != null || WiiDsUpdater.installing.get()) {\n'
            '                status.text = "Finish or cancel game setup before managing saves."\n'
            '                return\n            }\n'
            '            if (!SaveManagerProfiles.busy.compareAndSet(false, true)) return\n        }')
    for installer in ('EclipseMod.kt', 'MsmbwiiMod.kt'):
        replace(rom / installer, '            state = State(running = true',
                '            if (com.driftdroid.android.SaveManagerProfiles.blocker(context) != null ||\n'
                '                runCatching { com.driftdroid.android.SaveManagerProfiles.requireStopped(context) }.isFailure) return false\n'
                '            state = State(running = true')
    games = kotlin / 'GamesActivity.kt'
    replace(games, 'import com.driftdroid.android.rom.EclipseMod',
            'import com.driftdroid.android.rom.EclipseMod\nimport com.driftdroid.android.rom.MkddPlusMod')
    replace(games, 'private var eclipseMessage: String? = null',
            'private var eclipseMessage: String? = null\n    private var plusMessage: String? = null')
    replace(games, '            refresh()\n            handler.postDelayed(this, 300)',
            '            if (MkddPlusMod.state.finished) {\n'
            '                plusMessage = MkddPlusMod.state.error ?: ""\n'
            '                MkddPlusMod.acknowledge()\n'
            '            }\n            refresh()\n            handler.postDelayed(this, 300)')
    replace(games, '        if (requestCode == REQUEST_PICK_ECLIPSE_PATCH) {',
            '        if (requestCode == REQUEST_PICK_PLUS_PATCH) {\n'
            '            if (resultCode == RESULT_OK && data?.data != null) {\n'
            '                if (!MkddPlusMod.start(this, data.data!!)) plusMessage = "Close the running game and finish other setup or save operations first."\n'
            '                else plusMessage = null\n'
            '                refresh()\n'
            '            }\n            return\n        }\n'
            '        if (requestCode == REQUEST_PICK_ECLIPSE_PATCH) {')
    replace(games, 'val installBusy = state.running || eclipseBusy',
            'val installBusy = GameInstaller.running || eclipseBusy')
    replace(games, '            when {\n                installed && (plus ||',
            '            card.progress.visibility = View.GONE\n'
            '            when {\n'
            '                plus && MkddPlusMod.state.running -> {\n'
            '                    val mod = MkddPlusMod.state\n'
            '                    show(card, mod.phase, WHITE)\n'
            '                    card.progress.visibility = View.VISIBLE\n'
            '                    card.progress.progress = (mod.done * 100 / mod.total.coerceAtLeast(1)).toInt().coerceIn(0,100)\n'
            '                    button(card, "Cancel", true) { MkddPlusMod.cancel() }\n'
            '                }\n                installed && (plus ||')
    replace(games, '                    show(card, "This preview uses a separate Plus disc prepared on a PC.", MUTED)\n'
                   '                    button(card, "PC setup required", false) {}',
            '                    val originalReady = GameStorage.ready(this, FrontendGame.MKDD)\n'
            '                    show(card, plusMessage?.takeIf { it.isNotEmpty() } ?: if (originalReady)\n'
            '                        "Choose the creator’s MKDD Plus 3.3 NTSC xdelta patch. WiiDS builds all five course sets here. Original Double Dash and its memory card are kept."\n'
            '                        else "Import your USA Double Dash revision 0 disc first (GM4E01). Plus keeps its own game image and memory card.", MUTED)\n'
            '                    button(card, if (originalReady) "Choose Plus 3.3 patch" else "Double Dash required", originalReady && !installBusy) {\n'
            '                        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")\n'
            '                        runCatching { startActivityForResult(intent, REQUEST_PICK_PLUS_PATCH) }\n'
            '                            .onFailure { plusMessage = "This device has no file chooser."; refresh() }\n'
            '                    }')
    replace(games, 'private const val REQUEST_PICK = 4710',
            'private const val REQUEST_PICK_PLUS_PATCH = 4714\n        private const val REQUEST_PICK = 4710')
    replace(games, '        separateSetupCards[game] = Card(status, ProgressBar(this@GamesActivity), action)',
            '        val progress = ProgressBar(this@GamesActivity, null, android.R.attr.progressBarStyleHorizontal).apply {\n'
            '            max = 100; visibility = View.GONE\n        }\n'
            '        addView(progress, LinearLayout.LayoutParams(-1, dp(10)).apply { topMargin = dp(8) })\n'
            '        separateSetupCards[game] = Card(status, progress, action)\n'
            '        if (game == FrontendGame.MKDD_PLUS) addView(styledButton("Plus patch and instructions") {\n'
            '            runCatching { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://retroachievements.org/forums/topic/27182"))) }\n'
            '        }, LinearLayout.LayoutParams(-2,-2).apply { topMargin = dp(8) })')
    print('Portable MKDD/Plus setup overlay applied')


if __name__ == '__main__':
    main()
