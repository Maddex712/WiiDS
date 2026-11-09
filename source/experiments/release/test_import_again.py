"""Execute the actual installed-card branch: recovery must let the owner choose a new disc."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TASK = ROOT / 'build/discord-bugfix-2026-10-26/rom-import'
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe')
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'


def jar(group, name, version):
    return next((CACHE / group / name / version).rglob('*.jar'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=ROOT / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/GamesActivity.kt')
    parser.add_argument('--label', default='ui-fixed')
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    out = TASK / args.label
    out.mkdir(parents=True, exist_ok=True)
    source = args.source.read_text(encoding='utf-8')
    begin = source.index('                disc.isInstalled(this) -> {')
    end = source.index('                found.isNotEmpty() -> {', begin)
    branch = source[begin:end]
    fixture = '''data class Found(val source: String)
class Disc { fun isInstalled(context: Any) = true }
class Card { var label = ""; var enabled = false; var action: () -> Unit = {}; var status = "" }
object GameInstaller { var starts = 0; fun start(context: Any, disc: Disc, source: String): Boolean { starts++; return true } }
const val GREEN = 1
const val BUSY = "busy"
class Screen(val game: String, val found: List<Found>, val installBusy: Boolean) {
    val card = Card(); val disc = Disc(); val message: String? = null
    val messages = mutableMapOf<Disc, String>(); var picked: String? = null
    fun chooseFile(game: String) { picked = game }
    fun show(card: Card, text: String, color: Int) { card.status = text }
    fun button(card: Card, label: String, enabled: Boolean, action: () -> Unit) {
        card.label = label; card.enabled = enabled; card.action = action
    }
    fun render() { when {
''' + branch + '''    } }
}
fun main() {
    var checks = 0; var failures = 0
    fun verify(ok: Boolean, text: String) { checks++; if (!ok) { failures++; println("FAIL $text") } }
    for (game in listOf("sunshine", "mario-kart", "nsmbw", "eclipse")) {
        for (found in listOf(emptyList(), listOf(Found("previous-bad.rvz")), listOf(Found("previous-bad.rvz"), Found("replacement.iso")))) {
            for (busy in listOf(false, true)) {
                val screen = Screen(game, found, busy)
                GameInstaller.starts = 0
                screen.render()
                verify(screen.card.label == "Import again", "$game keeps existing action")
                verify(screen.card.enabled == !busy, "$game retry available without a scanner match, busy=$busy")
                if (screen.card.enabled) screen.card.action()
                verify(screen.picked == (if (busy) null else game), "$game retry chooses the replacement, busy=$busy")
                verify(GameInstaller.starts == 0, "$game must not automatically retry the first old source")
            }
        }
    }
    println("checks=$checks failures=$failures")
    if (failures > 0) throw IllegalStateException("$failures failures")
}
'''
    (out / 'Test.kt').write_text(fixture)
    kotlin = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
    compiler = [jar('org.jetbrains.kotlin', n, '1.9.24') for n in ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
    compiler += [kotlin, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'), jar('org.jetbrains', 'annotations', '13.0')]
    compiled = subprocess.run([str(JAVA), '-cp', ';'.join(map(str, compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
        '-no-stdlib', '-no-reflect', '-jvm-target', '17', '-classpath', str(kotlin), '-d', str(out / 'classes'), str(out / 'Test.kt')], capture_output=True, text=True)
    (out / 'compile.log').write_text(compiled.stdout + compiled.stderr)
    assert compiled.returncode == 0, compiled.stderr
    run = subprocess.run([str(JAVA), '-cp', f'{out / "classes"};{kotlin}', 'TestKt'], capture_output=True, text=True)
    (out / 'run.log').write_text(run.stdout + run.stderr)
    summary = next(line for line in run.stdout.splitlines() if line.startswith('checks='))
    values = dict(field.split('=') for field in summary.split())
    receipt = {'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
        'checks': int(values['checks']), 'failures': int(values['failures']),
        'status': 'EXPECTED_BASELINE_FAILURES' if args.baseline and run.returncode else 'PASS' if not run.returncode else 'FAIL'}
    (out / 'verification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))
    if run.returncode and not args.baseline:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
