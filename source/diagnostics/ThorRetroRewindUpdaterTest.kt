package com.driftdroid.android

import java.io.*
import java.net.*
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** Host fixtures only: no network, Android device, real pack, game data or generated code. */
fun main(args: Array<String>) {
    val base = File(args.single()).absoluteFile
    require(!base.exists()) { "Use a fresh fixture output directory" }
    base.mkdirs()
    val passed = mutableListOf<String>()
    val goodCode = "fixture-native-code".toByteArray()
    val goodHash = RetroRewindCatalog.sha256Hex(goodCode)
    val source = RetroRewindCatalog.Release("6.13.1", "https://update.rwfc.net/fixture.zip")
    fun test(name: String, block: (File) -> Unit) {
        val folder = File(base, name).apply { mkdirs() }
        block(folder); passed += name; println("PASS $name")
    }
    fun installed(folder: File): File = File(folder, "RetroRewind6").apply {
        File(this, "Binaries").mkdirs(); File(this, "Binaries/Code.pul").writeBytes(goodCode)
        File(this, "version.txt").writeText("6.13.0\n")
        for (path in listOf("Ghosts/local.rkg", "MyStuff/local.txt", "unknown/personal.txt")) {
            File(this, path).apply { parentFile.mkdirs(); writeText("keep:$path") }
        }
        File(this, "MyStuff/empty").mkdirs()
        File(folder, "riivolution/save/profile.dat").apply { parentFile.mkdirs(); writeText("save") }
    }
    fun archive(folder: File, entries: List<Pair<String, ByteArray>>): File = File(folder, "delta.zip").apply {
        ZipOutputStream(outputStream()).use { zip -> entries.forEach { (name, bytes) ->
            zip.putNextEntry(ZipEntry(name)); zip.write(bytes); zip.closeEntry()
        } }
    }
    fun pack(folder: File) = archive(folder, listOf("RetroRewind6/Tracks/example.szs" to "new-course".toByteArray()))
    fun bytes(root: File): Map<String, List<Byte>> = root.walkTopDown().filter { it.isFile }.associate {
        it.relativeTo(root).invariantSeparatorsPath to it.readBytes().toList()
    }
    fun repo(zip: File, verdict: RetroRewindCatalog.DeltaVerdict = RetroRewindCatalog.DeltaVerdict.AssetsOnly,
             releases: List<RetroRewindCatalog.Release> = listOf(source)) = object : ThorRetroRewindUpdater.Repository {
        override fun releases() = releases
        override fun inspect(release: RetroRewindCatalog.Release) = verdict
        override fun download(release: RetroRewindCatalog.Release, file: File, cancelled: () -> Boolean,
                              progress: (Long, Long) -> Unit) { zip.copyTo(file, overwrite = true); progress(file.length(), file.length()) }
    }
    fun engine(root: File, repository: ThorRetroRewindUpdater.Repository, retained: () -> Boolean = { false },
               cancelled: () -> Boolean = { false }, progress: (ThorRetroRewindUpdateStatus.Snapshot) -> Unit = {},
               move: (File, File) -> Unit = { a, b -> Files.move(a.toPath(), b.toPath(), StandardCopyOption.ATOMIC_MOVE); Unit },
               syncDirectory: (File) -> Unit = {},
               supportedLoaderXml: (String) -> Boolean = { false },
               pinnedArchive: (String) -> Pair<Long, String>? = { null },
               knownCode: (String) -> String? = { null }, minFullArchiveBytes: Long = 1) =
        ThorRetroRewindUpdater(root, File(root.parentFile, "cache"), repository, retained, cancelled, progress,
            supportedCode = { it == goodHash }, move = move, reserveBytes = 0, syncDirectory = syncDirectory,
            supportedLoaderXml = supportedLoaderXml, pinnedArchive = pinnedArchive, knownCode = knownCode,
            minFullArchiveBytes = minFullArchiveBytes)
    fun fails(block: () -> Unit): Throwable = try { block(); error("Expected failure") } catch (e: Throwable) {
        check(e.message != "Expected failure"); e
    }
    test("compatible-assets-and-personal-content") { f ->
        val root = installed(f); val before = bytes(root); val observations = mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        engine(root, repo(pack(f)), progress = observations::add).update()
        check(File(root, "version.txt").readText().trim() == "6.13.1")
        before.filterKeys { it != "version.txt" }.forEach { (p, v) -> check(File(root,p).readBytes().toList() == v) }
        check(File(root,"MyStuff/empty").isDirectory)
        check(File(f,"riivolution/save/profile.dat").readText() == "save")
        check(observations.last().state == ThorRetroRewindUpdateStatus.State.COMPLETE)
        check(!ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("known-external-loader-xml-validated-never-extracted") { f ->
        val root=installed(f); val xml="synthetic-supported-loader".toByteArray()
        val zip=archive(f,listOf("riivolution/" to byteArrayOf(), "riivolution/RetroRewind6.xml" to xml,
            "RetroRewind6/Tracks/example.szs" to "new-course".toByteArray()))
        engine(root,repo(zip),supportedLoaderXml={it==RetroRewindCatalog.sha256Hex(xml)}).update()
        check(File(root,"version.txt").readText().trim()=="6.13.1")
        check(!File(root,"riivolution").exists());check(!File(f,"riivolution/RetroRewind6.xml").exists())
        check(File(f,"riivolution/save/profile.dat").readText()=="save")
    }
    test("external-loader-xml-crc-is-checked-before-skip") { f ->
        val root=installed(f); val before=bytes(root); val zip=File(f,"delta.zip")
        val payload="CRC-LOADER-FIXTURE".toByteArray()
        ZipOutputStream(zip.outputStream()).use { out ->
            val crc=java.util.zip.CRC32().apply {update(payload)}
            val entry=ZipEntry("riivolution/RetroRewind6.xml").apply {
                method=ZipEntry.STORED;size=payload.size.toLong();compressedSize=size;setCrc(crc.value)
            }
            out.putNextEntry(entry);out.write(payload);out.closeEntry()
        }
        val content=zip.readBytes()
        val at=(0..content.size-payload.size).first {i -> payload.indices.all {content[i+it]==payload[it]}}
        content[at]=(content[at].toInt() xor 1).toByte();zip.writeBytes(content)
        fails {engine(root,repo(zip),supportedLoaderXml={true}).update()}
        check(bytes(root)==before);check(!File(root,"riivolution").exists())
    }
    test("changed-external-loader-xml-needs-build") { f ->
        val root=installed(f); val before=bytes(root)
        val zip=archive(f,listOf("riivolution/RetroRewind6.xml" to "changed-loader".toByteArray()))
        val failure=fails {engine(root,repo(zip),supportedLoaderXml={it==RetroRewindCatalog.sha256Hex("supported-loader".toByteArray())}).update()}
        check(failure.message==ThorRetroRewindUpdater.NEED_BUILD);check(bytes(root)==before)
        check(File(f,"riivolution/save/profile.dat").readText()=="save")
    }
    for (path in listOf("riivolution/save/profile.dat", "riivolution/Other.xml", "RetroRewind6/riivolution/RetroRewind6.xml",
        "Riivolution/RetroRewind6.xml", "riivolution/RetroRewind6.xml/")) test("external-loader-reject-"+path.replace('/','_')) { f ->
        val root=installed(f); val before=bytes(root)
        val zip=archive(f,listOf(path to "unauthorized".toByteArray()))
        fails {engine(root,repo(zip),supportedLoaderXml={true}).update()}
        check(bytes(root)==before);check(File(f,"riivolution/save/profile.dat").readText()=="save")
    }
    test("retained-game-check-but-no-update") { f ->
        val root=installed(f); val before=bytes(root); val e=engine(root,repo(pack(f)),retained={true})
        check(e.check()?.targetVersion == "6.13.1"); fails { e.update() }; check(bytes(root)==before)
        check(!File(f,"cache").exists())
    }
    test("unsupported-preflight-no-mutation") { f ->
        val root=installed(f); val before=bytes(root); val states=mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        val e=engine(root,repo(pack(f),RetroRewindCatalog.DeltaVerdict.UnsupportedCodeChange("bad")),progress=states::add)
        e.update(); check(bytes(root)==before); check(states.last().state==ThorRetroRewindUpdateStatus.State.BLOCKED)
        check(!File(f,"cache").exists())
    }
    test("unsupported-real-code-despite-preflight") { f ->
        val root=installed(f); val before=bytes(root)
        val zip=archive(f,listOf("RetroRewind6/Binaries/Code.pul" to byteArrayOf(1,2,3)))
        fails { engine(root,repo(zip)).update() }; check(bytes(root)==before); check(!ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("supported-code-archive") { f ->
        val root=installed(f); val zip=archive(f,listOf("Binaries/Code.pul" to goodCode))
        engine(root,repo(zip,RetroRewindCatalog.DeltaVerdict.SupportedCodeChange)).update()
        check(File(root,"Binaries/Code.pul").readBytes().contentEquals(goodCode))
    }
    for ((name,path) in listOf("traversal" to "RetroRewind6/Tracks/../../oops", "absolute" to "/Tracks/a",
        "backslash" to "RetroRewind6/Tracks\\bad", "ghost-protected" to "RetroRewind6/Ghosts/local.rkg",
        "mystuff-protected" to "RetroRewind6/MyStuff/local.txt", "unknown-protected" to "RetroRewind6/unknown/personal.txt")) {
        test(name) { f -> val root=installed(f); val before=bytes(root)
            fails { engine(root,repo(archive(f,listOf(path to byteArrayOf(1))))).update() }; check(bytes(root)==before)
        }
    }
    test("case-conflicting-zip") { f ->
        val root=installed(f); val before=bytes(root)
        val zip=archive(f,listOf("Tracks/a" to byteArrayOf(1),"Tracks/A" to byteArrayOf(2)))
        fails { engine(root,repo(zip)).update() }; check(bytes(root)==before)
    }
    test("file-directory-conflict-rolls-back") { f ->
        val root=installed(f); val before=bytes(root)
        val zip=archive(f,listOf("Tracks/a" to byteArrayOf(1),"Tracks/a/b" to byteArrayOf(2)))
        fails { engine(root,repo(zip)).update() }; check(bytes(root)==before); check(!ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("corrupt-zip-can-retry") { f ->
        val root=installed(f); val before=bytes(root); val zip=File(f,"bad.zip").apply {writeText("not zip")}
        fails { engine(root,repo(zip)).update() }; check(bytes(root)==before)
        engine(root,repo(pack(f))).update(); check(File(root,"version.txt").readText().trim()=="6.13.1")
    }
    test("identical-manifest-version-deduplicated") { f ->
        val root=installed(f)
        val plan=engine(root,repo(pack(f),releases=listOf(source,source.copy(version="6.13.1.0")))).check()!!
        check(plan.deltas==listOf(source))
    }
    test("historical-multipart-manifest-does-not-block-new-release") { f ->
        val root=installed(f)
        val old=listOf(
            RetroRewindCatalog.Release("4.0.0","https://cdn.update.rwfc.net/RetroRewind/zip/1000.zip"),
            RetroRewindCatalog.Release("4.0.0","https://cdn.update.rwfc.net/RetroRewind/zip/1000Music.zip"))
        check(engine(root,repo(pack(f),releases=old+source)).check()!!.deltas==listOf(source))
    }
    test("multipart-forward-release-order") { f ->
        val root=installed(f); val extra=source.copy(deltaUrl="https://update.rwfc.net/music.zip")
        check(engine(root,repo(pack(f),releases=listOf(source,extra,source))).check()!!.deltas==listOf(source,extra))
    }
    test("check-publishes-installed-before-network-failure") { f ->
        val root=installed(f); val states=mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        val repository=object:ThorRetroRewindUpdater.Repository by repo(pack(f)) {
            override fun releases():List<RetroRewindCatalog.Release> = throw IOException("offline")
        }
        fails { engine(root,repository,progress=states::add).check() }
        check(states.last().currentVersion=="6.13.0")
    }
    test("complete-corrupt-live-restores-intact-backup") { f ->
        val root=installed(f); val before=bytes(root); val token=UUID.randomUUID().toString()
        root.copyRecursively(File(f,".thor-rr-backup-$token"))
        File(f,".thor-rr-update.sha256").writeText(root.walkTopDown().filter { it.isFile }.joinToString("\n",postfix="\n") {
            "${RetroRewindCatalog.sha256Hex(it.readBytes())}\t${it.length()}\t${it.relativeTo(root).invariantSeparatorsPath}"
        })
        File(root,"Binaries/Code.pul").writeText("corrupted")
        File(f,".thor-rr-update.properties").writeText("token=$token\nphase=COMPLETE\n")
        engine(root,repo(pack(f))).recoverPending()
        check(bytes(root)==before);check(!ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("complete-missing-receipt-restores-intact-backup") { f ->
        val root=installed(f); val before=bytes(root); val token=UUID.randomUUID().toString()
        root.copyRecursively(File(f,".thor-rr-backup-$token"))
        File(root,"Binaries/Code.pul").writeText("corrupted")
        File(f,".thor-rr-update.properties").writeText("token=$token\nphase=COMPLETE\n")
        engine(root,repo(pack(f))).recoverPending()
        check(bytes(root)==before)
    }
    test("multipart-code-change-blocks-entire-version") { f ->
        val root=installed(f); val extra=source.copy(deltaUrl="https://update.rwfc.net/code.zip")
        val repository=object:ThorRetroRewindUpdater.Repository by repo(pack(f),releases=listOf(source,extra)) {
            override fun inspect(release:RetroRewindCatalog.Release)=if(release==extra)
                RetroRewindCatalog.DeltaVerdict.UnsupportedCodeChange("uncompiled") else RetroRewindCatalog.DeltaVerdict.AssetsOnly
        }
        check(engine(root,repository).check()==null)
    }
    test("check-publishes-latest-before-inspection-failure") { f ->
        val root=installed(f); val states=mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        val repository=object:ThorRetroRewindUpdater.Repository by repo(pack(f)) {
            override fun inspect(release:RetroRewindCatalog.Release)=RetroRewindCatalog.DeltaVerdict.Failed("offline")
        }
        fails { engine(root,repository,progress=states::add).check() }
        check(states.last().currentVersion=="6.13.0" && states.last().latestVersion=="6.13.1")
    }
    test("complete-without-receipt-or-backup-fails-closed") { f ->
        val root=installed(f); val before=bytes(root); val token=UUID.randomUUID().toString()
        File(f,".thor-rr-update.properties").writeText("token=$token\nphase=COMPLETE\n")
        fails { engine(root,repo(pack(f))).recoverPending() }
        check(bytes(root)==before);check(ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("stage-and-parent-durability-before-publication") { f ->
        val root=installed(f); val events=mutableListOf<String>()
        val zip=archive(f,listOf("Tracks/new/subdir/example.szs" to "new".toByteArray()))
        engine(root,repo(zip),move={a,b ->
            if(a==root) {
                check(events.contains("dir:MyStuff/empty"))
                check(events.indexOf("dir:Tracks/new/subdir") < events.indexOf("dir:Tracks/new"))
                check(events.indexOf("dir:Tracks/new") < events.indexOf("dir:Tracks"))
                check(events.last()=="parent:PREPARED")
            }
            events += "move:${a.name}->${b.name}"
            Files.move(a.toPath(),b.toPath(),StandardCopyOption.ATOMIC_MOVE)
        },syncDirectory={folder ->
            if(folder==f) {
                val journal=File(f,".thor-rr-update.properties")
                val phase=if(journal.exists()) journal.readLines().first {it.startsWith("phase=")}.substringAfter('=') else "CLEARED"
                if(phase=="CLEARED" && File(f,".thor-rr-update.sha256").exists()) events += "receipt-retained-until-journal-durable"
                events += "parent:$phase"
            } else {
                val stage=folder.toPath().iterator().asSequence().map { it.toString() }.first { it.startsWith(".thor-rr-stage-") }
                events += "dir:"+folder.absolutePath.substringAfter(stage).trimStart(File.separatorChar).replace(File.separatorChar,'/')
            }
        }).update()
        check(events.contains("receipt-retained-until-journal-durable"))
        val firstMove=events.indexOfFirst {it.startsWith("move:")}
        check(events[firstMove+1]=="parent:PREPARED")
        val secondMove=events.indexOfLast {it.startsWith("move:")}
        check(events[secondMove+1]=="parent:SAVED")
        check(events.last()=="parent:CLEARED")
    }
    // Inject a sustained storage failure at each durability barrier, then retry with healthy
    // storage. This checks journal ordering and recovery, not physical hardware power-loss.
    var barrierCount=0
    test("count-durability-barriers") { f ->
        val root=installed(f)
        engine(root,repo(pack(f)),syncDirectory={barrierCount++}).update()
        check(barrierCount>=15)
    }
    for (barrier in 1..barrierCount) test("durability-barrier-$barrier-recovery") { f ->
        val root=installed(f); val before=bytes(root); var calls=0
        fails { engine(root,repo(pack(f)),syncDirectory={
            calls++;if(calls>=barrier) throw IOException("injected storage sync failure")
        }).update() }
        if(ThorRetroRewindUpdater.needsRecovery(root)) engine(root,repo(pack(f))).recoverPending()
        val version=File(root,"version.txt").readText().trim()
        check(version=="6.13.0" || version=="6.13.1")
        before.filterKeys { it!="version.txt" }.forEach {(p,b) -> check(File(root,p).readBytes().toList()==b)}
        check(File(root,"MyStuff/empty").isDirectory)
        if(version=="6.13.0") check(bytes(root)==before)
        else check(File(root,"Tracks/example.szs").readText()=="new-course")
        check(!ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("current-up-to-date") { f ->
        val root=installed(f); val states=mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        check(engine(root,repo(pack(f),releases=listOf(source.copy(version="6.13.0"))),progress=states::add).check()==null)
        check(states.last().state==ThorRetroRewindUpdateStatus.State.UP_TO_DATE)
    }
    for (ordinal in 1..2) for (after in listOf(false,true)) test("rename-$ordinal-${if(after)"after" else "before"}") { f ->
        val root=installed(f); val before=bytes(root); var calls=0
        val e=engine(root,repo(pack(f)),move={a,b ->
            calls++
            if(calls==ordinal&&!after) throw IOException("injected rename failure")
            Files.move(a.toPath(),b.toPath(),StandardCopyOption.ATOMIC_MOVE)
            if(calls==ordinal&&after) throw IOException("injected lost rename reply")
        })
        fails { e.update() }; check(bytes(root)==before); check(!ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("verification-failure-rolls-back") { f ->
        val root=installed(f); val before=bytes(root); var calls=0
        fails { engine(root,repo(pack(f)),move={a,b ->
            Files.move(a.toPath(),b.toPath(),StandardCopyOption.ATOMIC_MOVE); calls++
            if(calls==2) File(root,"Binaries/Code.pul").writeText("corrupted write")
        }).update() }; check(bytes(root)==before)
    }
    test("cancel-before-commit") { f ->
        val root=installed(f); val before=bytes(root); var cancel=false
        fails { engine(root,repo(pack(f)),cancelled={cancel},progress={if(it.state==ThorRetroRewindUpdateStatus.State.STAGING)cancel=true}).update() }
        check(bytes(root)==before); check(!ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("cancel-during-commit-finishes") { f ->
        val root=installed(f); var cancel=false
        engine(root,repo(pack(f)),cancelled={cancel},progress={if(it.state==ThorRetroRewindUpdateStatus.State.COMMITTING)cancel=true}).update()
        check(File(root,"version.txt").readText().trim()=="6.13.1")
    }
    for (newRoot in listOf(false,true)) test("offline-recovery-${if(newRoot)"unacknowledged-new" else "missing-live"}") { f ->
        val root=installed(f); val before=bytes(root); val token=UUID.randomUUID().toString()
        root.renameTo(File(f,".thor-rr-backup-$token"))
        if(newRoot) { root.mkdir(); File(root,"bad").writeText("bad") }
        File(f,".thor-rr-update.properties").writeText("token=$token\nphase=SAVED\n")
        val repo=object: ThorRetroRewindUpdater.Repository {
            override fun releases():List<RetroRewindCatalog.Release> = error("must not use network")
            override fun inspect(release:RetroRewindCatalog.Release):RetroRewindCatalog.DeltaVerdict = error("must not inspect")
            override fun download(release:RetroRewindCatalog.Release,file:File,cancelled:()->Boolean,progress:(Long,Long)->Unit)=error("must not download")
        }
        check(ThorRetroRewindUpdater.needsRecovery(root)); engine(root,repo).recoverPending()
        check(bytes(root)==before); check(!ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("crc-corruption-rejected") { f ->
        val root=installed(f); val before=bytes(root)
        val zip=archive(f,listOf("Tracks/crc.bin" to "CORRUPT-ME".toByteArray()))
        // Rebuild as STORED so a changed payload retains the original declared CRC.
        ZipOutputStream(zip.outputStream()).use { out ->
            val payload="CORRUPT-ME".toByteArray();val crc=java.util.zip.CRC32().apply {update(payload)}
            val entry=ZipEntry("Tracks/crc.bin").apply {method=ZipEntry.STORED;size=payload.size.toLong();compressedSize=size;setCrc(crc.value)}
            out.putNextEntry(entry);out.write(payload);out.closeEntry()
        }
        val content=zip.readBytes();val marker="CORRUPT-ME".toByteArray()
        val at=(0..content.size-marker.size).first {i -> marker.indices.all {content[i+it]==marker[it]}}
        content[at]=(content[at].toInt() xor 1).toByte();zip.writeBytes(content)
        fails {engine(root,repo(zip)).update()};check(bytes(root)==before)
    }
    test("archive-changes-after-preflight") { f ->
        val root=installed(f);val before=bytes(root);val zip=pack(f)
        fails {engine(root,repo(zip),progress={state ->
            if(state.state==ThorRetroRewindUpdateStatus.State.STAGING) {
                val cached=File(f,"cache").listFiles()!!.single {it.extension=="zip"}
                archive(File(f,"replacement").apply{mkdir()},listOf("RetroRewind6/Tracks/example.szs" to "changed".toByteArray())).copyTo(cached,true)
            }
        }).update()};check(bytes(root)==before)
    }
    test("compatible-prefix-stops-at-new-code") { f ->
        val root=installed(f);val states=mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        val a=source;val b=source.copy(version="6.14.0");val c=source.copy(version="6.14.1")
        val repository=object:ThorRetroRewindUpdater.Repository {
            override fun releases()=listOf(a,b,c)
            override fun inspect(release:RetroRewindCatalog.Release)=if(release==b) RetroRewindCatalog.DeltaVerdict.UnsupportedCodeChange("uncompiled") else RetroRewindCatalog.DeltaVerdict.AssetsOnly
            override fun download(release:RetroRewindCatalog.Release,file:File,cancelled:()->Boolean,progress:(Long,Long)->Unit)=error("check only")
        }
        val plan=engine(root,repository,progress=states::add).check()!!
        check(plan.targetVersion==a.version && plan.latestVersion==c.version && plan.deltas==listOf(a))
        check(states.last().canUpdate && states.last().message.contains("compatible Thor build"))
    }
    test("rollback-failure-keeps-recovery-journal") { f ->
        val root=installed(f);val before=bytes(root);val zip=pack(f);var calls=0
        val failure=fails {engine(root,repo(zip),move={a,b ->
            calls++;if(calls in 2..3) throw IOException("injected unavailable rename")
            Files.move(a.toPath(),b.toPath(),StandardCopyOption.ATOMIC_MOVE)
        }).update()}
        check(failure.suppressed.isNotEmpty());check(ThorRetroRewindUpdater.needsRecovery(root))
        engine(root,repo(zip)).recoverPending();check(bytes(root)==before)
    }
    class Connection(val code:Int,val content:InputStream,val headers:Map<String,String>,val size:Long):HttpURLConnection(URL(source.deltaUrl)) {
        override fun connect() {}; override fun disconnect() {}; override fun usingProxy()=false
        override fun getResponseCode()=code; override fun getHeaderField(name:String)=headers[name]
        override fun getContentLengthLong()=size; override fun getInputStream()=content
    }
    fun download(file:File,connection:Connection) = ThorRetroRewindUpdater.downloadOfficial(source.deltaUrl,file,{false},{_,_->},{connection})
    test("network-interruption-resumes-with-validator") { f ->
        val file=File(f,"cached.zip"); var count=0
        val broken=object:InputStream() {
            override fun read():Int = if(count++<3) "abc"[count-1].code else throw IOException("connection lost")
            override fun read(b:ByteArray,o:Int,l:Int):Int { if(count>0)throw IOException("connection lost"); "abc".toByteArray().copyInto(b,o);count=3;return 3 }
        }
        fails { download(file,Connection(200,broken,mapOf("ETag" to "\"v1\""),6)) }
        check(File(file.path+".part").readText()=="abc")
        val next=Connection(206,ByteArrayInputStream("def".toByteArray()),mapOf("ETag" to "\"v1\"","Content-Range" to "bytes 3-5/6"),3)
        download(file,next); check(next.getRequestProperty("Range")=="bytes=3-")
        check(next.getRequestProperty("If-Range")=="\"v1\"");check(file.readText()=="abcdef")
    }
    test("range-ignored-restarts") { f ->
        val file=File(f,"cached.zip");File(file.path+".part").writeText("old")
        File(file.path+".resume").writeText("url=${source.deltaUrl}\nvalidator=v1\n")
        download(file,Connection(200,ByteArrayInputStream("fresh".toByteArray()),mapOf("ETag" to "v2"),5))
        check(file.readText()=="fresh")
    }
    test("no-validator-restarts-partial") { f ->
        val file=File(f,"cached.zip");File(file.path+".part").writeText("old")
        File(file.path+".resume").writeText("url=${source.deltaUrl}\nvalidator=\n")
        val connection=Connection(200,ByteArrayInputStream("new".toByteArray()),emptyMap(),3)
        download(file,connection);check(connection.getRequestProperty("Range")==null);check(file.readText()=="new")
    }
    for (kind in listOf("range","validator","expired")) test("bad-resume-$kind") { f ->
        val file=File(f,"cached.zip");File(file.path+".part").writeText("abc")
        File(file.path+".resume").writeText("url=${source.deltaUrl}\nvalidator=v1\n")
        val headers=mapOf("ETag" to if(kind=="validator") "v2" else "v1", "Content-Range" to if(kind=="range")"bytes 0-2/6" else "bytes 3-5/6")
        fails { download(file,Connection(if(kind=="expired")416 else 206,ByteArrayInputStream("def".toByteArray()),headers,3)) }
        check(!File(file.path+".part").exists());check(!File(file.path+".resume").exists())
    }
    test("redirect-off-official-host-rejected") { f ->
        fails { download(File(f,"cached.zip"),Connection(302,ByteArrayInputStream(byteArrayOf()),mapOf("Location" to "https://untrusted.invalid/file"),0)) }
        check(!File(f,"cached.zip").exists())
    }
    // ---- First install: no pack yet; the full archive of the newest runnable lineage, then deltas.
    val base613 = RetroRewindCatalog.Release("6.13.0", "https://update.rwfc.net/RetroRewind/zip/6.13.0.zip")
    val loaderXml = "first-install-loader".toByteArray()
    val loaderOk: (String) -> Boolean = { it == RetroRewindCatalog.sha256Hex(loaderXml) }
    fun fullArchive(folder: File, extra: List<Pair<String, ByteArray>> = emptyList(), code: ByteArray = goodCode): File =
        File(folder, "full.zip").apply {
            ZipOutputStream(outputStream()).use { zip ->
                for ((name, data) in listOf(
                    "apps/RetroRewind/boot.dol" to "wii-only".toByteArray(),
                    "RetroRewind.wad" to "wad".toByteArray(),
                    "RetroRewindChannel/runtime-ext-P.dol" to "channel".toByteArray(),
                    "riivolution/RetroRewind6.xml" to loaderXml,
                    "RetroRewind6/version.txt" to "6.13.0".toByteArray(),
                    "RetroRewind6/Binaries/Code.pul" to code,
                    "RetroRewind6/Tracks/base.szs" to "base-course".toByteArray(),
                    "RetroRewind6/Ghosts/ExpertsCT/0_150.rkg" to "expert".toByteArray()) + extra) {
                    zip.putNextEntry(ZipEntry(name)); zip.write(data); zip.closeEntry()
                }
                zip.putNextEntry(ZipEntry("RetroRewind6/MyStuff/")); zip.closeEntry()
            }
        }
    class FirstRepo(val full: File, val delta: File,
                    val fullSize: (RetroRewindCatalog.Release) -> Long,
                    val verdict: (RetroRewindCatalog.Release) -> RetroRewindCatalog.DeltaVerdict,
                    val list: List<RetroRewindCatalog.Release>) : ThorRetroRewindUpdater.Repository {
        var fullDownloads = 0
        override fun releases() = list
        override fun inspect(release: RetroRewindCatalog.Release) = verdict(release)
        override fun download(release: RetroRewindCatalog.Release, file: File, cancelled: () -> Boolean,
                              progress: (Long, Long) -> Unit) { delta.copyTo(file, overwrite = true); progress(file.length(), file.length()) }
        override fun fullArchiveSize(release: RetroRewindCatalog.Release) = fullSize(release)
        override fun downloadFull(release: RetroRewindCatalog.Release, file: File, cancelled: () -> Boolean,
                                  progress: (Long, Long) -> Unit) {
            fullDownloads++
            if (!file.isFile) full.copyTo(file, overwrite = true)
            progress(file.length(), file.length())
        }
    }
    fun firstRepo(f: File, full: File = fullArchive(f),
                  fullSize: (RetroRewindCatalog.Release) -> Long = { if (it == base613) full.length() else -1L },
                  verdict: (RetroRewindCatalog.Release) -> RetroRewindCatalog.DeltaVerdict = {
                      if (it == base613) RetroRewindCatalog.DeltaVerdict.SupportedCodeChange else RetroRewindCatalog.DeltaVerdict.AssetsOnly },
                  list: List<RetroRewindCatalog.Release> = listOf(base613, source)) = FirstRepo(full, pack(f), fullSize, verdict, list)
    fun absentRoot(f: File) = File(f, "RetroRewind6")
    fun checkInstalledFirst(f: File, root: File) {
        check(File(root, "version.txt").readText().trim() == "6.13.1")
        check(File(root, "Binaries/Code.pul").readBytes().contentEquals(goodCode))
        check(File(root, "Tracks/base.szs").readText() == "base-course")
        check(File(root, "Tracks/example.szs").readText() == "new-course")
        check(File(root, "Ghosts/ExpertsCT/0_150.rkg").readText() == "expert")
        check(File(root, "MyStuff").isDirectory)
        // Wii-only installer files and the loader XML are never written.
        check(!File(root, "apps").exists() && !File(f, "apps").exists() && !File(root, "RetroRewind.wad").exists())
        check(!File(root, "riivolution").exists() && !File(f, "riivolution").exists() && !File(f, "RetroRewindChannel").exists())
        check(!ThorRetroRewindUpdater.needsRecovery(root))
    }
    fun cachedFull(f: File) = File(f, "cache").listFiles()?.filter { it.name.contains("-full-") } ?: emptyList()
    test("first-install-check-plan") { f ->
        val root = absentRoot(f); val states = mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        val plan = engine(root, firstRepo(f), progress = states::add, supportedLoaderXml = loaderOk).check()!!
        check(plan.firstInstall && plan.base == base613 && plan.deltas == listOf(source) && plan.targetVersion == "6.13.1")
        check(states.last().state == ThorRetroRewindUpdateStatus.State.READY && states.last().canUpdate)
        check(states.last().currentVersion == null)
        check(!root.exists() && !File(f, "cache").exists())
    }
    test("first-install-complete") { f ->
        val root = absentRoot(f); val states = mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        engine(root, firstRepo(f), progress = states::add, supportedLoaderXml = loaderOk).update()
        checkInstalledFirst(f, root)
        check(states.last().state == ThorRetroRewindUpdateStatus.State.COMPLETE && states.last().currentVersion == "6.13.1")
        check(cachedFull(f).isEmpty())
        // Now an ordinary installed pack: the next check is an update check.
        check(engine(root, repo(pack(f), releases = listOf(source)), supportedLoaderXml = loaderOk).check() == null)
    }
    test("first-install-full-only-no-deltas") { f ->
        val root = absentRoot(f)
        engine(root, firstRepo(f, list = listOf(base613)), supportedLoaderXml = loaderOk).update()
        check(File(root, "version.txt").readText().trim() == "6.13.0" && !File(root, "Tracks/example.szs").exists())
        check(File(root, "Binaries/Code.pul").readBytes().contentEquals(goodCode))
    }
    test("first-install-into-empty-folder") { f ->
        val root = absentRoot(f); File(root, "MyStuff/empty").mkdirs()
        engine(root, firstRepo(f), supportedLoaderXml = loaderOk).update()
        checkInstalledFirst(f, root)
    }
    test("first-install-incomplete-pack-blocked") { f ->
        val root = absentRoot(f); File(root, "Tracks").mkdirs(); File(root, "Tracks/x.szs").writeText("partial")
        val states = mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        check(engine(root, firstRepo(f), progress = states::add, supportedLoaderXml = loaderOk).check() == null)
        check(states.last().state == ThorRetroRewindUpdateStatus.State.BLOCKED)
        check(File(root, "Tracks/x.szs").readText() == "partial" && !File(f, "cache").exists())
    }
    test("first-install-unsupported-lineage-blocked") { f ->
        val root = absentRoot(f); val states = mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        check(engine(root, firstRepo(f, verdict = { RetroRewindCatalog.DeltaVerdict.UnsupportedCodeChange("new") }),
            progress = states::add, supportedLoaderXml = loaderOk).check() == null)
        check(states.last().state == ThorRetroRewindUpdateStatus.State.BLOCKED && states.last().message == ThorRetroRewindUpdater.NEED_BUILD_FIRST)
        check(!root.exists())
    }
    test("first-install-newer-unsupported-falls-back") { f ->
        val root = absentRoot(f); val newer = RetroRewindCatalog.Release("6.14.0", "https://update.rwfc.net/6.14.0.zip")
        val plan = engine(root, firstRepo(f, list = listOf(base613, source, newer), verdict = {
            when (it) { base613 -> RetroRewindCatalog.DeltaVerdict.SupportedCodeChange
                newer -> RetroRewindCatalog.DeltaVerdict.UnsupportedCodeChange("new")
                else -> RetroRewindCatalog.DeltaVerdict.AssetsOnly } }), supportedLoaderXml = loaderOk).check()!!
        check(plan.targetVersion == "6.13.1" && plan.latestVersion == "6.14.0" && plan.base == base613)
    }
    test("first-install-no-full-archive-blocked") { f ->
        val root = absentRoot(f); val states = mutableListOf<ThorRetroRewindUpdateStatus.Snapshot>()
        check(engine(root, firstRepo(f, fullSize = { -1L }), progress = states::add, supportedLoaderXml = loaderOk).check() == null)
        check(states.last().state == ThorRetroRewindUpdateStatus.State.BLOCKED && !root.exists())
    }
    test("first-install-pinned-match") { f ->
        val root = absentRoot(f); val full = fullArchive(f)
        val pin = full.length() to RetroRewindCatalog.sha256Hex(full.readBytes())
        engine(root, firstRepo(f, full), supportedLoaderXml = loaderOk, pinnedArchive = { if (it == "6.13.0") pin else null }).update()
        checkInstalledFirst(f, root)
    }
    test("first-install-pinned-mismatch-rejected") { f ->
        val root = absentRoot(f); val full = fullArchive(f)
        fails { engine(root, firstRepo(f, full), supportedLoaderXml = loaderOk,
            pinnedArchive = { if (it == "6.13.0") full.length() to "0".repeat(64) else null }).update() }
        check(!root.exists() && !ThorRetroRewindUpdater.needsRecovery(root) && cachedFull(f).isEmpty())
    }
    for ((name, extra) in listOf("unknown-folder" to "RetroRewind6/Weird/x.bin", "traversal" to "RetroRewind6/Tracks/../../evil",
        "backslash" to "RetroRewind6/Tracks\\evil", "duplicate-case" to "RetroRewind6/tracks/base.szs")) {
        test("first-install-reject-$name") { f ->
            val root = absentRoot(f)
            fails { engine(root, firstRepo(f, fullArchive(f, listOf(extra to byteArrayOf(1)))), supportedLoaderXml = loaderOk).update() }
            check(!root.exists() && !ThorRetroRewindUpdater.needsRecovery(root))
        }
    }
    test("first-install-unsupported-code-in-archive") { f ->
        val root = absentRoot(f)
        val failure = fails { engine(root, firstRepo(f, fullArchive(f, code = byteArrayOf(9, 9))), supportedLoaderXml = loaderOk).update() }
        check(failure.message == ThorRetroRewindUpdater.NEED_BUILD_FIRST && !root.exists())
    }
    test("first-install-changed-loader-needs-build") { f ->
        val root = absentRoot(f)
        val failure = fails { engine(root, firstRepo(f), supportedLoaderXml = { false }).update() }
        check(failure.message == ThorRetroRewindUpdater.NEED_BUILD_FIRST && !root.exists())
    }
    test("first-install-cancel-before-commit-keeps-download") { f ->
        val root = absentRoot(f); var cancel = false; val repository = firstRepo(f)
        fails { engine(root, repository, cancelled = { cancel }, supportedLoaderXml = loaderOk,
            progress = { if (it.state == ThorRetroRewindUpdateStatus.State.STAGING) cancel = true }).update() }
        check(!root.exists() && !ThorRetroRewindUpdater.needsRecovery(root) && cachedFull(f).size == 1)
        engine(root, repository, supportedLoaderXml = loaderOk).update()
        checkInstalledFirst(f, root)
    }
    for (after in listOf(false, true)) test("first-install-rename-${if (after) "after" else "before"}") { f ->
        val root = absentRoot(f)
        fails { engine(root, firstRepo(f), supportedLoaderXml = loaderOk, move = { a, b ->
            if (!after) throw IOException("injected rename failure")
            Files.move(a.toPath(), b.toPath(), StandardCopyOption.ATOMIC_MOVE)
            throw IOException("injected lost rename reply")
        }).update() }
        check(!root.exists() && !ThorRetroRewindUpdater.needsRecovery(root))
        check(f.listFiles()!!.none { it.name.startsWith(".thor-rr-stage-") })
    }
    test("first-install-verification-failure-rolls-back") { f ->
        val root = absentRoot(f)
        fails { engine(root, firstRepo(f), supportedLoaderXml = loaderOk, move = { a, b ->
            Files.move(a.toPath(), b.toPath(), StandardCopyOption.ATOMIC_MOVE)
            File(root, "Binaries/Code.pul").writeText("corrupted write")
        }).update() }
        check(!root.exists() && !ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("first-install-offline-recovery-removes-unacknowledged") { f ->
        val root = absentRoot(f); val token = UUID.randomUUID().toString()
        File(root, "Binaries").mkdirs(); File(root, "Binaries/Code.pul").writeBytes(goodCode)
        File(f, ".thor-rr-update.properties").writeText("token=$token\nphase=COMMITTED\nfirst=true\n")
        val offline = object : ThorRetroRewindUpdater.Repository {
            override fun releases(): List<RetroRewindCatalog.Release> = error("must not use network")
            override fun inspect(release: RetroRewindCatalog.Release): RetroRewindCatalog.DeltaVerdict = error("must not inspect")
            override fun download(release: RetroRewindCatalog.Release, file: File, cancelled: () -> Boolean, progress: (Long, Long) -> Unit) = error("must not download")
        }
        check(ThorRetroRewindUpdater.needsRecovery(root)); engine(root, offline).recoverPending()
        check(!root.exists() && !ThorRetroRewindUpdater.needsRecovery(root))
    }
    test("first-install-copying-recovery-keeps-live-name") { f ->
        // Before PREPARED the live name was never touched, even if something sits there.
        val root = absentRoot(f); val token = UUID.randomUUID().toString()
        File(root, "Tracks").mkdirs(); File(root, "Tracks/x.szs").writeText("not ours")
        File(f, ".thor-rr-stage-$token/Tracks").mkdirs(); File(f, ".thor-rr-stage-$token/Tracks/y.szs").writeText("partial")
        File(f, ".thor-rr-update.properties").writeText("token=$token\nphase=COPYING\nfirst=true\n")
        engine(root, repo(pack(f))).recoverPending()
        check(File(root, "Tracks/x.szs").readText() == "not ours" && !File(f, ".thor-rr-stage-$token").exists())
        check(!ThorRetroRewindUpdater.needsRecovery(root))
    }
    var firstBarrierCount = 0
    test("first-install-count-durability-barriers") { f ->
        engine(absentRoot(f), firstRepo(f), supportedLoaderXml = loaderOk, syncDirectory = { firstBarrierCount++ }).update()
        check(firstBarrierCount >= 10)
    }
    for (barrier in 1..firstBarrierCount) test("first-install-durability-barrier-$barrier-recovery") { f ->
        val root = absentRoot(f); var calls = 0; val repository = firstRepo(f)
        fails { engine(root, repository, supportedLoaderXml = loaderOk, syncDirectory = {
            calls++; if (calls >= barrier) throw IOException("injected storage sync failure")
        }).update() }
        if (ThorRetroRewindUpdater.needsRecovery(root)) engine(root, repository, supportedLoaderXml = loaderOk).recoverPending()
        check(!ThorRetroRewindUpdater.needsRecovery(root))
        // Either rolled back to "not installed" or fully installed; never a partial pack.
        if (root.exists()) checkInstalledFirst(f, root)
        else {
            engine(root, repository, supportedLoaderXml = loaderOk).update()
            checkInstalledFirst(f, root)
        }
        check(repository.fullDownloads <= 2)
    }
    // ---- First install: review fixes (anchor, soft probes, recovery, cache, fail-closed commit).
    test("first-install-anchored-on-known-code") { f ->
        // Many assets-only releases above the known-good one, then one this build can't run.
        val above = (1..12).map { RetroRewindCatalog.Release("6.13.${it + 10}", "https://update.rwfc.net/a$it.zip") }
        val unsupported = RetroRewindCatalog.Release("6.14.0", "https://update.rwfc.net/new.zip")
        val inspected = mutableListOf<String>()
        val repository = object : ThorRetroRewindUpdater.Repository by firstRepo(f, list = listOf(base613) + above + unsupported) {
            override fun inspect(release: RetroRewindCatalog.Release): RetroRewindCatalog.DeltaVerdict {
                inspected += release.version
                return if (release == unsupported) RetroRewindCatalog.DeltaVerdict.UnsupportedCodeChange("new")
                else RetroRewindCatalog.DeltaVerdict.AssetsOnly
            }
        }
        val full = File(f, "full.zip"); val pin = full.length() to RetroRewindCatalog.sha256Hex(full.readBytes())
        val plan = engine(absentRoot(f), repository, supportedLoaderXml = loaderOk, pinnedArchive = { if (it == "6.13.0") pin else null },
            knownCode = { if (it == "6.13.0") goodHash else null }).check()!!
        check(plan.base == base613 && plan.targetVersion == "6.13.22" && plan.deltas == above)
        check("6.13.0" !in inspected && inspected.last() == "6.14.0")
    }
    test("first-install-probe-failure-falls-back-to-pinned") { f ->
        val full = fullArchive(f); val pin = full.length() to RetroRewindCatalog.sha256Hex(full.readBytes())
        val repository = firstRepo(f, full, fullSize = { if (it == base613) error("pinned must not be probed") else throw IOException("HTTP 403") })
        val root = absentRoot(f)
        val plan = engine(root, repository, supportedLoaderXml = loaderOk, pinnedArchive = { if (it == "6.13.0") pin else null }).check()!!
        check(plan.base == base613 && plan.baseBytes == full.length())
        engine(root, repository, supportedLoaderXml = loaderOk, pinnedArchive = { if (it == "6.13.0") pin else null }).update()
        checkInstalledFirst(f, root)
    }
    test("first-install-tiny-archive-skipped") { f ->
        val full = fullArchive(f)
        val plan = engine(absentRoot(f), firstRepo(f, full, fullSize = { if (it == source) 10L else full.length() }),
            supportedLoaderXml = loaderOk, minFullArchiveBytes = full.length()).check()!!
        check(plan.base == base613)
    }
    test("first-install-archive-version-mismatch") { f ->
        val root = absentRoot(f)
        val other = File(f, "other").apply { mkdirs() }
        val full = File(other, "full.zip").apply {
            ZipOutputStream(outputStream()).use { zip ->
                for ((name, data) in listOf("RetroRewind6/version.txt" to "6.12.0".toByteArray(), "RetroRewind6/Binaries/Code.pul" to goodCode)) {
                    zip.putNextEntry(ZipEntry(name)); zip.write(data); zip.closeEntry()
                }
            }
        }
        fails { engine(root, firstRepo(f, full), supportedLoaderXml = loaderOk).update() }
        check(!root.exists() && cachedFull(f).isEmpty())
    }
    test("first-install-recovery-verification-not-cancellable") { f ->
        val root = absentRoot(f); val repository = firstRepo(f)
        // Leave a durable COMPLETE first-install journal: fail the first cleanup barrier after it.
        var calls = 0; var completeAt = -1
        fails { engine(root, repository, supportedLoaderXml = loaderOk, syncDirectory = {
            calls++
            if (completeAt < 0 && File(f, ".thor-rr-update.properties").let { j -> j.exists() && j.readText().contains("phase=COMPLETE") }) completeAt = calls
            if (completeAt in 1 until calls) throw IOException("injected cleanup failure")
        }).update() }
        check(ThorRetroRewindUpdater.needsRecovery(root) && root.isDirectory)
        // A cancel during recovery's verification must not read as a mismatch and delete the pack.
        engine(root, repository, supportedLoaderXml = loaderOk, cancelled = { true }).recoverPending()
        checkInstalledFirst(f, root)
        check(cachedFull(f).isEmpty())
    }
    test("first-install-stale-full-downloads-pruned") { f ->
        val cache = File(f, "cache").apply { mkdirs() }
        File(cache, "6.12.0-full-0123456789abcdef.zip.part").writeText("stale partial")
        File(cache, "6.12.0-full-0123456789abcdef.zip.resume").writeText("url=x\n")
        engine(absentRoot(f), firstRepo(f), supportedLoaderXml = loaderOk).update()
        check(cachedFull(f).isEmpty())
    }
    test("first-install-commit-fails-closed-if-files-appear") { f ->
        val root = absentRoot(f); File(root, "MyStuff").mkdirs()
        fails { engine(root, firstRepo(f), supportedLoaderXml = loaderOk, progress = {
            if (it.state == ThorRetroRewindUpdateStatus.State.COMMITTING) File(root, "Tracks").apply { mkdirs(); File(this, "late.szs").writeText("late") }
        }).update() }
        check(File(root, "Tracks/late.szs").readText() == "late" && !File(root, "Binaries").exists())
        check(!ThorRetroRewindUpdater.needsRecovery(root) && f.listFiles()!!.none { it.name.startsWith(".thor-rr-stage-") })
    }
    test("first-install-cancel-message") { f ->
        var cancel = false
        val failure = fails { engine(absentRoot(f), firstRepo(f), cancelled = { cancel }, supportedLoaderXml = loaderOk,
            progress = { if (it.state == ThorRetroRewindUpdateStatus.State.STAGING) cancel = true }).update() }
        check(failure.message!!.startsWith("Installation paused"))
    }
    File(base,"results.json").writeText("{\"passed\":${passed.size},\"cases\":[${passed.joinToString(","){"\"$it\""}}],\"network\":false,\"device\":false}")
    println("${passed.size} host fixture cases passed; no network/device/game data.")
}
