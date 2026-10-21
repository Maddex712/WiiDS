package com.driftdroid.android.rom

import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import java.io.File

/**
 * The player's own disc images, read by libwiidsdisc (ISO/GCM, WBFS incl. split, RVZ, WIA, GCZ,
 * CISO; NKit is recognised and refused). Only headers are read to identify a file.
 */
object WiiDsDisc {
    enum class Platform { WII, GAMECUBE }

    data class Info(
        val format: String,
        val platform: Platform,
        val gameId: String,
        val disc: Int,
        val revision: Int,
        val region: String,
        val discSize: Long,
        val title: String,
    ) {
        val isNkit get() = format == "NKit"
    }

    /** Where an image lives: a file path (one or more split parts) or a picked document. */
    sealed class Source {
        abstract val name: String
        abstract fun open(context: Context): IntArray

        data class Files(val parts: List<File>) : Source() {
            override val name: String get() = parts.first().name
            val size: Long get() = parts.sumOf { it.length() }
            override fun open(context: Context): IntArray = openAll(parts.map {
                { ParcelFileDescriptor.open(it, ParcelFileDescriptor.MODE_READ_ONLY) }
            })
        }

        data class Document(val uri: Uri, override val name: String) : Source() {
            override fun open(context: Context): IntArray = openAll(listOf({
                context.contentResolver.openFileDescriptor(uri, "r") ?: error("Couldn't open $name.")
            }))
        }
    }

    private var loaded = false

    @Synchronized
    private fun load() {
        if (!loaded) { System.loadLibrary("wiidsdisc"); loaded = true }
    }

    /** libwiidsdisc also holds the art builder (WiiDsArt). */
    fun ensureLoaded() = load()

    /** Opens every part and hands the descriptors over (the native side closes them). */
    private fun openAll(openers: List<() -> ParcelFileDescriptor>): IntArray {
        val opened = ArrayList<ParcelFileDescriptor>()
        try {
            openers.forEach { opened.add(it()) }
        } catch (e: Exception) {
            opened.forEach { runCatching { it.close() } }
            throw e
        }
        return IntArray(opened.size) { opened[it].detachFd() }
    }

    /** Blocking. Returns the disc's identity, or throws with a player-facing reason. */
    fun identify(context: Context, source: Source): Info {
        load()
        val fields = nativeIdentify(source.open(context)).split('\t')
        if (fields[0] != "ok") throw IllegalArgumentException(fields.getOrElse(1) { "The file couldn't be read." })
        return Info(fields[1], if (fields[2] == "Wii") Platform.WII else Platform.GAMECUBE, fields[3],
            fields[4].toInt(), fields[5].toInt(), fields[6], fields[7].toLong(), fields.getOrElse(8) { "" })
    }

    /** Blocking. Wii: writes destDir/sys + destDir/files. Returns null on success or the reason. */
    fun extractWii(context: Context, source: Source, destDir: File): String? {
        load()
        return runCatching { nativeExtractWii(source.open(context), destDir.absolutePath) }
            .getOrElse { it.message ?: "The disc image couldn't be opened." }
    }

    /** Blocking. GameCube: writes a plain 1:1 ISO. Returns null on success or the reason. */
    fun extractGameCube(context: Context, source: Source, destIso: File): String? {
        load()
        return runCatching { nativeExtractGameCube(source.open(context), destIso.absolutePath) }
            .getOrElse { it.message ?: "The disc image couldn't be opened." }
    }

    /** [done, total] bytes of the running extraction. */
    fun progress(): LongArray = if (loaded) nativeProgress() else longArrayOf(0, 0)

    fun cancel() { if (loaded) nativeCancel() }

    /** Clears a previous cancel; called when a new import starts, before Cancel is offered. */
    fun resetCancel() { load(); nativeResetCancel() }

    @JvmStatic private external fun nativeIdentify(fds: IntArray): String
    @JvmStatic private external fun nativeExtractWii(fds: IntArray, destDir: String): String?
    @JvmStatic private external fun nativeExtractGameCube(fds: IntArray, destIso: String): String?
    @JvmStatic private external fun nativeProgress(): LongArray
    @JvmStatic private external fun nativeCancel()
    @JvmStatic private external fun nativeResetCancel()
}
