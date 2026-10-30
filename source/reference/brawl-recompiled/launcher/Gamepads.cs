using System.Runtime.InteropServices;

namespace BrawlRecompiled;

// Connected gamepads, through the SDL3.dll the game ships in Play\ (the same library and type detection the game
// uses, so the launcher lists exactly what the game will see). Empty when SDL can't be loaded.
static partial class Gamepads
{
    const uint SDL_INIT_GAMEPAD = 0x2000;
    static bool ready;
    public static bool Ready => ready;

    [LibraryImport("SDL3")] [return: MarshalAs(UnmanagedType.U1)] private static partial bool SDL_Init(uint flags);
    [LibraryImport("SDL3")] private static partial void SDL_PumpEvents();
    [LibraryImport("SDL3")] private static partial IntPtr SDL_GetGamepads(out int count);
    [LibraryImport("SDL3")] private static partial IntPtr SDL_GetGamepadNameForID(uint id);
    [LibraryImport("SDL3")] private static partial int SDL_GetGamepadTypeForID(uint id);
    [LibraryImport("SDL3")] private static partial void SDL_free(IntPtr mem);
    [LibraryImport("SDL3", StringMarshalling = StringMarshalling.Utf8)] [return: MarshalAs(UnmanagedType.U1)] private static partial bool SDL_SetHint(string name, string value);
    [LibraryImport("SDL3")] private static partial IntPtr SDL_OpenGamepad(uint id);
    [LibraryImport("SDL3")] [return: MarshalAs(UnmanagedType.U1)] private static partial bool SDL_GetGamepadButton(IntPtr pad, int button);
    [LibraryImport("SDL3")] private static partial short SDL_GetGamepadAxis(IntPtr pad, int axis);

    // SDL_GamepadButton / SDL_GamepadAxis order, as the names virtual_remote.cpp parses (guide is never bound).
    static readonly string[] ButtonNames =
    {
        "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder", "rightshoulder",
        "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad",
    };
    static readonly string[] AxisNames = { "leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger" };
    static readonly Dictionary<uint, IntPtr> open = new();

    public static bool Opens(uint id) { Active(id); return open.ContainsKey(id); }

    // The inputs held right now on one pad, as config tokens (sticks with a direction: "leftx-").
    public static HashSet<string> Active(uint id)
    {
        var held = new HashSet<string>();
        if (!ready) return held;
        if (!open.TryGetValue(id, out var pad))
        {
            pad = SDL_OpenGamepad(id);
            if (pad == IntPtr.Zero) return held;
            open[id] = pad;
        }
        SDL_PumpEvents();
        for (int b = 0; b < ButtonNames.Length; b++)
            if (b != 5 && SDL_GetGamepadButton(pad, b)) held.Add(ButtonNames[b]);
        for (int a = 0; a < AxisNames.Length; a++)
        {
            float v = SDL_GetGamepadAxis(pad, a) / 32767f;
            if (a >= 4) { if (v > 0.6f) held.Add(AxisNames[a]); }
            else if (Math.Abs(v) > 0.6f) held.Add(AxisNames[a] + (v < 0 ? "-" : "+"));
        }
        return held;
    }

    [LibraryImport("SDL3")] private static partial void SDL_CloseGamepad(IntPtr pad);
    [LibraryImport("SDL3")] private static partial void SDL_QuitSubSystem(uint flags);

    // Let go of every pad while the game runs: two SDL processes reading the same pad (raw input) can starve the game.
    public static void Release()
    {
        if (!ready) return;
        foreach (var pad in open.Values) SDL_CloseGamepad(pad);
        open.Clear();
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        ready = false;
    }

    public static void Resume()
    {
        try { if (!ready) ready = SDL_Init(SDL_INIT_GAMEPAD); } catch (Exception) { ready = false; }
    }

    public static void Init(string playDir)
    {
        try
        {
            var dll = NativeLibrary.Load(Path.Combine(playDir, OperatingSystem.IsWindows() ? "SDL3.dll" : "libSDL3.so.0"));  // the Linux launcher ships libSDL3.so.0 in engine/
            NativeLibrary.SetDllImportResolver(typeof(Gamepads).Assembly, (name, _, _) => name == "SDL3" ? dll : IntPtr.Zero);
            SDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");  // SDL has no window of its own here
            ready = SDL_Init(SDL_INIT_GAMEPAD);
        }
        catch (Exception) { ready = false; }  // no SDL: the Controls page shows the keyboard only
    }

    // (SDL instance id, name, layout: 0 Xbox, 1 PlayStation, 2 Nintendo), matching virtual_remote.cpp StyleFor.
    public static List<(uint id, string name, int style)> Connected()
    {
        var pads = new List<(uint, string, int)>();
        if (!ready) return pads;
        SDL_PumpEvents();  // picks up controllers plugged in or removed since the last call
        IntPtr ids = SDL_GetGamepads(out int count);
        for (int i = 0; ids != IntPtr.Zero && i < count; i++)
        {
            uint id = (uint)Marshal.ReadInt32(ids, i * 4);
            string name = Marshal.PtrToStringUTF8(SDL_GetGamepadNameForID(id)) ?? "Controller";
            int style = SDL_GetGamepadTypeForID(id) switch
            {
                4 or 5 or 6 => 1,     // PS3, PS4, PS5
                7 or 8 or 9 or 10 => 2,  // Switch Pro, Joy-Con left / right / pair
                _ => 0,
            };
            pads.Add((id, name, style));
        }
        if (ids != IntPtr.Zero) SDL_free(ids);
        return pads;
    }
}
