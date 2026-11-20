#pragma once
#include "slrr/gc_roots.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace slrr {
// Lights created by scripts through java.render.LightSource (#235). Each entry
// is keyed by the Java light object and attached to a parent GameRef (any
// ResourceRef with a GameRef state, e.g. a RenderRef). Position and direction
// are in the parent's local script space (metres, script Z); the renderer
// converts them with the parent's drawn matrix.
enum class ScriptLightKind : int32_t { Point = 0, Spot = 1, Rect = 2 };

struct ScriptLight {
  void *key = nullptr;    // the Java LightSource object
  void *parent = nullptr; // canonical GameRef key of the parent
  ScriptLightKind kind = ScriptLightKind::Point;
  std::array<float, 3> position{}, direction{0, -1, 0}, color{1, 1, 1};
  float intensity = 24.f, range = 20.f;
  float inner_degrees = 65.f, outer_degrees = 80.f; // half-angles
  float width = 0.f, height = 0.f;                  // RectLight only
  bool shadows = true, enabled = true;
};

// Limits applied by the natives. Inputs outside them are clamped; non-finite
// values are ignored (the previous value stays).
inline constexpr float kScriptLightMaxIntensity = 1000.f;
inline constexpr float kScriptLightMinRange = .2f, kScriptLightMaxRange = 500.f;
inline constexpr float kScriptLightMaxSize = 100.f;
inline constexpr float kScriptLightMaxPosition = 1.0e5f;

// Returns false when parent has no GameRef state; the light is not created.
bool script_lights_create(void *key, void *parent, ScriptLightKind kind);
ScriptLight *script_lights_get(void *key);
// Removes one light (LightSource.destroy). Unknown keys are ignored.
void script_lights_destroy(void *key);
// Called by gameref_remove: every light attached to that parent goes too.
void script_lights_forget_parent(void *parent);
// World teardown (gameref_clear, native re-registration).
void script_lights_clear();
size_t script_lights_count();
// Snapshot in creation order, so the renderer's light budget is stable.
std::vector<const ScriptLight *> script_lights_all();
// Keeps registered Java light objects alive until destroy or parent removal.
void script_lights_visit_gc_roots(const GcRootVisitor &visit);
} // namespace slrr
