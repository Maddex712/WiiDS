"""Run the real NSMB Android motion/settings classes with controlled sensor samples."""
from pathlib import Path
import os
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / 'reference/driftdroid/android/app/src/main/java/com/wither/nsmbw'
OUT = ROOT / 'build/feature-1.1/motion-tests'

STUBS = {
 'android/content/SharedPreferences.java': '''package android.content; public interface SharedPreferences { boolean getBoolean(String k,boolean d); int getInt(String k,int d); float getFloat(String k,float d); Editor edit(); interface Editor { Editor putBoolean(String k,boolean v); Editor putInt(String k,int v); Editor putFloat(String k,float v); boolean commit(); }}''',
 'android/content/Context.java': '''package android.content; public abstract class Context { public static final String SENSOR_SERVICE="sensor"; public static final int MODE_PRIVATE=0; public abstract Object getSystemService(String s); public abstract SharedPreferences getSharedPreferences(String s,int m); }''',
 'android/app/Activity.java': '''package android.app; public abstract class Activity extends android.content.Context { public abstract android.view.Display getDisplay(); }''',
 'android/view/Display.java': '''package android.view; public class Display { public int rotation; public int getRotation(){return rotation;} }''',
 'android/view/Surface.java': '''package android.view; public class Surface { public static final int ROTATION_0=0,ROTATION_90=1,ROTATION_180=2,ROTATION_270=3; }''',
 'android/os/SystemClock.java': '''package android.os; public class SystemClock { public static long now=1000; public static long uptimeMillis(){return now;} }''',
 'android/hardware/Sensor.java': '''package android.hardware; public class Sensor { public static final int TYPE_ACCELEROMETER=1; }''',
 'android/hardware/SensorEvent.java': '''package android.hardware; public class SensorEvent { public float[] values; public SensorEvent(float... v){values=v;} }''',
 'android/hardware/SensorEventListener.java': '''package android.hardware; public interface SensorEventListener {void onSensorChanged(SensorEvent e); void onAccuracyChanged(Sensor s,int a);}''',
 'android/hardware/SensorManager.java': '''package android.hardware; public class SensorManager { public static final int SENSOR_DELAY_GAME=1; public boolean present=true,accept=true; public int registrations; public Sensor getDefaultSensor(int t){return present?new Sensor():null;} public boolean registerListener(SensorEventListener l,Sensor s,int d){registrations++;return accept;} public void unregisterListener(SensorEventListener l){} }''',
 'android/util/Log.java': '''package android.util; public class Log {public static int i(String a,String b){return 0;} public static int w(String a,String b){return 0;}}''',
 'com/wither/nsmbw/NsmbBuild.java': '''package com.wither.nsmbw; class NsmbBuild {static final boolean THOR_BUILD=true;}''',
 'com/wither/nsmbw/NsmbVariant.java': '''package com.wither.nsmbw; class NsmbVariant {String id="nsmbw";static NsmbVariant current(){return new NsmbVariant();}}''',
 'com/wither/nsmbw/MotionTest.java': '''package com.wither.nsmbw;
import android.content.*; import android.hardware.*; import android.view.*;
public class MotionTest {
 static int checks; static void check(boolean b,String why){checks++;if(!b)throw new AssertionError(why);}
 static class Prefs implements SharedPreferences,SharedPreferences.Editor {
  java.util.Map<String,Object> m=new java.util.HashMap<>();
  public boolean getBoolean(String k,boolean d){return m.containsKey(k)?(Boolean)m.get(k):d;}
  public int getInt(String k,int d){return m.containsKey(k)?(Integer)m.get(k):d;}
  public float getFloat(String k,float d){return m.containsKey(k)?(Float)m.get(k):d;}
  public SharedPreferences.Editor edit(){return this;} public SharedPreferences.Editor putBoolean(String k,boolean v){m.put(k,v);return this;}
  public SharedPreferences.Editor putInt(String k,int v){m.put(k,v);return this;} public SharedPreferences.Editor putFloat(String k,float v){m.put(k,v);return this;} public boolean commit(){return true;}
 }
 static class Host extends android.app.Activity { Prefs p=new Prefs();SensorManager s=new SensorManager();Display d=new Display();public Object getSystemService(String x){return s;}public SharedPreferences getSharedPreferences(String x,int mode){return p;}public Display getDisplay(){return d;} }
 static class Listener implements MotionInput.Listener { float tilt;boolean shake;int calls;public void onMotion(float t,boolean s){tilt=t;shake=s;calls++;} }
 static void sample(MotionInput motion,float... values){motion.onSensorChanged(new SensorEvent(values));}
 public static void main(String[] args){
  Host h=new Host(); AppSettings settings=new AppSettings(h);
  check(settings.motionTilt(),"Thor tilt must default ON");
  settings.setMotionTilt(false);check(!settings.motionTilt(),"explicit tilt off must survive");
  settings.setMotionTilt(true);check(settings.motionTilt(),"explicit tilt on");
  check(settings.motionShake(),"physical shake must default ON on Thor");
  settings.setMotionShake(false);check(!settings.motionShake(),"explicit shake off must survive");
  settings.setMotionShake(true);check(settings.motionShake(),"explicit shake on");
  for(int rot=0;rot<4;rot++) {
   Host a=new Host();a.d.rotation=rot;Listener l=new Listener();MotionInput m=new MotionInput(a,l);m.start(true,false);
   float[][] neutral={{0,9.8f},{9.8f,0},{0,-9.8f},{-9.8f,0}};
   sample(m,neutral[rot][0],neutral[rot][1],0);check(Math.abs(l.tilt)<.01,"neutral rotation "+rot);
   // A clockwise 30-degree rotation in screen coordinates.
   float[][] right={{-4.9f,8.487f},{8.487f,4.9f},{4.9f,-8.487f},{-8.487f,-4.9f}};
   for(int i=0;i<80;i++)sample(m,right[rot][0],right[rot][1],0);
   check(l.tilt>.98,"right tilt rotation "+rot);
   for(int i=0;i<80;i++)sample(m,-right[rot][0]+2*neutral[rot][0]*.866f,-right[rot][1]+2*neutral[rot][1]*.866f,0);
   check(l.tilt<-.98,"left tilt rotation "+rot);
   m.stop();check(l.tilt==0&&!l.shake,"stop clears input");int calls=l.calls;
   sample(m,right[rot][0],right[rot][1],0);check(l.calls==calls,"queued sample ignored after stop");
  }
  Listener l=new Listener(); MotionInput m=new MotionInput(h,l);m.start(true,true);sample(m,0,9.8f,0);sample(m,20,9.8f,0);check(l.shake,"jolt produces shake");
  m.stop();m.start(true,true);sample(m,0,9.8f,0);check(!l.shake,"restart must not replay stale shake");
  for(int axis=0;axis<3;axis++)for(int sign:new int[]{-1,1}) {
   m.start(true,true);sample(m,0,9.8f,0);
   float[] impulse={0,9.8f,0};impulse[axis]+=sign*20;
   sample(m,impulse);check(l.shake,"shake direction axis="+axis+" sign="+sign);
   android.os.SystemClock.now+=100;sample(m,0,9.8f,0);check(l.shake,"short impulse is held for the game's reads");
   android.os.SystemClock.now+=81;sample(m,0,9.8f,0);check(!l.shake,"shake expires after the hold");
   sample(m,impulse);check(l.shake,"a second shake triggers again");
   m.stop();check(!l.shake,"stopping clears a held shake");
  }
  m.start(true,true);sample(m,0,9.8f,0);
  for(int i=0;i<80;i++)sample(m,-4.9f,8.487f,0);
  check(!l.shake,"normal tilting does not trigger shake");
  sample(m,-4.9f,8.487f,20);check(l.shake&&Float.isFinite(l.tilt),"tilt and shake coexist");
  m.start(true,false);sample(m,0,9.8f,0);sample(m,20,9.8f,0);check(!l.shake,"shake off suppresses physical jolts");
  int count=l.calls;sample(m,Float.NaN,9.8f,0);sample(m,1);check(l.calls==count,"invalid samples ignored");
  m.stop();h.s.accept=false;m.start(true,true);count=l.calls;sample(m,20,9.8f,0);check(l.calls==count,"failed registration stays inactive");
  System.out.println("PASS "+checks+" motion/settings checks");
 }
}'''
}

class MotionTests(unittest.TestCase):
 def test_actual_motion_and_settings(self):
    jdk = Path(os.environ.get('JAVA_HOME', 'C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot')) / 'bin'
    javac = shutil.which('javac') or str(jdk / 'javac.exe')
    java = shutil.which('java') or str(jdk / 'java.exe')
    OUT.mkdir(parents=True, exist_ok=True)
    files=[]
    for name, data in STUBS.items():
        path=OUT/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(data,encoding='utf-8');files.append(str(path))
    files += [str(JAVA/name) for name in ('AppSettings.java','MotionInput.java')]
    compiled=subprocess.run([javac,'-d',str(OUT),*files],capture_output=True,text=True)
    self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
    result=subprocess.run([java,'-cp',str(OUT),'com.wither.nsmbw.MotionTest'],capture_output=True,text=True)
    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    print(result.stdout.strip())

if __name__=='__main__': unittest.main()
