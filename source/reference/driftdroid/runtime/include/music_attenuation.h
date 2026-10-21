#pragma once

#include <cstdint>

namespace MusicAttenuation {

// Enables the optional Windows media-session integration. The monitor is
// started lazily the first time this is enabled.
void SetEnabled(bool enabled) noexcept;
void SetMusicVolume(float volume) noexcept;
void SetSoundEffectsVolume(float volume) noexcept;
void SetUiVolume(float volume) noexcept;
void SetVoicesVolume(float volume) noexcept;
bool IsExternalMediaPlaying() noexcept;
bool IsMediaControlAvailable() noexcept;
bool IsMediaControlInitializationComplete() noexcept;

// Android has no polling monitor thread (see music_attenuation.cpp's StartMonitor) - instead the
// platform layer (android_jni_bridge.cpp, driven by MainActivity's AudioManager focus listener)
// reports state changes here directly as they happen. Also usable by any other platform that
// wants to drive this externally instead of via a monitor thread.
void ReportExternalMediaPlaying(bool playing) noexcept;

// The built-in music player (music_player.cpp) is playing with "mute the game's music" on: the
// game's music bus goes silent exactly as it does for external media, whatever SetEnabled says.
void SetMusicPlayerActive(bool active) noexcept;

// Called from the guest scheduler/audio path. This applies state changes to
// the live SoundPlayer buses, so changing a category or entering/leaving
// attenuation never requires a scene or race restart.
void TickGuest() noexcept;

// Accurate native replacement for nw4r::snd::SoundPlayer::SetVolume. Only
// the category multipliers are applied here; the game's requested bus volume
// remains the base value.
void SetSoundPlayerVolume(uint32_t soundPlayer, float requestedVolume);

} // namespace MusicAttenuation
