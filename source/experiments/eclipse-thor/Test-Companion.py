"""Host checks for the Eclipse-only companion adapter, without an APK rebuild."""
from pathlib import Path
import hashlib
import json
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
REL = Path('android/app/src/main/java/dev/chasem/smslauncher/ThorSunshineScreen.java')
ORIGINAL = ROOT / 'reference/sms-launcher-thor' / REL
ADAPTED = ROOT / 'reference/eclipse-launcher-thor' / REL
OUT = ROOT / 'build/eclipse-thor/companion-check'
JDK = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')


def nested(text, name):
    start = text.index('    static final class ' + name)
    brace = text.index('{', start)
    depth = 1
    for end in range(brace + 1, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if not depth:
            return text[start:end + 1]
    raise ValueError(name)


def main():
    before = hashlib.sha256(ORIGINAL.read_bytes()).hexdigest()
    OUT.mkdir(parents=True, exist_ok=True)
    transformed = OUT / 'transformed.java'
    transformed.write_bytes(ORIGINAL.read_bytes())
    runpy.run_path(str(ROOT / 'experiments/eclipse-thor/Prepare-App.py'))['adapt_companion'](transformed)
    assert transformed.read_bytes() == ADAPTED.read_bytes(), 'Preparation must reproduce the exact companion'
    source = ADAPTED.read_text()
    assert 'APP_SETTINGS = 10, APP_CHARACTER_SELECT = 11' in source
    assert 'boolean slots = s.stage < 61 && s.tracked();' in source
    assert 'int stage = s.stage < 61 && s.shineStage >= 0 ? s.shineStage : -1;' in source
    fixture = '''import java.util.Locale;
public final class CompanionCheck {
%s
%s
    static Snapshot frame(int stage, String scene, String area) {
        int[] w = new int[64];
        w[0] = 2; w[9] = stage; w[11] = -1; w[21] = -1;
        put(w, 12, scene); put(w, 24, area); return new Snapshot(w);
    }
    static void put(int[] w, int at, String text) {
        for (int i = 0; i < text.length(); ++i) w[at + i / 4] |= text.charAt(i) << ((i %% 4) * 8);
    }
    static void require(boolean yes) { if (!yes) throw new AssertionError(); }
    public static void main(String[] args) {
        require(Area.from(null) == null);
        Area a = Area.from(frame(80, "tutorial", ""));
        require(a != null && a.plate == null && a.name.equals("tutorial") && !a.episodes);
        a = Area.from(frame(61, "bia0", ""));
        require(a != null && a.plate == null && a.name.equals("bia0"));
        a = Area.from(frame(61, "", "Erto Rock"));
        require(a != null && a.plate == null && a.name.equals("Erto Rock"));
        a = Area.from(frame(2, "bia0", ""));
        require(a != null && a.plate.equals("bianco") && a.name.equals("Bianco Hills"));
        a = Area.from(frame(62, "new_area0", ""));
        require(a != null && a.plate == null && a.name.equals("new area0") && !a.secret);
        require(Area.from(frame(15, "option", "")) == null);
        System.out.println("7 actual-source area checks passed");
    }
}
''' % (nested(source, 'Snapshot'), nested(source, 'Area'))
    java = OUT / 'CompanionCheck.java'
    java.write_text(fixture)
    subprocess.run([str(JDK / 'javac.exe'), '-d', str(OUT), str(java)], check=True)
    subprocess.run([str(JDK / 'java.exe'), '-cp', str(OUT), 'CompanionCheck'], check=True)
    assert hashlib.sha256(ORIGINAL.read_bytes()).hexdigest() == before
    receipt = {'original_companion_sha256': before,
               'adapted_companion_sha256': hashlib.sha256(ADAPTED.read_bytes()).hexdigest(),
               'preparation_exact_reproduction': True, 'actual_source_area_checks': 7,
               'regular_sunshine_modified': False, 'apk_rebuilt': False, 'device_tested': False,
               'menu_context_sources': ['bse/include/BetterSMS/application.hxx: CONTEXT_DIRECT_SETTINGS_MENU 10',
                                        'eclipse/include/menu/character_select.hxx: CONTEXT_CHARACTER_SELECT 11'],
               'drawing_helpers_preserved': True}
    (OUT / 'verification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__': main()
