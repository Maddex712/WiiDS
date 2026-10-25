"""Build/run isolated Android save-storage checks. Never opens or reads WiiDS data."""
from pathlib import Path
import argparse
import subprocess
import json
import os
import zipfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/feature-1.1/thor-polish-2026-10-24/android-save-tests'
OUT.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial',required=True)
parser.add_argument('--expect-failure',action='store_true')
args=parser.parse_args()
SDK=ROOT/'.tools/android-sdk';TOOLS=SDK/'build-tools/34.0.0';ANDROID=SDK/'platforms/android-34/android.jar'
JDK=Path(os.environ.get('JAVA_HOME','C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot'))/'bin'
CACHE=Path.home()/'.gradle/caches/modules-2/files-2.1'
def jar(group,name,version):return next((CACHE/group/name/version).rglob('*.jar'))
def run(command,**kwargs):return subprocess.run(list(map(str,command)),check=True,**kwargs)

(OUT/'AndroidManifest.xml').write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="io.github.maddex712.wiids.savetests">
<uses-sdk android:minSdkVersion="30" android:targetSdkVersion="34"/>
<application android:label="WiiDS Save Tests" android:debuggable="true" android:allowBackup="false" android:theme="@android:style/Theme.Material.NoActionBar">
<activity android:name="com.driftdroid.android.SaveStorageCheck" android:exported="true" android:screenOrientation="landscape"/>
</application></manifest>''',encoding='utf-8')
(OUT/'TestDependencies.kt').write_text('''package com.driftdroid.android
object ThorGameSession { fun hasRetainedRuntime() = false; fun isUpdating() = false }
object ThorRetroRewindUpdateStatus { object snapshot { val running = false } }
''',encoding='utf-8')
(OUT/'SaveStorageCheck.java').write_text(r'''package com.driftdroid.android;
import android.app.Activity;
import android.os.Bundle;
import android.system.Os;
import android.util.Log;
import android.widget.TextView;
import com.driftdroid.android.saves.SaveArchive;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import org.json.*;

