"""Run production Dolphin adapters/transactions on disposable copies. No device access.
Protocol fixtures are independently packed/encrypted here, not by production code.
Retained owner save bytes remain private under build/ and are never modified.
"""
from pathlib import Path
import hashlib, json, os, struct, subprocess, zlib
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
R=Path(__file__).resolve().parents[2]
O=R/'build/feature-1.1/dolphin-import-2026-10-25/tests'
O.mkdir(parents=True,exist_ok=True)
J=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')
S=R/'reference/driftdroid/android/app/src/main/java/com/driftdroid/android/saves'
F=O/'fixtures';F.mkdir(exist_ok=True)
before=R/'build/feature-1.1/presentation-fix-2026-10-25/before-files'
nsmb=(before/'nsmbw/NAND/title/00010000/534d4e45/data/wiimj2d.sav').read_bytes()
galaxy=next((R/'build/galaxy-thor/galaxy-starter-backup/files/nand').rglob('GameData.bin')).read_bytes()
sms=(before/'sms/saves/card-a/super_mario_sunshine.dat').read_bytes()
stat=(before/'sms/saves/card-a/super_mario_sunshine.stat').read_bytes()
mkw=bytearray(0x2bc000);mkw[:8]=b'RKSD0006';struct.pack_into('>I',mkw,0x27ffc,zlib.crc32(mkw[:0x27ffc]))
for name,b in [('nsmb',nsmb),('galaxy-native',galaxy),('mkw',mkw)]: (F/name).write_bytes(b)
(F/'sunshine-native-stat').write_bytes(stat)
def gci(identity):
 h=bytearray(64);h[:6]=identity;h[6]=255;h[7]=stat[46];h[8:40]=stat[:32]
 struct.pack_into('>IIHH',h,40,*struct.unpack_from('<IIHH',stat,36)[:1],struct.unpack_from('<I',stat,48)[0],*struct.unpack_from('<HH',stat,52))
 struct.pack_into('>HHHI',h,54,5,7,65535,struct.unpack_from('<I',stat,56)[0])
 return bytes(h)+sms
for name,code in [('sunshine.gci',b'GMSE01'),('eclipse.gci',b'GMSE04')]: (F/name).write_bytes(gci(code))
def encrypt(data,iv):return Cipher(algorithms.AES(bytes.fromhex('ab01b9d8e1622b08afbad84dbfc2a55d')),modes.CBC(iv)).encryptor().update(data)
def export(title,name,payload,extra=None):
 h=bytearray(0xf0c0);struct.pack_into('>QI',h,0,title,0x72a0);h[12]=0x3f;h[14:30]=bytes.fromhex('0e65378199be4517ab06ec22451a5793');h[32:36]=b'WIBN';h[14:30]=hashlib.md5(h).digest()
 files=bytearray(); entries=[(name,payload)]+([] if extra is None else [extra])
 for index,(name,data) in enumerate(entries):
  fh=bytearray(128);struct.pack_into('>II',fh,0,0x03adf17e,len(data));fh[8]=0x3f;fh[10]=1;fh[11:11+len(name)]=name.encode();iv=bytes([index+3])*16;fh[80:96]=iv
  files+=fh+encrypt(data+bytes((-len(data))%64),iv)
 bk=bytearray(128);struct.pack_into('>IIIII',bk,0,0x70,0x426b0001,0,len(entries),len(files));struct.pack_into('>I',bk,28,len(files)+0x3c0);struct.pack_into('>Q',bk,96,title)
 return encrypt(h,bytes.fromhex('216712e6aa1f689f95c5a22324dc6a98'))+bk+files+bytes(0x340)
