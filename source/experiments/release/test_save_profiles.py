"""Verify production profile destinations and native-process/restore gates."""
from pathlib import Path
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/feature-1.1/save-manager/profile-tests'
OUT.mkdir(parents=True, exist_ok=True)
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'
JDK = Path(os.environ.get('JAVA_HOME', 'C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot')) / 'bin'
def jar(group, name, version): return next((CACHE / group / name / version).rglob('*.jar'))
fixtures = {
'Context.kt': '''package android.content
class Context(val filesDir: java.io.File, var external: java.io.File?, val packageName: String = "io.github.maddex712.wiids") {
 val manager = android.app.ActivityManager()
 fun getExternalFilesDir(type: String?) = external
 fun getSystemService(name: String) = manager
 companion object { const val ACTIVITY_SERVICE = "activity" }
}
''',
'Manager.kt': '''package android.app
class ActivityManager {
 class RunningAppProcessInfo(val processName: String)
 var runningAppProcesses: List<RunningAppProcessInfo>? = emptyList()
}
''',
'App.kt': '''package com.driftdroid.android
object ThorGameSession {
 var retained = false; var updating = false
 fun hasRetainedRuntime() = retained
 fun isUpdating() = updating
}
object ThorRetroRewindUpdateStatus { object snapshot { var running = false } }
object WiiDsUpdater { val installing = java.util.concurrent.atomic.AtomicBoolean(false) }
''',
'Test.kt': '''package com.driftdroid.android
import android.content.Context
import android.app.ActivityManager.RunningAppProcessInfo
import com.driftdroid.android.saves.SaveArchive
import java.io.File
private var checks = 0
fun verify(ok: Boolean, why: String) { checks++; check(ok) { why } }
fun reject(why: String, action: () -> Unit) { checks++; check(runCatching(action).isFailure) { why } }
fun main(args: Array<String>) {
 val base = java.nio.file.Files.createTempDirectory(File(args[0]).toPath(), "profiles-").toFile()
 val c = Context(File(base,"internal").apply { mkdirs() }, File(base,"external").apply { mkdirs() })
 val profiles = FrontendGame.values().associateWith { SaveManagerProfiles.profile(c,it) }
 val seen = mutableSetOf<String>()
 for ((game,profile) in profiles) {
  verify(profile.id == game.id,"Stable profile ID")
  for (root in profile.roots.values) verify(seen.add(root.canonicalPath),"Original and mod destinations do not overlap")
 }
 fun path(game: FrontendGame, key: String = "main") = profiles.getValue(game).roots.getValue(key).path.replace('\\\\','/')
 verify(path(FrontendGame.MARIO_KART).endsWith("WiiCompiled/NAND/title/00010004/524d4350/data"),"PAL MKW NAND title")
 verify(path(FrontendGame.RETRO_REWIND).endsWith("WiiCompiled/riivolution/save/RetroWFC/RMCP"),"RR redirects save")
 verify(path(FrontendGame.RETRO_REWIND,"alternate").endsWith("RetroWFC2/RMCP"),"RR second redirect captured separately")
 verify(path(FrontendGame.NSMB).endsWith("nsmbw/NAND/title/00010000/534d4e45/data"),"NSMB title storage")
 verify(path(FrontendGame.MSMBWII).endsWith("msmbwii/NAND/title/00010000/534d4e45/data"),"MSMB independent title storage")
 verify(path(FrontendGame.SUNSHINE).endsWith("sms/saves/card-a"),"Sunshine card directory")
 verify(path(FrontendGame.ECLIPSE).endsWith("eclipse/saves/card-a"),"Eclipse card directory")
 verify(profiles.getValue(FrontendGame.GALAXY).roots.size == 6,"Galaxy NAND, mirror and four slot roots")
 verify(path(FrontendGame.KAIZO,"mirror").endsWith("galaxy-kaizo/GalaxyData/saves"),"Kaizo mirror stays isolated")
 verify(path(FrontendGame.GALAXY,"slot1").endsWith("galaxy/nand_slot1/title"),"Galaxy slot includes authoritative NAND copy")
 SaveManagerProfiles.requireStopped(c); verify(true,"Idle library allowed")
 for (suffix in listOf(":nsmbw_game",":msmbwii_game",":sms_game",":eclipse_game",":galaxy_game",":galaxy_kaizo_game",":galaxy_import",":galaxy_kaizo_import")) {
  c.manager.runningAppProcesses = listOf(RunningAppProcessInfo(c.packageName+suffix))
  reject("Running game/importer blocked $suffix") { SaveManagerProfiles.requireStopped(c) }
 }
 c.manager.runningAppProcesses = listOf(RunningAppProcessInfo("other.package:nsmbw_game"))
 SaveManagerProfiles.requireStopped(c); verify(true,"Unrelated app allowed")
 c.manager.runningAppProcesses = null
 reject("Unknown process state refuses save operation") { SaveManagerProfiles.requireStopped(c) }
 c.manager.runningAppProcesses = emptyList()
 ThorGameSession.retained = true; reject("MKW retained runtime blocked") { SaveManagerProfiles.requireStopped(c) }; ThorGameSession.retained = false
 ThorGameSession.updating = true; reject("Runtime update blocked") { SaveManagerProfiles.requireStopped(c) }; ThorGameSession.updating = false
 ThorRetroRewindUpdateStatus.snapshot.running = true; reject("RR update blocked") { SaveManagerProfiles.requireStopped(c) }; ThorRetroRewindUpdateStatus.snapshot.running = false
 verify(SaveManagerProfiles.blocker(c) == null,"No save blocker at idle")
 WiiDsUpdater.installing.set(true); verify(SaveManagerProfiles.blocker(c)?.contains("update installation") == true,"App update blocks new games and save work"); WiiDsUpdater.installing.set(false)
 verify(SaveManagerProfiles.blocker(c) == null,"Cancelled app update releases launch gate")
 SaveManagerProfiles.busy.set(true); verify(SaveManagerProfiles.blocker(c) != null,"Save worker blocks game launch"); SaveManagerProfiles.busy.set(false)
 File(SaveManagerProfiles.work(c),"restore.properties").apply { parentFile.mkdirs(); writeText("pending") }
 verify(SaveManagerProfiles.blocker(c)?.contains("recover") == true,"Interrupted restore blocks launching")
 val config = File(c.external,"nsmbw/Config.toml").apply { parentFile.mkdirs() }
 config.writeText("[paths]\\n# nand_root = 'unused'\\n")
 SaveManagerProfiles.profile(c,FrontendGame.NSMB); verify(true,"Commented custom path ignored")
 config.writeText("[paths]\\nnand_root = '/custom/nand'\\n")
 reject("Custom NAND not guessed") { SaveManagerProfiles.profile(c,FrontendGame.NSMB) }
 config.writeText("[paths]\\nnand_root = ''\\n")
 SaveManagerProfiles.profile(c,FrontendGame.NSMB); verify(true,"Empty path uses managed NAND")
 File(c.external,"nsmbw/Saves/123456789abcdef0/NAND").mkdirs()
 reject("Extra NAND profiles not silently substituted") { SaveManagerProfiles.profile(c,FrontendGame.NSMB) }
 c.external = null
 reject("Unavailable Galaxy external storage") { SaveManagerProfiles.profile(c,FrontendGame.GALAXY) }
 reject("Unavailable Sunshine external storage") { SaveManagerProfiles.profile(c,FrontendGame.SUNSHINE) }
 verify(SaveManagerProfiles.profile(c,FrontendGame.NSMB).roots.getValue("main").path.contains("data"+File.separator+"nsmbw"),"NSMB fallback matches runtime")
 println("PASS $checks production save profile/launch gate checks")
}
'''}
for name, content in fixtures.items(): (OUT / name).write_text(content, encoding='utf-8')
source = ROOT / 'reference/driftdroid/android/app/src/main'
classes = OUT / 'classes'; classes.mkdir(exist_ok=True)
subprocess.run([str(JDK / 'javac.exe'), '--release', '17', '-d', str(classes),
    str(source / 'java/com/driftdroid/android/FrontendGame.java'),
    str(source / 'java/com/driftdroid/android/saves/SaveArchive.java')], check=True)
stdlib = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
compiler = [jar('org.jetbrains.kotlin', name, '1.9.24') for name in ('kotlin-compiler-embeddable','kotlin-script-runtime','kotlin-daemon-embeddable')]
compiler += [stdlib, jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330'), jar('org.jetbrains','annotations','13.0')]
subprocess.run([str(JDK/'java.exe'), '-cp', ';'.join(map(str,compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
    '-nowarn','-no-stdlib','-no-reflect','-jvm-target','17','-classpath',f'{stdlib};{classes}','-d',str(classes),
    *map(str, OUT.glob('*.kt')), str(source/'kotlin/com/driftdroid/android/SaveManagerProfiles.kt')], check=True)
run = subprocess.run([str(JDK/'java.exe'),'-cp',f'{classes};{stdlib}','com.driftdroid.android.TestKt',str(OUT)],capture_output=True,text=True)
assert run.returncode == 0, run.stdout + run.stderr
print(run.stdout.strip())
(OUT/'verification.json').write_text(json.dumps({'status':'PASS','result':run.stdout.strip(),
    'scope':'Production profile adapter against mocked Android directories/processes and real disposable config files'},indent=2)+'\n',encoding='utf-8')
