"""Run the actual Kotlin art repair predicates against interrupted/deleted-art fixtures."""
from pathlib import Path
import subprocess
import textwrap

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/release-audit/art-fixtures'
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe')
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'


def jar(group, module, version):
    return next((CACHE / group / module / version).rglob('*.jar'))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    fixtures = {
        'Context.kt': '''package android.content
            import java.io.File
            open class Context {
                val filesDir = File("unused")
                val applicationContext get() = this
                fun getExternalFilesDir(type: String?): File? = File("unused")
            }''',
        'Log.kt': '''package android.util
            object Log { fun w(tag: String, message: String) = 0
                fun w(tag: String, message: String, e: Throwable) = 0 }''',
        'Runtime.kt': '''package com.driftdroid.android.rom
            import android.content.Context
            import java.io.File
            object GameCatalog { enum class Disc(val gameName: String) {
                MARIO_KART_PAL("MKW"), NSMB_USA_REV1("NSMB"), SUNSHINE_USA("SMS"), ECLIPSE_110("Eclipse");
                fun installDir(c: Context) = File("unused")
                fun installedFile(c: Context) = File("unused")
                fun isInstalled(c: Context) = false
            } }
            object WiiDsDisc { fun ensureLoaded() {} }''',
        'Test.kt': '''package com.driftdroid.android.rom
            import java.io.File
            import java.nio.file.Files
            fun main() {
                val root = Files.createTempDirectory("wiids-art-fixture").toFile()
                val predicate = WiiDsArt::class.java.getDeclaredMethod("manifestComplete", File::class.java, String::class.java).apply { isAccessible = true }
                val jobClass = Class.forName("com.driftdroid.android.rom.WiiDsArt\\$Job")
                val make = jobClass.declaredConstructors.single().apply { isAccessible = true }
                val complete = WiiDsArt::class.java.getDeclaredMethod("complete", jobClass).apply { isAccessible = true }
                var checks = 0
                fun assertValue(want: Boolean, actual: Boolean, label: String) { check(want == actual) { label }; checks++ }
                fun put(name: String, text: String = "pixels") { File(root, name).apply { parentFile.mkdirs(); writeText(text) } }
                fun manifest(name: String) = predicate.invoke(WiiDsArt, root, name) as Boolean
                fun job(kind: String, marker: String) = complete.invoke(WiiDsArt, make.newInstance(kind, root, root, marker, false)) as Boolean
                try {
                    put("audio.json", """{"music":{"file":"music.wav"},"sounds":{"select":{"file":"select.wav"}}}""")
                    assertValue(false, manifest("audio.json"), "manifest alone is incomplete")
                    put("music.wav"); put("select.wav")
                    assertValue(true, manifest("audio.json"), "all sounds complete")
                    put("select.wav", "")
                    assertValue(false, manifest("audio.json"), "empty sound is incomplete")
                    put("manifest.json", """{"stage.png":{"archive":"option.szs","entry":"timg/stage.bti"}}""")
                    assertValue(false, manifest("manifest.json"), "Sunshine keyed picture missing")
                    put("stage.png")
                    assertValue(true, manifest("manifest.json"), "Sunshine keyed picture complete")
                    put("manifest.json", """{"images":{"coin":{"file":"coin.png"}},"source":{"file":"Font/mario.brfnt"}}""")
                    assertValue(false, manifest("manifest.json"), "NSMB picture missing")
                    put("coin.png")
                    assertValue(true, manifest("manifest.json"), "source file is not an output")
                    put("manifest.json", """{"images":{"coin":{"file":"../outside.png"}}}""")
                    assertValue(false, manifest("manifest.json"), "escaping path rejected")
                    put("manifest.json", "broken")
                    assertValue(false, manifest("manifest.json"), "damaged manifest rejected")
                    put("artwork.json", """{"images":{"original-cover":{"file":"cover.png"},"original-disc":{"file":"disc.png"}}}""")
                    put("disc.png")
                    assertValue(false, job("mkw-cases", "disc.png"), "disc marker cannot hide missing cover")
                    put("cover.png")
                    assertValue(true, job("mkw-cases", "disc.png"), "case complete")
                    put("course_31.json")
                    assertValue(false, job("mkw-maps", "course_31.json"), "last receipt cannot hide missing maps")
                    for (i in 0 until 32) { val stem = "course_" + i.toString().padStart(2, '0'); put("$stem.png"); put("$stem.json") }
                    assertValue(true, job("mkw-maps", "course_31.json"), "all map pairs complete")
                    File(root, "course_00.png").delete()
                    assertValue(false, job("mkw-maps", "course_31.json"), "deleted earlier map triggers repair")
                    put("menu.json", """{"images":{"panel":{"texture":"panel.png"}}}""")
                    put("hud.json", """{"fonts":{"main":{"texture":"font.png"}}}""")
                    put("panel.png"); put("font.png")
                    assertValue(false, job("mkw-art", "menu.json"), "missing native settings trigger repair")
                    for (name in listOf("settings.tsv", "font.rgba", "background.rgba", "panel.rgba", "header.rgba", "footer.rgba", "button.rgba", "button_focus.rgba")) put("thor_settings/$name")
                    assertValue(true, job("mkw-art", "menu.json"), "HUD/menu/settings complete")
                    File(root, "panel.png").delete()
                    assertValue(false, job("mkw-art", "menu.json"), "deleted menu picture triggers repair")
                    val installs = File(root, "imports").apply { mkdirs() }
                    val dest = File(installs, "game")
                    val staged = File(installs, "game.importing")
                    val previous = File(installs, "game.previous-import")
                    val save = File(root, "saves/owner.dat").apply { parentFile.mkdirs(); writeText("owner save") }
                    fun disc(folder: File, text: String) { folder.mkdirs(); File(folder, "game.iso").writeText(text) }
                    fun reset() { installs.deleteRecursively(); installs.mkdirs() }
                    disc(staged, "first")
                    assertValue(true, DiscInstallCommit.publish(staged, dest) == null && File(dest, "game.iso").readText() == "first", "first import")
                    disc(staged, "replacement")
                    assertValue(true, DiscInstallCommit.publish(staged, dest) == null && File(dest, "game.iso").readText() == "replacement" && !previous.exists(), "replacement import")
                    disc(staged, "new")
                    assertValue(true, DiscInstallCommit.publish(staged, dest) { _, _ -> false } != null && File(dest, "game.iso").readText() == "replacement", "backup failure keeps old")
                    var moves = 0
                    val failure = DiscInstallCommit.publish(staged, dest) { from, to -> ++moves != 2 && from.renameTo(to) }
                    assertValue(true, failure != null && File(dest, "game.iso").readText() == "replacement" && staged.exists(), "failed publish rolls back old")
                    moves = 0
                    val recoveryNeeded = DiscInstallCommit.publish(staged, dest) { from, to -> ++moves == 1 && from.renameTo(to) }
                    assertValue(true, recoveryNeeded != null && !dest.exists() && File(previous, "game.iso").readText() == "replacement", "failed rollback preserves recovery copy")
                    assertValue(true, DiscInstallCommit.recover(dest) == null && File(dest, "game.iso").readText() == "replacement", "next attempt recovers old")
                    reset(); disc(previous, "old after process death"); dest.mkdirs()
                    assertValue(true, DiscInstallCommit.recover(dest) == null && File(dest, "game.iso").readText() == "old after process death", "empty recreated directory cannot discard old")
                    reset(); disc(previous, "old"); disc(dest, "committed new")
                    assertValue(true, DiscInstallCommit.recover(dest) == null && !previous.exists() && File(dest, "game.iso").readText() == "committed new", "completed publish retained after process death")
                    reset(); disc(previous, "old"); dest.mkdirs(); File(dest, "partial").writeText("incomplete")
                    assertValue(true, DiscInstallCommit.recover(dest) != null && File(previous, "game.iso").readText() == "old", "unknown partial directory cannot discard old")
                    assertValue(true, save.readText() == "owner save", "all commit failures preserve separate saves")
                    println("$checks actual-source Kotlin art/installation checks passed")
                } finally { root.deleteRecursively() }
            }''',
    }
    for name, content in fixtures.items():
        (OUT / name).write_text(textwrap.dedent(content), encoding='utf-8')
    kotlin = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
    json = jar('org.json', 'json', '20240303')
    compiler = [jar('org.jetbrains.kotlin', module, '1.9.24') for module in
                ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
    compiler += [kotlin, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'), jar('org.jetbrains', 'annotations', '13.0')]
    source = ROOT / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/rom/WiiDsArt.kt'
    commit = source.with_name('DiscInstallCommit.kt')
    out = OUT / 'classes'
    subprocess.run([str(JAVA), '-cp', ';'.join(map(str, compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
                    '-no-stdlib', '-no-reflect', '-jvm-target', '17', '-classpath', f'{kotlin};{json}',
                    '-d', str(out), str(source), str(commit), *[str(OUT / name) for name in fixtures]], check=True)
    subprocess.run([str(JAVA), '-cp', f'{out};{kotlin};{json}', 'com.driftdroid.android.rom.TestKt'], check=True)


if __name__ == '__main__':
    main()
