package com.driftdroid.android

import android.content.Context
import com.driftdroid.android.rom.*
import com.driftdroid.android.storage.SafeOffload
import java.io.File
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean

/** Only disc files and known generated maps are disposable. Save roots never enter a plan. */
internal object GameStorage {
    val busy = AtomicBoolean(false)
    fun work(c: Context) = File(c.applicationContext.filesDir.canonicalFile, "storage-manager")
    private fun flag(c: Context, g: FrontendGame) = File(work(c), "offloaded/${g.id}")
    fun disabled(c: Context, g: FrontendGame) = flag(c,g).isFile
    fun enable(c: Context, g: FrontendGame) { flag(c,g).delete() }
    fun blocker(c: Context): String? = when {
        busy.get() -> "Wait for Manage games to finish."
        SafeOffload.pending(work(c)) -> "Open Options > Manage games to finish the interrupted offload."
        else -> null
    }
    fun discGame(d: GameCatalog.Disc) = when(d) {
        GameCatalog.Disc.MARIO_KART_PAL -> FrontendGame.MARIO_KART
        GameCatalog.Disc.NSMB_USA_REV1 -> FrontendGame.NSMB
        GameCatalog.Disc.SUNSHINE_USA -> FrontendGame.SUNSHINE
        GameCatalog.Disc.ECLIPSE_110 -> FrontendGame.ECLIPSE
        GameCatalog.Disc.BRAWL_USA_REV2 -> FrontendGame.BRAWL
        GameCatalog.Disc.GALAXY_USA -> FrontendGame.GALAXY
    }
    fun ready(c: Context, g: FrontendGame): Boolean = when(g) {
        FrontendGame.MARIO_KART -> GameCatalog.Disc.MARIO_KART_PAL.isInstalled(c)
        FrontendGame.RETRO_REWIND -> File(c.filesDir,"WiiCompiled/RetroRewind6/Binaries/Code.pul").isFile
        FrontendGame.NSMB -> ThorGameLaunch.nsmbInstalled(c)
        FrontendGame.MSMBWII -> ThorGameLaunch.msmbwiiInstalled(c)
        FrontendGame.SUNSHINE -> ThorGameLaunch.sunshineInstalled(c)
        FrontendGame.ECLIPSE -> ThorGameLaunch.eclipseInstalled(c)
        FrontendGame.GALAXY -> ThorGameLaunch.galaxyInstalled(c)
        FrontendGame.KAIZO -> ThorGameLaunch.kaizoInstalled(c)
        FrontendGame.BRAWL -> ThorGameLaunch.brawlInstalled(c)
    }
    fun requireIdle(c: Context) {
        SaveManagerProfiles.requireStopped(c)
        if (SaveManagerProfiles.busy.get() || WiiDsUpdater.installing.get() ||
            GameInstaller.running || EclipseMod.state.running || MsmbwiiMod.state.running)
            throw IOException("Finish the import, update or save operation first.")
        if (com.driftdroid.android.saves.SaveArchive.pending(SaveManagerProfiles.work(c)))
            throw IOException("Recover the interrupted save restore in Save manager first.")
    }
    fun dependency(c: Context,g: FrontendGame): String? = when {
        g==FrontendGame.MARIO_KART && ready(c,FrontendGame.RETRO_REWIND) -> "Retro Rewind uses the Mario Kart disc. Offload Retro Rewind first."
        g==FrontendGame.NSMB && ready(c,FrontendGame.MSMBWII) -> "MSMBWii uses the NSMB disc. Offload MSMBWii first."
        else -> null
    }
    fun plan(c: Context,g: FrontendGame): SafeOffload.Plan {
        val app=c.applicationContext
        val inside=app.filesDir.canonicalFile
        val outside=app.getExternalFilesDir(null)?.canonicalFile ?: File(inside,"data")
        val targets=ArrayList<SafeOffload.Target>()
        fun target(id:String,base:File,path:String) { targets+=SafeOffload.Target(id,base,path) }
        fun local(path:String) = target("disc",inside,path)
        when(g) {
            FrontendGame.MARIO_KART -> {
                local("WiiCompiled/DiscData")
                // Fixed identifiers also allow recovery after some files have been removed.
                for(n in 0 until 32) for(ext in listOf("png","json")) {
                    val stem="course_"+n.toString().padStart(2,'0')
                    target("map-$n-$ext",inside,"WiiCompiled/ThorMap/$stem.$ext")
                }
            }
            FrontendGame.RETRO_REWIND -> {
                // Keep My Stuff, pack preferences and any unknown user-created directories.
                for(part in listOf("Binaries","Tracks","Music"))
                    target(part.lowercase(),inside,"WiiCompiled/RetroRewind6/$part")
                target("maps",inside,"WiiCompiled/ThorMap/retro_rewind")
            }
            FrontendGame.NSMB -> local("games/nsmbw")
            FrontendGame.MSMBWII -> app.assets.open("content/msmb-content.txt").bufferedReader().useLines { lines ->
                lines.filter { it.isNotBlank() }.forEachIndexed { i,path -> target("mod-$i",inside,"games/mod/$path") }
            }
            FrontendGame.SUNSHINE -> local("games/sms")
            FrontendGame.ECLIPSE -> { local("games/eclipse"); target("installer",inside,"content-packs/eclipse-installer") }
            FrontendGame.GALAXY,FrontendGame.KAIZO -> {
                val ns=if(g==FrontendGame.KAIZO) "galaxy-kaizo" else "galaxy"
                for(part in listOf("files","sys","ThorHud",".ready"))
                    target(if(part==".ready") "ready" else part.lowercase(),outside,"$ns/GalaxyData/$part")
            }
            FrontendGame.BRAWL -> target("disc",outside,"brawl/game")
        }
        val protected=ArrayList<File>()
        for(game in FrontendGame.values())
            protected.addAll(SaveManagerProfiles.profile(app,game).roots.values)
        protected+=File(inside,"games/mod/msmbwiijala/save")
        protected+=File(inside,"games/mod/riivolution/config")
        protected+=File(inside,"save-manager")
        protected+=File(outside,"brawl/SaveBackups")
        protected+=File(outside,"brawl/ThorHud")
        protected+=File(outside,"brawl/Codes")
        protected+=File(outside,"brawl/sd.raw")
        protected+=File(outside,"brawl/Config.toml")
        protected+=File(outside,"custom")
        return SafeOffload.Plan(g.id,work(app),targets,protected)
    }
    fun offload(c: Context,g: FrontendGame,progress:(Long,Int)->Unit): SafeOffload.Result {
        synchronized(GameInstaller) {
            requireIdle(c)
            dependency(c,g)?.let { throw IOException(it) }
            if(!busy.compareAndSet(false,true)) throw IOException("Another storage operation is running.")
        }
        try {
            // Art repair uses this same monitor. It rechecks busy before writing anything.
            synchronized(WiiDsArt) {
                requireIdle(c)
                val p=plan(c,g)
                val f=flag(c,g); f.parentFile!!.mkdirs()
                java.io.FileOutputStream(f).use { it.write(1); it.fd.sync() }
                return SafeOffload.offload(p,{ requireIdle(c) },{ bytes,files -> progress(bytes,files) })
            }
        } finally { busy.set(false) }
    }
}
