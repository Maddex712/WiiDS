"""Compile and execute the actual launcher touch handler against menu/carousel gestures."""
from pathlib import Path
import subprocess
import textwrap

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/release-audit/launcher-touch-fixtures'
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe')
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'


def jar(group, name, version):
    return next((CACHE / group / name / version).rglob('*.jar'))


def actual_handler(source):
    start = source.index('override fun onTouchEvent(event: MotionEvent): Boolean {')
    begin = source.index('{', start)
    depth = 1
    for end in range(begin + 1, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if not depth:
                return source[start:end + 1].replace('override fun', 'fun', 1)
    raise AssertionError('Unclosed production touch method')


def main():
    source = ROOT / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/ThorLauncherView.kt'
    handler = actual_handler(source.read_text(encoding='utf-8'))
    OUT.mkdir(parents=True, exist_ok=True)
    fixture = OUT / 'Test.kt'
    fixture.write_text(textwrap.dedent('''
        private data class MotionEvent(val actionMasked: Int, val x: Float, val y: Float) {
            companion object { const val ACTION_DOWN = 0; const val ACTION_UP = 1; const val ACTION_CANCEL = 3 }
        }
        private data class RectF(val left: Float, val top: Float, val right: Float, val bottom: Float) {
            fun contains(x: Float, y: Float) = x >= left && x < right && y >= top && y < bottom
        }
        private class Launcher {
            enum class State { BROWSE, OPEN, EXTRACT }
            var scale = 1f; var offsetX = 0f; var offsetY = 0f
            val caseCount = 6
            var options = true; var musicPanel = false; var count = 9
            var selection = 0; var state = State.BROWSE
            var downTarget = -1; var downWasFront = true
            val bounds = ArrayList<Pair<RectF, Int>>()
            val activated = ArrayList<Int>()
            var discs = 0; var clicks = 0; var muteToggles = 0
            fun optionEntries() = (0 until count).toList()
            fun select(index: Int) { selection = index }
            fun activate() { if (options && musicPanel) muteToggles++ else activated.add(selection) }
            fun chooseDisc() { discs++ }
            fun performClick(): Boolean { clicks++; return true }
            fun rows(n: Int) {
                bounds.clear()
                repeat(n) { i -> bounds.add(RectF(0f, i * 20f, 100f, i * 20f + 18f) to i) }
            }
            fun event(action: Int, row: Int) = onTouchEvent(MotionEvent(action,
                offsetX + 10f * scale, offsetY + (row * 20f + 5f) * scale))
            fun tap(row: Int) { event(MotionEvent.ACTION_DOWN, row); event(MotionEvent.ACTION_UP, row) }
    ''') + handler + textwrap.dedent('''
        }
        private var checks = 0
        private fun verify(value: Boolean, message: String) { check(value) { message }; checks++ }
        fun main() {
            val menu = Launcher()
            menu.rows(9)
            repeat(9) { row ->
                menu.selection = (row + 3) % 9
                menu.tap(row)
                verify(menu.selection == row, "Every Options row selects itself, including Server and Music")
                verify(menu.activated.last() == row, "Touch must activate touched row, not stale highlight")
            }
            menu.count = 13; menu.rows(13)
            repeat(13) { row ->
                menu.selection = 0; menu.tap(row)
                verify(menu.activated.last() == row, "Future Options indices >=10 are not disc targets")
            }
            verify(menu.discs == 0, "Options never choose a carousel disc")
            val before = menu.activated.size
            menu.event(MotionEvent.ACTION_DOWN, -1); menu.event(MotionEvent.ACTION_UP, -1)
            verify(menu.activated.size == before, "Outside touch does not activate stale focus")
            menu.event(MotionEvent.ACTION_DOWN, 7); menu.event(MotionEvent.ACTION_UP, 8)
            verify(menu.activated.size == before, "Dragging across rows does not activate either")
            menu.event(MotionEvent.ACTION_DOWN, 7); menu.event(MotionEvent.ACTION_UP, -1)
            verify(menu.activated.size == before && menu.downTarget == -1, "Drag outside releases without activation")
            menu.event(MotionEvent.ACTION_DOWN, -1); menu.event(MotionEvent.ACTION_UP, 8)
            verify(menu.activated.size == before, "Outside-down/inside-up cannot activate")
            menu.event(MotionEvent.ACTION_DOWN, 8); menu.event(MotionEvent.ACTION_CANCEL, 8)
            menu.event(MotionEvent.ACTION_UP, 8)
            verify(menu.activated.size == before, "Canceled gesture cannot activate")
            menu.scale = 0.5f; menu.offsetX = 80f; menu.offsetY = 30f
            menu.tap(8)
            verify(menu.activated.last() == 8, "Letterboxed/scaled coordinates still hit correct row")
            val beforePanel = menu.activated.size
            menu.musicPanel = true
            repeat(13) { menu.tap(it) }
            menu.event(MotionEvent.ACTION_DOWN, -1); menu.event(MotionEvent.ACTION_UP, -1)
            verify(menu.muteToggles == 0, "Touch behind controller-operated music panel cannot toggle mute")
            verify(menu.activated.size == beforePanel && menu.downTarget == -1, "Modal panel blocks all backing row actions")
            menu.musicPanel = false
            menu.event(MotionEvent.ACTION_DOWN, 7)
            menu.musicPanel = true
            menu.event(MotionEvent.ACTION_UP, 7)
            verify(menu.muteToggles == 0 && menu.downTarget == -1, "Panel appearing during gesture cancels stale row target")

            val shelf = Launcher().apply { options = false; rows(7) }
            shelf.selection = 0; shelf.tap(2)
            verify(shelf.selection == 2 && shelf.activated.isEmpty(), "Side case comes forward without opening")
            shelf.tap(2)
            verify(shelf.activated == listOf(2), "Second touch opens front case")
            shelf.tap(6)
            verify(shelf.selection == 6 && shelf.activated.last() == 6, "Carousel Options button still activates")
            shelf.state = Launcher.State.OPEN
            shelf.bounds.clear(); shelf.bounds.add(RectF(0f, 0f, 100f, 18f) to 12)
            shelf.tap(0)
            verify(shelf.discs == 1, "Encoded open-case disc still selects disc")
            shelf.state = Launcher.State.BROWSE; shelf.tap(0)
            verify(shelf.discs == 1, "Closed case cannot select encoded disc")
            shelf.state = Launcher.State.EXTRACT; shelf.tap(0)
            verify(shelf.discs == 1, "Extraction cannot double-select disc")
            println("$checks actual-source launcher touch checks passed")
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
