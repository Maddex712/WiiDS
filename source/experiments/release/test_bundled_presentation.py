"""Run the actual Kotlin presentation installer against curated payload and negative fixtures."""
from pathlib import Path
import argparse
import subprocess
import textwrap

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/release-audit/presentation-fixtures'
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe')


def jar(group, name, version):
    return next((CACHE / group / name / version).rglob('*.jar'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=ROOT / 'build/wiids-merge/assets')
    parser.add_argument('--previous-assets', type=Path)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    fixtures = {
        'Assets.kt': '''package android.content.res
            import java.io.File
            class AssetManager(private val root: File) {
                var onOpen: ((String) -> Unit)? = null
                fun open(name: String): java.io.InputStream { onOpen?.invoke(name); return File(root, name).inputStream() }
            }''',
        'Context.kt': '''package android.content
            import java.io.File
            import android.content.res.AssetManager
            class Context(val filesDir: File, val external: File, assetsRoot: File) {
                val assets = AssetManager(assetsRoot)
                fun getExternalFilesDir(type: String?): File? = external
            }''',
        'Bitmap.kt': '''package android.graphics
            import java.io.File
            import javax.imageio.ImageIO
            class Bitmap(val width: Int, val height: Int) {
                val isRecycled = false
                enum class Config { ARGB_8888 }
            }
            object BitmapFactory {
                class Options {
                    var inJustDecodeBounds = false; var outWidth = 0; var outHeight = 0
                    var outMimeType: String = ""; var inSampleSize = 1; var inScaled = false
                    var inPreferredConfig = Bitmap.Config.ARGB_8888
                }
                fun decodeFile(path: String, o: Options): Bitmap? {
                    val input = ImageIO.createImageInputStream(File(path)) ?: return null
                    input.use {
                        val readers = ImageIO.getImageReaders(input)
                        if (!readers.hasNext()) return null
                        val reader = readers.next()
                        try {
                            reader.input = input
                            o.outWidth = reader.getWidth(0); o.outHeight = reader.getHeight(0)
                            o.outMimeType = if (reader.formatName.equals("png", true)) "image/png" else "image/jpeg"
                            if (o.inJustDecodeBounds) return null
                            val options = reader.defaultReadParam
                            options.setSourceSubsampling(o.inSampleSize, o.inSampleSize, 0, 0)
                            val decoded = reader.read(0, options)
                            return Bitmap(decoded.width, decoded.height)
                        } finally { reader.dispose() }
                    }
                }
            }''',
        'Test.kt': '''package com.driftdroid.android
            import android.content.Context
            import java.io.File
            import java.nio.file.Files
            import java.security.MessageDigest
            import org.json.JSONObject
            import org.json.JSONArray
            fun main(args: Array<String>) {
                val root = Files.createTempDirectory("wiids-presentation-fixture").toFile()
                var checks = 0
                fun expect(value: Boolean, message: String) { check(value) { message }; checks++ }
                fun hash(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it.toInt() and 255) }
                val assets = File(root, "assets").apply { mkdirs() }
                val files = File(root, "internal").apply { mkdirs() }
                val external = File(root, "external").apply { mkdirs() }
                val context = Context(files, external, assets)
                fun put(root: File, name: String, text: String) = File(root, name).apply { parentFile.mkdirs(); writeText(text) }
                fun entry(target: String, text: String = target): JSONObject {
                    val data = text.toByteArray()
                    put(assets, "presentation/files/$target", text)
                    return JSONObject().put("asset", "presentation/files/$target").put("target", target)
                        .put("sha256", hash(data)).put("bytes", data.size)
                }
                fun manifest(vararg records: JSONObject) {
                    put(assets, "presentation/manifest.json", JSONObject().put("schema", 1)
                        .put("kind", "wiids-bundled-presentation").put("revision", "fixture-r1")
                        .put("files", JSONArray(records.toList())).toString())
                }
                fun reject(record: JSONObject, label: String) {
                    manifest(record)
                    expect(runCatching { ThorBundledPresentation.install(context) }.isFailure, label)
                }
                try {
                    val owner = put(external, "custom/launcher/artwork.json", "owner custom case metadata")
                    val save = put(external, "sms/saves/card-a/owner.dat", "owner save")
                    val settings = put(external, "sms/game/settings.txt", "owner settings")
                    val existingHud = put(external, "sms/ThorHud/coin.png", "owner HUD")
                    val records = listOf(entry("launcher/artwork.json"), entry("launcher/cover.png"),
                        entry("launcher-audio/audio.json"), entry("launcher-audio/music.wav"),
                        entry("thor-map/menu.json"), entry("thor-map/thor_settings/font.rgba"),
                        entry("nsmbw-hud/coin.png"), entry("msmbwii-hud/coin.png"),
                        entry("sms-hud/coin.png"), entry("eclipse-hud/coin.png"))
                    manifest(*records.toTypedArray())
                    expect(ThorBundledPresentation.root(context) == null, "nothing active before install")
                    var progress = 0
                    ThorBundledPresentation.install(context) { done, total -> check(total == records.size); progress = done }
                    expect(progress == records.size, "all files report completion")
                    var packaged = ThorBundledPresentation.root(context)!!
                    expect(File(packaged, "launcher/cover.png").readText() == "launcher/cover.png", "case pixels installed")
                    expect(File(packaged, "launcher-audio/music.wav").readText() == "launcher-audio/music.wav", "music installed")
                    expect(File(files, "WiiCompiled/ThorMap/menu.json").isFile, "first-run menu installed")
                    expect(File(files, "WiiCompiled/ThorMap/thor_settings/font.rgba").isFile, "native settings pixels installed")
                    expect(File(external, "eclipse/ThorHud/coin.png").readText() == "eclipse-hud/coin.png", "Eclipse has its own HUD")
                    expect(existingHud.readText() == "owner HUD", "existing HUD preserved")
                    expect(owner.readText() == "owner custom case metadata", "custom cases preserved")
                    expect(save.readText() == "owner save" && settings.readText() == "owner settings", "saves and settings preserved")
                    expect(ThorLauncherFolders.cases(context, File(files, "WiiCompiled/ThorMap")) == owner.parentFile, "custom case set has priority")
                    expect(ThorLauncherFolders.audio(context, File(files, "WiiCompiled/ThorMap")) == File(packaged, "launcher-audio"), "bundled music chosen before generated")
                    owner.delete()
                    expect(ThorLauncherFolders.cases(context, File(files, "WiiCompiled/ThorMap")) == File(packaged, "launcher"), "bundled cases chosen without custom")
                    owner.writeText("owner custom case metadata")
                    ThorBundledPresentation.install(context)
                    expect(ThorBundledPresentation.root(context) == packaged, "repeat install keeps same revision")
                    File(files, "WiiCompiled/ThorMap/menu.json").delete()
                    File(external, "eclipse/ThorHud/coin.png").writeText("")
                    File(packaged, "launcher-audio/music.wav").delete()
                    ThorBundledPresentation.install(context)
                    packaged = ThorBundledPresentation.root(context)!!
                    expect(File(files, "WiiCompiled/ThorMap/menu.json").isFile, "deleted generated menu repaired")
                    expect(File(packaged, "launcher-audio/music.wav").isFile, "incomplete packaged revision repaired")
                    expect(File(external, "eclipse/ThorHud/coin.png").readText() == "eclipse-hud/coin.png", "empty HUD placeholder repaired")
                    val race = entry("thor-map/race.png")
                    manifest(*(records + race).toTypedArray())
                    context.assets.onOpen = { name ->
                        if (name == "presentation/files/thor-map/race.png") put(files, "WiiCompiled/ThorMap/race.png", "owner file created during copy")
                    }
                    ThorBundledPresentation.install(context)
                    context.assets.onOpen = null
                    expect(File(files, "WiiCompiled/ThorMap/race.png").readText() == "owner file created during copy", "concurrent nonempty creation wins")

                    reject(entry("thor-map/../Config.json"), "traversal rejected")
                    reject(entry("unknown/picture.png"), "unknown target root rejected")
                    reject(entry("thor-map/game.iso"), "disc extension rejected")
                    reject(entry("sms-hud/save.sav"), "save extension rejected")
                    reject(entry("launcher/huge.png").put("bytes", 64L * 1024 * 1024 + 1), "oversize file rejected")
                    reject(entry("launcher/bad.png").put("sha256", "z".repeat(64)), "invalid hash rejected")
                    reject(entry("launcher/mismatch.png").put("sha256", "0".repeat(64)), "copied bytes hash checked")
                    reject(entry("launcher/mismatch.png").put("bytes", 1), "copied bytes length checked")
                    reject(entry("launcher/picture.png").put("asset", "other/picture.png"), "asset escape rejected")
                    val duplicate = entry("launcher/repeated.png")
                    manifest(duplicate, duplicate)
                    expect(runCatching { ThorBundledPresentation.install(context) }.isFailure, "duplicate target rejected")
                    for (badRevision in listOf("../outside", "name/child", ".", "..", "-leading", "a".repeat(65))) {
                        manifest(*records.toTypedArray())
                        val bad = JSONObject(File(assets, "presentation/manifest.json").readText()).put("revision", badRevision)
                        put(assets, "presentation/manifest.json", bad.toString())
                        expect(runCatching { ThorBundledPresentation.install(context) }.isFailure, "unsafe revision rejected")
                    }
                    expect(save.readText() == "owner save" && settings.readText() == "owner settings", "negative fixtures preserve profile")
                    val outside = File(root, "outside").apply { mkdirs() }
                    val link = File(files, "WiiCompiled/ThorMap/symlink")
                    try {
                        Files.createSymbolicLink(link.toPath(), outside.toPath())
                        reject(entry("thor-map/symlink/picture.png"), "symbolic-link target chain rejected")
                        expect(!File(outside, "picture.png").exists(), "nothing written through symbolic link")
                    } catch (e: java.nio.file.FileSystemException) { println("Symbolic-link creation unavailable on this host; production rejection remains enabled") }
                    finally { Files.deleteIfExists(link.toPath()) }

                    val realAssets = File(args[0])
                    if (File(realAssets, "presentation/manifest.json").isFile) {
                        val real = Context(File(root, "real-internal").apply { mkdirs() }, File(root, "real-external").apply { mkdirs() }, realAssets)
                        var previous: File? = null
                        if (args.size > 1) {
                            val old = Context(real.filesDir, real.external, File(args[1]))
                            ThorBundledPresentation.install(old)
                            previous = ThorBundledPresentation.root(old)
                            expect(previous != null, "previous published presentation installed")
                            expect(ThorLauncherArtwork.bitmap(File(previous, "launcher"), "kaizo-cover") == null,
                                   "previous published package reproduces absent Kaizo cover")
                        }
                        var count = 0
                        ThorBundledPresentation.install(real) { done, _ -> count = done }
                        val declared = JSONObject(File(realAssets, "presentation/manifest.json").readText()).getJSONArray("files").length()
                        expect(count == declared && ThorBundledPresentation.root(real) != null, "complete curated real payload installed")
                        val current = ThorBundledPresentation.root(real)!!
                        if (previous != null) expect(current != previous, "upgrade selects corrected revision without uninstall")
                        val cases = ThorLauncherFolders.cases(real, File(real.filesDir, "WiiCompiled/ThorMap"))
                        expect(cases == File(current, "launcher"), "clean storage selects bundled artwork without owner custom art")
                        for (profile in listOf("original", "retro-rewind", "nsmbw", "sunshine", "msmbwii", "eclipse", "galaxy", "kaizo")) {
                            for (part in listOf("cover", "disc", "spine")) {
                                expect(ThorLauncherArtwork.bitmap(cases, "$profile-$part") != null, "production loader decodes $profile-$part")
                            }
                        }
                        ThorBundledPresentation.install(real)
                        expect(ThorBundledPresentation.root(real) == current, "corrected revision remains selected after repeat startup")
                        println("Verified all $count curated presentation entries")
                    }
                    println("$checks actual-source presentation installer checks passed")
                } finally { root.deleteRecursively() }
            }''',
    }
    for name, content in fixtures.items():
        (OUT / name).write_text(textwrap.dedent(content), encoding='utf-8')
    folder_fixture = ROOT / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/ThorLauncherArtwork.kt'
    kotlin = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
    json = jar('org.json', 'json', '20240303')
    compiler = [jar('org.jetbrains.kotlin', module, '1.9.24') for module in
                ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
    compiler += [kotlin, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'), jar('org.jetbrains', 'annotations', '13.0')]
    source = ROOT / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/ThorBundledPresentation.kt'
    classes = OUT / 'classes'
    subprocess.run([str(JAVA), '-cp', ';'.join(map(str, compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
                    '-no-stdlib', '-no-reflect', '-jvm-target', '17', '-classpath', f'{kotlin};{json}',
                    '-d', str(classes), str(source), str(folder_fixture), *[str(OUT / name) for name in fixtures]], check=True)
    subprocess.run([str(JAVA), '-cp', f'{classes};{kotlin};{json}', 'com.driftdroid.android.TestKt',
                    str(args.assets), *([str(args.previous_assets)] if args.previous_assets else [])], check=True)


if __name__ == '__main__':
    main()
