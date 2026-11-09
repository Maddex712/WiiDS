"""Build only an isolated, read-only Android SAF/path probe. Never uses ADB."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import zipfile

R = Path(__file__).resolve().parents[2]
O = R / 'build/github-bugfix-2026-10-26/microsd/android'
O.mkdir(parents=True, exist_ok=True)
SDK = R / '.tools/android-sdk'
BT = SDK / 'build-tools/34.0.0'
AJ = SDK / 'platforms/android-34/android.jar'
J = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')
C = Path.home() / '.gradle/caches/modules-2/files-2.1'
REL = 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/GamesActivity.kt'
fixed = R / REL
old = O.parent / 'before' / REL
pkg = 'io.github.maddex712.wiids.microsdtests'


def jar(g, n, v):
    return next((C / g / n / v).rglob('*.jar'))


def method(p):
    text = p.read_text(encoding='utf-8')
    begin = text.index('    private fun discPath(')
    return text[begin:text.index('\n    private fun importGalaxy(', begin)]


code = r'''package com.driftdroid.android
import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.DocumentsContract
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import com.driftdroid.android.rom.WiiDsDisc
import java.io.File
import org.json.JSONObject

class MicrosdCheck : Activity() {
    private lateinit var status: TextView
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        val column = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32,32,32,32) }
        status = TextView(this).apply { textSize = 18f; text = "Read-only SD import path check. Select the existing Mario Kart Wii Europe ISO. No game is imported or changed." }
        column.addView(status)
        column.addView(Button(this).apply { text = "Choose existing SD disc"; setOnClickListener {
            val pick = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                .putExtra(DocumentsContract.EXTRA_INITIAL_URI, DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "69F2-3F53:ROMs/wii"))
            startActivityForResult(pick, 1)
        } })
        setContentView(column)
    }
    override fun onActivityResult(request: Int, result: Int, data: Intent?) {
        super.onActivityResult(request, result, data)
        if (request != 1 || result != RESULT_OK || data?.data == null) return
        val uri = data.data!!
        status.text = "Checking the granted URI and its existing file path..."
        Thread {
            val report = JSONObject()
            try {
                val source = WiiDsDisc.Source.Document(uri, "selected disc")
                val before = beforeDiscPath(source)
                val after = discPath(source)
                report.put("uri", uri.toString()).put("document_id", DocumentsContract.getDocumentId(uri))
                    .put("before_path", before ?: JSONObject.NULL).put("fixed_path", after ?: JSONObject.NULL)
                val expected = File(intent.getStringExtra("expected_path") ?: "/storage/69F2-3F53/ROMs/wii/Mario Kart Wii (Europe).iso").canonicalFile
                check(uri.authority == "com.android.externalstorage.documents") { "Select the mounted card in Android Files." }
                check(DocumentsContract.getDocumentId(uri).startsWith("69F2-3F53:", ignoreCase = true)) { "Selected document is not on the requested microSD card." }
                check(before == null) { "The public resolver unexpectedly accepted this card ID." }
                check(after != null && File(after).canonicalFile == expected) { "Resolved path must match the specified existing ROM; no other file will be read." }
                val sizeBefore = expected.length()
                val modifiedBefore = expected.lastModified()
                check(sizeBefore > 1024 * 1024) { "Expected disc is absent or too small." }
                fun head(input: java.io.InputStream): ByteArray = input.use {
                    val bytes = ByteArray(0x220); var at = 0
                    while (at < bytes.size) { val n = it.read(bytes, at, bytes.size-at); check(n > 0) { "Short disc header" }; at += n }
                    bytes
                }
                val granted = head(contentResolver.openInputStream(uri) ?: error("Granted URI could not be opened."))
                val direct = head(expected.inputStream())
                check(granted.contentEquals(direct)) { "Resolved file differs from granted document." }
                val gameId = String(direct, 0, 6, Charsets.US_ASCII)
                check(gameId == (intent.getStringExtra("expected_id") ?: "RMCP01")) { "Wrong disc header: $gameId" }
                check(expected.length() == sizeBefore && expected.lastModified() == modifiedBefore) { "Source metadata changed." }
                report.put("status", "PASS").put("game_id", gameId).put("header_bytes_read_per_source", direct.size)
                    .put("header_sha256", java.security.MessageDigest.getInstance("SHA-256").digest(direct).joinToString("") { "%02x".format(it.toInt() and 255) })
                    .put("disc_bytes", sizeBefore).put("source_metadata_unchanged", true)
                    .put("scope", "Actual old and fixed discPath methods; real StorageManager/DocumentsContract and user-granted SAF URI; 544-byte read-only header equality, no import")
            } catch (error: Throwable) {
                report.put("status", "FAIL").put("error", error.toString())
                android.util.Log.e("MicrosdCheck", "Read-only check failed", error)
            }
            File(filesDir, "result.json").writeText(report.toString(2))
            android.util.Log.i("MicrosdCheck", report.toString())
            runOnUiThread { status.text = report.toString(2) }
        }.start()
    }
''' + method(fixed) + '\n' + method(old).replace('private fun discPath(', 'private fun beforeDiscPath(', 1) + '\n}\n'
(O / 'MicrosdCheck.kt').write_text(code, encoding='utf-8')
(O / 'AndroidManifest.xml').write_text(f'''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="{pkg}" android:versionCode="1" android:versionName="1">
<uses-sdk android:minSdkVersion="30" android:targetSdkVersion="34"/><uses-permission android:name="android.permission.MANAGE_EXTERNAL_STORAGE"/>
<application android:label="WiiDS SD Path Check" android:debuggable="true" android:allowBackup="false" android:theme="@android:style/Theme.Material.NoActionBar"><activity android:name="com.driftdroid.android.MicrosdCheck" android:exported="true" android:screenOrientation="landscape"/></application></manifest>''', encoding='utf-8')
classes = O / 'classes'; classes.mkdir(exist_ok=True)
dex = O / 'dex'; dex.mkdir(exist_ok=True)
stdlib = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
compiler = [jar('org.jetbrains.kotlin', n, '1.9.24') for n in ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
compiler += [stdlib, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'), jar('org.jetbrains', 'annotations', '13.0')]
env = os.environ.copy(); env['JAVA_HOME'] = str(J.parent)
with (O / 'build.log').open('w') as log:
    def run(cmd):
        subprocess.run(list(map(str, cmd)), check=True, stdout=log, stderr=subprocess.STDOUT, env=env)
    # Whole public Source.Document class; paused workbench copy is never used or changed.
    disc = O.parent / 'before/reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/rom/WiiDsDisc.kt'
    run([J/'java.exe', '-cp', ';'.join(map(str, compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler', '-no-stdlib', '-no-reflect', '-jvm-target', '11', '-classpath', f'{stdlib};{AJ}', '-d', classes, O/'MicrosdCheck.kt', disc])
    run([J/'jar.exe', 'cf', O/'classes.jar', '-C', classes, '.'])
    run([J/'java.exe', '-cp', jar('com.android.tools.build', 'builder', '8.5.2'), 'com.android.tools.r8.D8', '--min-api', '30', '--lib', AJ, '--output', dex, O/'classes.jar', stdlib])
    run([BT/'aapt2.exe', 'link', '-I', AJ, '--manifest', O/'AndroidManifest.xml', '-o', O/'unsigned.apk'])
    with zipfile.ZipFile(O/'unsigned.apk', 'a') as z:
        for path in dex.glob('*.dex'): z.write(path, path.name)
    run([BT/'zipalign.exe', '-f', '4', O/'unsigned.apk', O/'aligned.apk'])
    run([BT/'apksigner.bat', 'sign', '--ks', Path.home()/'.android/debug.keystore', '--ks-key-alias', 'androiddebugkey', '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', O/'microsdtests.apk', O/'aligned.apk'])
    run([BT/'apksigner.bat', 'verify', O/'microsdtests.apk'])
report = {'status': 'BUILT_NOT_RUN', 'apk': str(O/'microsdtests.apk'), 'sha256': hashlib.sha256((O/'microsdtests.apk').read_bytes()).hexdigest(), 'package': pkg, 'activity': 'com.driftdroid.android.MicrosdCheck', 'fixed_source_sha256': hashlib.sha256(fixed.read_bytes()).hexdigest(), 'public_source_sha256': hashlib.sha256(old.read_bytes()).hexdigest(), 'no_adb_actions': True, 'source_read_limit_bytes': 544, 'owner_files_written': False}
(O/'build-verification.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
