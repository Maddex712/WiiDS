"""Execute actual Eclipse/MSMBWii card branches through cancel/error/retry flows."""
from pathlib import Path
import subprocess
import textwrap

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/release-audit/eclipse-retry-fixtures'
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe')
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'


def jar(group, name, version):
    return next((CACHE / group / name / version).rglob('*.jar'))


def card_body(source, marker):
    start = source.index(marker) + len(marker)
    depth = 1
    for end in range(start, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if not depth:
                return source[start:end]
    raise AssertionError('Unclosed production mod card branch')


def main():
    source = ROOT / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/GamesActivity.kt'
    text = source.read_text(encoding='utf-8')
    body = card_body(text, 'game == Game.ECLIPSE && !disc.isInstalled(this) && GameCatalog.Disc.SUNSHINE_USA.isInstalled(this) -> {')
    msmb = card_body(text, 'disc.isInstalled(this) && game == Game.MSMBWII && !GameCatalog.msmbwiiModInstalled(this) -> {')
    OUT.mkdir(parents=True, exist_ok=True)
    fixture = OUT / 'Test.kt'
    fixture.write_text(textwrap.dedent('''
        private const val GREEN = 1
        private const val AMBER = 2
        private const val BUSY = "Another disc is being imported. Wait for it to finish."
        private class Card {
            var status = ""; var color = 0; var label = ""; var enabled = false
            var click: () -> Unit = {}
            fun activate() { if (enabled) click() }
        }
        private object EclipseMod {
            var bundledPatch = true; var accept = true; var starts = 0
            fun bundled(context: Any) = bundledPatch
            fun start(context: Any): Boolean { starts++; return accept }
        }
        private object MsmbwiiMod {
            const val SOURCE_NAME = "official.example"
            var bundledPatch = true; var accept = true; var starts = 0
            fun bundled(context: Any) = bundledPatch
            fun start(context: Any): Boolean { starts++; return accept }
        }
        private class Screen {
            var eclipseMessage: String? = null
            var modMessage: String? = null
            val card = Card()
            var pickerOpened = false
            var eclipsePicker = false
            fun chooseModFile(eclipse: Boolean = false) { pickerOpened = true; eclipsePicker = eclipse }
            fun show(card: Card, status: String, color: Int) { card.status = status; card.color = color }
            fun button(card: Card, label: String, enabled: Boolean, action: () -> Unit) {
                card.label = label; card.enabled = enabled; card.click = action
            }
            fun render(installBusy: Boolean = false) {
    ''') + body + textwrap.dedent('''
            }
            fun renderMsmb(installBusy: Boolean = false) {
    ''') + msmb + textwrap.dedent('''
            }
        }
        private var checks = 0
        private fun verify(value: Boolean, message: String) { check(value) { message }; checks++ }
        fun main() {
            val screen = Screen()
            screen.render()
            verify(screen.card.label == "Install Eclipse", "Fresh bundled install remains available")
            screen.card.activate()
            verify(EclipseMod.starts == 1 && !screen.pickerOpened, "Fresh install uses bundled patch")

            for (message in listOf("Cancelled.", "Eclipse needs more free space.",
                                   "The selected Eclipse patch couldn't be opened.", BUSY)) {
                screen.eclipseMessage = message
                screen.render()
                verify(screen.card.label == "Retry Eclipse installation", "Failure must preserve bundled retry")
                verify(screen.card.status.startsWith(message) && screen.card.color == AMBER, "Keep actionable error visible")
                val before = EclipseMod.starts
                screen.card.activate()
                verify(EclipseMod.starts == before + 1, "Retry invokes the installer again")
                verify(screen.eclipseMessage == null && !screen.pickerOpened, "Retry clears stale error without opening file picker")
                // A successful or newly running attempt must stop advertising the previous error.
                screen.render()
                verify(screen.card.label == "Install Eclipse" && screen.card.color == GREEN, "Successful retry clears warning state")
            }

            screen.eclipseMessage = "Cancelled."
            screen.render(installBusy = true)
            verify(!screen.card.enabled, "Another import disables retry")
            val beforeBusy = EclipseMod.starts
            screen.card.activate()
            verify(EclipseMod.starts == beforeBusy, "Disabled retry cannot start conflicting installation")
            screen.render()
            EclipseMod.accept = false
            screen.card.activate()
            verify(screen.eclipseMessage == BUSY, "Concurrent start rejection remains visible")
            screen.render()
            verify(screen.card.label == "Retry Eclipse installation", "Busy rejection does not force manual patch selection")

            EclipseMod.bundledPatch = false
            for (message in listOf<String?>(null, "Cancelled.", "Unreadable patch.")) {
                screen.eclipseMessage = message
                screen.pickerOpened = false
                screen.render()
                verify(screen.card.label == "Choose Eclipse patch", "Unbundled builds retain manual patch fallback")
                screen.card.activate()
                verify(screen.pickerOpened && screen.eclipsePicker, "Fallback opens the correct Eclipse picker")
                verify(screen.card.color == if (message == null) GREEN else AMBER, "Fallback preserves warning status")
            }
            screen.pickerOpened = false
            screen.renderMsmb()
            verify(screen.card.label == "Install mod", "Fresh MSMBWii bundled install remains available")
            screen.card.activate()
            verify(MsmbwiiMod.starts == 1 && !screen.pickerOpened, "Fresh MSMBWii install uses bundled mod")
            for (message in listOf("Mod installation cancelled.", "Insufficient space.", "Invalid archive.", BUSY)) {
                screen.modMessage = message
                screen.renderMsmb()
                verify(screen.card.label == "Retry mod installation", "MSMBWii failure retains bundled retry")
                verify(screen.card.status.startsWith(message) && screen.card.color == AMBER, "MSMBWii error remains visible")
                val before = MsmbwiiMod.starts
                screen.card.activate()
                verify(MsmbwiiMod.starts == before + 1, "MSMBWii retry starts installer")
                verify(screen.modMessage == null && !screen.pickerOpened, "MSMBWii retry clears error without external ZIP picker")
                screen.renderMsmb()
                verify(screen.card.label == "Install mod" && screen.card.color == GREEN, "MSMBWii retry clears warning state")
            }
            screen.modMessage = "Cancelled."
            screen.renderMsmb(installBusy = true)
            verify(!screen.card.enabled, "Other import disables MSMBWii retry")
            val beforeModBusy = MsmbwiiMod.starts
            screen.card.activate()
            verify(MsmbwiiMod.starts == beforeModBusy, "Disabled MSMBWii retry does not start")
            screen.renderMsmb()
            MsmbwiiMod.accept = false
            screen.card.activate()
            verify(screen.modMessage == BUSY, "MSMBWii concurrent start rejection remains visible")
            screen.renderMsmb()
            verify(screen.card.label == "Retry mod installation", "MSMBWii busy rejection preserves bundled retry")

            MsmbwiiMod.bundledPatch = false
            MsmbwiiMod.accept = true
            screen.modMessage = null
            screen.renderMsmb()
            verify(screen.card.label == "Download mod", "Unbundled MSMBWii initial setup retains official download")
            screen.card.activate()
            verify(screen.modMessage == null, "Official download starts normally")
            for (message in listOf("Cancelled.", "Unreadable ZIP.")) {
                screen.modMessage = message
                screen.pickerOpened = false
                screen.renderMsmb()
                verify(screen.card.label == "Choose MSMBWii.zip", "Unbundled MSMBWii retains manual fallback after error")
                screen.card.activate()
                verify(screen.pickerOpened && !screen.eclipsePicker, "MSMBWii fallback opens mod ZIP picker")
                verify(screen.card.color == AMBER, "MSMBWii fallback preserves error")
            }
            println("$checks actual-source Eclipse/MSMBWii retry checks passed")
        }
    '''), encoding='utf-8')
    kotlin = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
    compiler = [jar('org.jetbrains.kotlin', name, '1.9.24') for name in
                ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
    compiler += [kotlin, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'), jar('org.jetbrains', 'annotations', '13.0')]
    classes = OUT / 'classes'
    subprocess.run([str(JAVA), '-cp', ';'.join(map(str, compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
                    '-no-stdlib', '-no-reflect', '-jvm-target', '17', '-classpath', str(kotlin),
                    '-d', str(classes), str(fixture)], check=True)
    subprocess.run([str(JAVA), '-cp', f'{classes};{kotlin}', 'TestKt'], check=True)


if __name__ == '__main__':
    main()
