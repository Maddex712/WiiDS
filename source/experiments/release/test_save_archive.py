"""Execute production save transactions against disposable filesystem fixtures."""
from pathlib import Path
import os
import subprocess
import json

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/feature-1.1/save-manager/tests'
OUT.mkdir(parents=True, exist_ok=True)
JDK = Path(os.environ.get('JAVA_HOME', 'C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot')) / 'bin'
test = r'''
package com.driftdroid.android.saves;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.*;
public class SaveArchiveTest {
 static int checks;
 static File base;
 interface Attempt { void run() throws Exception; }
 static void yes(boolean b, String why) { checks++; if(!b) throw new AssertionError(why); }
 static void rejects(Attempt a, String why) throws Exception {
  checks++; try { a.run(); } catch(IOException | IllegalArgumentException expected) { return; }
  throw new AssertionError("Must reject: " + why);
 }
 static File file(String name) { return new File(base,name); }
 static void put(File f,String s) throws IOException { f.getParentFile().mkdirs(); Files.write(f.toPath(),s.getBytes(StandardCharsets.UTF_8)); }
 static String get(File f) throws IOException { return new String(Files.readAllBytes(f.toPath()),StandardCharsets.UTF_8); }
 static SaveArchive.Profile profile(String id,String path) throws Exception {
  LinkedHashMap<String,File> roots=new LinkedHashMap<>();
  roots.put("main",file(path+"/main")); roots.put("mirror",file(path+"/mirror")); roots.put("optional",file(path+"/optional"));
  return new SaveArchive.Profile(id,roots);
 }
 static void zip(File target,LinkedHashMap<String,String> entries) throws IOException {
  target.getParentFile().mkdirs();
  try(ZipOutputStream z=new ZipOutputStream(new FileOutputStream(target))) {
   for(Map.Entry<String,String> e:entries.entrySet()) {z.putNextEntry(new ZipEntry(e.getKey())); z.write(e.getValue().getBytes(StandardCharsets.UTF_8));z.closeEntry();}
  }
 }
 static String manifest(File archive) throws IOException {
  try(ZipFile z=new ZipFile(archive);InputStream in=z.getInputStream(z.getEntry("wiids-save.tsv"))) {return new String(in.readAllBytes(),StandardCharsets.UTF_8);}
 }
 static void badManifest(SaveArchive.Profile p,String text,String name) throws Exception {
  File f=file("bad/"+name+".zip");LinkedHashMap<String,String> e=new LinkedHashMap<>();e.put("wiids-save.tsv",text);zip(f,e);
  rejects(()->SaveArchive.verify(p,f,null),name);
 }
 static Properties journal(SaveArchive.Profile p,String token) {
  Properties j=new Properties();j.setProperty("profile",p.id);j.setProperty("token",token);j.setProperty("roots",String.join(",",p.roots.keySet()));
  for(Map.Entry<String,File> e:p.roots.entrySet())j.setProperty("had."+e.getKey(),e.getValue().exists()?"1":"0");return j;
 }
 static void saveJournal(File work,Properties p)throws IOException {work.mkdirs();try(FileOutputStream out=new FileOutputStream(new File(work,"restore.properties"))){p.store(out,"fixture");}}
 static File adjacent(File target,String token,String suffix){return new File(target.getParentFile(),".wiids-"+token+"-"+target.getName()+suffix);}
 public static void main(String[] args)throws Exception {
  base=Files.createTempDirectory(Paths.get(args[0]),"save-test-").toFile();
  SaveArchive.Profile p=profile("nsmbw","original");
  File main=p.roots.get("main"),mirror=p.roots.get("mirror"),optional=p.roots.get("optional");
  put(new File(main,"wiimj2d.sav"),"original save");put(new File(main,"nested/banner.bin"),"banner");mirror.mkdirs();
  SaveArchive.Inventory before=SaveArchive.inventory(p);
  yes(before.files.size()==2 && before.bytes()==19,"Inventory includes nested files and lengths");
  yes(before.roots.get("mirror") && !before.roots.get("optional"),"Empty and absent roots distinguished");
  File backup=file("backups/original.zip");SaveArchive.backup(p,backup,false);
  yes(backup.isFile(),"Backup published");yes(SaveArchive.verify(p,backup,null).files.size()==2,"Read-back verified");
  rejects(()->SaveArchive.backup(p,backup,false),"Existing backup never overwritten");
  put(new File(main,"wiimj2d.sav"),"new save");put(new File(main,"extra.dat"),"new file");put(new File(optional,"created.bin"),"new optional");
  File previous=file("backups/before-restore.zip"),work=file("work");
  SaveArchive.restore(p,backup,work,previous);
  yes(get(new File(main,"wiimj2d.sav")).equals("original save"),"Original bytes restored");
  yes(!new File(main,"extra.dat").exists(),"Stale files removed by snapshot restore");
  yes(!optional.exists() && mirror.isDirectory(),"Absent/empty roots restored accurately");
  yes(!SaveArchive.pending(work),"Successful restore clears journal");
  SaveArchive.Inventory old=SaveArchive.verify(p,previous,null);
  yes(old.files.size()==4,"Previous save kept as complete verified backup");
  SaveArchive.restore(p,previous,work,file("backups/undo.zip"));
  yes(get(new File(main,"wiimj2d.sav")).equals("new save") && new File(optional,"created.bin").exists(),"Undo restores complete previous profile");

  SaveArchive.Profile mod=profile("msmbwii","mod");put(new File(mod.roots.get("main"),"wiimj2d.sav"),"mod save");
  rejects(()->SaveArchive.restore(mod,backup,file("mod-work"),file("backups/mod-before.zip")),"Original backup rejected for mod");
  yes(get(new File(mod.roots.get("main"),"wiimj2d.sav")).equals("mod save"),"Wrong-game refusal preserves mod");
  rejects(()->SaveArchive.restore(p,backup,work,backup),"Cannot overwrite previous archive");
  yes(get(new File(main,"wiimj2d.sav")).equals("new save"),"Backup failure preserves live save");

  String m=manifest(backup);
  String head="WIIDS-SAVE\t1\nprofile\tnsmbw\ncreated\t1\nroot\tmain\t1\nroot\tmirror\t1\nroot\toptional\t0\n";
  for(String path:new String[]{"../escape","main/../escape","main//bad","main/./bad","main/C:bad","main\\bad","/main/bad","main/bad\rname"})
   badManifest(p,head+"file\t"+path+"\t0\t"+"0".repeat(64)+"\n","path-"+checks);
  badManifest(p,m.replace("profile\tnsmbw","profile\tmkw"),"wrong-game");
  badManifest(p,m.replace("created\t","created\tbroken"),"bad-date");
  badManifest(p,m+"root\tmain\t1\n","duplicate-root");
  badManifest(p,m.replace("root\toptional\t0\n",""),"missing-root");
  badManifest(p,m.replace("root\toptional\t0","root\tunknown\t0"),"unknown-root");
  badManifest(p,head+"file\toptional/x\t0\t"+"0".repeat(64)+"\n","absent-root-file");
  badManifest(p,head+"file\tmain/x\t134217729\t"+"0".repeat(64)+"\n","oversize");
  badManifest(p,head+"file\tmain/x\t-1\t"+"0".repeat(64)+"\n","negative-size");
  badManifest(p,head+"file\tmain/x\t1\twrong\n","bad-hash");
  LinkedHashMap<String,String> e=new LinkedHashMap<>();e.put("wiids-save.tsv",m);e.put("data/main/wiimj2d.sav","corrupted");e.put("data/main/nested/banner.bin","banner");
  File corrupt=file("bad/corrupt.zip");zip(corrupt,e);
  rejects(()->SaveArchive.restore(p,corrupt,work,file("backups/corrupt-before.zip")),"Corrupt payload rejected before backup/rename");
  yes(!file("backups/corrupt-before.zip").exists() && get(new File(main,"wiimj2d.sav")).equals("new save"),"Invalid import touches no live save");
  e.remove("data/main/nested/banner.bin");zip(corrupt,e);rejects(()->SaveArchive.verify(p,corrupt,null),"Missing payload");
  e.put("unexpected.so","not a save");zip(corrupt,e);rejects(()->SaveArchive.verify(p,corrupt,null),"Unexpected code entry");
  byte[] bytes=Files.readAllBytes(backup.toPath());Files.write(corrupt.toPath(),Arrays.copyOf(bytes,bytes.length-30));
  rejects(()->SaveArchive.verify(p,corrupt,null),"Truncated ZIP central directory");

  SaveArchive.Profile empty=profile("sunshine","empty");
  rejects(()->SaveArchive.backup(empty,file("empty.zip"),false),"No-save state is explicit");
  SaveArchive.backup(empty,file("empty-before.zip"),true);
  yes(SaveArchive.verify(empty,file("empty-before.zip"),null).files.isEmpty(),"Empty previous profile can be preserved");

  // Real interrupted directory layouts, recovered by the production journal reader.
  for(int stoppedAfter=0;stoppedAfter<=3;stoppedAfter++) {
   SaveArchive.Profile r=profile("galaxy","recovery-"+stoppedAfter);
   put(new File(r.roots.get("main"),"GameData.bin"),"old internal");put(new File(r.roots.get("mirror"),"GameData.bin"),"old mirror");
   String token=UUID.randomUUID().toString();Properties j=journal(r,token);File w=file("recover-work-"+stoppedAfter);saveJournal(w,j);
   int n=0;
   for(File target:r.roots.values()) {
    File stage=adjacent(target,token,".new");put(new File(stage,"GameData.bin"),"new staged");
    if(n++<stoppedAfter) {if(target.exists())Files.move(target.toPath(),adjacent(target,token,".old").toPath());Files.move(stage.toPath(),target.toPath());}
   }
   SaveArchive.recover(r,w);
   yes(get(new File(r.roots.get("main"),"GameData.bin")).equals("old internal"),"Interrupted restore recovers internal "+stoppedAfter);
   yes(get(new File(r.roots.get("mirror"),"GameData.bin")).equals("old mirror"),"Interrupted restore recovers mirror "+stoppedAfter);
   yes(!r.roots.get("optional").exists()&&!SaveArchive.pending(w),"New roots and journal removed "+stoppedAfter);
   SaveArchive.recover(r,w);yes(!SaveArchive.pending(w),"Recovery is idempotent "+stoppedAfter);
  }
  SaveArchive.Profile committed=profile("kaizo","committed");String token=UUID.randomUUID().toString();
  Properties j=journal(committed,token);j.setProperty("committed","1");File cw=file("committed-work");saveJournal(cw,j);
  put(new File(committed.roots.get("main"),"GameData.bin"),"committed");put(new File(adjacent(committed.roots.get("main"),token,".old"),"GameData.bin"),"old");
  SaveArchive.recover(committed,cw);
  yes(get(new File(committed.roots.get("main"),"GameData.bin")).equals("committed"),"Committed restore kept after cleanup interruption");
  yes(!adjacent(committed.roots.get("main"),token,".old").exists(),"Committed rollback folder cleaned");
  File wrong=file("wrong-recovery");saveJournal(wrong,j);rejects(()->SaveArchive.recover(p,wrong),"Wrong recovery profile refused");yes(SaveArchive.pending(wrong),"Unknown recovery retained");
  LinkedHashMap<String,File> overlap=new LinkedHashMap<>();overlap.put("main",file("a"));overlap.put("mirror",file("a/b"));
  rejects(()->new SaveArchive.Profile("mkw",overlap),"Overlapping roots rejected");
  System.out.println("PASS "+checks+" production save archive/transaction checks");
 }
}
'''
(OUT / 'SaveArchiveTest.java').write_text(test, encoding='utf-8')
classes = OUT / 'classes'
classes.mkdir(exist_ok=True)
subprocess.run([str(JDK / 'javac.exe'), '--release', '17', '-d', str(classes),
    str(ROOT / 'reference/driftdroid/android/app/src/main/java/com/driftdroid/android/saves/SaveArchive.java'),
    str(OUT / 'SaveArchiveTest.java')], check=True)
run = subprocess.run([str(JDK / 'java.exe'), '-cp', str(classes),
    'com.driftdroid.android.saves.SaveArchiveTest', str(OUT)], capture_output=True, text=True)
assert run.returncode == 0, run.stdout + run.stderr
print(run.stdout.strip())
(OUT / 'verification.json').write_text(json.dumps({'status': 'PASS', 'result': run.stdout.strip(),
    'scope': 'Actual Java filesystem/ZIP/hash/restore/recovery operations on disposable host fixtures; not device saves'}, indent=2) + '\n', encoding='utf-8')
