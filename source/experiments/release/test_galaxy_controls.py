"""Host gesture checks and ARM64 policy harness; no game/device interaction."""
from pathlib import Path
import json,subprocess
R=Path(__file__).resolve().parents[2]
O=R/'build/feature-1.1/galaxy-controls-lighting-2026-10-25/tests'
O.mkdir(parents=True,exist_ok=True)
J=R/'reference/driftdroid/android/app/src/main/java/dev/thor/galaxy/GalaxyTrackpad.java'
test=O/'GalaxyTrackpadTest.java'
test.write_text('''package dev.thor.galaxy;
import java.util.*;
public class GalaxyTrackpadTest {
 static int checks; static List<float[]> events=new ArrayList<>();
 static void ok(boolean v){checks++;if(!v)throw new AssertionError("check "+checks);}
 static GalaxyTrackpad p=new GalaxyTrackpad((x,y,phase)->events.add(new float[]{x,y,phase}));
 static float[] last(){return events.get(events.size()-1);}
 public static void main(String[] args){
  p.move(10,10,100,50);ok(events.isEmpty());
  p.begin(80,40);ok(last()[0]==0&&last()[1]==0&&last()[2]==0);
  p.move(90,45,100,50);ok(Math.abs(last()[0]-.2f)<1e-6&&Math.abs(last()[1]-.2f)<1e-6);
  p.end(95,50,100,50);ok(last()[2]==2);ok(events.get(events.size()-2)[0]==.1f);
  p.begin(5,5);ok(last()[0]==0&&last()[1]==0); // lifting/repositioning never jumps
  p.move(0,0,100,50);ok(last()[0]==-.1f&&last()[1]==-.2f);
  p.cancel();ok(last()[2]==3);int n=events.size();p.move(90,45,100,50);ok(n==events.size());
  p.begin(1,1);p.move(Float.NaN,0,100,50);ok(last()[2]==3);
  p.begin(1,1);p.move(2,2,0,50);ok(last()[2]==3);
  p.begin(0,0);p.move(100,50,100,50);ok(last()[0]==2&&last()[1]==2);
  p.cancel();System.out.println(checks+" actual Java gesture checks PASS");
 }
}''')
jdk=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')
subprocess.run([str(jdk/'javac.exe'),'-d',str(O),str(J),str(test)],check=True)
r=subprocess.run([str(jdk/'java.exe'),'-cp',str(O),'dev.thor.galaxy.GalaxyTrackpadTest'],check=True,capture_output=True,text=True)
print(r.stdout.strip())
(O/'java-result.json').write_text(json.dumps({'status':'PASS','output':r.stdout.strip()}))
