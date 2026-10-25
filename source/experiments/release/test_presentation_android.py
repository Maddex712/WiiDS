"""Fresh-package actual Android presentation install; never reads or clears WiiDS data."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, time, zipfile
R=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial',required=True);p.add_argument('--baseline',action='store_true')
a=p.parse_args()
O=R/'build/feature-1.1/presentation-fix-2026-10-25/android'
O.mkdir(parents=True,exist_ok=True)
SDK=R/'.tools/android-sdk';BT=SDK/'build-tools/34.0.0';AJ=SDK/'platforms/android-34/android.jar'
J=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')
C=Path.home()/'.gradle/caches/modules-2/files-2.1'
def jar(g,n,v):return next((C/g/n/v).rglob('*.jar'))
def run(cmd,**kw):return subprocess.run(list(map(str,cmd)),check=True,**kw)
adb=[str(R/'.tools/platform-tools/adb.exe'),'-s',a.serial]
pkg='io.github.maddex712.wiids.presentationtests'
def idle():
    state=subprocess.check_output(adb+['shell','dumpsys','activity','activities'],text=True)
    allowed=('rip.moth.cocoonshell/','io.github.maddex712.wiids/com.driftdroid.android.ModePickerActivity',pkg+'/')
    active=[s for s in state.splitlines() if 'topResumedActivity=' in s]
    assert active and all(any(x in s for x in allowed) for s in active),'Owner activity is not idle; test refused'
idle()
(O/'AndroidManifest.xml').write_text(f'''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="{pkg}">
<uses-sdk android:minSdkVersion="30" android:targetSdkVersion="34"/>
<application android:label="WiiDS Asset Check" android:debuggable="true" android:allowBackup="false" android:theme="@android:style/Theme.Material.NoActionBar">
<activity android:name="com.driftdroid.android.PresentationCheck" android:exported="true" android:screenOrientation="landscape"/>
</application></manifest>''')
(O/'R.kt').write_text('package com.driftdroid.android\nobject R { object raw { const val ui_click=0 } }\n')
(O/'PresentationCheck.kt').write_text(r'''package com.driftdroid.android
import android.app.Activity
import android.os.Bundle
import android.widget.TextView
import java.io.File
import org.json.JSONObject
class PresentationCheck: Activity() {
 override fun onCreate(state:Bundle?) {
  super.onCreate(state)
  val text=TextView(this).apply { text="Checking the APK's artwork and music in fresh test storage…";textSize=22f }
  setContentView(text)
  Thread {
   var checks=0
   fun checkIt(value:Boolean,message:String) { check(value) { message };checks++ }
   val result=JSONObject()
   try {
    checkIt(ThorBundledPresentation.root(this)==null,"fresh test package has no active presentation")
    val manifest=JSONObject(assets.open("presentation/manifest.json").bufferedReader().use { it.readText() })
    var copied=0
    ThorBundledPresentation.install(this) { done,total -> copied=done; if(done==total) checkIt(done==manifest.getJSONArray("files").length(),"complete file count") }
    val root=ThorBundledPresentation.root(this)
    checkIt(root!=null,"completed dotted release revision is selected")
    val generated=File(filesDir,"WiiCompiled/ThorMap")
    val cases=ThorLauncherFolders.cases(this,generated)
    val audio=ThorLauncherFolders.audio(this,generated)
    checkIt(cases==File(root,"launcher"),"fresh install uses bundled cases without external custom data")
    checkIt(audio==File(root,"launcher-audio"),"fresh install uses bundled music without external custom data")
    val images=JSONObject(File(cases,"artwork.json").readText()).getJSONObject("images")
    val art=images.keys()
    var decoded=0
    while(art.hasNext()) { val stem=art.next();checkIt(ThorLauncherArtwork.bitmap(cases,stem)!=null,"image decoded: "+stem);decoded++ }
    val metadata=JSONObject(File(audio,"audio.json").readText())
    val verify=ThorLauncherAudio::class.java.getDeclaredMethod("checked",File::class.java,JSONObject::class.java,java.lang.Long.TYPE).apply { isAccessible=true }
    val parse=ThorLauncherAudio::class.java.getDeclaredMethod("readWav",File::class.java).apply { isAccessible=true }
    val entry=metadata.getJSONObject("music")
    val music=verify.invoke(ThorLauncherAudio,audio,entry,16L*1024*1024) as File?
    checkIt(music!=null,"production music hash/length checks pass")
    checkIt(parse.invoke(ThorLauncherAudio,music)!=null,"production music PCM parser succeeds")
    val sounds=metadata.getJSONObject("sounds");val keys=sounds.keys();var cues=0
    while(keys.hasNext()) { val k=keys.next();val f=verify.invoke(ThorLauncherAudio,audio,sounds.getJSONObject(k),4L*1024*1024) as File?;checkIt(f!=null && parse.invoke(ThorLauncherAudio,f)!=null,"UI sound valid: "+k);cues++ }
    val original=root
    ThorBundledPresentation.install(this)
    checkIt(ThorBundledPresentation.root(this)==original,"repeat bootstrap retains completed revision")
    File(audio,entry.getString("file")).delete()
    ThorBundledPresentation.install(this)
    checkIt(verify.invoke(ThorLauncherAudio,audio,entry,16L*1024*1024)!=null,"missing bundled music repaired")
    result.put("status","PASS").put("installed_files",copied).put("decoded_case_disc_spine_images",decoded).put("validated_sound_cues",cues)
   } catch(e:Throwable) {result.put("status","FAIL").put("error",e.javaClass.simpleName+": "+e.message)}
   result.put("checks",checks).put("owner_app_data_accessed",false).put("audio_played",false)
   File(filesDir,"result.json").writeText(result.toString(2))
   runOnUiThread {text.text=result.toString(2)}
  }.start()
 }
}
''')
src=R/'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android'
loader=R/'build/feature-1.1/presentation-fix-2026-10-25/before/ThorBundledPresentation.kt' if a.baseline else src/'ThorBundledPresentation.kt'
classes=O/('classes-before' if a.baseline else 'classes-fixed');classes.mkdir(exist_ok=True)
dex=O/('dex-before' if a.baseline else 'dex-fixed');dex.mkdir(exist_ok=True)
stdlib=jar('org.jetbrains.kotlin','kotlin-stdlib','1.9.24')
compiler=[jar('org.jetbrains.kotlin',n,'1.9.24') for n in ('kotlin-compiler-embeddable','kotlin-script-runtime','kotlin-daemon-embeddable')]
compiler += [stdlib,jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330'),jar('org.jetbrains','annotations','13.0')]
with (O/('before-build.log' if a.baseline else 'fixed-build.log')).open('w') as log:
 run([J/'java.exe','-cp',';'.join(map(str,compiler)),'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-jvm-target','17',
     '-classpath',f'{stdlib};{AJ}','-d',classes,loader,src/'ThorLauncherArtwork.kt',src/'ThorLauncherAudio.kt',O/'R.kt',O/'PresentationCheck.kt'],stdout=log,stderr=subprocess.STDOUT)
 run([J/'jar.exe','cf',O/'classes.jar','-C',classes,'.'],stdout=log,stderr=subprocess.STDOUT)
 run([J/'java.exe','-cp',jar('com.android.tools.build','builder','8.5.2'),'com.android.tools.r8.D8','--min-api','30','--lib',AJ,'--output',dex,O/'classes.jar',stdlib],stdout=log,stderr=subprocess.STDOUT)
 run([BT/'aapt2.exe','link','-I',AJ,'--manifest',O/'AndroidManifest.xml','-o',O/'unsigned.apk'],stdout=log,stderr=subprocess.STDOUT)
 with zipfile.ZipFile(O/'unsigned.apk','a',zipfile.ZIP_DEFLATED,compresslevel=1) as z:
  for path in dex.glob('*.dex'):z.write(path,path.name)
  assets=R/'build/wiids-merge/assets'
  manifest=json.loads((assets/'presentation/manifest.json').read_text())
  for name in ['presentation/manifest.json']+[e['asset'] for e in manifest['files']]:z.write(assets/name,'assets/'+name)
 run([BT/'zipalign.exe','-f','4',O/'unsigned.apk',O/'aligned.apk'],stdout=log,stderr=subprocess.STDOUT)
 run([BT/'apksigner.bat','sign','--ks',Path.home()/'.android/debug.keystore','--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',O/'presentationtests.apk',O/'aligned.apk'],stdout=log,stderr=subprocess.STDOUT)
for name in ('unsigned.apk','aligned.apk'):(O/name).unlink()
idle()
run(adb+['install','-r',str(O/'presentationtests.apk')],capture_output=True)
run(adb+['shell','run-as',pkg,'rm','-f','files/result.json'],capture_output=True)
run(adb+['shell','am','start','-n',pkg+'/com.driftdroid.android.PresentationCheck'],capture_output=True)
for _ in range(180):
 result=subprocess.run(adb+['exec-out','run-as',pkg,'cat','files/result.json'],capture_output=True,text=True)
 if result.returncode==0:
  try:report=json.loads(result.stdout)
  except json.JSONDecodeError:pass
  else:break
 time.sleep(.5)
else:raise RuntimeError('Test result did not arrive')
(O/('before.json' if a.baseline else 'fixed.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
assert report['status']==('FAIL' if a.baseline else 'PASS')
