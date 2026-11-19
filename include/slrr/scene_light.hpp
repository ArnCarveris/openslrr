#pragma once
#include <array>
#include <cmath>
namespace slrr {
// Renderer light contract, independent of legacy meshes and material formats.
// Position is render-space metres; intensity uses engine units (not calibrated
// lumens). This first implementation budgets one shadowed point source per
// interior.
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
// Broad fixture source with an explicit hood/cone, separate from point
// fallback.
struct SpotLight {
  void *owner = nullptr;
  std::array<float, 3> position{}, direction{0, -1, 0}, color{1, 1, 1};
  float intensity = 24.f, range = 20.f;
  float outer_cos =
      .173648f; // Fixed 65-degree inner / default 80-degree outer half-angle
  bool valid() const {
    float n = 0;
    for (int i = 0; i < 3; ++i) {
      if (!std::isfinite(position[i]) || !std::isfinite(direction[i]) ||
          !std::isfinite(color[i]) || color[i] < 0)
        return false;
      n += direction[i] * direction[i];
    }
    return n > .99f && n < 1.01f && std::isfinite(intensity) &&
           intensity >= 0 && std::isfinite(range) && range > .1f &&
           std::isfinite(outer_cos) && outer_cos > 0 && outer_cos < .422618f;
  }
};
} // namespace slrr
