"""Actual Kotlin MSMBWii installer: cancellation, save preservation and rename faults."""
from pathlib import Path
import json
import subprocess
import textwrap

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/final-qol/msmbwii-fixtures'
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe')
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'


def jar(group, module, version):
    return next((CACHE / group / module / version).rglob('*.jar'))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    fixtures = {
        'Context.kt': '''package android.content
            import java.io.File
            import java.io.InputStream
            import java.io.FilterInputStream
            import com.driftdroid.android.rom.MsmbwiiMod
            open class Context(val filesDir: File, asset: File) {
                val applicationContext get() = this
                val assets = Assets(asset)
                val contentResolver = Resolver()
            }
            class Assets(private val asset: File) {
                var opens = 0
                var cancelOnSecondClose = false
                fun open(name: String): InputStream {
                    val number = ++opens
                    return object : FilterInputStream(asset.inputStream()) {
                        override fun close() {
                            super.close()
                            if (cancelOnSecondClose && number == 2) MsmbwiiMod.cancel()
                        }
                    }
                }
            }
            class Resolver { fun openInputStream(uri: android.net.Uri): InputStream? = null }''',
        'Uri.kt': '''package android.net
            class Uri''',
        'Catalog.kt': '''package com.driftdroid.android.rom
            import android.content.Context
            import java.io.File
            object GameCatalog { fun msmbwiiModDir(c: Context) = File(c.filesDir, "games/mod") }''',
        'Test.kt': r'''package com.driftdroid.android.rom
            import android.content.Context
            import java.io.File
            import java.io.IOException
            import java.lang.reflect.InvocationTargetException
            import java.nio.file.Files
            import java.util.zip.ZipEntry
            import java.util.zip.ZipOutputStream

            fun main(args: Array<String>) {
                val root = Files.createTempDirectory("wiids-msmbwii-fixture").toFile()
                val asset = File(args[0])
                val parent = File(root, "transaction").apply { mkdirs() }
                val dest = File(parent, "mod")
                val staged = File(parent, "mod.importing")
                val previous = File(parent, "mod.previous-install")
                val record = File(parent, "mod.install-journal")
                val savePath = "msmbwiijala/save/slot-a/earned.dat"
                val owner = ByteArray(200_000) { (it * 31).toByte() }
                var checks = 0
                fun verify(ok: Boolean, label: String) { check(ok) { label }; checks++ }
                fun put(base: File, path: String, value: String) = File(base, path).apply { parentFile.mkdirs(); writeText(value) }
                fun mod(base: File, value: String) {
                    put(base, "msmbwiijala/AnotherRes/SystemUS_1.bin", value)
                    put(base, "riivolution/config/SMNE.xml", "enabled")
                }
                fun ownerSave(base: File) = File(base, savePath).apply { parentFile.mkdirs(); writeBytes(owner) }
                fun exactSave(base: File) = File(base, savePath).readBytes().contentEquals(owner)
                fun reset() { parent.deleteRecursively(); parent.mkdirs() }
                fun prepare() { reset(); mod(dest, "old"); ownerSave(dest); mod(staged, "new"); MsmbwiiInstallCommit.preserveSave(dest, staged) {} }
                fun cancelled(block: () -> Unit): Boolean = try { block(); false } catch (e: IOException) { true }
                fun privateCall(name: String, vararg values: Any) {
                    val types = values.map { when (it) { is File -> File::class.java; is java.io.InputStream -> java.io.InputStream::class.java; else -> error("type") } }.toTypedArray()
                    val method = MsmbwiiMod::class.java.getDeclaredMethod(name, *types).apply { isAccessible = true }
                    try { method.invoke(MsmbwiiMod, *values) } catch (error: InvocationTargetException) { throw error.targetException }
                }
                fun waitInstall(context: Context) {
                    check(MsmbwiiMod.start(context))
                    val end = System.nanoTime() + 15_000_000_000L
                    while (MsmbwiiMod.state.running && System.nanoTime() < end) Thread.sleep(2)
                    check(!MsmbwiiMod.state.running) { "installer timeout" }
                }
                try {
                    mod(staged, "first")
                    verify(MsmbwiiInstallCommit.publish(staged, dest, {}) == null, "first install commits")
                    verify(File(dest, "msmbwiijala/AnotherRes/SystemUS_1.bin").readText() == "first", "first install content")
                    prepare()
                    verify(exactSave(dest) && exactSave(staged), "save copy leaves live original intact")
                    verify(MsmbwiiInstallCommit.publish(staged, dest, {}) == null && exactSave(dest), "replacement preserves earned save")
                    verify(!previous.exists() && !record.exists(), "successful commit cleans recovery state")
                    prepare()
                    var moves = 0
                    verify(cancelled { MsmbwiiInstallCommit.publish(staged, dest, {}, { from, to -> ++moves != 1 && from.renameTo(to) }) }, "backup rename failure reports failure")
                    verify(exactSave(dest) && File(dest, "msmbwiijala/AnotherRes/SystemUS_1.bin").readText() == "old", "backup failure preserves live mod and save")
                    prepare(); moves = 0
                    verify(cancelled { MsmbwiiInstallCommit.publish(staged, dest, {}, { from, to -> ++moves != 2 && from.renameTo(to) }) }, "publish rename failure reports failure")
                    verify(exactSave(dest) && !previous.exists() && !record.exists(), "failed publish rolls back original save")
                    prepare(); moves = 0
                    val error = MsmbwiiInstallCommit.publish(staged, dest, {}, { from, to -> ++moves == 1 && from.renameTo(to) })
                    verify(error != null && !dest.exists() && exactSave(previous), "failed rollback retains old save in backup")
                    verify(record.isFile, "failed rollback retains recovery journal")
                    verify(MsmbwiiInstallCommit.recover(dest) == null && exactSave(dest), "next attempt recovers previous save")
                    prepare()
                    var stop = false
                    verify(cancelled { MsmbwiiInstallCommit.publish(staged, dest, { if (stop) throw IOException("cancel") }, { from, to -> from.renameTo(to).also { if (to == previous) stop = true } }) }, "cancel after backup interrupts publish")
                    verify(exactSave(dest) && !previous.exists() && !record.exists(), "cancel after backup rolls back save")
                    prepare()
                    verify(cancelled { MsmbwiiInstallCommit.publish(staged, dest, { throw IOException("cancel") }) }, "precommit cancel rejects")
                    verify(exactSave(dest) && !record.exists(), "precommit cancel preserves originals")
                    prepare()
                    var chunks = 0
                    verify(cancelled { MsmbwiiInstallCommit.preserveSave(dest, staged) { if (++chunks == 5) throw IOException("cancel") } }, "save copy cancellation is checked in chunks")
                    verify(exactSave(dest), "partial save copy never moves original")
                    prepare(); dest.renameTo(previous); record.writeText("wiids-msmbwii-install-v1\n")
                    verify(MsmbwiiInstallCommit.recover(dest) == null && exactSave(dest), "process death after backup recovers original")
                    prepare(); dest.renameTo(previous); staged.renameTo(dest); record.writeText("wiids-msmbwii-install-v1\n")
                    verify(MsmbwiiInstallCommit.recover(dest) == null && exactSave(dest) && !previous.exists(), "process death after commit keeps complete new save")
                    prepare(); dest.renameTo(previous); dest.mkdirs(); record.writeText("wiids-msmbwii-install-v1\n")
                    verify(MsmbwiiInstallCommit.recover(dest) == null && exactSave(dest), "empty recreated directory cannot strand the previous save")
                    prepare(); dest.renameTo(previous); dest.mkdirs(); put(dest, "partial", "incomplete"); record.writeText("wiids-msmbwii-install-v1\n")
                    verify(MsmbwiiInstallCommit.recover(dest) != null && exactSave(previous), "ambiguous partial tree never deletes recovery save")
                    reset(); mod(previous, "old"); ownerSave(previous)
                    verify(MsmbwiiInstallCommit.recover(dest) != null && exactSave(previous), "missing journal preserves backup for inspection")
                    reset(); mod(staged, "new"); ownerSave(staged)
                    verify(MsmbwiiInstallCommit.orphanedStagedSave(staged, dest), "legacy stranded save detected before stage deletion")
                    val context = Context(File(root, "full"), asset)
                    waitInstall(context)
                    verify(MsmbwiiMod.state.error == null, "actual installer accepts exact bundled archive")
                    val fullDest = GameCatalog.msmbwiiModDir(context)
                    ownerSave(fullDest)
                    val original = fullDest.walkTopDown().filter { it.isFile }.associate { it.relativeTo(fullDest).path to it.readBytes().toList() }
                    context.assets.opens = 0; context.assets.cancelOnSecondClose = true
                    waitInstall(context)
                    verify(MsmbwiiMod.state.error?.contains("cancelled") == true, "actual installer honors cancellation immediately after archive copy")
                    verify(original == fullDest.walkTopDown().filter { it.isFile }.associate { it.relativeTo(fullDest).path to it.readBytes().toList() }, "actual cancelled reinstall keeps every installed file and earned save")
                    verify(!File(fullDest.parentFile, "mod.importing").exists(), "cancelled installer leaves no disposable stage")
                    context.assets.opens = 0; context.assets.cancelOnSecondClose = false
                    waitInstall(context)
                    verify(MsmbwiiMod.state.error == null && exactSave(fullDest), "actual successful reinstall preserves earned save")
                    verify(original == fullDest.walkTopDown().filter { it.isFile }.associate { it.relativeTo(fullDest).path to it.readBytes().toList() }, "actual successful reinstall preserves all expected mod and save bytes")
                    MsmbwiiMod.cancel()
                    verify(cancelled { privateCall("sha256", asset) }, "actual SHA verification checks cancellation")
                    verify(cancelled { privateCall("unpack", asset, File(root, "unpacked")) }, "actual unpack checks cancellation")
                    val legacyContext = Context(File(root, "legacy"), asset)
                    val legacyDest = GameCatalog.msmbwiiModDir(legacyContext)
                    val legacyStage = File(legacyDest.parentFile, "mod.importing")
                    mod(legacyStage, "stranded"); ownerSave(legacyStage)
                    waitInstall(legacyContext)
                    verify(MsmbwiiMod.state.error?.contains("recovery") == true && exactSave(legacyStage), "actual installer refuses to wipe legacy stranded save")
                    println("$checks actual-source MSMBWii cancellation/transaction checks passed")
                } finally { root.deleteRecursively() }
            }''',
    }
    for name, content in fixtures.items():
        (OUT / name).write_text(textwrap.dedent(content), encoding='utf-8')
    kotlin = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
    compiler = [jar('org.jetbrains.kotlin', module, '1.9.24') for module in
                ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
    compiler += [kotlin, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'), jar('org.jetbrains', 'annotations', '13.0')]
    src = ROOT / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/rom'
    classes = OUT / 'classes'
    subprocess.run([str(JAVA), '-cp', ';'.join(map(str, compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
                    '-no-stdlib', '-no-reflect', '-jvm-target', '17', '-classpath', str(kotlin),
                    '-d', str(classes), str(src / 'MsmbwiiMod.kt'), str(src / 'MsmbwiiInstallCommit.kt'),
                    *[str(OUT / name) for name in fixtures]], check=True)
    result = subprocess.run([str(JAVA), '-cp', f'{classes};{kotlin}', 'com.driftdroid.android.rom.TestKt',
                             str(ROOT / 'build/wiids-merge/assets/mods/MSMBWii.zip')], check=True, capture_output=True, text=True)
    print(result.stdout.strip())
    (ROOT / 'build/final-qol/msmbwii-install-verification.json').write_text(json.dumps({
        'status':'pass', 'actual_production_kotlin_compiled': ['MsmbwiiMod.kt','MsmbwiiInstallCommit.kt'],
        'result':result.stdout.strip(), 'fixture_data':'Host temporary folders and already bundled exact mod archive; no device input or owner profile mutation',
        'limits':'No live Android fault injection or filesystem power-loss durability claim'
    }, indent=2)+'\n')


if __name__ == '__main__':
    main()
