package com.driftdroid.android

import android.app.Activity
import android.app.AlertDialog
import java.io.File

/**
 * Renderer choice, shown from the launcher rather than the in-game sidebar: it only takes effect
 * when the runtime starts up, and a device that cannot start the game at all (no working Vulkan
 * driver - see GitHub issue #3) never reaches the in-game settings to change it.
 *
 * Written straight into Config.toml's [video] section, which the runtime reads at startup
 * (runtime/include/runtime_config.h, "graphics_api").
 */
object GraphicsSettings {
    private const val SECTION = "video"
    private const val KEY = "graphics_api"

    private data class Option(val value: String, val label: String, val description: String)

    private val options =
        listOf(
            Option(
                "auto",
                "Automatic (recommended)",
                "Vulkan when the device supports it, OpenGL ES otherwise.",
            ),
            Option("vulkan", "Vulkan", "Fastest on devices with a working Vulkan driver."),
            Option(
                "opengles",
                "OpenGL ES",
                "For devices whose Vulkan driver is missing or broken. Slower, but it runs.",
            ),
        )

    private fun configFile(activity: Activity) = File(File(activity.filesDir, "WiiCompiled"), "Config.toml")

    fun currentValue(activity: Activity): String {
        val file = configFile(activity)
        val line =
            runCatching {
                file.takeIf { it.isFile }?.readLines()?.firstOrNull { it.trimStart().startsWith(KEY) }
            }.getOrNull() ?: return "auto"
        val value = line.substringAfter('=').trim().trim('"')
        return options.firstOrNull { it.value == value }?.value ?: "auto"
    }

    /** Rewrites the key in place, or appends it under [video], leaving every other line alone. */
    private fun write(activity: Activity, value: String) {
        val file = configFile(activity)
        runCatching {
            file.parentFile?.mkdirs()
            val lines = if (file.isFile) file.readLines().toMutableList() else mutableListOf()
            val entry = "$KEY = \"$value\""
            val existing = lines.indexOfFirst { it.trimStart().startsWith(KEY) }
            if (existing >= 0) {
                lines[existing] = entry
            } else {
                val section = lines.indexOfFirst { it.trim() == "[$SECTION]" }
                if (section >= 0) {
                    lines.add(section + 1, entry)
                } else {
                    if (lines.isNotEmpty() && lines.last().isNotBlank()) lines.add("")
                    lines.add("[$SECTION]")
                    lines.add(entry)
                }
            }
            AtomicConfigFile.writeText(file, lines.joinToString("\n") + "\n")
        }
    }

    fun show(activity: Activity) {
        val current = currentValue(activity)
        var selected = options.indexOfFirst { it.value == current }.coerceAtLeast(0)
        val labels = options.map { if (BuildConfig.THOR_BUILD) it.label else "${it.label}\n${it.description}" }.toTypedArray()

        val dialog = ThorDialogs.builder(activity)
            .setTitle("Graphics")
            .setSingleChoiceItems(labels, selected) { _, which -> selected = which }
            .setNegativeButton("Cancel", null)
            .setNeutralButton(if (BuildConfig.THOR_BUILD) "GPU Driver" else "GPU driver...") { _, _ -> GpuDriverSettings.show(activity) }
            .setPositiveButton("Save") { _, _ ->
                write(activity, options[selected].value)
            }
            .create()
        if (BuildConfig.THOR_BUILD) {
            // The driver picker is a child of this editor. Keep the renderer
            // selection alive; the shared focus treatment hides its backing
            // until the child closes, then Android restores the same focus.
            dialog.setOnShowListener {
                dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener {
                    GpuDriverSettings.show(activity)
                }
            }
        }
        dialog.show()
    }
}
