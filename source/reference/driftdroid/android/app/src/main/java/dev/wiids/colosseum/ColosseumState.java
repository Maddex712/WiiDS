package dev.wiids.colosseum;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** One read-only snapshot from a verified Colosseum runtime. */
public final class ColosseumState {
    public static final int FRONTEND = 1;
    public static final int FIELD = 2;
    public static final int BATTLE = 3;
    public static final int SEEN = 1;
    public static final int SNAGGED = 2;
    public static final int PURIFIED = 3;

    public final int scene;
    public final int room;
    public final int mapRevision;
    public final String location;
    public final float playerX;
    public final float playerZ;
    public final List<PartyMember> party;
    public final List<ShadowPokemon> shadows;
    private final float[] mapEdges;

    static native ColosseumState read(int knownMapRevision);
    /** Player-side battlers from the guest battle records. Layout is documented in companion_state.h. */
    static native boolean readBattle(int[] ints, String[] names);
    /** out[0] = the game's own menu is up (party HP may be on the upper screen), out[1] = which kind if known. */
    static native boolean readUi(int[] out);
    /** The battle input window waiting for the player (ColosseumMenu). */
    static native boolean readMenu(int[] ints, String[] labels);
    /** Mean brightness 0..255 of the upper game image, -1 until measured. */
    static native int topLevel();

    private ColosseumState(int scene, int room, int mapRevision, String archive, float playerX,
                           float playerZ, String[] names, int[] partyStats, int[] shadowStatus,
                           int[] shadowHeart, float[] mapEdges) {
        this.scene = scene;
        this.room = room;
        this.mapRevision = mapRevision;
        this.location = locationName(archive);
        if (archive != null && !archive.isEmpty() && this.location.isEmpty()) android.util.Log.i("WiiDsColoPlace", "unnamed archive " + archive + " room " + room);
        this.playerX = playerX;
        this.playerZ = playerZ;
        this.mapEdges = mapEdges;
        List<PartyMember> members = new ArrayList<>(6);
        for (int slot = 0; slot < 6; ++slot) {
            if (names[slot] != null) {
                int index = slot * 3;
                members.add(new PartyMember(slot, names[slot], partyStats[index], partyStats[index + 1],
                        partyStats[index + 2]));
            }
        }
        List<ShadowPokemon> observed = new ArrayList<>();
        for (int index = 0; index < shadowStatus.length; ++index) {
            if (shadowStatus[index] > 0) {
                String name = names[index + 6];
                observed.add(new ShadowPokemon(index + 1, name, shadowStatus[index], shadowHeart[index]));
            }
        }
        party = Collections.unmodifiableList(members);
        shadows = Collections.unmodifiableList(observed);
    }

    /** X/Z endpoints in groups of four. Null means the known map is unchanged. */
    public float[] copyMapEdges() {
        return mapEdges == null ? null : mapEdges.clone();
    }

    public static final class PartyMember {
        public final int slot;
        public final String name;
        public final int level;
        public final int hp;
        public final int maxHp;

        PartyMember(int slot, String name, int level, int hp, int maxHp) {
            this.slot = slot;
            this.name = name;
            this.level = level;
            this.hp = hp;
            this.maxHp = maxHp;
        }
    }

    public static final class ShadowPokemon {
        public final int id;
        public final String name;
        public final int status;
        /** Remaining heart gauge percentage, or -1 when unavailable. */
        public final int heart;

        ShadowPokemon(int id, String name, int status, int heart) {
            this.id = id;
            this.name = name;
            this.status = status;
            this.heart = heart;
        }
    }

    // Area names are derived from the identified retail field archive.
    private static String locationName(String archive) {
        if (archive == null || archive.isEmpty()) return "";
        String[] identifiers = archive.split("_", 3);
        String town;
        switch (identifiers[0]) {
            case "S1": town = "Outskirt Stand"; break;
            case "S2": town = "Snagem Hideout"; break;
            case "M1": town = "Phenac City"; break;
            case "M2": town = "Pyrite Town"; break;
            case "M3": town = "Agate Village"; break;
            case "M4": town = "The Under"; break;
            case "D1": town = "Shadow Pokémon Lab"; break;
            case "D2": town = "Mt. Battle"; break;
            case "D4": town = "Realgam Tower"; break;
            case "T1": town = "Orre Colosseum"; break;
            default: return "";
        }
        // The building inside the town, from the archive's second part (the disc's own room names).
        String place = archive.endsWith("colo") ? "Stadium" : identifiers.length > 1 ? building(identifiers[1]) : null;
        return place == null ? town : town + " · " + place;
    }

    private static String building(String part) {
        if (part.endsWith("colo")) return "Stadium";
        switch (part) {
            case "stadium": case "gym": return "Stadium";
            case "pc": return "Pokémon Center";
            case "shop": return "Poké Mart";
            case "hotel": return "Hotel";
            case "police": return "Police";
            case "guild": return "Pre Gym";
            case "uranai": return "Fortune Teller";
            case "windmill": return "Windmill";
            case "cave": return "Cave";
            case "shrine": return "Relic Stone";
            case "subway": return "Subway";
            case "labo": return "Lab";
            case "garage": return "Garage";
            case "tower": return "Tower";
            case "dome": return "Dome";
            case "building": return "Building";
            case "houseA": case "houseB": case "houseC": case "houseD": return "House";
            default: return null;   // the town itself (out, enter...)
        }
    }
}
