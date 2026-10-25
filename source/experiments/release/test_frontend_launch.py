"""Execute the production frontend resolver against controlled intents and disc identities."""
from pathlib import Path
import os,subprocess,json
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/frontend-tests';O.mkdir(parents=True,exist_ok=True)
cache=Path.home()/'.gradle/caches/modules-2/files-2.1'
def jar(group,name,version):return next((cache/group/name/version).rglob('*.jar'))
src=R/'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/FrontendLaunchActivity.kt'
def method(name):
 text=src.read_text();start=text.index('    private fun '+name);begin=text.index('{',start);depth=1
 for end in range(begin+1,len(text)):
  depth+=(text[end]=='{')-(text[end]=='}')
  if not depth:return text[start:end+1].replace('private fun','fun',1)
 raise AssertionError('Unclosed method')
fixture='''import java.io.File
import com.driftdroid.android.FrontendGame
class Uri(val scheme:String?,val host:String?=null,val path:String?=null,val query:String?=null,val fragment:String?=null,val authority:String?=host) {
 val pathSegments get()=path.orEmpty().split('/').filter{it.isNotEmpty()}
}
class Intent(val action:String?,val data:Uri?=null,val extras:Map<String,String?> = emptyMap()) {
 fun hasExtra(k:String)=extras.containsKey(k);fun getStringExtra(k:String)=extras[k]
 companion object {const val ACTION_VIEW="android.intent.action.VIEW"}
}
object WiiDsDisc {
 data class Info(val id:String,val revision:Int=0,val nkit:Boolean=false,val disc:Int=0)
 sealed class Source {data class Files(val parts:List<File>):Source();data class Document(val uri:Uri,val name:String):Source()}
 var next=Info("RMCP01");var reads=0;var last:Source?=null
 fun identify(context:Any,source:Source):Info {reads++;last=source;return next}
}
object GameCatalog {
 enum class Disc(val id:String,val revision:Int=0) {
  MARIO_KART_PAL("RMCP01"),NSMB_USA_REV1("SMNE01",1),SUNSHINE_USA("GMSE01"),ECLIPSE_110("GMSE04"),GALAXY_USA("RMGE01");
  fun matches(info:WiiDsDisc.Info)=info.id==id&&info.revision==revision&&!info.nkit&&info.disc==0
 }
}
class Router {
 companion object {const val ACTION_PLAY="io.github.maddex712.wiids.action.PLAY"}
''' + method('resolve')+'\n'+method('fileSource')+'''
}
var checks=0
fun verify(value:Boolean,why:String){checks++;check(value){why}}
fun rejected(r:Router,i:Intent){checks++;check(runCatching{r.resolve(i)}.isFailure){"Must reject $i"}}
fun main(args:Array<String>){
 val r=Router();val play=Router.ACTION_PLAY;val view=Intent.ACTION_VIEW
 for(g in FrontendGame.values()) {
  verify(r.resolve(Intent(play,extras=mapOf("game" to g.id)))==g,"explicit ${g.id}")
  verify(r.resolve(Intent(view,Uri("wiids","play","/${g.id}")))==g,"deep link ${g.id}")
 }
 verify(WiiDsDisc.reads==0,"explicit IDs do not read a supplied filename")
 for(id in listOf("unknown","NSMBW","","../nsmbw","com.example.Activity"))rejected(r,Intent(play,extras=mapOf("game" to id)))
 rejected(r,Intent(play));rejected(r,Intent(play,extras=mapOf("game" to null)))
 rejected(r,Intent("android.intent.action.SEND",extras=mapOf("game" to "nsmbw")))
 rejected(r,Intent(view,Uri("https","play","/nsmbw")))
 rejected(r,Intent(view,Uri("wiids","wrong","/nsmbw")))
 rejected(r,Intent(view,Uri("wiids","play","/nsmbw/extra")))
 rejected(r,Intent(view,Uri("wiids","play","/nsmbw",query="game=mkw")))
 rejected(r,Intent(view,Uri("wiids","play","/nsmbw",fragment="x")))
 rejected(r,Intent(view,Uri("wiids","play","/nsmbw"),mapOf("game" to "mkw")))
 val content=Uri("content","fixture","/a-file-named-Sunshine.iso")
 for((id,rev,target) in listOf(Triple("RMCP01",0,FrontendGame.MARIO_KART),Triple("SMNE01",1,FrontendGame.NSMB),Triple("GMSE01",0,FrontendGame.SUNSHINE),Triple("GMSE04",0,FrontendGame.ECLIPSE),Triple("RMGE01",0,FrontendGame.GALAXY))) {
  WiiDsDisc.next=WiiDsDisc.Info(id,rev);verify(r.resolve(Intent(view,content))==target,"disc identity $id")
 }
 for((id,rev,target) in listOf(Triple("RMCP01",0,"retro-rewind"),Triple("SMNE01",1,"msmbwii"),Triple("RMGE01",0,"kaizo"))) {
  WiiDsDisc.next=WiiDsDisc.Info(id,rev);verify(r.resolve(Intent(view,content,mapOf("game" to target))).id==target,"explicit mod selection")
 }
 WiiDsDisc.next=WiiDsDisc.Info("SMNE01",1)
 rejected(r,Intent(view,content,mapOf("game" to "sunshine")))
 for(info in listOf(WiiDsDisc.Info("SMNE01",0),WiiDsDisc.Info("SMNP01",1),WiiDsDisc.Info("SMNE01",1,true),WiiDsDisc.Info("SMNE01",1,false,1))) {
  WiiDsDisc.next=info;rejected(r,Intent(view,content))
 }
 rejected(r,Intent(view,Uri("file","server","/disc.iso")))
 val folder=File(args[0]);folder.mkdirs();val wbfs=File(folder,"split.wbfs");wbfs.writeBytes(byteArrayOf(1))
 File(folder,"split.wbf1").writeBytes(byteArrayOf(2));File(folder,"split.wbf2").writeBytes(byteArrayOf(3))
 WiiDsDisc.next=WiiDsDisc.Info("SMNE01",1)
 verify(r.resolve(Intent(view,Uri("file",path=wbfs.absolutePath)))==FrontendGame.NSMB,"filesystem WBFS")
 verify((WiiDsDisc.last as WiiDsDisc.Source.Files).parts.size==3,"split WBFS keeps all parts")
 rejected(r,Intent(view,Uri("file",path=File(folder,"absent.iso").absolutePath)))
 println("PASS $checks production frontend resolver checks")
}
'''
(O/'Test.kt').write_text(fixture)
jdk=Path(os.environ.get('JAVA_HOME','C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot'))/'bin';java=jdk/'java.exe';javac=jdk/'javac.exe'
classes=O/'classes';classes.mkdir(exist_ok=True)
subprocess.run([str(javac),'--release','17','-d',str(classes),str(R/'reference/driftdroid/android/app/src/main/java/com/driftdroid/android/FrontendGame.java')],check=True)
kotlin=jar('org.jetbrains.kotlin','kotlin-stdlib','1.9.24')
compiler=[jar('org.jetbrains.kotlin',name,'1.9.24') for name in ('kotlin-compiler-embeddable','kotlin-script-runtime','kotlin-daemon-embeddable')]
compiler += [kotlin,jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330'),jar('org.jetbrains','annotations','13.0')]
subprocess.run([str(java),'-cp',';'.join(map(str,compiler)),'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-jvm-target','17','-classpath',f'{kotlin};{classes}','-d',str(classes),str(O/'Test.kt')],check=True)
result=subprocess.run([str(java),'-cp',f'{classes};{kotlin}','TestKt',str(O/'files')],capture_output=True,text=True)
assert result.returncode==0,result.stdout+result.stderr
print(result.stdout.strip());(O/'verification.json').write_text(json.dumps({'status':'PASS','result':result.stdout.strip(),'scope':'Production resolver with mocked Android intents and disc reader; not Cocoon UI or real disc decoding'},indent=2)+'\n')
