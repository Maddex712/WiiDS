"""Build an isolated, non-debuggable Android probe for Galaxy's address window."""
from pathlib import Path
import os
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/galaxy-thor/memory-probe'
SDK = ROOT / '.tools/android-sdk'
JDK = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot')
BT = SDK / 'build-tools/34.0.0'
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'classes').mkdir(exist_ok=True)
(OUT / 'dex').mkdir(exist_ok=True)
env = dict(os.environ, JAVA_HOME=str(JDK))
env['PATH'] = str(JDK / 'bin') + os.pathsep + env['PATH']
def run(args):
    subprocess.run([str(a) for a in args], env=env, check=True)

(OUT / 'AndroidManifest.xml').write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="dev.thor.galaxyprobe" android:versionCode="1" android:versionName="1"><uses-sdk android:minSdkVersion="30" android:targetSdkVersion="34"/><application android:label="Galaxy memory check" android:debuggable="false" android:extractNativeLibs="true" android:theme="@android:style/Theme.Material.NoActionBar"><activity android:name=".Probe" android:exported="true"/></application></manifest>''')
(OUT / 'Probe.java').write_text('''package dev.thor.galaxyprobe;
public class Probe extends android.app.Activity {
 static { System.loadLibrary("galaxyprobe"); }
 private static native String check();
 public void onCreate(android.os.Bundle b) { super.onCreate(b); String r=check();
 for(String l: r.split(String.valueOf((char) 10))) android.util.Log.i("GalaxyMemoryProbe",l); android.widget.TextView t=new android.widget.TextView(this); t.setText(r); setContentView(t); }
}''')
(OUT / 'probe.c').write_text(r'''#include <jni.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
// Version 2 (2026-10-22): the whole map below 4 GB and its free gaps, for relocating Galaxy's
// window into a zygote-forked (non-debuggable) process such as WiiDS's.
JNIEXPORT jstring JNICALL Java_dev_thor_galaxyprobe_Probe_check(JNIEnv* env,jclass cls) {
 static char result[65536]; size_t n=0; unsigned long long prev=0x10000;
 FILE* f=fopen("/proc/self/maps","r");char line[512];
 while(f&&fgets(line,sizeof line,f)){unsigned long long lo,hi;char perms[8]={0};char name[256]={0};
  if(sscanf(line,"%llx-%llx %7s %*s %*s %*s %255[^\n]",&lo,&hi,perms,name)<3)continue;
  if(lo>=0x100000000ull)break;
  if(lo>prev&&lo-prev>=(16ull<<20)&&n<sizeof result-200)n+=snprintf(result+n,sizeof result-n,"GAP %llx-%llx %llu MB\n",prev,lo,(lo-prev)>>20);
  if(n<sizeof result-300)n+=snprintf(result+n,sizeof result-n,"MAP %llx-%llx %s %s\n",lo,hi,perms,name);
  if(hi>prev)prev=hi;}
 if(prev<0x100000000ull)n+=snprintf(result+n,sizeof result-n,"GAP %llx-100000000 %llu MB\n",prev,(0x100000000ull-prev)>>20);
 if(f)fclose(f);return (*env)->NewStringUTF(env,result);
}''')
clang = SDK / 'ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang.cmd'
run([clang, '-shared', '-fPIC', OUT / 'probe.c', '-o', OUT / 'libgalaxyprobe.so'])
run([BT / 'aapt2.exe', 'link', '--manifest', OUT / 'AndroidManifest.xml', '-I', SDK / 'platforms/android-34/android.jar', '-o', OUT / 'base.apk'])
run([JDK / 'bin/javac.exe', '-source', '8', '-target', '8', '-cp', SDK / 'platforms/android-34/android.jar', '-d', OUT / 'classes', OUT / 'Probe.java'])
run([BT / 'd8.bat', '--min-api', '30', '--lib', SDK / 'platforms/android-34/android.jar', '--output', OUT / 'dex', OUT / 'classes/dev/thor/galaxyprobe/Probe.class'])
with zipfile.ZipFile(OUT / 'base.apk', 'a', compression=zipfile.ZIP_DEFLATED) as apk:
    apk.write(OUT / 'dex/classes.dex', 'classes.dex')
    apk.write(OUT / 'libgalaxyprobe.so', 'lib/arm64-v8a/libgalaxyprobe.so')
run([BT / 'zipalign.exe', '-f', '4', OUT / 'base.apk', OUT / 'aligned.apk'])
run([BT / 'apksigner.bat', 'sign', '--ks', Path.home() / '.android/debug.keystore', '--ks-pass', 'pass:android', '--out', OUT / 'probe.apk', OUT / 'aligned.apk'])
print(OUT / 'probe.apk')
