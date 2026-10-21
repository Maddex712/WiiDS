"""Exercise production timing at real panel rates, stalls and independent snapshot rates."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
JAVA=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')
SOURCE=ROOT/'reference/driftdroid/android/app/src/main/java/dev/chasem/smslauncher/ThorSunshineMotion.java'
HARNESS=r'''
package dev.chasem.smslauncher;
public class MotionTest {
 static int checks;
 static void check(boolean ok,String message){checks++;if(!ok)throw new AssertionError(message);}
 static int frames(double hz,int seconds){
  ThorSunshineMotion m=new ThorSunshineMotion();int draws=0;
  for(int i=0;i<(int)(hz*seconds);i++)if(m.frameDue(1000000000L+Math.round(i*1e9/hz)))draws++;
  return draws;
 }
 public static void main(String[] args){
  for(double hz:new double[]{59.94,60,90,120,144}){
   int n=frames(hz,10);
   check(n>=598&&n<=601,"60 Hz target on "+hz+" Hz panel: "+n);
  }
  check(frames(30,10)==300,"cannot invent panel refreshes");
  ThorSunshineMotion m=new ThorSunshineMotion();
  check(m.frameDue(1000000000L),"first frame");
  check(!m.frameDue(1000000000L),"no duplicate same-vsync frame");
  check(m.frameDue(31000000000L),"resume after long stall");
  check(!m.frameDue(31000000001L),"no catchup burst");
  m.reset();check(m.frameDue(31000000001L),"reattach starts fresh");
  float previous=-1;
  for(int i=0;i<7200;i++){
   float p=ThorSunshineMotion.cloudOffset(i/60.0,8f,2480f);
   if(previous>=0){float delta=(p-previous+2480f)%2480f;check(delta<.15f,"cloud step/wrap is smooth");}
   previous=p;
  }
  check(ThorSunshineMotion.cloudOffset(20,8,2480)>ThorSunshineMotion.cloudOffset(10,8,2480),"gameplay sky actually moves");
  float a=0,b=0,c=0;
  for(int i=0;i<30;i++)a=ThorSunshineMotion.follow(a,1,1f/30,.065f);
  for(int i=0;i<60;i++)b=ThorSunshineMotion.follow(b,1,1f/60,.065f);
  for(int i=0;i<120;i++)c=ThorSunshineMotion.follow(c,1,1f/120,.065f);
  check(Math.abs(a-b)<.00001&&Math.abs(b-c)<.00001,"fade duration independent of display rate");
  check(ThorSunshineMotion.follow(.3f,.9f,-1,.055f)==.3f,"no backwards-time jump");
  System.out.println(checks+" production-motion checks passed");
 }
}
'''
def main():
    with tempfile.TemporaryDirectory(prefix='wiids-motion-') as folder:
        folder=Path(folder); test=folder/'MotionTest.java';test.write_text(HARNESS,encoding='utf-8')
        subprocess.run([str(JAVA/'javac.exe'),'-d',str(folder),str(SOURCE),str(test)],check=True)
        subprocess.run([str(JAVA/'java.exe'),'-cp',str(folder),'dev.chasem.smslauncher.MotionTest'],check=True)
    screen=(SOURCE.parent/'ThorSunshineScreen.java').read_text(encoding='utf-8')
    assert 'postInvalidateDelayed(33)' not in screen
    assert 'removeFrameCallback(frame)' in screen
    assert 'PHOTO_W = 640, PHOTO_H = 360' in screen
    assert 'SKY_W = 320, SKY_H = 180' in screen
    print('Screen pacing/lifecycle and separate photo contract checks passed')
if __name__=='__main__':main()
