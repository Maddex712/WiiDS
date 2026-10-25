"""Run the production shortcut builder/request flow with controlled Android services.

This checks routing and refusal/error handling, not a real launcher accepting a pin.
"""
from pathlib import Path
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/feature-1.1/shortcuts/tests'
OUT.mkdir(parents=True, exist_ok=True)
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'
JDK = Path(os.environ.get('JAVA_HOME', 'C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot')) / 'bin'


def jar(group, name, version):
    return next((CACHE / group / name / version).rglob('*.jar'))


fixtures = {
    'Context.kt': '''package android.content
open class Context {
 var manager: android.content.pm.ShortcutManager? = android.content.pm.ShortcutManager()
 val filesDir = java.io.File("unused-fixture")
 fun <T> getSystemService(type: Class<T>): T? = manager?.let { type.cast(it) }
}
class Intent(val context: Context, val target: Class<*>) {
 var action: String? = null; val extras = mutableMapOf<String,String>()
 fun setAction(value: String) = apply { action = value }
 fun putExtra(key: String, value: String) = apply { extras[key] = value }
}
''',
    'Activity.kt': '''package android.app
class Activity: android.content.Context() {
 var isFinishing = false; var isDestroyed = false
 val messages = java.util.concurrent.LinkedBlockingQueue<String>()
 val uiDone = java.util.concurrent.CountDownLatch(1)
 fun runOnUiThread(action: () -> Unit) { try { action() } finally { uiDone.countDown() } }
}
''',
    'Shortcut.kt': '''package android.content.pm
class ShortcutInfo(val id: String, val shortLabel: String, val longLabel: String,
 val icon: android.graphics.drawable.Icon, val intent: android.content.Intent) {
 class Builder(val context: android.content.Context, val id: String) {
  lateinit var shortLabel: String; lateinit var longLabel: String
  lateinit var icon: android.graphics.drawable.Icon; lateinit var intent: android.content.Intent
  fun setShortLabel(value: String) = apply { shortLabel = value }
  fun setLongLabel(value: String) = apply { longLabel = value }
  fun setIcon(value: android.graphics.drawable.Icon) = apply { icon = value }
  fun setIntent(value: android.content.Intent) = apply { intent = value }
  fun build() = ShortcutInfo(id, shortLabel, longLabel, icon, intent)
 }
}
class ShortcutManager {
 var isRequestPinShortcutSupported = true; var accept = true; var failure = false
 val requests = mutableListOf<ShortcutInfo>()
 fun requestPinShortcut(info: ShortcutInfo, callback: Any?): Boolean {
  check(callback == null); requests += info
  if (failure) throw IllegalStateException("controlled launcher failure")
  return accept
 }
}
''',
    'Bitmap.kt': '''package android.graphics
class Bitmap(val width: Int, val height: Int) {
 companion object { fun createScaledBitmap(source: Bitmap, width: Int, height: Int, filter: Boolean) = Bitmap(width,height) }
}
''',
    'Icon.kt': '''package android.graphics.drawable
class Icon(val bitmap: android.graphics.Bitmap? = null, val resource: Int? = null) {
 companion object {
  fun createWithBitmap(bitmap: android.graphics.Bitmap) = Icon(bitmap = bitmap)
  fun createWithResource(context: android.content.Context, resource: Int) = Icon(resource = resource)
 }
}
''',
    'Toast.kt': '''package android.widget
class Toast(val activity: android.app.Activity, val text: String) {
 fun show() { activity.messages.add(text) }
 companion object {
  const val LENGTH_LONG = 1
  fun makeText(activity: android.app.Activity, text: String, length: Int) = Toast(activity,text)
 }
}
''',
    'App.kt': '''package com.driftdroid.android
class FrontendLaunchActivity { companion object { const val ACTION_PLAY = "io.github.maddex712.wiids.action.PLAY" } }
object BuildConfig { var THOR_RETRO_REWIND = true }
object R { object mipmap { const val ic_launcher = 101 } }
object ThorLauncherFolders { fun cases(context: android.content.Context, folder: java.io.File) = folder }
object ThorLauncherArtwork {
 var art: android.graphics.Bitmap? = null; var failure = false; var stem = ""
 fun bitmap(folder: java.io.File, value: String): android.graphics.Bitmap? {
  stem = value; if (failure) error("controlled artwork failure"); return art
 }
}
object ThorDialogs {
 lateinit var latest: Builder
 fun builder(activity: android.app.Activity) = Builder().also { latest = it }
 class Builder {
  var title = ""; var items = emptyArray<String>(); var message = ""
  var pick: ((Any?,Int)->Unit)? = null; var positive: ((Any?,Int)->Unit)? = null
  fun setTitle(value: String) = apply { title = value }
  fun setMessage(value: String) = apply { message = value }
  fun setItems(value: Array<String>, action: (Any?,Int)->Unit) = apply { items = value; pick = action }
  fun setPositiveButton(value: String, action: ((Any?,Int)->Unit)?) = apply { positive = action }
  fun setNegativeButton(value: String, action: ((Any?,Int)->Unit)?) = this
  fun show() {}
 }
}
''',
    'Test.kt': '''package com.driftdroid.android
import android.app.Activity
import android.graphics.Bitmap
import android.graphics.drawable.Icon
import java.util.concurrent.TimeUnit

private var checks = 0
private fun verify(value: Boolean, message: String) { checks++; check(value) { message } }
private fun result(a: Activity): String = a.messages.poll(5, TimeUnit.SECONDS) ?: error("No result message")
fun main() {
 val games = FrontendGame.values(); val context = Activity(); val ids = mutableSetOf<String>()
 for (game in games) {
  val info = GameShortcuts.create(context, game, Icon())
  verify(ids.add(info.id), "Distinct shortcut ID per original/mod")
  verify(info.id == GameShortcuts.create(context, game, Icon()).id, "Stable shortcut ID")
  verify(info.shortLabel == game.label && info.longLabel.contains(game.label), "Game-specific labels")
  verify(info.intent.target == FrontendLaunchActivity::class.java, "Use public frontend gate")
  verify(info.intent.action == FrontendLaunchActivity.ACTION_PLAY, "Use PLAY action")
  verify(info.intent.extras == mapOf("game" to game.id), "Only explicit stable game ID is forwarded")
 }
 val absent = Activity().apply { manager = null }
 GameShortcuts.request(absent, FrontendGame.RETRO_REWIND)
 verify(result(absent).contains("default launcher"), "Missing service explains launcher requirement")
 val unsupported = Activity().apply { manager!!.isRequestPinShortcutSupported = false }
 GameShortcuts.request(unsupported, FrontendGame.RETRO_REWIND)
 verify(result(unsupported).contains("default launcher"), "Unsupported launcher explains requirement")
 verify(unsupported.manager!!.requests.isEmpty(), "Unsupported launcher receives no request")

 val accepted = Activity(); GameShortcuts.request(accepted, FrontendGame.RETRO_REWIND)
 verify(result(accepted).startsWith("Finish adding Retro Rewind"), "Request is not reported as a completed pin")
 verify(accepted.manager!!.requests.single().intent.extras["game"] == "retro-rewind", "Pin Retro Rewind, not original")
 verify(accepted.manager!!.requests.single().icon.resource == R.mipmap.ic_launcher, "Missing art uses WiiDS icon")

 ThorLauncherArtwork.art = Bitmap(512,512)
 val art = Activity(); GameShortcuts.request(art, FrontendGame.MSMBWII); result(art)
 verify(ThorLauncherArtwork.stem == "msmbwii-disc", "Mod uses its own art")
 val bitmap = art.manager!!.requests.single().icon.bitmap!!
 verify(bitmap.width == 192 && bitmap.height == 192, "Artwork is bounded for launcher")

 val rejected = Activity().apply { manager!!.accept = false }
 GameShortcuts.request(rejected, FrontendGame.NSMB)
 verify(result(rejected).contains("did not accept"), "Launcher refusal is reported")
 val failed = Activity().apply { manager!!.failure = true }
 GameShortcuts.request(failed, FrontendGame.GALAXY)
 verify(result(failed).contains("could not be requested"), "Launcher failure is handled")
 ThorLauncherArtwork.failure = true
 val broken = Activity(); GameShortcuts.request(broken, FrontendGame.GALAXY)
 verify(result(broken).contains("could not be created"), "Creation failure is handled")
 verify(broken.manager!!.requests.isEmpty(), "Creation failure sends no request")
 ThorLauncherArtwork.failure = false
 for (destroyed in listOf(false,true)) {
  val closing = Activity().apply { isDestroyed = destroyed; isFinishing = !destroyed }
  GameShortcuts.request(closing, FrontendGame.KAIZO)
  verify(closing.uiDone.await(5,TimeUnit.SECONDS), "Background icon work completes")
  verify(closing.manager!!.requests.isEmpty() && closing.messages.isEmpty(), "Closing activity sends no pin or toast")
 }
 GameShortcuts.show(Activity())
 val menu = ThorDialogs.latest
 verify(menu.items.toList() == games.map { it.label }, "All eight games offered")
 menu.pick!!(null,1)
 verify(ThorDialogs.latest.title == "Retro Rewind", "Correct mod confirmation")
 verify(ThorDialogs.latest.message.contains("default launcher"), "Cocoon requirement shown")
 BuildConfig.THOR_RETRO_REWIND = false
 GameShortcuts.show(Activity())
 verify("Retro Rewind" !in ThorDialogs.latest.items, "Build without RR does not offer it")
 println("PASS $checks production shortcut checks")
}
''',
}
for name, content in fixtures.items():
    (OUT / name).write_text(content, encoding='utf-8')