public class SaveStorageCheck extends Activity {
 private static final String TAG="WiiDsSaveTest";
 private final JSONArray results=new JSONArray(); private boolean passed=true; private int count;
 private interface Attempt {void run()throws Exception;}
 private void test(String name,Attempt action){
  try{action.run();results.put(new JSONObject().put("name",name).put("status","PASS"));count++;}
  catch(Exception e){passed=false;try{results.put(new JSONObject().put("name",name).put("status","FAIL").put("error",e.toString()));}catch(Exception ignored){}Log.e(TAG,name,e);}
 }
 private void check(boolean value)throws Exception{if(!value)throw new IOException("Check failed");}
 private void reject(Attempt a)throws Exception{try{a.run();}catch(IOException expected){return;}throw new IOException("Unsafe path accepted");}
 private void put(File f,String text)throws Exception{f.getParentFile().mkdirs();try(FileOutputStream out=new FileOutputStream(f)){out.write(text.getBytes(StandardCharsets.UTF_8));out.getFD().sync();}}
 private String text(File f)throws Exception{return new String(Files.readAllBytes(f.toPath()),StandardCharsets.UTF_8);}
 @Override public void onCreate(Bundle state){super.onCreate(state);TextView view=new TextView(this);view.setText("Checking isolated save storage…");setContentView(view);
  new Thread(()->{try{checks();}catch(Exception e){passed=false;Log.e(TAG,"test setup",e);}try{
   JSONObject r=new JSONObject().put("status",passed?"PASS":"FAIL").put("checks",count).put("results",results);
   put(new File(getFilesDir(),"result.json"),r.toString(2));Log.i(TAG,"DONE "+r);
   runOnUiThread(()->view.setText((passed?"PASS ":"FAIL ")+count+" isolated save-storage checks"));
  }catch(Exception e){Log.e(TAG,"result",e);}}).start();}
 private void checks()throws Exception{
  Log.i(TAG,"raw files="+getFilesDir()+" canonical="+getFilesDir().getCanonicalPath());
  Log.i(TAG,"raw external="+getExternalFilesDir(null)+" canonical="+getExternalFilesDir(null).getCanonicalPath());
  for(File base:new File[]{getFilesDir(),getExternalFilesDir(null)})for(File p=base;p!=null;p=p.getParentFile())
   if(Files.isSymbolicLink(p.toPath()))Log.i(TAG,"SYMLINK ancestor "+p);
  test("production work directory",()->SaveArchive.mkdir(SaveManagerProfiles.INSTANCE.work(this)));
  test("all eight production profile paths",()->{for(FrontendGame g:FrontendGame.values())SaveManagerProfiles.INSTANCE.profile(this,g);});
  test("stopped-process check",()->SaveManagerProfiles.INSTANCE.requireStopped(this));
  File internal=new File(getFilesDir().getCanonicalFile(),"fixture-"+UUID.randomUUID());
  File external=new File(getExternalFilesDir(null).getCanonicalFile(),"fixture-"+UUID.randomUUID());
  internal.mkdirs();external.mkdirs();
  LinkedHashMap<String,File> roots=new LinkedHashMap<>();roots.put("main",new File(internal,"nand"));roots.put("mirror",new File(external,"saves"));
  File work=new File(internal,"work"),archive=new File(internal,"backup.zip"),previous=new File(internal,"previous.zip");
  final SaveArchive.Profile[] profile=new SaveArchive.Profile[1];
  test("internal/external profile",()->profile[0]=new SaveArchive.Profile("galaxy",roots));
  if(profile[0]!=null){
   test("write fixture only",()->{put(new File(roots.get("main"),"GameData.bin"),"internal original");put(new File(roots.get("mirror"),"GameData.bin"),"mirror original");});
   test("backup actual Android filesystems",()->SaveArchive.backup(profile[0],archive,false));
   test("verify Android archive",()->check(SaveArchive.verify(profile[0],archive,null).files.size()==2));
   test("restore across internal/external roots",()->{put(new File(roots.get("main"),"GameData.bin"),"internal new");put(new File(roots.get("mirror"),"GameData.bin"),"mirror new");SaveArchive.restore(profile[0],archive,work,previous);});
   test("restored contents",()->{check(text(new File(roots.get("main"),"GameData.bin")).equals("internal original"));check(text(new File(roots.get("mirror"),"GameData.bin")).equals("mirror original"));});
   test("previous Android save retained",()->check(SaveArchive.verify(profile[0],previous,null).files.size()==2));
   test("wrong mod rejected",()->reject(()->SaveArchive.verify(new SaveArchive.Profile("kaizo",roots),archive,null)));
   test("real symlink inside save refused",()->{File link=new File(roots.get("main"),"link");Os.symlink(new File(roots.get("main"),"GameData.bin").getPath(),link.getPath());try{reject(()->SaveArchive.inventory(profile[0]));}finally{Files.delete(link.toPath());}});
  }
 }
}
''',encoding='utf-8')
source=ROOT/'reference/driftdroid/android/app/src/main'
classes=OUT/'classes';classes.mkdir(exist_ok=True);dex=OUT/'dex';dex.mkdir(exist_ok=True)
stdlib=jar('org.jetbrains.kotlin','kotlin-stdlib','1.9.24')
compiler=[jar('org.jetbrains.kotlin',n,'1.9.24') for n in ('kotlin-compiler-embeddable','kotlin-script-runtime','kotlin-daemon-embeddable')]
compiler += [stdlib,jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330'),jar('org.jetbrains','annotations','13.0')]
adb=[str(ROOT/'.tools/platform-tools/adb.exe'),'-s',args.serial]
activity=subprocess.check_output(adb+['shell','dumpsys','activity','activities'],text=True)
assert not any('topResumedActivity=' in s and any(t in s for t in ['GameActivity','MainActivity','GalaxyActivity','MsmbwiiActivities$Game']) and 'io.github.maddex712.wiids/' in s for s in activity.splitlines()),'Do not start test package over a game'
with (OUT/'build.log').open('w',encoding='utf-8') as log:
 run([JDK/'javac.exe','--release','17','-classpath',ANDROID,'-d',classes,
     source/'java/com/driftdroid/android/FrontendGame.java',source/'java/com/driftdroid/android/saves/SaveArchive.java'],stdout=log,stderr=subprocess.STDOUT)
 run([JDK/'java.exe','-cp',';'.join(map(str,compiler)),'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-jvm-target','17',
     '-classpath',f'{stdlib};{ANDROID};{classes}','-d',classes,OUT/'TestDependencies.kt',source/'kotlin/com/driftdroid/android/SaveManagerProfiles.kt'],stdout=log,stderr=subprocess.STDOUT)
 run([JDK/'javac.exe','--release','17','-classpath',f'{ANDROID};{stdlib};{classes}','-d',classes,OUT/'SaveStorageCheck.java'],stdout=log,stderr=subprocess.STDOUT)
 run([JDK/'jar.exe','cf',OUT/'classes.jar','-C',classes,'.'],stdout=log,stderr=subprocess.STDOUT)
 # Use the same D8 bundled with AGP 8.5.2 as the production build. The SDK
 # standalone 8.2 D8 crashes on this javac-21-produced enum.
 run([JDK/'java.exe','-cp',jar('com.android.tools.build','builder','8.5.2'),'com.android.tools.r8.D8',
     '--min-api','30','--lib',ANDROID,'--output',dex,OUT/'classes.jar',stdlib],stdout=log,stderr=subprocess.STDOUT)
 run([TOOLS/'aapt2.exe','link','-I',ANDROID,'--manifest',OUT/'AndroidManifest.xml','-o',OUT/'unsigned.apk'],stdout=log,stderr=subprocess.STDOUT)
 with zipfile.ZipFile(OUT/'unsigned.apk','a') as z:
  for path in dex.glob('*.dex'):z.write(path,path.name)
 run([TOOLS/'zipalign.exe','-f','4',OUT/'unsigned.apk',OUT/'aligned.apk'],stdout=log,stderr=subprocess.STDOUT)
 run([TOOLS/'apksigner.bat','sign','--ks',Path.home()/'.android/debug.keystore','--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',OUT/'savetests.apk',OUT/'aligned.apk'],stdout=log,stderr=subprocess.STDOUT)
run(adb+['install','-r',str(OUT/'savetests.apk')],capture_output=True)
run(adb+['shell','run-as','io.github.maddex712.wiids.savetests','rm','-f','files/result.json'],capture_output=True)
run(adb+['shell','am','start','-n','io.github.maddex712.wiids.savetests/com.driftdroid.android.SaveStorageCheck'],capture_output=True)
# Poll only this test app's private result. No owner app data is read.
import time
for _ in range(25):
 result=subprocess.run(adb+['exec-out','run-as','io.github.maddex712.wiids.savetests','cat','files/result.json'],capture_output=True,text=True)
 if result.returncode==0:
  try: report=json.loads(result.stdout)
  except json.JSONDecodeError: pass
  else: break
 time.sleep(.2)
else:raise RuntimeError('Test result did not arrive')
diagnostic=subprocess.check_output(adb+['logcat','-d','-t','180','-s','WiiDsSaveTest:I'],text=True)
(OUT/('baseline-log.txt' if args.expect_failure else 'fixed-log.txt')).write_text(diagnostic,encoding='utf-8')
(OUT/('baseline.json' if args.expect_failure else 'verification.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
print('\n'.join(x for x in diagnostic.splitlines() if 'SYMLINK' in x or 'raw files=' in x or 'raw external=' in x))
assert (report['status']!='PASS') if args.expect_failure else (report['status']=='PASS')