(F/'nsmb-export.bin').write_bytes(export(0x00010000534d4e45,'wiimj2d.sav',nsmb))
(F/'mkw-export.bin').write_bytes(export(0x00010004524d4350,'rksys.dat',mkw))
(F/'wrong-region.bin').write_bytes(export(0x00010000534d4e50,'wiimj2d.sav',nsmb))
(F/'traversal.bin').write_bytes(export(0x00010000534d4e45,'../wiimj2d.sav',nsmb))
(F/'unexpected.bin').write_bytes(export(0x00010000534d4e45,'wiimj2d.sav',nsmb,('config.ini',b'evil')))
(O/'DolphinSaveTest.java').write_text(r'''
package com.driftdroid.android.saves;
import java.io.*;import java.nio.file.*;import java.util.*;
public class DolphinSaveTest {
 static int checks;static File fixtures,base;
 public static Map<String,SaveArchive.Profile> deviceProfiles;
 interface Attempt{void run()throws Exception;}
 static void yes(boolean b,String why){checks++;if(!b)throw new AssertionError(why);}
 static void rejects(Attempt a,String why)throws Exception{checks++;try{a.run();}catch(IOException expected){return;}throw new AssertionError("Must reject: "+why);}
 static byte[] fixture(String name)throws Exception{return Files.readAllBytes(new File(fixtures,name).toPath());}
 static void put(File f,byte[] b)throws Exception{f.getParentFile().mkdirs();Files.write(f.toPath(),b);}
 static void put(File f,String s)throws Exception{put(f,s.getBytes(java.nio.charset.StandardCharsets.UTF_8));}
 static String get(File f)throws Exception{return new String(Files.readAllBytes(f.toPath()),java.nio.charset.StandardCharsets.UTF_8);}
 static SaveArchive.Profile profile(String game,String name)throws Exception {
  if(deviceProfiles!=null)return deviceProfiles.get(game);
  LinkedHashMap<String,File> roots=new LinkedHashMap<>();
  for(String r:game.equals("retro-rewind")?new String[]{"main","alternate","mod-settings"}:game.equals("galaxy")||game.equals("kaizo")?new String[]{"main","mirror","slot1","slot2","slot3","quick"}:new String[]{"main"})roots.put(r,new File(base,name+"/"+r));
  return new SaveArchive.Profile(game,roots);
 }
 static byte[] corrupt(byte[] b,int p){b=b.clone();b[p]^=1;return b;}
 public static void main(String[] args)throws Exception{
  fixtures=new File(args[0]);base=Files.createTempDirectory(Paths.get(args[1]),"dolphin-").toFile();
  byte[] nativeSave=fixture("galaxy-native"),be=GalaxySaveCodec.convert(nativeSave,false);
  yes(Arrays.equals(GalaxySaveCodec.convert(be,true),nativeSave),"actual Galaxy native -> BE -> native lossless");
  yes(Arrays.equals(GalaxySaveCodec.toNative(be),nativeSave),"retail byte order detected");
  yes(Arrays.equals(GalaxySaveCodec.toNative(nativeSave),nativeSave),"native input is validated without double conversion");
  Files.write(new File(fixtures,"galaxy-be").toPath(),be);
  rejects(()->GalaxySaveCodec.toNative(corrupt(be,900)),"Galaxy input checksum");
  byte[] bad=be.clone();GalaxySaveCodec.put(bad,28,0xfffffff0L,4,true);GalaxySaveCodec.put(bad,0,GalaxySaveCodec.checksum(bad,true),4,true);
  rejects(()->GalaxySaveCodec.toNative(bad),"Galaxy wrapped file offset with valid checksum");
  byte[] badChunk=be.clone();GalaxySaveCodec.put(badChunk,0x14c,0xfffffff0L,4,true);GalaxySaveCodec.put(badChunk,0,GalaxySaveCodec.checksum(badChunk,true),4,true);
  rejects(()->GalaxySaveCodec.toNative(badChunk),"Galaxy chunk boundary with valid checksum");
  for(String game:new String[]{"mkw","retro-rewind","nsmbw","msmbwii","sunshine","eclipse","galaxy","kaizo"}){
   String filename=game.equals("mkw")||game.equals("retro-rewind")?"mkw":game.equals("nsmbw")||game.equals("msmbwii")?"nsmb":game.equals("sunshine")||game.equals("eclipse")?game+".gci":"galaxy-be";
   byte[] input=fixture(filename);DolphinSaveImport.Plan plan=DolphinSaveImport.inspect(game,input);
   yes(plan.game.equals(game),"plan tied to "+game);yes(DolphinSaveImport.instructions(game).length()>40,"per-game guidance "+game);
   SaveArchive.Profile p=profile(game,game);File main=p.roots.get("main");
   put(new File(main,"kept.dat"),"unrelated card/settings bytes");
   if(p.roots.containsKey("mod-settings"))put(new File(p.roots.get("mod-settings"),"settings.pul"),"mod options");
   if(p.roots.containsKey("quick"))put(new File(p.roots.get("quick"),"manual-slot"),"keep quick save");
   File prior=new File(base,game+"-before.zip"),work=new File(base,"work-"+game);
   SaveArchive.Inventory before=SaveArchive.inventory(p);
   yes(!prior.exists(),"inspection creates no backup or live files "+game);
   DolphinSaveImport.restore(p,plan,work,prior);
   yes(prior.isFile() && SaveArchive.verify(p,prior,null).files.size()==before.files.size(),"previous profile fully backed up "+game);
   yes(get(new File(main,"kept.dat")).equals("unrelated card/settings bytes"),"merge preserves unrelated bytes "+game);
   if(p.roots.containsKey("quick"))yes(get(new File(p.roots.get("quick"),"manual-slot")).equals("keep quick save"),"manual quick slot preserved "+game);
   if(p.roots.containsKey("mod-settings"))yes(get(new File(p.roots.get("mod-settings"),"settings.pul")).equals("mod options"),"RR settings preserved");
   if(game.equals("sunshine")||game.equals("eclipse")){
    String stem=game.equals("eclipse")?"GMSE04_super_mario_sunshine":"super_mario_sunshine";
    byte[] stat=Files.readAllBytes(new File(main,stem+".stat").toPath());
    yes(stat.length==108 && GalaxySaveCodec.number(stat,32,4,false)==57344,"native CARDStat size/LE length "+game);
    if(game.equals("sunshine"))yes(Arrays.equals(stat,fixture("sunshine-native-stat")),"GCI reconstructs actual native CARDStat byte-for-byte");
    yes(get(new File(main,"index.txt")).equals(stem+"\n"),"native card index "+game);
    yes(Arrays.equals(Files.readAllBytes(new File(main,stem+".dat").toPath()),Arrays.copyOfRange(input,64,input.length)),"GCI payload byte exact "+game);
   } else if(game.equals("galaxy")||game.equals("kaizo")){
    for(String title:new String[]{"524d4750","524d4745","524d474a","524d474b"})
     yes(Arrays.equals(Files.readAllBytes(new File(main,"00010000/"+title+"/data/GameData.bin").toPath()),nativeSave),"Galaxy native title mirror "+game+title);
    yes(Arrays.equals(Files.readAllBytes(new File(p.roots.get("mirror"),"GameData.bin").toPath()),nativeSave),"Galaxy external mirror "+game);
   } else {
    String name=filename.equals("mkw")?"rksys.dat":"wiimj2d.sav";
    yes(Arrays.equals(Files.readAllBytes(new File(main,name).toPath()),input),"raw save byte exact "+game);
    if(game.equals("retro-rewind"))yes(Arrays.equals(Files.readAllBytes(new File(p.roots.get("alternate"),name).toPath()),input),"RR alternate matched");
   }
   yes(!SaveArchive.pending(work),"no pending transaction "+game);
   rejects(()->DolphinSaveImport.restore(profile("nsmbw","wrong"),DolphinSaveImport.inspect("mkw",fixture("mkw")),work,new File(base,"wrong.zip")),"plan cannot cross profiles");
   SaveArchive.restore(p,prior,work,new File(base,game+"-undo.zip"));
   yes(SaveArchive.inventory(p).files.keySet().equals(before.files.keySet()),"undo returns exact original file list "+game);
  }
  for(String game:new String[]{"nsmbw","msmbwii"})yes(DolphinSaveImport.inspect(game,fixture("nsmb-export.bin")).format.equals("Wii data.bin export"),"independent encrypted NSMB fixture "+game);
  for(String game:new String[]{"mkw","retro-rewind"})yes(DolphinSaveImport.inspect(game,fixture("mkw-export.bin")).format.equals("Wii data.bin export"),"independent encrypted MKW fixture "+game);
  for(String name:new String[]{"wrong-region.bin","traversal.bin","unexpected.bin"})rejects(()->DolphinSaveImport.inspect("nsmbw",fixture(name)),name);
  rejects(()->DolphinSaveImport.inspect("mkw",fixture("nsmb-export.bin")),"export wrong title");
  rejects(()->DolphinSaveImport.inspect("galaxy",fixture("nsmb")),"raw wrong game");
  rejects(()->DolphinSaveImport.inspect("sunshine",fixture("eclipse.gci")),"mod GCI cannot overwrite original");
  rejects(()->DolphinSaveImport.inspect("eclipse",fixture("sunshine.gci")),"original GCI cannot overwrite mod");
  byte[] oneBad=corrupt(fixture("sunshine.gci"),64+8192+100);
  yes(DolphinSaveImport.inspect("sunshine",oneBad)!=null,"GCI keeps working redundant slot copy");
  rejects(()->DolphinSaveImport.inspect("sunshine",corrupt(oneBad,64+16384+100)),"GCI both copies corrupt");
  rejects(()->DolphinSaveImport.inspect("nsmbw",corrupt(fixture("nsmb"),50)),"NSMB header CRC");
  rejects(()->DolphinSaveImport.inspect("nsmbw",corrupt(fixture("nsmb"),0x1000)),"NSMB record CRC");
  rejects(()->DolphinSaveImport.inspect("mkw",corrupt(fixture("mkw"),50)),"MKW CRC");
  rejects(()->DolphinSaveImport.inspect("nsmbw",corrupt(fixture("nsmb-export.bin"),50)),"encrypted header corruption");
  rejects(()->DolphinSaveImport.inspect("nsmbw",Arrays.copyOf(fixture("nsmb-export.bin"),62000)),"truncated export");
  rejects(()->DolphinSaveImport.inspect("unknown",fixture("nsmb")),"unknown profile");
  rejects(()->DolphinSaveImport.read(new ByteArrayInputStream(new byte[DolphinSaveImport.MAX_INPUT+1])),"bounded picker input");
  yes(DolphinSaveImport.read(new ByteArrayInputStream(fixture("nsmb"))).length==0x3fa0,"bounded picker normal input");
  SaveArchive.Profile card=profile("sunshine","merge-card");File main=card.roots.get("main"),index=new File(main,"index.txt");
  put(index,"keep_entry\n\nolder\n");put(new File(main,"keep_entry.dat"),"old card bytes");put(new File(main,"older.dat"),"older bytes");
  File undo=new File(base,"card-before.zip"),work=new File(base,"card-work");
  DolphinSaveImport.Plan cardPlan=DolphinSaveImport.inspect("sunshine",fixture("sunshine.gci"));
  DolphinSaveImport.restore(card,cardPlan,work,undo);
  yes(get(index).equals("keep_entry\nsuper_mario_sunshine\nolder\n"),"card index fills empty entry without moving others");
  yes(get(new File(main,"keep_entry.dat")).equals("old card bytes"),"other card payload preserved");
  SaveArchive.restore(card,undo,work,new File(base,"card-undo.zip"));
  File blocked=new File(base,"blocked-backup.zip");put(blocked,"existing archive");
  rejects(()->DolphinSaveImport.restore(card,cardPlan,work,blocked),"failed previous backup prevents import");
  yes(get(index).equals("keep_entry\n\nolder\n") && !new File(main,"super_mario_sunshine.dat").exists(),"failed import preserves card and index");
  File fullBackup=new File(base,"before-full.zip");SaveArchive.backup(card,fullBackup,true);
  put(index,"large\n");put(new File(main,"large.dat"),new byte[53*8192]);
  rejects(()->DolphinSaveImport.restore(card,cardPlan,work,new File(base,"full-before.zip")),"virtual card capacity checked");
  yes(get(index).equals("large\n"),"full card refusal preserves index");
  SaveArchive.restore(card,fullBackup,work,new File(base,"after-full.zip"));
  if(new File(fixtures,"galaxy-export.bin").isFile())for(String game:new String[]{"galaxy","kaizo"})
   yes(DolphinSaveImport.inspect(game,fixture("galaxy-export.bin")).format.equals("Wii data.bin export"),"encrypted Galaxy export "+game);
  System.out.println("PASS "+checks+" Dolphin import checks");
 }
}
''')
sources=[S/n for n in ['SaveArchive.java','GalaxySaveCodec.java','DolphinSaveImport.java']]+[O/'DolphinSaveTest.java']
subprocess.run([str(J/'javac.exe'),'--release','11','-d',str(O/'classes'),*map(str,sources)],check=True)
run=subprocess.run([str(J/'java.exe'),'-cp',str(O/'classes'),'com.driftdroid.android.saves.DolphinSaveTest',str(F),str(O)],text=True,capture_output=True)
print(run.stdout,run.stderr);run.check_returncode()
(F/'galaxy-export.bin').write_bytes(export(0x00010000524d4745,'GameData.bin',(F/'galaxy-be').read_bytes()))
run=subprocess.run([str(J/'java.exe'),'-cp',str(O/'classes'),'com.driftdroid.android.saves.DolphinSaveTest',str(F),str(O)],text=True,capture_output=True)
print(run.stdout,run.stderr);run.check_returncode()
(O/'result.json').write_text(json.dumps({'status':'PASS','output':run.stdout,'sources':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}},indent=2))
