"""Execute the actual menu touch handler: first-tap activation and canceled gestures."""
from pathlib import Path
import json,re,subprocess
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/nightly-1.1-2026-10-25/menu-touch-tests';O.mkdir(parents=True,exist_ok=True)
source=R/'reference/driftdroid/android/app/src/main/java/dev/thor/galaxy/GalaxySettingsMenu.java'
method=re.search(r'    @Override public boolean onTouchEvent\(MotionEvent e\) \{.*?\n    \}\n',source.read_text(),re.S)[0].replace('@Override ','')
stub='''class MotionEvent {
 static final int ACTION_DOWN=0,ACTION_UP=1,ACTION_MOVE=2,ACTION_CANCEL=3,ACTION_POINTER_DOWN=5;
 int action,count;float x,y;MotionEvent(int a,float x,float y,int n){action=a;this.x=x;this.y=y;count=n;}
 int getActionMasked(){return action;}int getPointerCount(){return count;}float getX(){return x;}float getY(){return y;}
}
class RectF {float a,b,c,d;RectF(float a,float b,float c,float d){this.a=a;this.b=b;this.c=c;this.d=d;}
 boolean contains(float x,float y){return x>=a&&x<c&&y>=b&&y<d;}}
class Row {}
public class GalaxyMenuTouchTest {
 static final float W=1920,H=1080;int focus,touchRow=-1;boolean touchClose;
 int selected=-1,selectCount,backCount;Row[] items={new Row(),new Row(),new Row(),new Row()};
 float getWidth(){return W;}float getHeight(){return H;}Row[] rows(){return items;}
 RectF rowRect(int i){return new RectF(100,200+i*100,1900,290+i*100);}
 RectF closeRect(){return new RectF(1520,70,1790,134);}void invalidate(){}
 void select(){selected=focus;selectCount++;}void back(){backCount++;}
'''
test='''static int checks;static void ok(boolean v){checks++;if(!v)throw new AssertionError("check "+checks);}
 void event(int a,float x,float y){onTouchEvent(new MotionEvent(a,x,y,1));}
 void tap(float x,float y){event(0,x,y);event(1,x,y);}
 public static void main(String[] args){
  GalaxyMenuTouchTest m=new GalaxyMenuTouchTest();m.tap(500,350);ok(m.selectCount==1&&m.selected==1);
  m.tap(500,450);ok(m.selectCount==2&&m.selected==2);
  m.event(0,500,250);m.event(2,500,550);m.event(1,500,550);ok(m.selectCount==2);
  m.event(0,10,900);m.event(1,500,450);ok(m.selectCount==2);
  m.event(0,500,550);m.event(3,500,550);m.event(1,500,550);ok(m.selectCount==2);
  m.event(0,500,550);m.onTouchEvent(new MotionEvent(5,500,550,2));m.event(1,500,550);ok(m.selectCount==2);
  m.tap(1600,100);ok(m.backCount==1);
  m.event(0,1600,100);m.event(1,500,550);ok(m.backCount==1&&m.selectCount==2);
  m.tap(500,550);ok(m.selectCount==3&&m.selected==3);
  System.out.println(checks+" actual menu touch checks PASS");
 }
}'''
file=O/'GalaxyMenuTouchTest.java';file.write_text(stub+method+test)
jdk=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')
subprocess.run([str(jdk/'javac.exe'),'-d',str(O),str(file)],check=True)
r=subprocess.run([str(jdk/'java.exe'),'-cp',str(O),'GalaxyMenuTouchTest'],capture_output=True,text=True)
print(r.stdout+r.stderr);assert r.returncode==0,'Menu first tap/cancellation behavior failed'
(O/'result.json').write_text(json.dumps({'status':'PASS','checks':9,'scope':'Actual production touch handler extracted verbatim with layout/action test doubles'},indent=2))
