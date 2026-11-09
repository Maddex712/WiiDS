"""Execute the real GPU-driver menu/SAF handler with controlled Android services.

The Thor runtime is compiled without custom-driver support. Its settings page must
not offer an import/save flow which cannot be used by that runtime. This does not
test or enable third-party drivers, and does not claim to fix an unknown ZIP.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/github-bugfix-2026-10-26/driver/tests'
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe')
APP = ROOT / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android'


def jar(group, name, version):
    return next((CACHE / group / name / version).rglob('*.jar'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=APP / 'GpuDriverSettings.kt')
    parser.add_argument('--apk', type=Path,
                        default=Path('M:/WiiDS-updater-1.1-2026-10-25/WiiDS-1.1-arm64.apk'))
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    # Inspect the actual released APK, not a fabricated capability manifest.
    hooks = ['libmain_hook.so', 'libhook_impl.so', 'libfile_redirect_hook.so', 'libgsl_alloc_hook.so']
    with zipfile.ZipFile(args.apk) as apk:
        missing = [name for name in hooks if f'lib/arm64-v8a/{name}' not in apk.namelist()]
    assert missing == hooks, 'The referenced release changed; reassess its backend before testing'
    fixtures = {
        'Activity.kt': r'''package android.app
import java.io.File
class Activity(val filesDir: File, source: String) {
 val applicationInfo = ApplicationInfo(source)
 val contentResolver = android.content.Resolver()
 val messages = mutableListOf<String>()
 var picks = 0
 fun runOnUiThread(action: () -> Unit) { action() }
 fun startActivityForResult(intent: android.content.Intent, code: Int) { picks++ }
}
class ApplicationInfo(val sourceDir: String)
class AlertDialog
''',
        'Intent.kt': r'''package android.content
class Intent(val action: String) {
 var type = ""; fun addCategory(value: String) = this
 companion object { const val ACTION_OPEN_DOCUMENT = "open"; const val CATEGORY_OPENABLE = "openable" }
}
class Resolver {
 var opens = 0
 fun openInputStream(uri: android.net.Uri): java.io.InputStream? { opens++; error("Unexpected document access") }
}
''',
        'Uri.kt': 'package android.net\nclass Uri\n',
        'Build.kt': 'package android.os\nobject Build { object VERSION { const val SDK_INT = 34 } }\n',
        'Toast.kt': r'''package android.widget
class Toast(val activity: android.app.Activity, val message: String) {
 fun show() { activity.messages += message }
 companion object { const val LENGTH_LONG = 1
  fun makeText(activity: android.app.Activity, message: String, length: Int) = Toast(activity,message)
 }
}
''',
        'App.kt': r'''package com.driftdroid.android
object BuildConfig { var THOR_BUILD = true }
class Dialog {
 var title = ""; var message = ""; var items = emptyArray<String>()
 var positiveLabel = ""; var neutralLabel = ""; var negativeLabel = ""
 var positive: ((Any?,Int)->Unit)? = null; var neutral: ((Any?,Int)->Unit)? = null
 var choice: ((Any?,Int)->Unit)? = null
 fun setTitle(v:String) = apply { title = v }
 fun setMessage(v:String) = apply { message = v }
 fun setSingleChoiceItems(v:Array<String>, selected:Int, action:(Any?,Int)->Unit) = apply { items=v;choice=action }
 fun setNegativeButton(v:String, action:((Any?,Int)->Unit)?) = apply { negativeLabel=v }
 fun setNeutralButton(v:String, action:((Any?,Int)->Unit)?) = apply { neutralLabel=v;neutral=action }
 fun setPositiveButton(v:String, action:((Any?,Int)->Unit)?) = apply { positiveLabel=v;positive=action }
 fun show() = this
}
object ThorDialogs {
 lateinit var latest: Dialog; var count = 0
 fun builder(activity:android.app.Activity) = Dialog().also { latest=it;count++ }
}
''',
        'Test.kt': r'''package com.driftdroid.android
import java.io.File
import java.security.MessageDigest
fun main(args:Array<String>) {
 var checks = 0
 fun verify(value:Boolean, name:String) { check(value) { name }; checks++ }
 val root = File(args[0]); root.mkdirs()
 val config = File(root,"WiiCompiled/Config.toml"); config.parentFile.mkdirs()
 config.writeText("[video]\ngraphics_api = \"vulkan\"\n[controls]\na = 96\n".replace("\\n","\n"))
 val dir = File(root,"WiiCompiled/gpu_drivers/test-package"); dir.mkdirs()
 File(dir,"meta.json").writeText("""{"name":"Test package","libraryName":"test.so"}""")
 File(dir,"test.so").writeText("Synthetic package; never loaded")
 fun snapshot():Map<String,String> = root.walkTopDown().filter { it.isFile }.associate {
  it.relativeTo(root).path to MessageDigest.getInstance("SHA-256").digest(it.readBytes()).joinToString("") { b -> "%02x".format(b) }
 }
 val before = snapshot()
 val activity = android.app.Activity(root,args[1])
 GpuDriverSettings.show(activity)
 val unavailable = ThorDialogs.latest
 verify(unavailable.neutral == null, "Unsupported Thor build must not offer Install ZIP")
 verify(unavailable.positiveLabel == "OK", "Unsupported page only offers dismissal")
 verify(unavailable.items.isEmpty(), "Unavailable custom selections must not be offered")
 verify(unavailable.message.contains("system",true), "System driver use is explained")
 verify(unavailable.message.contains("not supported",true), "Unavailable support is explained")
 verify(!unavailable.message.contains("next time a game starts"), "No promise of unused driver selection")
 verify(snapshot() == before, "Opening driver page preserves configuration and installed packages")
 verify(activity.messages.isEmpty(), "No doomed-save failure toast")
 val count = ThorDialogs.count
 GpuDriverSettings.onZipPicked(activity,null)
 verify(ThorDialogs.count == count, "Cancelled picker is a no-op")
 GpuDriverSettings.onZipPicked(activity,android.net.Uri())
 verify(activity.contentResolver.opens == 0, "Stale picker callback must not read a ZIP on unsupported build")
 verify(snapshot() == before, "Stale picker callback preserves config and installed packages")
 verify(activity.picks == 0, "Unavailable page never launches the document picker")
 // The upstream non-Thor variant retains its existing menu and system-selection behavior.
 BuildConfig.THOR_BUILD = false
 GpuDriverSettings.show(activity)
 val supported = ThorDialogs.latest
 verify(supported.items.size == 2, "Non-Thor existing driver menu remains available")
 verify(supported.neutralLabel == "Install .zip...", "Non-Thor import action retained")
 supported.choice!!(null,1)
 supported.positive!!(null,0)
 verify(activity.messages.last() == "Could not save the driver choice.", "Actual release missing-hook failure is reproduced in original path")
 verify(snapshot() == before, "Missing-hook save failure leaves config/package bytes unchanged")
 supported.choice!!(null,0)
 supported.positive!!(null,0)
 verify(activity.messages.last() == "Using the system driver.", "Existing system-driver selection succeeds")
 verify(config.readText().contains("graphics_api") && config.readText().contains("controls"), "System choice preserves other configuration")
 verify(File(dir,"test.so").readText() == "Synthetic package; never loaded", "System choice preserves installed package")
 println("PASS $checks actual-source GPU driver availability checks")
}
''',
    }
    for name, content in fixtures.items():
        (OUT / name).write_text(content, encoding='utf-8')
    stdlib = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
    json_jar = jar('org.json', 'json', '20240303')
    compiler = [jar('org.jetbrains.kotlin', name, '1.9.24') for name in
                ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
    compiler += [stdlib, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'),
                 jar('org.jetbrains', 'annotations', '13.0')]
    classes = OUT / 'classes'
    subprocess.run([str(JAVA), '-cp', ';'.join(map(str, compiler)),
                    'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler', '-nowarn', '-no-stdlib', '-no-reflect',
                    '-jvm-target', '17', '-classpath', f'{stdlib};{json_jar}', '-d', str(classes),
                    *map(str, OUT.glob('*.kt')), str(args.source), str(APP / 'AtomicConfigFile.kt')], check=True)
    run = subprocess.run([str(JAVA), '-cp', f'{classes};{stdlib};{json_jar}',
                          'com.driftdroid.android.TestKt', str(OUT / 'private-fixture'), str(args.apk)],
                         capture_output=True, text=True)
    result = {'status': 'PASS' if run.returncode == 0 else 'FAIL', 'exitCode': run.returncode,
              'source': str(args.source), 'sourceSha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
              'apk': str(args.apk), 'missingHookLibraries': missing,
              'stdout': run.stdout, 'stderr': run.stderr,
              'scope': 'Production Kotlin with Android service mocks; no device, third-party driver, or actual GPU load test'}
    (OUT / ('verification.json' if run.returncode == 0 else 'regression-before.json')).write_text(
        json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(run.stdout or run.stderr)
    raise SystemExit(run.returncode)


if __name__ == '__main__':
    main()
