#include "settings.h"

namespace engine::settings {

namespace {

std::vector<Definition> StandardSettings() {
  using D = Definition;
  using C = Category;
  return {
      D::Int("video.resolution_scale", C::Video, "Resolution",
             "How sharp the picture is drawn: at the GameCube's own 640x480 (Native), at a multiple of it, or at "
             "the window's size. Sharper edges and text, the same picture otherwise. Native is scaled up to fill "
             "the window and looks softer, as on the console.",
             0, 8, 1, 0)
          .IntNames({{1, "Native (640x480)"},
                     {2, "2x (1280x960)"},
                     {3, "3x (1920x1440)"},
                     {4, "4x (2560x1920)"},
                     {5, "5x (3200x2400)"},
                     {6, "6x (3840x2880)"},
                     {8, "8x (5120x3840)"},
                     {7, "1080p (1920x1080)"},
                     {0, "Match the window"}})
          .OriginalNote("the GameCube draws 640x480 (Native).")
          .Implemented(),
      D::Enum("video.anti_aliasing", C::Video, "Anti-aliasing", "Smooths the jagged edges of 3D models.",
              {{"off", "Off"}, {"2x", "2x"}, {"4x", "4x"}, {"8x", "8x"}}, "off", "4x"),
      D::Enum("video.texture_filtering", C::Video, "Texture filtering",
              "Keeps textures sharp when seen at an angle (anisotropic filtering).",
              {{"original", "Original"}, {"2x", "2x"}, {"4x", "4x"}, {"8x", "8x"}, {"16x", "16x"}}, "original", "16x")
          .OriginalNote("the filtering the game itself asks for, as on the console."),
      D::Bool("video.battle_revolution_models", C::Video, "Battle Revolution models",
              "Shows Pokemon Battle Revolution's models for the Pokemon in battle instead of XD's, where there is one "
              "(from the models you made on the launcher's Extra assets page, with your own Battle Revolution disc; built in, not a mod). "
              "Only how they look changes. Takes effect the next time the game starts (also after a soft reset). "
              "Off: XD's own models.",
              false, false)
          .OriginalNote("XD's own models.")
          .Implemented(),
      D::Bool("video.widescreen", C::Video, "Widescreen",
              "Shows a wider picture (Widescreen shape): 3D scenes show more at the sides, while menus, text and "
              "the on-screen displays keep their shape (menus at the screen's sides move out to the wide edges: "
              "Menus at the screen edges). Off: the original 4:3.",
              false, true)
          .Implemented(),
      D::Enum("video.widescreen_shape", C::Video, "Widescreen shape",
              "The shape of the picture when Widescreen is on: 16:9 (most monitors and TVs), 16:10, 21:9 "
              "(ultrawide) or the window's own shape (resize the window freely).",
              {{"16_9", "16:9"}, {"16_10", "16:10"}, {"21_9", "21:9"}, {"window", "The window's shape"}}, "16_9",
              "16_9")
          .Preference()
          .Implemented(),
      D::Bool("video.widescreen_edge_menus", C::Video, "Menus at the screen edges",
              "When Widescreen is on: menus and battle displays that sit at the left or right edge of the 4:3 "
              "picture move out to the edges of the wide picture. Off: every menu stays in the 4:3 middle. Centred "
              "menus and text boxes always stay in the middle. Does nothing while Widescreen is off.",
              true, true)
          .Preference()
          .Implemented(),
      D::Enum("video.frame_pacing", C::Video, "Frame pacing",
              "How pictures are handed to the monitor. The game always runs at the GameCube's own speed (59.94 "
              "pictures a second); this only decides how they reach the monitor. Original: every picture waits "
              "for the monitor's next refresh (vsync). Matched to the monitor: pictures are handed over on an "
              "exactly even 59.94 Hz rhythm, so on a 120/144/180 Hz monitor each stays on screen as evenly as "
              "the monitor allows; on monitors slower than 59.94 Hz (50 Hz TVs) a picture that can't be shown "
              "in time is skipped instead of holding the game up. Lowest delay: never waits for the monitor "
              "(the newest picture is shown at each refresh). With Frame rate at 60, Original behaves as Matched "
              "to the monitor (the in-between pictures need the even rhythm).",
              {{"original", "Original"}, {"auto", "Matched to the monitor"}, {"low_latency", "Lowest delay"}},
              "original", "auto")
          .OriginalNote("vsync on every picture, as the port always did; on a 60 Hz TV that is the console's "
                        "own rhythm.")
          .Implemented(),
      D::Enum("video.frame_rate", C::Video, "Frame rate",
              "30: the game's own pictures, as on the GameCube (most of the game draws 30 a second). 60: between "
              "two of the game's pictures the port shows an in-between picture, with the camera and every 3D "
              "object half way, so movement looks smoother. The game itself still runs exactly as before (same "
              "speed, same timing, same random numbers: it can be used in a Faithful playthrough); menus, text and "
              "effects that can't be matched stay at the game's own rate. While it is on the game's picture is "
              "shown 1/60 s later, frame pacing is matched to the monitor, and each picture is drawn three times: "
              "on a PC that can't afford that the in-between pictures are left out by themselves. Applies at once.",
              {{"original", "30 (original)"}, {"60", "60"}}, "original", "original")
          .OriginalNote("the game's own pictures only.")
          .Implemented(),
      D::Bool("video.original_fog_bloom", C::Video, "Original fog and glow",
              "Keeps the game's fog and bloom (glow) effects as on the GameCube.", true, true),
      D::Bool("video.photo_mode", C::Video, "Photo mode",
              "A key (Photo mode key, F8 at first) pauses the game and lets you move the camera freely, hide the "
              "menus and text, and save pictures (PNG, in the photos folder) at full or higher resolution. The game "
              "carries on as if no time had passed.",
              false, true)
          .Implemented(),
      D::Text("video.photo_mode_key", C::Video, "Photo mode key",
              "The key that opens and closes the photo mode, by its name (F8, F7, Home, P...).", "F8", "F8",
              [](std::string_view text) { return !text.empty() && text.size() <= 24; })
          .Preference()
          .Implemented(),
      D::Bool("video.fullscreen", C::Video, "Full screen",
              "Shows the game on the whole screen instead of in a window (the monitor keeps its own resolution and "
              "refresh rate, so switching is quick and the game's speed doesn't change). Alt+Enter switches it while "
              "playing. The picture keeps its shape, with black bars at the sides or top as needed.",
              false, false)
          .Preference()
          .Implemented(),
      D::Bool("video.borderless_window", C::Video, "Borderless window",
              "Shows the game in a window without a frame or title bar that covers its whole monitor (when Full "
              "screen is off). Off: a normal window you can move and resize.",
              false, false)
          .Preference()
          .Implemented(),
      D::Bool("video.fps_counter", C::Video, "Frame rate counter",
              "Shows a small line in the top left corner: how many pictures a second the game is making (the game "
              "itself makes 30 in most places) and its speed against a GameCube (100% = full speed). Only something "
              "drawn over the picture: the game is not changed.",
              false, false)
          .Preference()
          .Implemented(),

      D::Bool("system.pause_on_focus_loss", C::System, "Pause in the background",
              "Pauses the game (and its sound) while its window isn't in front, for example when you switch to "
              "another program; it carries on when you come back as if no time had passed. Off: it keeps running, "
              "as a console does.",
              false, true)
          .Implemented(),
      D::Enum("system.disc_timing", C::System, "Loading speed",
              "How long the game waits for the disc. GameCube: as long as the GameCube's disc drive takes, so "
              "every load lasts as long as on the console. Fast: loads finish at the next frame. GameCube drive "
              "(testing): the drive as Dolphin emulates it - seeks, the disc's speed by position, its read-ahead "
              "buffer, each read finishing at its exact time (TIMING_STEP2D.md). Buffered drive (testing): Dolphin's "
              "fast disc speed (every read as if from the drive's buffer).",
              {{"gamecube", "GameCube"}, {"fast", "Fast"}, {"drive", "GameCube drive (testing)"},
               {"buffered", "Buffered drive (testing)"}},
              "gamecube", "fast")
          .OriginalNote("about 2.7 MB a second, like its disc drive.")
          .Implemented(),
      D::Enum("system.card_timing", C::System, "Memory card speed",
              "How long saving and loading take. GameCube: as long as a GameCube memory card takes, so a save "
              "takes about two seconds as on the console. Instant: saves and loads finish at once.",
              {{"gamecube", "GameCube"}, {"instant", "Instant"}}, "gamecube", "instant")
          .OriginalNote("about 96 KB a second writing and 512 KB a second reading, like the memory card.")
          .Implemented(),
      D::Enum("system.game_clock", C::System, "Game clock (testing)",
              "Which clock the game measures its frame times with. GameCube: the time of the last vertical "
              "blank, so every frame measures whole fields as on the console (battle and script waits keep the "
              "console's pace; a PC stall pauses the game instead of making it jump). PC: this PC's own clock "
              "at the moment the game reads it, as the port did before 2026-11-08 (waits that drop the "
              "fraction then run up to 45 % long).",
              {{"gamecube", "GameCube"}, {"pc", "PC"}}, "gamecube", "gamecube")
          .OriginalNote("whole fields, like the console.")
          .Testing()
          .Implemented(),
      D::Enum("system.cpu_timing", C::System, "CPU time per frame (testing)",
              "How long the game's work takes each frame. GameCube: as long as the GameCube's processor takes "
              "(a model of it, measured against Dolphin), so heavy frames - a scene being set up, a big load - "
              "take several fields as on the console and the game measures them. Instant: the PC's speed, "
              "every frame's work done at once, as the port did before (timing step 2b, not yet complete).",
              {{"gamecube", "GameCube"}, {"instant", "Instant"}}, "instant", "instant")
          .OriginalNote("the PC's speed for now: the model isn't complete yet (TIMING_STEP2B.md).")
          .Testing()
          .Implemented(),
      D::Float("system.game_speed", C::System, "Game clock speed (testing)",
               "Runs the game's clock this many times faster than real time, for test runs. The game does exactly "
               "the same, only the waiting is shorter.",
               1.0f, 16.0f, 1.0f, 1.0f)
          .Testing()
          .Implemented(),
      D::Text("system.disc_path", C::System, "Disc image",
              "The disc image file the game starts with, as chosen in the launcher (empty: none chosen yet).", "",
              "")
          .Preference()
          .Implemented(),
      D::Text("system.extra_disc.pbr", C::System, "Pokemon Battle Revolution disc image",
              "The Pokemon Battle Revolution disc image the launcher's Extra assets page makes the Battle Revolution "
              "models from (empty: none chosen yet).", "", "")
          .Preference()
          .Implemented(),
      D::Bool("system.launcher_stays_open", C::System, "Keep the launcher open",
              "Keeps the launcher's window open while the game runs. Off: it closes when the game starts.", false,
              false)
          .Preference()
          .Implemented(),
      D::Text("system.app_icon", C::System, "Icon",
              "The icon of the windows, the taskbar and the shortcuts, as chosen in the launcher: one of the "
              "program's own (\"original\"...) or of the icons made from your disc, by name. Empty: the first one "
              "made from your disc, or the program's own before a disc has been checked.",
              "", "")
          .Preference()
          .Implemented(),
      D::Text("system.launcher_background", C::System, "Launcher background",
              "The picture behind the launcher's pages, from your disc, as chosen in the launcher: \"none\" (plain), "
              "a picture's name, or empty (the first picture from your disc).",
              "", "")
          .Preference()
          .Implemented(),
      D::Bool("system.update_check", C::System, "Check for updates",
              "When the launcher starts, it asks the publisher's page whether a newer version exists and, if so, "
              "asks you whether to install it. Nothing is ever downloaded or installed without your yes. Off: the "
              "launcher never goes online by itself (\"Check now\" in the launcher still works).",
              false, false)
          .Preference()
          .Implemented(),

      D::Bool("system.settings_entry", C::System, "SETTINGS in the game's menu",
              "Adds SETTINGS after EXIT in the menu the game opens with START, and left of OPTIONS on the title "
              "screen: picture, sound and the other settings you may change while playing. The game is paused while it is open and never notices it.",
              true, true)
          .Preference()
          .Implemented(),
      D::Bool("system.newgame_setup", C::System, "New Game setup screen",
              "After NEW GAME on the title screen, a screen to choose how the new playthrough plays: Faithful (exactly "
              "the GameCube game) or Enhanced (speed-ups allowed), and which mods it uses. The choice is kept with the "
              "save and checked when you CONTINUE. The game is paused while it is open. Off: NEW GAME starts at once.",
              false, true)
          .Implemented(),

      D::Enum("gameplay.text_speed", C::Gameplay, "Text speed",
              "How fast message boxes write out their text, in the field and in battle: 2x and 3x write that many "
              "letters per frame (pauses inside the text get as much shorter), Instant shows the whole box at "
              "once. What the text says and the button press to go on stay as they are.",
              {{"original", "Normal"}, {"2x", "2x"}, {"3x", "3x"}, {"instant", "Instant"}}, "original", "2x")
          .OriginalNote("one letter per frame (holding A or B: three).")
          .Implemented(),
      D::Enum("gameplay.battle_speed", C::Gameplay, "Battle speed", "Speeds up battles.",
              {{"original", "Original"}, {"2x", "2x"}, {"3x", "3x"}}, "original", "original"),
      D::Bool("gameplay.fast_battle_animations", C::Gameplay, "Fast battle animations",
              "While a move's (or an ability's) animation plays in battle, the game runs at the Fast-forward speed "
              "by itself, then carries on at normal speed. The animations are all still there, only shorter; the "
              "battle plays out the same.",
              false, false)
          .FormerIds({"gameplay.skip_animations"})
          .Implemented(),
      D::Bool("gameplay.physical_special_split", C::Gameplay, "Physical/special split",
              "Whether a move hits with Attack or Sp. Atk follows the move itself, as from Pokemon Diamond and "
              "Pearl on, instead of its type: Fire Punch, Crunch and Dragon Claw become physical; Shadow Ball, "
              "Hyper Beam, Sludge Bomb and Hidden Power special (54 moves change). Shadow moves stay as they are. "
              "Changes how battles play, so it is off unless you turn it on.",
              false, false)
          .OriginalNote("physical or special by the move's type, as in every game before Diamond and Pearl.")
          .Implemented(),
      D::Bool("gameplay.fast_forward", C::Gameplay, "Fast-forward key",
              "While the fast-forward key is held (Tab at first, or the right stick pressed in; Controls page), the "
              "whole game runs faster (Fast-forward speed) and its sound plays sped up. Only the waiting gets "
              "shorter: the game itself plays out the same. It waits at normal speed while the memory card saves.",
              false, true)
          .Implemented(),
      D::Enum("gameplay.fast_forward_mode", C::Gameplay, "Fast-forward key works",
              "Hold: fast while the key is held. Toggle: one press turns fast-forward on, the next off.",
              {{"hold", "Hold"}, {"toggle", "Toggle"}}, "hold", "hold")
          .Preference()
          .Implemented(),
      D::Int("gameplay.fast_forward_speed", C::Gameplay, "Fast-forward speed",
             "How many times faster the game runs while fast-forwarding (the key, and Fast battle animations).", 2,
             16, 4, 4)
          .Preference()
          .Implemented(),
      D::Bool("gameplay.run_toggle", C::Gameplay, "Run button",
              "A button switches between walking and running faster. Off: as the original.",
              false, true),
      D::Bool("gameplay.fix_original_bugs", C::Gameplay, "Fix original bugs",
              "Fixes known bugs of the original game. This can change how some things play out.", false, false),

      D::Enum("controls.button_icons", C::Controls, "Button pictures",
              "Which buttons the game's menus show next to their help texts (A, B, X, R...): the GameCube's, or "
              "the buttons and keys that press them on your own device, as your controls are set. The controller "
              "in use: whatever you pressed last, the keyboard or controller 1's controller; it changes as soon as "
              "you switch. Only the pictures change.",
              {{"gamecube", "GameCube"}, {"auto", "The controller in use"}, {"xbox", "Xbox"},
               {"playstation", "PlayStation"}, {"nintendo", "Nintendo Switch"}, {"keyboard", "Keyboard"}},
              "gamecube", "auto")
          .Implemented(),

      D::Int("audio.master_volume", C::Audio, "Volume", "Volume of all the game's sound, in percent.", 0, 100, 100,
             100)
          .Preference()
          .Implemented(),
      D::Int("audio.music_volume", C::Audio, "Music volume", "Volume of the music, in percent.", 0, 100, 100, 100)
          .Preference()
          .Implemented(),
      D::Int("audio.sfx_volume", C::Audio, "Sound effects volume", "Volume of the sound effects, in percent.", 0, 100,
             100, 100)
          .Preference()
          .Implemented(),
      D::Int("audio.cry_volume", C::Audio, "Pokemon cry volume", "Volume of the Pokemon cries, in percent.", 0, 100,
             100, 100)
          .Preference()
          .Implemented(),
      D::Enum("audio.menu_music", C::Audio, "Music in menus",
              "What the music does while the SETTINGS page is open (the game itself is paused). Fades out: fades "
              "out and back in. Plays on: the music carries on as far as the game had already loaded it (for music "
              "read from the disc, a few seconds), then fades out; sound effects fade out at once. Stops: stops at "
              "once. Either way the game carries on exactly as if it had never been paused.",
              {{"fade", "Fades out"}, {"keep", "Plays on"}, {"stop", "Stops"}}, "fade", "fade")
          .Preference()
          .Implemented(),
  };
}

}

void RegisterStandardSettings(Registry& registry) {
  for (const Definition& d : StandardSettings()) {
    registry.Add(d);
  }
}

}
