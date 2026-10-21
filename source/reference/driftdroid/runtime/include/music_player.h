#pragma once

// Plays the player's own MP3 files (the "Music" folder beside Config.toml: on Switch
// sdmc:/switch/WiiCompiled/Music) over the game, from the settings menu. Decoding happens on a
// helper thread that keeps about half a second queued in its own SDL audio stream; SDL mixes that
// stream with the game's.

#include <string>
#include <vector>

namespace MusicPlayer {

struct Status {
    bool loaded = false;   // a track is selected (playing or paused)
    bool playing = false;  // and not paused
    int index = -1;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
};

// Folder the tracks are read from, for display.
std::string FolderForDisplay();
// Re-reads the folder. Returns the track names (file names without ".mp3"), sorted.
std::vector<std::string> Rescan();
std::vector<std::string> Tracks();

void Play(int index);
void TogglePause();
void Next();
void Previous();
void Stop();

float Volume();
void SetVolume(float volume);
bool Shuffle();
void SetShuffle(bool shuffle);
// Silences the game's own music (not its sound effects) while a track is playing.
bool MuteGameMusic();
void SetMuteGameMusic(bool mute);

Status GetStatus();

}  // namespace MusicPlayer
