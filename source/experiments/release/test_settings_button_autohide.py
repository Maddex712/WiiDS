"""Run the production gear policy and exact Kotlin dispatch/timer methods on a host JVM."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'build/issues-9-10-2026-10-31/implementation/gear-tests'
OUT.mkdir(parents=True, exist_ok=True)
JDK = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')
JAVA = ROOT/'reference/driftdroid/android/app/src/main/java/com/driftdroid/android/SettingsButtonAutoHide.java'
KOTLIN = ROOT/'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/MainActivity.kt'

test = r'''
package com.driftdroid.android;
public class SettingsButtonAutoHideTest {
    static int checks;
    static void yes(boolean value, String why) { checks++; if (!value) throw new AssertionError(why); }
    public static void main(String[] args) {
        SettingsButtonAutoHide h = new SettingsButtonAutoHide();
        yes(!h.visible(0), "not active before runtime");
        yes(h.nextUpdateDelay(0) == 0, "no idle callback before runtime");
        h.setActive(true, 100);
        yes(h.visible(100), "startup discovery");
        yes(h.visible(5099), "before exact deadline");
        yes(h.nextUpdateDelay(5099) == 1, "last millisecond scheduled");
        yes(!h.visible(5100), "exact deadline hides");
        yes(h.nextUpdateDelay(5100) == 0, "no timer loop when hidden");
        h.reveal(60000);
        yes(h.visible(60000), "fresh game UI after a long importer");
        yes(!h.visible(65000), "attachment has its own five seconds");
        h.setPointers(1, 70000);
        yes(h.visible(90000), "held finger does not time out");
        yes(h.nextUpdateDelay(90000) == 0, "no callback while held");
        h.setPointers(2, 90001); h.setPointers(1, 90002);
        yes(h.visible(100000), "remaining multitouch finger holds gear");
        h.setPointers(0, 100001);
        yes(h.visible(105000), "release starts idle delay");
        yes(!h.visible(105001), "release deadline");
        h.setSettingsOpen(true, 110000);
        yes(h.visible(150000), "settings pin gear");
        yes(h.nextUpdateDelay(150000) == 0, "settings do not poll");
        h.setSettingsOpen(false, 150001);
        yes(h.visible(155000), "closing settings grants idle delay");
        yes(!h.visible(155001), "closing settings deadline");
        h.setEditing(true, 160000);
        yes(h.visible(190000), "editing pins gear");
        h.setManuallyHidden(true, 190001);
        h.setPointers(1, 190002); h.setSettingsOpen(true, 190003);
        yes(!h.visible(190003), "manual hide outranks touch, settings and edit");
        yes(h.nextUpdateDelay(190003) == 0, "manual hide removes timer");
        h.setPointers(0, 190004); h.setEditing(false, 190005); h.setSettingsOpen(false, 190006);
        h.setManuallyHidden(false, 200000);
        yes(h.visible(204999), "unhide grants five seconds");
        yes(!h.visible(205000), "unhide timeout");
        h.setPointers(1, 210000); h.setActive(false, 210001);
        yes(!h.visible(210002), "pause hides gear");
        yes(h.nextUpdateDelay(210002) == 0, "pause cancels callback");
        h.setActive(true, 300000);
        yes(!h.visible(300000), "resume does not reveal an expired gear without a tap");
        yes(!h.visible(305000), "pause cleared stale pointer count");
        h.setActive(true, 400000);
        yes(!h.visible(400000), "duplicate focus does not perpetually reset timer");
        h.setActive(false, 400001); h.setManuallyHidden(true, 400002); h.setActive(true, 400003);
        yes(!h.visible(400003), "manual preference survives resume");
        System.out.println("PASS " + checks + " production gear policy checks");
    }
}
'''
(OUT/'SettingsButtonAutoHideTest.java').write_text(test, encoding='utf-8')
subprocess.run([str(JDK/'javac.exe'), '--release', '17', '-d', str(OUT/'classes'),
    str(JAVA), str(OUT/'SettingsButtonAutoHideTest.java')], check=True)
policy = subprocess.run([str(JDK/'java.exe'), '-cp', str(OUT/'classes'),
    'com.driftdroid.android.SettingsButtonAutoHideTest'], capture_output=True, text=True, check=True)

def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    for end in range(opening+1, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end+1]
    raise AssertionError(signature)

source = KOTLIN.read_text(encoding='utf-8')
methods = '\n'.join(method(source, name) for name in [
    'private fun updateSettingsButtonVisibility()',
    'override fun dispatchTouchEvent(event: MotionEvent)',
    'fun onNativeSetSettingsButtonHidden(hidden: Boolean)',
])
harness = r'''
package com.driftdroid.android
object View { const val VISIBLE=0; const val GONE=8 }
object SystemClock { var now=0L; fun uptimeMillis()=now }
class Button {
    var visibility=View.GONE
    var pending: Runnable?=null
    var delay=0L
    fun removeCallbacks(r: Runnable) { if(pending===r) {pending=null;delay=0} }
    fun postDelayed(r: Runnable, d: Long) {pending=r;delay=d}
}
class MotionEvent(val actionMasked:Int,val pointerCount:Int=1,val overGear:Boolean=false) {
    companion object {const val ACTION_DOWN=0;const val ACTION_UP=1;const val ACTION_MOVE=2
      const val ACTION_CANCEL=3;const val ACTION_POINTER_DOWN=5;const val ACTION_POINTER_UP=6}
}
open class Parent {
    val button=Button()
    var handled=false
    var delivered=0
    var opens=0
    private var gearTarget=false
    open fun dispatchTouchEvent(event:MotionEvent):Boolean {
        delivered++
        if(event.actionMasked==MotionEvent.ACTION_DOWN)
            gearTarget=button.visibility==View.VISIBLE && event.overGear
        if(event.actionMasked==MotionEvent.ACTION_UP) {if(gearTarget) opens++;gearTarget=false}
        if(event.actionMasked==MotionEvent.ACTION_CANCEL) gearTarget=false
        return handled
    }
}
class Harness:Parent() {
    private var settingsButton: Button?=button
    private var nativeSettingsButtonHidden=false
    private val settingsButtonAutoHide=SettingsButtonAutoHide()
    private val settingsButtonTimeout=Runnable {updateSettingsButtonVisibility()}
    fun runOnUiThread(r:()->Unit)=r()
    fun start(now:Long) {SystemClock.now=now;settingsButtonAutoHide.setActive(true,now);updateSettingsButtonVisibility()}
    fun tick(now:Long) {SystemClock.now=now;button.pending?.run()}
    fun pause(now:Long) {SystemClock.now=now;settingsButtonAutoHide.setActive(false,now);updateSettingsButtonVisibility()}
    METHODS
}
fun main() {
    var checks=0
    fun yes(v:Boolean,why:String) {check(v) {why};checks++}
    val h=Harness();h.start(0)
    yes(h.button.visibility==View.VISIBLE,"startup visible")
    yes(h.button.delay==5000L,"one callback at deadline")
    h.tick(5000)
    yes(h.button.visibility==View.GONE,"timer hides")
    yes(h.button.pending==null,"hidden does not keep callback")
    SystemClock.now=6000
    val returned=h.dispatchTouchEvent(MotionEvent(MotionEvent.ACTION_DOWN,overGear=true))
    yes(!returned,"false SDL return retained")
    yes(h.delivered==1,"SDL receives reveal down")
    yes(h.button.visibility==View.VISIBLE,"down reveals gear")
    yes(h.button.pending==null,"holding finger cancels deadline")
    SystemClock.now=6001
    h.dispatchTouchEvent(MotionEvent(MotionEvent.ACTION_UP,overGear=true))
    yes(h.opens==0,"reveal tap at hidden gear location cannot open settings")
    yes(h.delivered==2,"SDL receives matching up")
    yes(h.button.delay==5000L,"release starts delay")
    h.handled=true
    yes(h.dispatchTouchEvent(MotionEvent(MotionEvent.ACTION_DOWN,overGear=true)),"true SDL return retained")
    h.dispatchTouchEvent(MotionEvent(MotionEvent.ACTION_UP,overGear=true))
    yes(h.opens==1,"second intentional tap opens gear")
    h.onNativeSetSettingsButtonHidden(true)
    yes(h.button.visibility==View.GONE && h.button.pending==null,"native manual hide wins")
    h.dispatchTouchEvent(MotionEvent(MotionEvent.ACTION_DOWN))
    yes(h.button.visibility==View.GONE,"touch cannot bypass manual hide")
    h.dispatchTouchEvent(MotionEvent(MotionEvent.ACTION_CANCEL))
    h.onNativeSetSettingsButtonHidden(false)
    yes(h.button.visibility==View.VISIBLE && h.button.delay==5000L,"native unhide resets deadline")
    val delivered=h.delivered
    h.dispatchTouchEvent(MotionEvent(MotionEvent.ACTION_MOVE))
    yes(h.delivered==delivered+1,"movement still delivered")
    h.pause(8000)
    yes(h.button.visibility==View.GONE && h.button.pending==null,"pause removes callback")
    println("PASS $checks actual Kotlin dispatch/timer adapter checks")
}
'''.replace('METHODS', methods)
(OUT/'Harness.kt').write_text(harness, encoding='utf-8')
cache = Path.home()/'.gradle/caches/modules-2/files-2.1'
def jar(group, name, version):
    return next((cache/group/name/version).rglob('*.jar'))
stdlib = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
compiler = [jar('org.jetbrains.kotlin', n, '1.9.24') for n in
    ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
compiler += [stdlib, jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330'),
             jar('org.jetbrains','annotations','13.0')]
subprocess.run([str(JDK/'java.exe'), '-cp', ';'.join(map(str, compiler)),
    'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler', '-no-stdlib', '-no-reflect', '-jvm-target', '17',
    '-classpath', f'{stdlib};{OUT/"classes"}', '-d', str(OUT/'classes'), str(OUT/'Harness.kt')], check=True)
adapter = subprocess.run([str(JDK/'java.exe'), '-cp', f'{OUT/"classes"};{stdlib}',
    'com.driftdroid.android.HarnessKt'], capture_output=True, text=True, check=True)
report = {'status':'PASS', 'policy':policy.stdout.strip(), 'adapter':adapter.stdout.strip(),
    'policy_sha256':hashlib.sha256(JAVA.read_bytes()).hexdigest(),
    'activity_sha256':hashlib.sha256(KOTLIN.read_bytes()).hexdigest(),
    'scope':'Production policy and exact Kotlin touch/timer/native-hide methods on JVM with fake views, clock and SDL dispatch. Android compile and physical acceptance are separate.'}
(OUT/'verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps(report, indent=2))
