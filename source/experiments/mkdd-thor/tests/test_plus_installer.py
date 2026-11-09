"""Exercise the actual Kotlin Plus installer against private own-disc inputs on the host.

Uses small Android/service stubs; does not establish Android lifecycle behavior.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1'


def jar(group, name, version):
    return next((CACHE / group / name / version).rglob('*.jar'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--android', type=Path, required=True)
    p.add_argument('--iso', type=Path, required=True)
    p.add_argument('--patch', type=Path, required=True)
    p.add_argument('--art-test', type=Path, required=True, help='Earlier overview test directory with its Bitmap stub')
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    out = a.out.resolve()
    if out == ROOT.parents[1] or ROOT.parents[1] in out.parents:
        p.error('Private installer outputs must be outside the source tree')
    out.mkdir(parents=True, exist_ok=True)
    fixtures = {
        'Context.kt': '''package android.content
import java.io.File
import android.net.Uri
class Context(val filesDir:File) {
 val applicationContext get()=this
 val contentResolver=Resolver()
 fun getExternalFilesDir(type:String?):File=filesDir
}
class Resolver { fun openInputStream(uri:Uri)=uri.file.inputStream() }
''',
        'Uri.kt': 'package android.net\nimport java.io.File\nclass Uri(val file:File)',
        'Storage.kt': '''package com.driftdroid.android
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import android.content.Context
enum class FrontendGame { MKDD_PLUS }
object GameStorage {fun blocker(c:Context):String?=null;fun enable(c:Context,g:FrontendGame){}}
object SaveManagerProfiles {
 val busy=AtomicBoolean(false)
 fun requireStopped(c:Context){}
 fun work(c:Context)=File(c.filesDir,"save-manager")
}
object WiiDsUpdater {val installing=AtomicBoolean(false)}
''',
        'Save.kt': 'package com.driftdroid.android.saves\nimport java.io.File\nobject SaveArchive {fun pending(f:File)=false}',
        'Runtime.kt': '''package com.driftdroid.android.rom
object GameInstaller {val running get()=MkddPlusMod.state.running}
object EclipseMod {class State(val running:Boolean=false);val state=State()}
object MsmbwiiMod {class State(val running:Boolean=false);val state=State()}
object WiiDsArt
''',
        'MkddStorage.java': '''package dev.thor.mkdd;
import android.content.Context;import java.io.File;
public final class MkddStorage {
 public static File root(Context c){return new File(c.getFilesDir(),"mkdd");}
 public static File iso(Context c){return new File(root(c),"game/game.iso");}
}''',
        'Test.kt': '''package com.driftdroid.android.rom
import android.content.Context
import android.net.Uri
import java.io.File
import java.security.MessageDigest
import com.driftdroid.android.SaveManagerProfiles
fun main(args:Array<String>) {
 val root=File(args[0]);val c=Context(root);val patch=Uri(File(args[1]));var checks=0
 fun sha(f:File):String {val d=MessageDigest.getInstance("SHA-256");f.inputStream().use {val b=ByteArray(1048576);while(true){val n=it.read(b);if(n<0)break;d.update(b,0,n)}};return d.digest().joinToString(""){"%02x".format(it.toInt()and 255)}}
 fun waitFor(){val until=System.currentTimeMillis()+300000;while(MkddPlusMod.state.running){check(System.currentTimeMillis()<until);Thread.sleep(20)}}
 val original=File(root,"mkdd/game/game.iso");val before=sha(original)
 val save=File(root,"mkdd/saves/owner.raw").apply{parentFile.mkdirs();writeText("original card")}
 val plusSave=File(root,"mkdd/plus/saves/owner.raw").apply{parentFile.mkdirs();writeText("plus card")}
 SaveManagerProfiles.busy.set(true);check(!MkddPlusMod.start(c,patch));checks++
 SaveManagerProfiles.busy.set(false);check(MkddPlusMod.start(c,patch));waitFor()
 check(MkddPlusMod.state.error==null){MkddPlusMod.state.toString()};checks++
 val disc=File(root,"mkdd/plus/game/game.iso");check(sha(disc)==MkddPlusMod.RESULT_SHA);checks++
 check(dev.thor.mkdd.MkddArt.ready(File(disc.parentFile,"lower-art")));checks++
 check(MkddPlusMod.start(c,patch));MkddPlusMod.cancel();waitFor();check(MkddPlusMod.state.error?.contains("cancelled")==true);checks++
 check(sha(disc)==MkddPlusMod.RESULT_SHA);checks++
 val bad=File(root,"bad.xdelta").apply{writeText("not a supported patch")}
 check(MkddPlusMod.start(c,Uri(bad)));waitFor();check(MkddPlusMod.state.error!=null);checks++
 check(sha(disc)==MkddPlusMod.RESULT_SHA);checks++
 check(sha(original)==before&&save.readText()=="original card"&&plusSave.readText()=="plus card");checks++
 check(!File(root,"mkdd/plus/game.importing").exists());checks++
 println("$checks actual installer, full patch/map, busy, cancellation, bad-input and card-preservation checks passed")
}'''}
    for name, text in fixtures.items():
        (out / name).write_text(text, encoding='utf-8')
    classes = out / 'classes'
    classes.mkdir(exist_ok=True)
    jsonjar = jar('org.json', 'json', '20240303')
    androidjar = Path('C:/Users/super/Documents/ChatGPT/PORTING/.tools/android-sdk/platforms/android-34/android.jar')
    # Context bytecode must be available while compiling the Java storage stub.
    kotlin = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
    compiler = [jar('org.jetbrains.kotlin', x, '1.9.24') for x in
                ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
    compiler += [kotlin, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'), jar('org.jetbrains', 'annotations', '13.0')]
    def kotlinc(inputs, extra=''):
        subprocess.run([str(JAVA/'java.exe'), '-cp', ';'.join(map(str, compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
                        '-no-stdlib', '-no-reflect', '-jvm-target', '1.8', '-classpath', f'{classes};{kotlin};{jsonjar};{extra}',
                        '-d', str(classes), *map(str, inputs)], check=True)
    kotlinc([out/'Context.kt', out/'Uri.kt'])
    production = ROOT / 'android/dev/thor/mkdd'
    javaclasses = out / 'java-classes'
    javaclasses.mkdir(exist_ok=True)
    subprocess.run([str(JAVA/'javac.exe'), '-source', '8', '-target', '8', '-cp', f'{classes};{jsonjar};{kotlin}', '-d', str(javaclasses),
                    str(a.art_test/'Bitmap.java'), str(a.art_test/'DuoArt.java'), str(out/'MkddStorage.java'),
                    *[str(production/f'{n}.java') for n in ('MkddArt', 'MkddOverview', 'MkddVcdiff')]], check=True)
    kotlinc([out/n for n in fixtures if n.endswith('.kt')] + [ROOT/'wiids/MkddPlusMod.kt',
            a.android/'app/src/main/kotlin/com/driftdroid/android/rom/DiscInstallCommit.kt'], str(javaclasses))
    source = out/'data/mkdd/game/game.iso'
    source.parent.mkdir(parents=True, exist_ok=True)
    if not source.exists():
        # Copy: the user's original disc must never share writable storage with a fixture.
        shutil.copyfile(a.iso, source)
    with source.open('rb') as f:
        assert hashlib.file_digest(f,'sha1').hexdigest() == '34bdef709a1ae9799fdabe8e54cff1c7d3665f2b'
    subprocess.run([str(JAVA/'java.exe'), '-Xmx256m', '-cp', f'{javaclasses};{classes};{kotlin};{jsonjar}',
                    'com.driftdroid.android.rom.TestKt', str(out/'data'), str(a.patch)], check=True)


if __name__ == '__main__':
    main()
