#pragma once
#include "slrr/host_game_options.hpp"
#include <algorithm>
#include <cstdint>
namespace slrr {
inline uint16_t local_shadow_resolution() {
  return uint16_t(
      256u << std::clamp(host_enhanced_graphics().shadow_quality, 0, 2));
}
inline uint16_t sun_shadow_resolution() {
  return uint16_t(
      512u << std::clamp(host_enhanced_graphics().shadow_quality, 0, 2));
}
inline uint16_t scenery_shadow_resolution() {
  return host_enhanced_graphics().shadow_quality == 0 ? 1024 : 2048;
}
} // namespace slrr
