#pragma once
#include <array>
#include <cmath>
namespace slrr {
// Renderer light contract, independent of legacy meshes and material formats.
// Position is render-space metres; intensity uses engine units (not calibrated lumens).
// This first implementation budgets one shadowed point source per interior.
struct PointLight {
  void *owner = nullptr;
  std::array<float, 3> position{}, color{1, 1, 1}, ambient{.04f, .04f, .04f};
  float intensity = 18.f;
  float range = 32.f;
  bool casts_shadows = true;
  bool valid() const {
    if (!std::isfinite(intensity) || intensity < 0 || !std::isfinite(range) ||
        range <= .1f)
      return false;
    for (int i = 0; i < 3; ++i)
      if (!std::isfinite(position[i]) || !std::isfinite(color[i]) ||
          color[i] < 0 || !std::isfinite(ambient[i]) || ambient[i] < 0)
        return false;
    return true;
  }
  bool operator==(const PointLight &) const = default;
};
} // namespace slrr
