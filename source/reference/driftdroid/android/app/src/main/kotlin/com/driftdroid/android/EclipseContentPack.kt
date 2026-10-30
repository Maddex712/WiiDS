package com.driftdroid.android

import android.content.Context
import android.net.Uri
import com.driftdroid.android.rom.EclipseMod
import com.driftdroid.android.rom.GameInstaller
import java.io.*
import java.nio.file.*
import java.security.MessageDigest
import java.util.zip.ZipFile

/** Optional local installer, pinned by the catalog compiled into this APK. No downloaded paths. */
internal object EclipseContentPack {
    fun patch(c:Context)=File(c.applicationContext.filesDir.canonicalFile,"content-packs/eclipse-installer/eclipse.xdelta")
    fun present(c:Context)=patch(c).let { it.isFile&&it.length()==EclipseMod.PATCH_BYTES }
    fun import(c:Context,uri:Uri,progress:(String)->Unit) {
        synchronized(GameInstaller) {
            GameStorage.requireIdle(c)
            GameStorage.blocker(c)?.let { error(it) }
            check(GameStorage.busy.compareAndSet(false,true)) { "Another storage operation is running." }
        }
        val raw=patch(c);val folder=raw.parentFile!!
        val archive=File(folder,"pack.zip.part");val staged=File(folder,"eclipse.xdelta.part")
        try {
            // These exact private paths belong only to this installer.
            folder.mkdirs()
            check(!Files.isSymbolicLink(folder.toPath())&&!Files.isSymbolicLink(archive.toPath())&&!Files.isSymbolicLink(staged.toPath())) { "Linked pack storage is not supported." }
            val catalog=org.json.JSONObject(c.assets.open("content/catalog.json").bufferedReader().use { it.readText() }).getJSONArray("packs")
            val spec=(0 until catalog.length()).map { catalog.getJSONObject(it) }.single { it.getString("id")=="eclipse-installer" }
            val total=spec.getLong("bytes");val hash=MessageDigest.getInstance("SHA-256");var done=0L
            c.contentResolver.openInputStream(uri).use { input ->
                checkNotNull(input) { "The selected installer couldn't be opened." }
                FileOutputStream(archive).use { output ->
                    val buffer=ByteArray(1024*1024)
                    while(true) { val n=input.read(buffer);if(n<0)break;done+=n;check(done<=total) { "The installer exceeds the supported size." };hash.update(buffer,0,n);output.write(buffer,0,n);progress("Checking Eclipse installer... ${done*100/total}%") }
                    output.fd.sync()
                }
            }
            fun hex(b:ByteArray)=b.joinToString("") { "%02x".format(it.toInt() and 255) }
            check(done==total&&hex(hash.digest())==spec.getString("sha256")) { "This installer doesn't match this WiiDS build. Your games and saves were kept." }
            ZipFile(archive).use { zip ->
                val entry=zip.getEntry("eclipse.xdelta")?:error("The installer is missing its patch.")
                check(zip.size()==1&&entry.size==EclipseMod.PATCH_BYTES) { "Unexpected installer contents." }
                val expected=spec.getJSONArray("files").getJSONObject(0).getString("sha256")
                hash.reset();done=0
                zip.getInputStream(entry).use { input -> FileOutputStream(staged).use { output ->
                    val buffer=ByteArray(1024*1024)
                    while(true) { val n=input.read(buffer);if(n<0)break;done+=n;check(done<=EclipseMod.PATCH_BYTES);hash.update(buffer,0,n);output.write(buffer,0,n);progress("Preparing Eclipse patch... ${done*100/EclipseMod.PATCH_BYTES}%") }
                    output.fd.sync()
                } }
                check(done==EclipseMod.PATCH_BYTES&&hex(hash.digest())==expected) { "The Eclipse patch didn't verify." }
            }
            Files.move(staged.toPath(),raw.toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING)
        } finally { archive.delete();staged.delete();GameStorage.busy.set(false) }
    }
}
