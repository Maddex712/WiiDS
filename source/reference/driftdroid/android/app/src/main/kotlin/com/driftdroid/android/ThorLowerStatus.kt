package com.driftdroid.android

/** The existing race snapshot proves a race stage, not a menu/results page. */
object ThorLowerStatus {
    enum class Phase(val title: String, val message: String) {
        WAITING("Course Map", "Continue on the upper screen."),
        LOADING("Preparing course map", "Please wait a moment."),
        UNAVAILABLE("Map unavailable", "Continue on the upper screen."),
        FINISHED("Race finished", "Continue on the upper screen."),
        RACE("", ""),
    }

    /** Full native-font lines, or a brief directing the player to the full upper message. */
    fun messageLines(message: String, maxWidth: Float, measure: (String) -> Float): List<String> {
        val brief = listOf("Check the upper screen for details.")
        val lines = ArrayList<String>()
        var line = ""
        for (word in message.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }) {
            if (measure(word) > maxWidth) return brief
            val candidate = if (line.isEmpty()) word else "$line $word"
            if (measure(candidate) <= maxWidth) line = candidate
            else {
                lines.add(line)
                if (lines.size >= 3) return brief
                line = word
            }
        }
        if (line.isNotEmpty()) lines.add(line)
        return lines
    }

    @JvmStatic fun resolve(raceAvailable: Boolean, stage: Int, identityAvailable: Boolean,
        hudReady: Boolean, hudFailed: Boolean, courseReady: Boolean, courseLoading: Boolean,
        courseFailed: Boolean): Phase = when {
        !raceAvailable -> Phase.WAITING
        identityAvailable && hudReady && courseReady -> Phase.RACE
        stage == 3 -> Phase.FINISHED
        hudFailed || courseFailed || !identityAvailable -> Phase.UNAVAILABLE
        courseLoading || !hudReady -> Phase.LOADING
        else -> Phase.UNAVAILABLE
    }
}
