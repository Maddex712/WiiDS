"""Actual Android adapters in a disposable app with production profile paths. No owner data access."""
from pathlib import Path
import argparse, hashlib, json, subprocess, time, zipfile
R=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);a=p.parse_args()
O=R/'build/feature-1.1/dolphin-import-2026-10-25/android';O.mkdir(parents=True,exist_ok=True)
F=O.parent/'tests/fixtures';assert (F/'galaxy-be').is_file(),'Run host checks first'
SDK=R/'.tools/android-sdk';BT=SDK/'build-tools/34.0.0';AJ=SDK/'platforms/android-34/android.jar'
J=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin');C=Path.home()/'.gradle/caches/modules-2/files-2.1'
S=R/'reference/driftdroid/android/app/src/main'
def jar(g,n,v):return next((C/g/n/v).rglob('*.jar'))
def run(cmd,**kw):return subprocess.run(list(map(str,cmd)),check=True,**kw)
adb=[str(R/'.tools/platform-tools/adb.exe'),'-s',a.serial];pkg='io.github.maddex712.wiids.dolphintests'
def idle():
 state=subprocess.check_output(adb+['shell','dumpsys','activity','activities'],text=True)
 active=[s for s in state.splitlines() if 'topResumedActivity=' in s]
 assert all(any(x in s for x in ('rip.moth.cocoonshell/','io.github.maddex712.wiids/com.driftdroid.android.ModePickerActivity',pkg+'/')) for s in active),'Owner game/activity foreground; abort'
