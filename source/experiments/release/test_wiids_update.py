"""Run actual WiiDS update release/hash policy against public metadata and negative fixtures."""
from pathlib import Path
import argparse,subprocess,textwrap
R=Path(__file__).resolve().parents[2];O=R/'build/updater-1.1-2026-10-25/policy-tests'
C=Path.home()/'.gradle/caches/modules-2/files-2.1'
J=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe')
def jar(g,n,v):return next((C/g/n/v).rglob('*.jar'))
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--release',type=Path,required=True);p.add_argument('--inventory',type=Path,required=True);p.add_argument('--live',action='store_true');a=p.parse_args()
O.mkdir(parents=True,exist_ok=True)
(O/'Test.kt').write_text(textwrap.dedent(r'''
package com.driftdroid.android
import org.json.JSONObject
import java.io.File
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
fun main(args:Array<String>) {
 var checks=0
 fun expect(value:Boolean,label:String) { check(value) {label};checks++ }
 fun reject(label:String, action:()->Unit) { expect(runCatching(action).isFailure,label) }
 val raw=File(args[0]).readText();val inventory=File(args[1]).readBytes()
 val listing=WiiDsUpdateRelease.parseListing(raw)
 val release=WiiDsUpdateRelease.bindInventory(listing,inventory)
 expect(release.version=="1.1" && release.versionCode==1101L,"actual artwork hotfix build recognized")
 expect(release.newerThan(1100),"same-version hotfix offered")
 expect(!release.newerThan(1101),"current build not reoffered")
 expect(!release.newerThan(1102),"newer local build not downgraded")
 expect(release.newerThan(906),"accepted development preview may update")
 expect(WiiDsUpdateRelease.restore(release.toJson().toString())==release,"pending download survives process recreation")
 expect(release.apk.bytes>1024L*1024*1024,"actual 1.85GB APK accepted")
 fun modified(change:(JSONObject)->Unit):String {val v=JSONObject(raw);change(v);return v.toString()}
 fun apk(v:JSONObject):JSONObject {val assets=v.getJSONArray("assets");for(i in 0 until assets.length()) {val x=assets.getJSONObject(i);if(x.getString("name")==release.apk.name)return x};error("no APK")}
 reject("draft refused") { WiiDsUpdateRelease.parseListing(modified {it.put("draft",true)}) }
 reject("prerelease refused") { WiiDsUpdateRelease.parseListing(modified {it.put("prerelease",true)}) }
 reject("foreign repository refused") { WiiDsUpdateRelease.parseListing(modified {it.put("html_url","https://github.com/other/WiiDS/releases/tag/v1.1.0")}) }
 reject("tag path traversal refused") { WiiDsUpdateRelease.parseListing(modified {it.put("tag_name","v../1.1.0")}) }
 reject("release name mismatch refused") { WiiDsUpdateRelease.parseListing(modified {apk(it).put("name","WiiDS-1.2-arm64.apk")}) }
 reject("foreign APK host refused") { WiiDsUpdateRelease.parseListing(modified {apk(it).put("browser_download_url","https://example.com/WiiDS-1.1-arm64.apk")}) }
 reject("HTTP APK refused") { WiiDsUpdateRelease.parseListing(modified {apk(it).put("browser_download_url",release.apk.url.replace("https:","http:"))}) }
 reject("query injection refused") { WiiDsUpdateRelease.parseListing(modified {apk(it).put("browser_download_url",release.apk.url+"?redirect=bad")}) }
 reject("missing digest refused") { WiiDsUpdateRelease.parseListing(modified {apk(it).remove("digest")}) }
 reject("wrong digest algorithm refused") { WiiDsUpdateRelease.parseListing(modified {apk(it).put("digest","md5:"+release.apk.sha256)}) }
 reject("incomplete upload refused") { WiiDsUpdateRelease.parseListing(modified {apk(it).put("state","new")}) }
 reject("empty APK refused") { WiiDsUpdateRelease.parseListing(modified {apk(it).put("size",0)}) }
 reject("oversized APK refused") { WiiDsUpdateRelease.parseListing(modified {apk(it).put("size",WiiDsUpdateRelease.MAX_APK_BYTES+1)}) }
 reject("duplicate APK refused") { WiiDsUpdateRelease.parseListing(modified {it.getJSONArray("assets").put(JSONObject(apk(it).toString()))}) }
 reject("corrupted inventory refused") { WiiDsUpdateRelease.bindInventory(listing,inventory+byteArrayOf(10)) }
 fun inventoryChange(change:(JSONObject)->Unit):WiiDsUpdateRelease.Release {
  val v=JSONObject(String(inventory));change(v);val data=v.toString().toByteArray()
  val info=listing.inventory.copy(bytes=data.size.toLong(),sha256=WiiDsUpdateRelease.sha256(data))
  return WiiDsUpdateRelease.bindInventory(listing.copy(inventory=info),data)
 }
 reject("inventory package mismatch refused") {inventoryChange {it.put("package","other.app")}}
 reject("inventory tag mismatch refused") {inventoryChange {it.put("tag","v1.2.0")}}
 reject("inventory APK changed during check refused") {inventoryChange {it.getJSONObject("artifacts").getJSONObject(release.apk.name).put("sha256","0".repeat(64))}}
 reject("debug APK inventory refused") {inventoryChange {it.put("debuggable",true)}}
 reject("invalid build number refused") {inventoryChange {it.put("versionCode",0)}}
 expect(inventoryChange {it.put("versionCode",1102)}.newerThan(1101),"next same-version update detected")
 val future=JSONObject(raw);future.put("tag_name","v1.2.0").put("html_url",WiiDsUpdateRelease.REPOSITORY+"/releases/tag/v1.2.0")
 val fassets=future.getJSONArray("assets")
 for(i in 0 until fassets.length()) { val x=fassets.getJSONObject(i);x.put("browser_download_url",x.getString("browser_download_url").replace("/v1.1.0/","/v1.2.0/").replace("WiiDS-1.1-arm64.apk","WiiDS-1.2-arm64.apk"));if(x.getString("name")==release.apk.name)x.put("name","WiiDS-1.2-arm64.apk") }
 expect(WiiDsUpdateRelease.parseListing(future.toString()).apk.name=="WiiDS-1.2-arm64.apk","future version APK selected")
 val data=ByteArray(700000) {(it%251).toByte()};val hash=WiiDsUpdateRelease.sha256(data)
 val out=ByteArrayOutputStream();var done=0L
 WiiDsUpdateRelease.copyVerified(ByteArrayInputStream(data),out,data.size.toLong(),hash,progress={done=it})
 expect(out.toByteArray().contentEquals(data) && done==data.size.toLong(),"verified streaming copy exact")
 reject("truncated APK refused") {WiiDsUpdateRelease.copyVerified(ByteArrayInputStream(data.dropLast(1).toByteArray()),ByteArrayOutputStream(),data.size.toLong(),hash)}
 reject("oversized stream refused") {WiiDsUpdateRelease.copyVerified(ByteArrayInputStream(data+byteArrayOf(1)),ByteArrayOutputStream(),data.size.toLong(),hash)}
 reject("wrong APK checksum refused") {WiiDsUpdateRelease.copyVerified(ByteArrayInputStream(data),ByteArrayOutputStream(),data.size.toLong(),"0".repeat(64))}
 reject("cancelled copy refused") {WiiDsUpdateRelease.copyVerified(ByteArrayInputStream(data),ByteArrayOutputStream(),data.size.toLong(),hash,{true})}
 reject("invalid persisted URL refused") {WiiDsUpdateRelease.restore(release.toJson().apply {getJSONObject("apk").put("url","https://example.com/a.apk")}.toString())}
 if(args.size>2) {val live=WiiDsUpdateRelease.latest();expect(live==release,"actual HTTPS latest release and inventory fetch verified")}
 println("$checks actual-source WiiDS update checks passed")
}
'''),encoding='utf-8')
k=jar('org.jetbrains.kotlin','kotlin-stdlib','1.9.24');j=jar('org.json','json','20240303')
compiler=[jar('org.jetbrains.kotlin',n,'1.9.24') for n in ('kotlin-compiler-embeddable','kotlin-script-runtime','kotlin-daemon-embeddable')]+[k,jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330'),jar('org.jetbrains','annotations','13.0')]
source=R/'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/WiiDsUpdateRelease.kt'
subprocess.run([str(J),'-cp',';'.join(map(str,compiler)),'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-jvm-target','17','-classpath',f'{k};{j}','-d',str(O/'classes'),str(source),str(O/'Test.kt')],check=True)
subprocess.run([str(J),'-cp',f'{O/"classes"};{k};{j}','com.driftdroid.android.TestKt',str(a.release),str(a.inventory),*(['live'] if a.live else [])],check=True)
