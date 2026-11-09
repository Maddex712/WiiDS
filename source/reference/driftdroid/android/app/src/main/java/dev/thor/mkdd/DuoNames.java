package dev.thor.mkdd;

/** Display text keyed by doldecomp/mkdd b56cc767 IDs. Text only; never stands in for disc art. */
final class DuoNames {
    private DuoNames() {}
    // kartEnums.h ECharID.
    private static final String[] CHARACTERS = {null, "Baby Mario", "Baby Luigi", "Paratroopa",
        "Koopa Troopa", "Peach", "Daisy", "Mario", "Luigi", "Wario", "Waluigi", "Yoshi", "Birdo",
        "Donkey Kong", "Diddy Kong", "Bowser", "Bowser Jr.", "Toad", "Toadette", "King Boo", "Petey Piranha"};
    // ItemObjMgr.cpp: mModel[kind] animation loads and getConvertSucItemKind. Kinds 1, 6, 9, 10,
    // 14 and 20 are only inferred from draw lists, so they stay unnamed until seen live.
    private static final String[] ITEMS = {"Green Shell", null, "Red Shell", "Banana", "Giant Banana",
        "Mushroom", null, "Chain Chomp", "Bob-omb", null, null, "Egg", "Golden Mushroom", "Spiny Shell",
        null, "Fake Item Box", null, "Triple Green Shells", "Triple Mushrooms", "Triple Red Shells", null,
        "Fireballs"};

    static String character(int id) { return id >= 1 && id < CHARACTERS.length ? CHARACTERS[id] : null; }
    /** Null for empty, unknown and not-yet-verified kinds. */
    static String item(int kind) { return kind >= 0 && kind < ITEMS.length ? ITEMS[kind] : null; }

    // kartEnums.h ECourseID. 0x27/0x2e/0x30 and the MINI_* battle slots have no confirmed name.
    static String course(int id) {
        switch (id) {
            case 0x21: return "Baby Park";        case 0x22: return "Peach Beach";
            case 0x23: return "Daisy Cruiser";    case 0x24: return "Luigi Circuit";
            case 0x25: return "Mario Circuit";    case 0x26: return "Yoshi Circuit";
            case 0x28: return "Mushroom Bridge";  case 0x29: return "Mushroom City";
            case 0x2a: return "Waluigi Stadium";  case 0x2b: return "Wario Colosseum";
            case 0x2c: return "Dino Dino Jungle"; case 0x2d: return "DK Mountain";
            case 0x2f: return "Bowser's Castle";  case 0x31: return "Rainbow Road";
            case 0x32: return "Dry Dry Desert";   case 0x33: return "Sherbet Land";
            default: return null;
        }
    }

    // kartEnums.h ERaceMode; ESCAPE_BATTLE has no shipped name.
    static String mode(int mode) {
        switch (mode) {
            case 1: return "Time Trial";     case 2: return "Grand Prix";
            case 3: return "Versus";         case 4: return "Balloon Battle";
            case 5: return "Shine Thief";    case 6: return "Bob-omb Blast";
            case 7: return "Battle";         default: return null;
        }
    }
}