classes = OUT / 'classes'
classes.mkdir(exist_ok=True)
source = ROOT / 'reference/driftdroid/android/app/src/main'
subprocess.run([str(JDK / 'javac.exe'), '--release', '17', '-d', str(classes),
                str(source / 'java/com/driftdroid/android/FrontendGame.java')], check=True)
stdlib = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
compiler = [jar('org.jetbrains.kotlin', name, '1.9.24') for name in
            ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
compiler += [stdlib, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'),
             jar('org.jetbrains', 'annotations', '13.0')]
subprocess.run([str(JDK / 'java.exe'), '-cp', ';'.join(map(str, compiler)),
                'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler', '-nowarn', '-no-stdlib', '-no-reflect',
                '-jvm-target', '17', '-classpath', f'{stdlib};{classes}', '-d', str(classes),
                *map(str, OUT.glob('*.kt')), str(source / 'kotlin/com/driftdroid/android/GameShortcuts.kt')], check=True)
run = subprocess.run([str(JDK / 'java.exe'), '-cp', f'{classes};{stdlib}',
                      'com.driftdroid.android.TestKt'], capture_output=True, text=True, check=True)
print(run.stdout.strip())
(OUT / 'verification.json').write_text(json.dumps({
    'status': 'PASS', 'result': run.stdout.strip(),
    'scope': 'Production shortcut code with mocked Android services; physical pin/Cocoon/gameplay not tested',
}, indent=2) + '\n', encoding='utf-8')
