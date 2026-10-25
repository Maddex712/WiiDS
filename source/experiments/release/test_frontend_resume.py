"""Run production task-selection policy against game/mod and stale-task fixtures."""
from pathlib import Path
import os,subprocess,json
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/thor-polish-2026-10-24/frontend-resume-tests';O.mkdir(parents=True,exist_ok=True)
J=Path(os.environ.get('JAVA_HOME','C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot'))/'bin'
src=r"""
package com.driftdroid.android;
import java.util.*;
public class FrontendResumeTest {
 static int checks;
 static void yes(boolean value,String why){checks++;if(!value)throw new AssertionError(why);}
 public static void main(String[] args){
  String owner="io.github.maddex712.wiids";
  FrontendGame[] games={FrontendGame.NSMB,FrontendGame.MSMBWII,FrontendGame.SUNSHINE,FrontendGame.ECLIPSE,FrontendGame.GALAXY,FrontendGame.KAIZO};
  String[] processes={":nsmbw_game",":msmbwii_game",":sms_game",":eclipse_game",":galaxy_game",":galaxy_kaizo_game"};
  String[] activities={"com.wither.nsmbw.GameActivity","com.wither.nsmbw.MsmbwiiActivities$Game","dev.chasem.smslauncher.GameActivity","dev.chasem.eclipselauncher.GameActivity","dev.thor.galaxy.GalaxyActivity","dev.thor.galaxy.KaizoActivities$Game"};
  for(int i=0;i<games.length;i++){
   String process=owner+processes[i];
   FrontendResume.Task t=new FrontendResume.Task(42,owner,activities[i]);
   List<String> running=List.of(owner,process,"unrelated:nsmbw_game");
   yes(FrontendResume.isRuntimeProcess(owner,process),"Engine identity");
   yes(Integer.valueOf(42).equals(FrontendResume.existingTask(games[i],owner,running,List.of(t))),"Same game resumes existing task");
   for(FrontendGame other:FrontendGame.values())if(other!=games[i])yes(FrontendResume.existingTask(other,owner,running,List.of(t))==null,"Other original/mod cannot replace game");
   yes(FrontendResume.existingTask(games[i],owner,List.of(),List.of(t))==null,"Stale task without process");
   yes(FrontendResume.existingTask(games[i],owner,running,List.of())==null,"Process without task never creates SDL");
   yes(FrontendResume.existingTask(games[i],owner,running,List.of(new FrontendResume.Task(42,"other",activities[i])))==null,"Foreign task refused");
   yes(FrontendResume.existingTask(games[i],owner,running,List.of(new FrontendResume.Task(42,owner,"com.driftdroid.android.ModePickerActivity")))==null,"Background runtime beneath library refused");
   yes(FrontendResume.existingTask(games[i],owner,running,List.of(t,t))==null,"Duplicate tasks refused");
   yes(FrontendResume.existingTask(games[i],owner,List.of(process,owner+processes[(i+1)%games.length]),List.of(t))==null,"Multiple runtimes refused");
  }
  yes(!FrontendResume.isRuntimeProcess(owner,owner+":galaxy_import"),"Importer is not a resumable runtime");
  yes(!FrontendResume.isRuntimeProcess(owner,"other:nsmbw_game"),"Foreign process ignored");
  yes(!FrontendResume.isRuntimeProcess(owner,owner),"Retained main-process MKW uses existing launch path");
  System.out.println("PASS "+checks+" production frontend resume checks");
 }
}
"""
(O/'FrontendResumeTest.java').write_text(src)
S=R/'reference/driftdroid/android/app/src/main/java/com/driftdroid/android'
subprocess.run([str(J/'javac.exe'),'--release','17','-d',str(O),str(S/'FrontendGame.java'),str(S/'FrontendResume.java'),str(O/'FrontendResumeTest.java')],check=True)
p=subprocess.run([str(J/'java.exe'),'-cp',str(O),'com.driftdroid.android.FrontendResumeTest'],capture_output=True,text=True,check=True)
print(p.stdout.strip());(O/'verification.json').write_text(json.dumps({'status':'PASS','result':p.stdout.strip(),'scope':'Actual selection policy; task moving and lifecycle require separate Thor acceptance'},indent=2)+'\n')