idle()
(O/'AndroidManifest.xml').write_text(f'''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="{pkg}"><uses-sdk android:minSdkVersion="30" android:targetSdkVersion="34"/>
<application android:label="WiiDS Dolphin Check" android:debuggable="true" android:allowBackup="false" android:theme="@android:style/Theme.Material.NoActionBar"><activity android:name="com.driftdroid.android.DolphinCheck" android:exported="true" android:screenOrientation="landscape"/></application></manifest>''')
(O/'TestDependencies.kt').write_text('''package com.driftdroid.android
object ThorGameSession { fun hasRetainedRuntime() = false; fun isUpdating() = false }
object ThorRetroRewindUpdateStatus { object snapshot { val running = false } }
''')
(O/'DolphinCheck.java').write_text(r'''package com.driftdroid.android;
import android.app.Activity;import android.os.Bundle;import android.widget.TextView;
import com.driftdroid.android.saves.*;import java.io.*;import java.util.*;import org.json.JSONObject;
public final class DolphinCheck extends Activity {
 public void onCreate(Bundle state){super.onCreate(state);TextView view=new TextView(this);view.setText("Checking Dolphin saves in isolated storage...");setContentView(view);
 new Thread(()->{JSONObject result=new JSONObject();try {
  File fixtures=new File(getFilesDir().getCanonicalFile(),"fixtures");fixtures.mkdirs();
  for(String name:getAssets().list("fixtures"))try(InputStream in=getAssets().open("fixtures/"+name);FileOutputStream out=new FileOutputStream(new File(fixtures,name))){out.write(DolphinSaveImport.read(in));}
  File work=new File(getFilesDir().getCanonicalFile(),"checks");work.mkdirs();
  DolphinSaveTest.deviceProfiles=new LinkedHashMap<>();
  for(FrontendGame game:FrontendGame.values())DolphinSaveTest.deviceProfiles.put(game.id,SaveManagerProfiles.INSTANCE.profile(this,game));
  SaveManagerProfiles.INSTANCE.requireStopped(this);
  ByteArrayOutputStream capture=new ByteArrayOutputStream();PrintStream old=System.out;
  try{System.setOut(new PrintStream(capture));DolphinSaveTest.main(new String[]{fixtures.getPath(),work.getPath()});}finally{System.setOut(old);}
  result.put("status","PASS").put("output",capture.toString("UTF-8")).put("profiles",8).put("scope","Production paths in isolated Android package; no owner saves or live games");
 }catch(Throwable error){try{result.put("status","FAIL").put("error",error.toString());android.util.Log.e("DolphinCheck","failed",error);}catch(Exception ignored){}}
 try(FileOutputStream out=new FileOutputStream(new File(getFilesDir(),"result.json"))){out.write(result.toString(2).getBytes("UTF-8"));}catch(Exception ignored){}
 runOnUiThread(()->view.setText(result.toString()));}).start();}
}''')
classes=O/'classes';classes.mkdir(exist_ok=True);dex=O/'dex';dex.mkdir(exist_ok=True)
stdlib=jar('org.jetbrains.kotlin','kotlin-stdlib','1.9.24')
compiler=[jar('org.jetbrains.kotlin',n,'1.9.24') for n in ('kotlin-compiler-embeddable','kotlin-script-runtime','kotlin-daemon-embeddable')]
compiler += [stdlib,jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330'),jar('org.jetbrains','annotations','13.0')]
with (O/'build.log').open('w') as log:
 sources=[S/'java/com/driftdroid/android/FrontendGame.java']+[S/'java/com/driftdroid/android/saves'/n for n in ('SaveArchive.java','GalaxySaveCodec.java','DolphinSaveImport.java')]+[O.parent/'tests/DolphinSaveTest.java']
 run([J/'javac.exe','--release','11','-classpath',AJ,'-d',classes,*sources],stdout=log,stderr=subprocess.STDOUT)
 run([J/'java.exe','-cp',';'.join(map(str,compiler)),'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-jvm-target','11','-classpath',f'{stdlib};{AJ};{classes}','-d',classes,O/'TestDependencies.kt',S/'kotlin/com/driftdroid/android/SaveManagerProfiles.kt'],stdout=log,stderr=subprocess.STDOUT)
 run([J/'javac.exe','--release','11','-classpath',f'{AJ};{stdlib};{classes}','-d',classes,O/'DolphinCheck.java'],stdout=log,stderr=subprocess.STDOUT)
 run([J/'jar.exe','cf',O/'classes.jar','-C',classes,'.'],stdout=log,stderr=subprocess.STDOUT)
 run([J/'java.exe','-cp',jar('com.android.tools.build','builder','8.5.2'),'com.android.tools.r8.D8','--min-api','30','--lib',AJ,'--output',dex,O/'classes.jar',stdlib],stdout=log,stderr=subprocess.STDOUT)
 run([BT/'aapt2.exe','link','-I',AJ,'--manifest',O/'AndroidManifest.xml','-o',O/'unsigned.apk'],stdout=log,stderr=subprocess.STDOUT)
 with zipfile.ZipFile(O/'unsigned.apk','a') as z:
  for path in dex.glob('*.dex'):z.write(path,path.name)
  for path in F.iterdir():z.write(path,'assets/fixtures/'+path.name)
 run([BT/'zipalign.exe','-f','4',O/'unsigned.apk',O/'aligned.apk'],stdout=log,stderr=subprocess.STDOUT)
 run([BT/'apksigner.bat','sign','--ks',Path.home()/'.android/debug.keystore','--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',O/'dolphintests.apk',O/'aligned.apk'],stdout=log,stderr=subprocess.STDOUT)
idle();run(adb+['install','-r',O/'dolphintests.apk'],capture_output=True)
# Production profile paths are reused within THIS disposable package. Each run
# starts fresh: prior capacity/index fixtures must not contaminate its next run.
assert pkg=='io.github.maddex712.wiids.dolphintests'
run(adb+['shell','pm','clear',pkg],capture_output=True)
run(adb+['shell','am','start','-n',pkg+'/com.driftdroid.android.DolphinCheck'],capture_output=True)
for _ in range(90):
 result=subprocess.run(adb+['exec-out','run-as',pkg,'cat','files/result.json'],capture_output=True,text=True)
 if result.returncode==0:
  try:report=json.loads(result.stdout)
  except json.JSONDecodeError:pass
  else:break
 time.sleep(.5)
else:raise RuntimeError('Android result did not arrive')
(O/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2));assert report['status']=='PASS' and '124 Dolphin import checks' in report['output']
