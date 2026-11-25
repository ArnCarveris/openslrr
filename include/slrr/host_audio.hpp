#pragma once

#include <cstdint>
#include <memory>

struct MIX_Mixer;
struct MIX_Track;

namespace slrr {

// Host-thread-only service shared by effects, music and stock FMV. Owners retain it
// so static destruction cannot destroy a mixer before its tracks and audio.
// Each group is its own SDL3_mixer mixer (a logical device on the default
// output), so its gain is independent of the other two. SDL_mixer mixes on
// SDL's audio thread; there is no per-frame update to pump.
struct HostAudio {
  MIX_Mixer* effects = nullptr;
  MIX_Mixer* music = nullptr;
  MIX_Mixer* fmv = nullptr;
  bool initialized = false;
  ~HostAudio();
};
std::shared_ptr<HostAudio> host_audio_acquire();
// Logs SDL_GetError() for a failed SDL/SDL3_mixer call and returns `ok`.
bool host_audio_check(bool ok, const char* operation);

// Diagnostics and tests: the track behind an effect instance (null when the
// instance is unknown) and the stock Cinepak FMV PCM track (null when none).
MIX_Track* host_sfx_track(int32_t instance);
MIX_Track* cinepak_fmv_audio_track();

} // namespace slrr
