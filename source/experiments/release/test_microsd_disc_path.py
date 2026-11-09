"""Run the actual GamesActivity disc-path method against mounted-volume fixtures.

Uses real temporary files and controlled Android document/storage services.
Does not import a game, modify device storage, or copy source discs.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import textwrap

R = Path(__file__).resolve().parents[2]
O = R / 'build/github-bugfix-2026-10-26/microsd'
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe')
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'


def jar(group, name, version):
    return next((CACHE / group / name / version).rglob('*.jar'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=R / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/GamesActivity.kt')
    p.add_argument('--label', default='fixed')
    a = p.parse_args()
    out = O / a.label
    out.mkdir(parents=True, exist_ok=True)
    text = a.source.read_text(encoding='utf-8')
    begin = text.index('    private fun discPath(')
    end = text.index('\n    private fun importGalaxy(', begin)
    method = text[begin:end].replace('private fun discPath', 'fun discPath', 1)
    sources = {
        'Uri.kt': '''package android.net
data class Uri(val authority: String?, val documentId: String?, val scheme: String = "content")
''',
        'Documents.kt': '''package android.provider
object DocumentsContract { fun getDocumentId(uri: android.net.Uri): String = uri.documentId ?: error("Not a document URI") }
''',
        'Environment.kt': '''package android.os
object Environment { lateinit var primary: java.io.File; fun getExternalStorageDirectory() = primary }
object Build { object VERSION { var SDK_INT = 32 } }
''',
        'Storage.kt': '''package android.os.storage
data class StorageVolume(val uuid: String?, val directory: java.io.File?)
class StorageManager(var storageVolumes: List<StorageVolume>)
''',
        'Test.kt': textwrap.dedent('''
            import java.io.File
            import android.net.Uri
            import android.os.Build
            import android.os.Environment
            import android.os.storage.StorageManager
            import android.os.storage.StorageVolume
            object WiiDsDisc {
                sealed class Source {
                    data class Files(val parts: List<File>) : Source()
                    data class Document(val uri: Uri, val name: String = "disc.iso") : Source()
                }
            }
            class Probe(var manager: StorageManager?) {
                @Suppress("UNCHECKED_CAST")
                fun <T> getSystemService(type: Class<T>): T? = manager as T?
        ''') + method + textwrap.dedent('''
            }
            var checks = 0; var failures = 0
            fun verify(value: Boolean, message: String) { checks++; if (!value) { failures++; println("FAIL: $message") } }
            fun main() {
                val root = java.nio.file.Files.createTempDirectory("wiids-sd-path-").toFile()
                try {
                    val primary = File(root, "primary").apply { mkdirs() }
                    // Mount path deliberately differs from UUID: use the actual StorageVolume mapping.
                    val card = File(root, "mounted-card").apply { mkdirs() }
                    val usb = File(root, "mounted-usb").apply { mkdirs() }
                    val galaxy = File(card, "ROMs/wii/Super Mario Galaxy (USA).iso").apply { parentFile.mkdirs(); writeText("RMGE01 fixture") }
                    val unicode = File(card, "ROMs/wii/日本語 Galaxy.iso").apply { writeText("fixture") }
                    val primaryDisc = File(primary, "ROMs/Galaxy.iso").apply { parentFile.mkdirs(); writeText("primary") }
                    val usbDisc = File(usb, "Galaxy.iso").apply { writeText("usb") }
                    val outside = File(root, "outside.iso").apply { writeText("outside") }
                    Environment.primary = primary
                    val manager = StorageManager(listOf(StorageVolume(null, primary), StorageVolume("69F2-3F53", card), StorageVolume("ABCD-1234", usb)))
                    val probe = Probe(manager)
                    fun doc(id: String?, authority: String? = "com.android.externalstorage.documents") = WiiDsDisc.Source.Document(Uri(authority, id))
                    verify(probe.discPath(doc("69F2-3F53:ROMs/wii/Super Mario Galaxy (USA).iso")) == galaxy.canonicalPath, "Standard SD-card document resolves without copying")
                    verify(probe.discPath(doc("69f2-3f53:ROMs/wii/Super Mario Galaxy (USA).iso")) == galaxy.canonicalPath, "Volume UUID comparison is case-insensitive")
                    verify(probe.discPath(doc("69F2-3F53:ROMs/wii/日本語 Galaxy.iso")) == unicode.canonicalPath, "Unicode/space paths are retained")
                    verify(probe.discPath(doc("ABCD-1234:Galaxy.iso")) == usbDisc.canonicalPath, "Mounted secondary volume uses its directory")
                    verify(probe.discPath(doc("primary:ROMs/Galaxy.iso"))?.let { File(it).canonicalFile } == primaryDisc.canonicalFile, "Primary picker behavior retained")
                    verify(probe.discPath(doc("raw:" + primaryDisc.path)) == primaryDisc.path, "Raw download document retained")
                    verify(probe.discPath(WiiDsDisc.Source.Files(listOf(galaxy))) == galaxy.path, "Scanner file source retained")
                    verify(probe.discPath(doc("69F2-3F53:missing.iso")) == null, "Absent card file refused")
                    verify(probe.discPath(doc("UNKNOWN:Galaxy.iso")) == null, "Unknown volume refused")
                    verify(probe.discPath(doc("69F2-3F53:")) == null, "Volume root is not a disc")
                    verify(probe.discPath(doc("69F2-3F53")) == null, "Malformed ID refused")
                    verify(probe.discPath(doc(":")) == null, "Empty ID components refused")
                    verify(probe.discPath(doc(null)) == null, "Non-document input remains contained")
                    verify(probe.discPath(doc("69F2-3F53:ROMs/wii")) == null, "Directory is not a disc")
                    verify(probe.discPath(doc("69F2-3F53:../outside.iso")) == null, "Relative path cannot escape selected volume")
                    verify(probe.discPath(doc("69F2-3F53:" + outside.path)) == null, "Absolute child cannot escape selected volume")
                    verify(probe.discPath(doc("69F2-3F53:ROMs/wii/Super Mario Galaxy (USA).iso", "cloud.example")) == null, "Foreign provider UUID-like ID not remapped")
                    manager.storageVolumes = listOf(StorageVolume("69F2-3F53", null))
                    verify(probe.discPath(doc("69F2-3F53:ROMs/wii/Super Mario Galaxy (USA).iso")) == null, "Unmounted card refused")
                    probe.manager = null
                    verify(probe.discPath(doc("69F2-3F53:ROMs/wii/Super Mario Galaxy (USA).iso")) == null, "Missing storage service refused")
                    probe.manager = StorageManager(listOf(StorageVolume("69F2-3F53", card)))
                    Build.VERSION.SDK_INT = 29
                    verify(probe.discPath(doc("69F2-3F53:ROMs/wii/Super Mario Galaxy (USA).iso")) == null, "No API30 mount access on older Android")
                    verify(probe.discPath(doc("raw:" + primaryDisc.path)) == primaryDisc.path, "Legacy raw source remains supported")
                    verify(galaxy.readText() == "RMGE01 fixture" && primaryDisc.readText() == "primary" && usbDisc.readText() == "usb", "All chosen files unchanged")
                    verify(root.walkTopDown().filter { it.isFile }.count() == 5, "Resolution creates no staging copy")
                    println("RESULT checks=$checks failures=$failures")
                    check(failures == 0) { "Regression failures: $failures" }
                } finally { root.deleteRecursively() }
            }
        '''),
    }
    for name, content in sources.items():
        (out / name).write_text(content, encoding='utf-8')
    kotlin = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
    compiler = [jar('org.jetbrains.kotlin', name, '1.9.24') for name in ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
    compiler += [kotlin, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'), jar('org.jetbrains', 'annotations', '13.0')]
    classes = out / 'classes'
    subprocess.run([str(JAVA), '-cp', ';'.join(map(str, compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler', '-no-stdlib', '-no-reflect', '-jvm-target', '17', '-classpath', str(kotlin), '-d', str(classes)] + [str(out / name) for name in sources], check=True)
    result = subprocess.run([str(JAVA), '-cp', f'{classes};{kotlin}', 'TestKt'], capture_output=True, text=True)
    (out / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    report = {'status': 'PASS' if result.returncode == 0 else 'FAIL', 'source': str(a.source), 'source_sha256': hashlib.sha256(a.source.read_bytes()).hexdigest(), 'scope': 'Exact production discPath method; controlled document/storage API and real temporary files. No device or native importer execution.', 'output': result.stdout, 'exit_code': result.returncode}
    (out / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(result.stdout, end='')
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
