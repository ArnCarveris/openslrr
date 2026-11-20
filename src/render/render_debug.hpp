#pragma once
// Renderer debug overrides and statistics for the OpenSLRR Debug menu (#243).
// Every override defaults to the normal renderer behaviour and is never saved:
// the menu only changes this struct. Options that already exist (Enhanced
// graphics, the debug overlay's RenderOptions) stay where they are and are
// edited in place, so the menu and the real options cannot disagree.
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

#include <bgfx/bgfx.h>

namespace slrr {

struct RenderDebugOverrides {
  // Pass toggles. false removes the pass (its view stays empty).
  bool ambient_occlusion = true;
  std::array<bool, 4> sun_cascades{true, true, true, true};
  bool shadow_caching = true;      // false redraws cached shadow maps every frame
  bool skinned_casters = true;     // characters in sun cascades and lamp tiles
  bool fixture_shadows = true;     // garage fixture shadow atlas
  bool lamp_lighting = true;       // street lamps outside garages
  bool lamp_shadows = true;        // scenery lamp shadow tiles
  bool car_lamp_tiles = true;      // car-only lamp shadow tiles
  bool reflections = true;         // dynamic reflection cubemap
  bool sky = true;                 // sky dome instances
  bool clouds = true;              // volumetric clouds (with the saved option)
  bool fog = true;
  bool msaa = true;                // with the saved MSAA level
  bool anisotropic = true;         // with the saved anisotropic option
  bool map_props = true;           // ground placements (instanced and single)
  bool vehicles = true;            // vehicle parts (draw, shadows, AO)
  bool characters = true;          // skinned characters
  // Budgets. Negative or zero means "renderer default" where noted.
  int lamp_budget = -1;            // frustum-selected lamps, default 40
  float lamp_distance = -1.f;      // metres, default 500
  int shadowed_lamps = -1;         // scenery lamp shadow slots, default 6
  int car_tiles = -1;              // car-only tiles, default 16
  int local_shadow_tile = 0;       // 0: from Shadow quality, else 256/512/1024
  int sun_shadow_size = 0;         // 0: from Shadow quality, else 1024/2048/4096
  float cascade_scale = 1.f;       // multiplies the four cascade radii
  float ao_resolution_scale = 1.f; // multiplies the AO buffer size (0.25-1)
  bool operator==(const RenderDebugOverrides &) const = default;
};

// Per-frame numbers the renderer publishes for the menu.
struct RenderDebugStats {
  uint32_t instances = 0, batches = 0, batch_instances = 0;
  uint32_t lamps_registered = 0, lamps_lit = 0, lamps_shadowed = 0, car_tiles = 0;
  uint64_t lamp_tile_redraws = 0, car_tile_redraws = 0, sun_atlas_redraws = 0;
  bool sun_cascades = false, fixture_atlas = false, ao = false, reflections = false;
  bgfx::TextureHandle fixture_atlas_texture = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle sun_atlas_texture = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle ao_texture = BGFX_INVALID_HANDLE;
  uint16_t fixture_tile = 0, sun_atlas_size = 0, ao_width = 0, ao_height = 0;
};

inline RenderDebugOverrides &render_debug() {
  static RenderDebugOverrides overrides;
  return overrides;
}
inline RenderDebugStats &render_debug_stats() {
  static RenderDebugStats stats;
  return stats;
}

// Effective budgets (overrides clamped to what the shaders hold).
inline int render_debug_budget(int value, int fallback, int maximum) {
  return value < 0 ? fallback : value > maximum ? maximum : value;
}

// One "name = value" line per override that differs from its default, for
// the snapshot header (#243). Empty when every override is at its default.
inline std::string render_debug_overrides_text(const RenderDebugOverrides &o) {
  const RenderDebugOverrides d;
  std::string out;
  char line[96];
  const auto flag = [&](const char *name, bool value, bool fallback) {
    if (value == fallback) return;
    std::snprintf(line, sizeof(line), "  %s = %s\n", name, value ? "on" : "off");
    out += line;
  };
  const auto number = [&](const char *name, double value, double fallback, const char *unit) {
    if (value == fallback) return;
    std::snprintf(line, sizeof(line), "  %s = %g%s\n", name, value, unit);
    out += line;
  };
  flag("ambient occlusion", o.ambient_occlusion, d.ambient_occlusion);
  for (size_t i = 0; i < o.sun_cascades.size(); ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "sun cascade %zu", i);
    flag(name, o.sun_cascades[i], d.sun_cascades[i]);
  }
  flag("shadow caching", o.shadow_caching, d.shadow_caching);
  flag("character casters", o.skinned_casters, d.skinned_casters);
  flag("garage fixture shadows", o.fixture_shadows, d.fixture_shadows);
  flag("street lamp lighting", o.lamp_lighting, d.lamp_lighting);
  flag("street lamp shadows", o.lamp_shadows, d.lamp_shadows);
  flag("car lamp tiles", o.car_lamp_tiles, d.car_lamp_tiles);
  flag("reflections", o.reflections, d.reflections);
  flag("sky", o.sky, d.sky);
  flag("clouds", o.clouds, d.clouds);
  flag("fog", o.fog, d.fog);
  flag("MSAA", o.msaa, d.msaa);
  flag("anisotropic", o.anisotropic, d.anisotropic);
  flag("map props", o.map_props, d.map_props);
  flag("vehicles", o.vehicles, d.vehicles);
  flag("characters", o.characters, d.characters);
  number("lit lamp budget", o.lamp_budget, d.lamp_budget, "");
  number("lamp distance", o.lamp_distance, d.lamp_distance, " m");
  number("shadowed lamps", o.shadowed_lamps, d.shadowed_lamps, "");
  number("car lamp tiles budget", o.car_tiles, d.car_tiles, "");
  number("local shadow tile", o.local_shadow_tile, d.local_shadow_tile, " px");
  number("sun cascade tile", o.sun_shadow_size, d.sun_shadow_size, " px");
  number("cascade radius scale", o.cascade_scale, d.cascade_scale, "x");
  number("AO resolution scale", o.ao_resolution_scale, d.ao_resolution_scale, "x");
  return out;
}

// Display name of a bgfx view id for the per-view timing table.
inline const char *render_debug_view_name(uint16_t view) {
  switch (view) {
  case 0: return "Sun cascade 0 / point face 0";
  case 1: return "Vehicle depth (sun) / point face 1";
  case 2: return "Sun cascade 1 / point face 2";
  case 3: return "Sun cascade 2 / point face 3";
  case 4: return "Sun cascade 3 / point face 4";
  case 5: return "World";
  case 6: return "FMV layer";
  case 7: return "OSD under world";
  case 64: return "Point face 5";
  case 65: return "Interior sun";
  case 66: return "Interior sun aperture";
  case 70: return "OSD";
  case 71: return "Debug overlay";
  case 136: return "Car lamp tiles";
  case 140: return "AO depth prepass";
  case 141: return "AO pass 1";
  case 142: return "AO pass 2";
  case 143: return "AO pass 3";
  default: break;
  }
  if (view >= 8 && view < 64) return "Script viewport preview";
  if (view >= 72 && view < 84) return "Reflection face";
  if (view >= 128 && view < 136) return "Fixture / lamp shadow tile";
  return "Other";
}
} // namespace slrr
