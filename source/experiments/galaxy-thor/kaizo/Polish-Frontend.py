"""Kaizo presentation polish in the WiiDS frontend (reference/driftdroid), idempotent.

- Case art: `kaizo` joins the optional cover/disc pairs (ThorLauncherArtwork) and the lower
  companion's collection and disc lists, so a staged kaizo-cover/-disc/-spine is drawn.
- Setup wording: Galaxy and Kaizo share one disc, so the found/chosen-disc text names the
  action of the card it is shown on ("Set up Rebalanced" on Kaizo's) instead of always Galaxy's.
"""
from pathlib import Path

K = Path(__file__).resolve().parents[3] / 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android'


def patch(rel, pairs):
    p = K / rel
    s = p.read_text(encoding='utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    changed = False
    for a, b in pairs:
        if b in s:
            continue  # already applied
        assert s.count(a) == 1, (rel, a[:70], s.count(a))
        s = s.replace(a, b)
        changed = True
    if changed:
        if crlf:
            s = s.replace('\n', '\r\n')
        p.write_text(s, encoding='utf-8', newline='')
    print(rel, 'patched' if changed else 'unchanged')


patch('ThorLauncherArtwork.kt', [
    ('''setOf("eclipse-cover", "eclipse-disc"), setOf("galaxy-cover", "galaxy-disc"))''',
     '''setOf("eclipse-cover", "eclipse-disc"), setOf("galaxy-cover", "galaxy-disc"),
        setOf("kaizo-cover", "kaizo-disc"))'''),
])
patch('ThorLibraryCompanionView.kt', [
    ('''val later = listOf("msmbwii", "sunshine", "eclipse", "galaxy").filter''',
     '''val later = listOf("msmbwii", "sunshine", "eclipse", "galaxy", "kaizo").filter'''),
    ('''for (game in listOf("original", "retro-rewind", "nsmbw", "msmbwii", "sunshine", "eclipse", "galaxy")) for (kind in listOf("cover", "disc")) {''',
     '''for (game in listOf("original", "retro-rewind", "nsmbw", "msmbwii", "sunshine", "eclipse", "galaxy", "kaizo")) for (kind in listOf("cover", "disc")) {'''),
    ('''for (name in listOf("original-disc", "retro-rewind-disc", "nsmbw-disc", "msmbwii-disc", "sunshine-disc", "eclipse-disc", "galaxy-disc")) images[name]?.let {''',
     '''for (name in listOf("original-disc", "retro-rewind-disc", "nsmbw-disc", "msmbwii-disc", "sunshine-disc", "eclipse-disc", "galaxy-disc", "kaizo-disc")) images[name]?.let {'''),
])
patch('GamesActivity.kt', [
    ('''                    messages[game.disc] = if (galaxyChosen != null) "${source.name} is the right disc. Set up Galaxy to convert it."
                        else "${source.name} is the right disc, but Galaxy setup can only read it from this device's storage. " +''',
     '''                    // One disc for both cards: each card adds its own action (refreshGalaxy).
                    messages[game.disc] = if (galaxyChosen != null) "${source.name} is the right disc."
                        else "${source.name} is the right disc, but setup can only read it from this device's storage. " +'''),
    ('''            message != null && message.isNotEmpty() -> show(card, message, if (galaxyChosen != null) GREEN else AMBER)
            path != null -> show(card, "Found ${java.io.File(path).name}, the right disc. Set up Galaxy to convert it " +
                "(about 15 minutes; keep the device charging).", GREEN)''',
     '''            message != null && message.isNotEmpty() ->
                show(card, if (galaxyChosen != null) "$message ${setupAction(kaizo)}." else message, if (galaxyChosen != null) GREEN else AMBER)
            path != null -> show(card, "Found ${java.io.File(path).name}, the right disc. ${setupAction(kaizo)} " +
                "(about 15 minutes; keep the device charging).", GREEN)'''),
    ('''    private fun refreshGalaxy(card: Card, kaizo: Boolean = false) {''',
     '''    /** What the card's setup button does with the shared Galaxy disc. */
    private fun setupAction(kaizo: Boolean) =
        if (kaizo) "Set up Rebalanced to build it with the official 2.1 patch" else "Set up Galaxy to convert it"

    private fun refreshGalaxy(card: Card, kaizo: Boolean = false) {'''),
])
