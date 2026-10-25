"""Exercise the actual five-second hint policy without Android or a device."""
from pathlib import Path
import subprocess,json
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/nightly-1.1-2026-10-25/hint-tests';O.mkdir(parents=True,exist_ok=True)
test=O/'GalaxyTouchHintTest.java'
test.write_text('''package dev.thor.galaxy;
public class GalaxyTouchHintTest {
 static int count;static void ok(boolean v){count++;if(!v)throw new AssertionError("check "+count);}
 public static void main(String[] args){
  GalaxyTouchHint h=new GalaxyTouchHint();ok(!h.visible());
  h.update(1000,false);ok(!h.visible());h.update(60000,false);ok(!h.visible());
  h.update(60001,true);ok(h.visible());h.update(65000,true);ok(h.visible());
  h.update(65001,true);ok(!h.visible());h.update(80000,true);ok(!h.visible());
  h.update(90000,false);h.update(100000,true);ok(!h.visible()); // no restart at next stage
  GalaxyTouchHint p=new GalaxyTouchHint();p.update(0,true);p.update(2000,false);ok(!p.visible());
  p.update(80000,true);ok(p.visible());p.update(82999,true);ok(p.visible());p.update(83000,true);ok(!p.visible());
  GalaxyTouchHint menu=new GalaxyTouchHint();menu.update(0,false);menu.update(10000,false);ok(!menu.visible());
  menu.update(20000,true);ok(menu.visible());menu.update(21000,false);ok(!menu.visible());
  menu.update(50000,true);menu.update(53999,true);ok(menu.visible());menu.update(54000,true);ok(!menu.visible());
  GalaxyTouchHint clock=new GalaxyTouchHint();clock.update(100,true);clock.update(50,true);ok(clock.visible());
  clock.update(5050,true);ok(!clock.visible());
  System.out.println(count+" actual hint timing/lifecycle checks PASS");
 }
}''')
jdk=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')
source=R/'reference/driftdroid/android/app/src/main/java/dev/thor/galaxy/GalaxyTouchHint.java'
subprocess.run([str(jdk/'javac.exe'),'-d',str(O),str(source),str(test)],check=True)
r=subprocess.run([str(jdk/'java.exe'),'-cp',str(O),'dev.thor.galaxy.GalaxyTouchHintTest'],check=True,capture_output=True,text=True)
result={'status':'PASS','output':r.stdout.strip(),'scope':'Actual policy class; real first-play appearance and rendering separate'}
(O/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
