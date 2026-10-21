"""Compile actual production ordering/shelf/spring code and exercise carousel regressions."""
from pathlib import Path
import re
import subprocess
import textwrap

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/launcher-spines/order-regression'
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe')
SOURCE = ROOT / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/ThorLauncherView.kt'


def block(source, marker):
    start = source.index(marker)
    begin = source.index('{', start)
    depth = 1
    for end in range(begin + 1, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if not depth:
                return source[start:end + 1]
    raise AssertionError(f'Unclosed production block: {marker}')


def jar(group, name, version):
    return next((CACHE / group / name / version).rglob('*.jar'))


def main():
    source = SOURCE.read_text(encoding='utf-8')
    ordering = block(source, 'internal object ThorCasePaintOrder')
    spring = block(source, 'private class Spring').replace('private class Spring', 'class Spring', 1)
    shelf = block(source, 'private fun shelfZ').replace('private fun shelfZ', 'fun shelfZ', 1)
    stack_z = re.search(r'private val STACK_Z = ([\d.]+f)', source).group(1)
    # Guard the production wiring as well as exercising the actual ordering implementation.
    drawing = block(source, 'private fun drawLibrary')
    assert drawing.count('val caseOrder = ThorCasePaintOrder.indices(') == 1
    assert drawing.count('for (i in caseOrder)') == 2
    assert 'sortedByDescending' not in drawing
    OUT.mkdir(parents=True, exist_ok=True)
    fixture = OUT / 'Test.kt'
    fixture.write_text('import kotlin.math.*\n' + ordering + '\n' + spring + '\n' + shelf + '\n' +
                      f'const val STACK_Z = {stack_z}\n' + textwrap.dedent('''
        private val bundles = listOf(listOf(0, 1), listOf(2, 3), listOf(4, 5))
        private var checks = 0
        private fun verify(value: Boolean, message: String) { check(value) { message }; checks++ }
        private fun depths(carousel: Float, focus: FloatArray) = FloatArray(6) { i ->
            shelfZ(i / 2 - carousel, focus[i / 2]) + STACK_Z * (i % 2)
        }
        private fun verifyFrame(z: FloatArray) {
            val order = ThorCasePaintOrder.indices(bundles, z, true)
            verify(order.sorted() == (0..5).toList(), "Every case drawn exactly once")
            bundles.forEach { members ->
                val positions = members.map(order::indexOf)
                verify(positions.max() - positions.min() == members.size - 1, "Bundle interleaves: $order")
                verify(order.indexOf(members[1]) < order.indexOf(members[0]), "Mod covers own base: $order")
            }
            // Exercise every possible overlap subset; reverse hit testing must choose the last
            // painted visible case's bundle. This is the same list used by the real bounds loop.
            for (mask in 1 until (1 shl 6)) {
                val painted = order.filter { mask and (1 shl it) != 0 }
                val hit = order.asReversed().first { mask and (1 shl it) != 0 } / 2
                verify(hit == painted.last() / 2, "Hit target disagrees with visible bundle")
            }
        }
        fun main() {
            val tie = depths(0f, floatArrayOf(1f, 0f, 0f))
            verify(tie[3] == 390f && tie[4] == 390f, "Reproduce actual MSMBWii/Sunshine tie")
            val legacy = tie.indices.sortedByDescending { tie[it] }
            verify(legacy.indexOf(3) < legacy.indexOf(4), "Old ordering must expose the reported overlap")
            val fixed = ThorCasePaintOrder.indices(bundles, tie, true)
            verify(fixed.indexOf(4) < fixed.indexOf(3), "Sunshine must stay behind NSMB stack")
            verifyFrame(tie)
            // The focus spring can cross zero while the carousel settles, inverting those
            // depths. Both sides of the crossover must retain the whole stack's order.
            for (f in floatArrayOf(-0.002f, 0f, 0.002f)) {
                val z = depths(0f, floatArrayOf(1f, f, 0f))
                verifyFrame(z)
                verify(ThorCasePaintOrder.indices(bundles, z, true).indexOf(4) <
                       ThorCasePaintOrder.indices(bundles, z, true).indexOf(3), "Crossover changed bundle priority")
            }
            // Run the real shelf and integration springs through every adjacent direction,
            // including negative and positive overshoot, at representative refresh rates.
            for ((from, to) in listOf(0 to 1, 1 to 0, 1 to 2, 2 to 1)) {
                for (hz in listOf(30, 60, 120)) {
                    val carousel = Spring(from.toFloat(), 110f, 0.9f).apply { target = to.toFloat() }
                    val focuses = Array(3) { g -> Spring(if (g == from) 1f else 0f, 150f, 0.82f)
                        .apply { target = if (g == to) 1f else 0f } }
                    repeat(hz * 3) {
                        carousel.step(1f / hz); focuses.forEach { it.step(1f / hz) }
                        verifyFrame(depths(carousel.value, FloatArray(3) { focuses[it].value }))
                    }
                }
            }
            for ((expanded, fan, opened) in listOf(Triple(0, 0f, -1), Triple(-1, 0.01f, -1),
                                                  Triple(-1, -0.01f, -1), Triple(-1, 0f, 3))) {
                verify(!ThorCasePaintOrder.closedShelf(expanded, fan, opened), "Fan/open must retain per-case sorting")
                verify(ThorCasePaintOrder.indices(bundles, tie, false) == legacy, "Fan/open painter order changed")
            }
            verify(ThorCasePaintOrder.closedShelf(-1, 0f, -1), "Closed shelf must group bundles")
            verify(ThorCasePaintOrder.closedShelf(-1, -0.0005f, -1), "Settling epsilon must be symmetric")
            println("$checks actual-source carousel ordering checks passed")
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
