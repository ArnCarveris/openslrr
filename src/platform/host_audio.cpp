#include "slrr/host_audio.hpp"
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <cstdio>

namespace slrr {
namespace {
std::weak_ptr<HostAudio>& shared_audio() {
  static std::weak_ptr<HostAudio> audio;
  return audio;
}
MIX_Mixer* open_group(const char* name) {
  // Each call opens a new logical device on the default output. SDL follows
  // the default device when the hardware changes.
  MIX_Mixer* mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
  host_audio_check(mixer != nullptr, name);
  return mixer;
}
}

bool host_audio_check(bool ok, const char* operation) {
  if (!ok) std::fprintf(stderr, "[audio] SDL_mixer %s: %s\n", operation, SDL_GetError());
  return ok;
}

HostAudio::~HostAudio() {
  if (effects) MIX_DestroyMixer(effects);
  if (music) MIX_DestroyMixer(music);
  if (fmv) MIX_DestroyMixer(fmv);
  if (initialized) MIX_Quit();
}

std::shared_ptr<HostAudio> host_audio_acquire() {
  if (auto audio = shared_audio().lock()) return audio;
  auto audio = std::make_shared<HostAudio>();
  audio->initialized = host_audio_check(MIX_Init(), "init");
  if (!audio->initialized || !(audio->effects = open_group("effects mixer")) ||
      !(audio->music = open_group("music mixer")) || !(audio->fmv = open_group("FMV mixer")))
    return {};
  shared_audio() = audio;
  const int version = MIX_Version();
  std::fprintf(stderr, "[audio] SDL_mixer %d.%d.%d initialized (%s)\n",
               SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
               SDL_VERSIONNUM_MICRO(version), SDL_GetCurrentAudioDriver());
  return audio;
}
} // namespace slrr
