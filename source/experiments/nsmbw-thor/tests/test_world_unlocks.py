"""Exercise real NSMB snapshot and lower-screen rendering methods; no device actions."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
TASK = ROOT / "build/discord-bugfix-2026-10-26/world-unlocks"
JDK = Path("C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin")


def method(text, start):
    at = text.index(start)
    brace = text.index("{", at)
    depth = 1
    end = brace + 1
    while depth:
        if text[end] == "{": depth += 1
        elif text[end] == "}": depth -= 1
        end += 1
    return text[at:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    parser.add_argument("--part", choices=("native", "render"), required=True)
    parser.add_argument("--arm64", action="store_true", help="Compile native runner only; never uses ADB")
    args = parser.parse_args()
    assert not args.arm64 or args.part == "native"
    base = TASK / "before" if args.before else ROOT
    out = TASK / ("before-tests" if args.before else "fixed-tests") / ("arm64" if args.arm64 else args.part)
    out.mkdir(parents=True, exist_ok=True)
    sources = []
    if args.part == "native":
        source = base / "reference/nsmbw-android-e1/runtime/src/thor_world_map.cpp"
        sources.append(source)
        # Include and execute all 33 existing actual graph regression checks too.
        fixture = r'''
#ifdef _WIN32
#define setenv(name,value,overwrite) _putenv_s(name,value)
#endif
#include "existing_graph_test.cpp"
int failures = 0;
void Verify(bool value, const char* reason) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", reason); }
}
constexpr uint32_t manager = 0x80800000;
void PutByte(uint32_t address, uint32_t value) {
    const uint32_t shift = 24u - (address & 3u) * 8u;
    words[address & ~3u] = (words[address & ~3u] & ~(255u << shift)) | ((value & 255u) << shift);
}
void Save(int slot, unsigned mask) {
    Setup(); words[0x8042a24c] = 0x00040000; words[0x8042a040] = manager;
    PutByte(manager + 0x26, slot);
    for (int file = 0; file < 3; ++file) {
        for (int w = 0; w < 10; ++w)
            PutByte(manager + 0x6c0 + file * 0x980 + 0x32 + w,
                file == slot && w < 9 && (mask & (1u << w)) ? 0x81 : 0x80);
    }
}
unsigned Unlocks(const std::vector<int32_t>& state) { return (uint32_t(state[2]) >> 8) & 511u; }
int main() {
    ExistingGraphChecks();
    for (int slot = 0; slot < 3; ++slot) {
        for (unsigned mask : {0u, 1u, 0x11u, 0x81u, 0x101u, 0x155u, 0x1ffu}) {
            Save(slot, mask); const auto original = words; auto state = Sample();
            Verify((state[2] & 4) != 0, "selected save unlock data available");
            Verify(Unlocks(state) == mask, "exact sparse per-world flags, not highest-world range");
            Verify(words == original, "snapshot never writes guest memory");
            Verify(state.size() == 48 && state[0] == 1 && (state[2] & 3) == 3,
                   "existing graph/header/player ABI retained");
        }
    }
    for (int slot : {3, 127, 255}) {
        Save(0, 0x11); PutByte(manager + 0x26, slot);
        auto state = Sample(); Verify((state[2] & 4) == 0 && Unlocks(state) == 0, "invalid slot is unavailable");
    }
    for (uint32_t bad : {0u, 0x80800001u, 0x817ffb00u, 0x81800000u, 0xfffffffcu}) {
        Save(0, 0x11); words[0x8042a040] = bad;
        auto state = Sample(); Verify((state[2] & 4) == 0 && Unlocks(state) == 0, "invalid manager/range is unavailable");
    }
    Save(0, 0x11); words.erase(manager + 0x24);
    Verify((Sample()[2] & 4) == 0, "missing selected-slot read unavailable");
    Save(0, 0x11); words.erase((manager + 0x6c0 + 0x32 + 8) & ~3u);
    auto state = Sample(); Verify((state[2] & 4) == 0 && Unlocks(state) == 0, "partial flags never published");
    Save(0, 0x11); words[0x80428730] = 5;
    state = Sample(); Verify(state[2] == 0, "no map state leaks across scenes");
    Save(0, 0x11); Sample(); Save(2, 1); state = Sample();
    Verify(Unlocks(state) == 1, "slot switch replaces sparse mask, not accumulated progress");
    Save(0, 0x11); Sample(); Save(0, 1); state = Sample();
    Verify(Unlocks(state) == 1, "restored save can remove previous unlocks");
    std::printf("RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
'''
        cpp = out / "world_unlock_test.cpp"
        cpp.write_text(fixture)
        # A C++ main may fall through; the included test is now a normal helper.
        graph_source = (Path(__file__).parent / "thor_world_map_test.cpp").read_text().replace(
            "int main() {", "int ExistingGraphChecks() {", 1).rstrip()
        assert graph_source.endswith("}")
        (out / "existing_graph_test.cpp").write_text(graph_source[:-1] + "return 0;\n}\n")
        includes = [ROOT / "experiments/nsmbw-thor/tests/mock",
                    base / "reference/nsmbw-android-e1/runtime/include", Path(__file__).parent]
        if args.arm64:
            compiler = ROOT / ".tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android30-clang++.cmd"
            binary = out / "world_unlock_test"
            command = [str(compiler), "-std=c++20", "-O2", "-static-libstdc++"]
        else:
            compiler = ROOT / "build/github-bugfix-2026-10-26/tooling/zig-x86_64-windows-0.15.2/zig.exe"
            binary = out / "world_unlock_test.exe"
            command = [str(compiler), "c++", "-std=c++20", "-O2"]
        command += ["-I" + str(i) for i in includes] + [str(cpp), str(source), "-o", str(binary)]
        subprocess.run(command, check=True)
        if args.arm64:
            result = None
        else:
            result = subprocess.run([str(binary)], capture_output=True, text=True)
    else:
        source = base / "reference/driftdroid/android/app/src/main/java/com/wither/nsmbw"
        for name in ("ThorWorldMap.java", "ThorCompanion.java", "ThorLowerScreen.java"):
            sources.append(source / name)
        companion = (source / "ThorCompanion.java").read_text()
        lower = (source / "ThorLowerScreen.java").read_text()
        methods = "\n".join(method(companion, s) for s in (
            "private void bubbles(", "private static float bx(", "private static float by("))
        setter_name = "void setUnlockedWorlds(" if "void setUnlockedWorlds(" in companion else "void setReachedWorld("
        setter = method(companion, setter_name)
        remember = method(lower, "private void rememberProgress(")
        visual = method(lower, "private long visualKey(")
        key_start = companion.index("String key =")
        key = companion[key_start:companion.index(";", key_start) + 1]
        java = r'''
package com.wither.nsmbw;
import java.util.*;
class Paint {
    enum Style { STROKE, FILL } enum Cap { ROUND }
    void setStyle(Style s) {} void setStrokeCap(Cap c) {} void setStrokeWidth(float f) {} void setColor(int c) {}
}
class Canvas {
    void drawLine(float a,float b,float c,float d,Paint p) {}
    void drawCircle(float a,float b,float c,Paint p) {}
    void drawOval(float a,float b,float c,float d,Paint p) {}
}
class ThorHudAssets {
    Set<String> pictures = new TreeSet<>(); int hidden;
    boolean has(String name) { return true; }
    void imageCircle(Canvas c,String name,float x,float y,float r,int alpha) { pictures.add(name); }
    void text(Canvas c,String text,float x,float y,float h,float w,String face,int tint,int alpha) { if(text.equals("W?"))hidden++; }
}
class Companion {
    Paint fill = new Paint(), shadow = new Paint(); int reachedWorld, unlockedWorlds;
    static final int GOLD=0,BUBBLE_RING=0,LOCKED_EDGE=0,LOCKED=0,LOCKED_TEXT=0;
    SETTER
    METHODS
    String cacheKey(ThorWorldMap map) { String kind="map"; CACHE_KEY return key; }
    ThorHudAssets render(int current) { ThorHudAssets assets = new ThorHudAssets(); bubbles(new Canvas(),assets,current);return assets; }
}
class Prefs {
    Map<String,Integer> values = new HashMap<>(); int reads,writes;
    int getInt(String key,int fallback) { reads++;return values.getOrDefault(key,fallback); }
    Prefs edit(){return this;} Prefs putInt(String key,int value){writes++;values.put(key,value);return this;}void apply(){}
}
class Log { static void i(String a,String b){} }
class Reserve {int context,item,uses,stores; boolean canUse,queued; Reserve(int slot){context=slot;}}
class ThorGameState {int fields,scene,mode,world,stage,character,lives,progress,starCoins,savedStarCoins,score;boolean gameStopped;}
class View {Companion companion=new Companion();}
class Presentation {View view=new View();}
class Lower {
    ThorWorldMap worldMap; ThorGameState state; Reserve reserve; Prefs progressPrefs = new Prefs();
    String progressKey="nsmbw",loggedMap="";int lastSlot=-1,reachedWorld;Presentation presentation=new Presentation();
    static final String TAG="test";
    REMEMBER
    VISUAL
    void update(){rememberProgress();}
    long key(){return visualKey();}
}
class GameActivity { static int[] words;static int[] nativeReadThorWorldMap(){return words;} }
public class WorldUnlockRenderTest {
    static int checks,failures;
    static void verify(boolean value,String message){checks++;if(!value){failures++;System.out.println("FAIL: "+message);}}
    static ThorWorldMap map(int current,int mask,boolean available){
        GameActivity.words=new int[]{1,1,3|(available?4|(mask<<8):0),current,0,1,0,0,0,0,0,0,
            0,1,0x57313031,0,0,0,-1,-1,-1,-1,-1,-1};return ThorWorldMap.read();
    }
    static void check(Lower lower,int current,int mask,boolean available,int slot,String label){
        lower.worldMap=map(current,mask,available);lower.reserve=new Reserve(slot);lower.update();
        ThorHudAssets assets=lower.presentation.view.companion.render(current);
        int expected=available?mask&511:current>=0&&current<9?1<<current:0;
        Set<String> pictures=new TreeSet<>();for(int w=0;w<9;w++)if((expected&(1<<w))!=0)pictures.add("course"+(w+1));
        verify(assets.pictures.equals(pictures),label+" exact displayed worlds");
        verify(assets.hidden==9-pictures.size(),label+" skipped worlds remain hidden");
    }
    public static void main(String[]args){
        Lower lower=new Lower();lower.progressPrefs.values.put("nsmbw_reached_0",9);
        lower.progressPrefs.values.put("nsmbw_reached_1",8);
        check(lower,4,0x11,true,0,"warp W1+W5 with stale highest=9");
        check(lower,0,0x11,true,0,"return from W5 to W1");
        check(lower,0,1,true,1,"switch to a different save slot");
        check(lower,8,0x101,true,0,"noncontiguous W1+W9");
        check(lower,0,1,true,0,"restore earlier save removes unlocks");
        check(lower,4,0,false,0,"unavailable native save flags use current world only");
        check(lower,-1,0,false,0,"invalid current world gives no guessed unlocks");
        check(lower,0,0,true,0,"valid empty mask is not guessed");
        check(lower,8,0x1ff,true,0,"all nine actual flags");
        check(lower,3,0x155,true,0,"alternating sparse unlocks");
        verify(lower.progressPrefs.reads==0&&lower.progressPrefs.writes==0,"no stale preference reads or writes");
        verify(GameActivity.words[2]==(7|(0x155<<8)),"frontend never mutates native snapshot");
        lower.worldMap=map(4,0x11,true);lower.update();
        long oldVisual=lower.key();String oldBase=lower.presentation.view.companion.cacheKey(lower.worldMap);
        lower.worldMap=map(4,0x13,true);lower.update();
        verify(oldVisual!=lower.key(),"same-world unlock changes invalidate lower view");
        verify(!oldBase.equals(lower.presentation.view.companion.cacheKey(lower.worldMap)),"same-world unlock changes rebuild cached bubbles");
        System.out.println("RESULT checks="+checks+" failures="+failures);
        if(failures!=0)System.exit(1);
    }
}
'''.replace("SETTER", setter).replace("METHODS", methods).replace("REMEMBER", remember).replace("VISUAL", visual).replace("CACHE_KEY", key)
        fixture = out / "WorldUnlockRenderTest.java"
        fixture.write_text(java, encoding="utf-8")
        subprocess.run([str(JDK / "javac.exe"), "-encoding", "UTF-8", "-d", str(out),
                        str(fixture), str(source / "ThorWorldMap.java")], check=True)
        result = subprocess.run([str(JDK / "java.exe"), "-cp", str(out), "com.wither.nsmbw.WorldUnlockRenderTest"],
                                capture_output=True, text=True)
    receipt = {"status": "BUILT_NOT_RUN" if result is None else "PASS" if result.returncode == 0 else "FAIL",
               "part": args.part, "before": args.before, "arm64": args.arm64,
               "sources": [{"path": str(s.relative_to(ROOT)), "sha256": hashlib.sha256(s.read_bytes()).hexdigest()} for s in sources],
               "scope": "Actual read-only world-map source with mock guest memory" if args.part == "native" else
                        "Actual ThorWorldMap parser, LowerScreen progress method, and Companion bubble drawing against recording Canvas/assets",
               "device_actions": "None", "owner_save_mutations": "None"}
    if args.part == "native": receipt.update(binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    if result is not None:
        receipt.update(exit_code=result.returncode, output=result.stdout, stderr=result.stderr)
        (out / "run.log").write_text(result.stdout + result.stderr)
        print(result.stdout, end="")
    (out / "verification.json").write_text(json.dumps(receipt, indent=2) + "\n")
    if result is not None: raise SystemExit(result.returncode)


if __name__ == "__main__": main()
