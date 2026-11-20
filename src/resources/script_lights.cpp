#include "slrr/script_lights.hpp"
#include "slrr/gameref.hpp"

#include <algorithm>
#include <unordered_map>

namespace slrr {
namespace {
struct Entry {
  ScriptLight light;
  uint64_t order = 0;
};
std::unordered_map<void *, Entry> g_script_lights;
uint64_t g_script_light_order = 0;
} // namespace

bool script_lights_create(void *key, void *parent, ScriptLightKind kind) {
  auto *node = gameref_get(parent);
  if (!key || !node || node->key == key)
    return false;
  // A second create on the same object restarts it with default values.
  Entry entry;
  entry.light.key = key;
  entry.light.parent = node->key;
  entry.light.kind = kind;
  entry.order = ++g_script_light_order;
  g_script_lights[key] = entry;
  return true;
}

ScriptLight *script_lights_get(void *key) {
  const auto it = g_script_lights.find(key);
  return it == g_script_lights.end() ? nullptr : &it->second.light;
}

void script_lights_destroy(void *key) { g_script_lights.erase(key); }

void script_lights_forget_parent(void *parent) {
  if (!parent)
    return;
  for (auto it = g_script_lights.begin(); it != g_script_lights.end();)
    it = it->second.light.parent == parent ? g_script_lights.erase(it)
                                           : std::next(it);
}

void script_lights_clear() { g_script_lights.clear(); }

size_t script_lights_count() { return g_script_lights.size(); }

std::vector<const ScriptLight *> script_lights_all() {
  std::vector<const Entry *> entries;
  entries.reserve(g_script_lights.size());
  for (const auto &[key, entry] : g_script_lights)
    entries.push_back(&entry);
  std::sort(entries.begin(), entries.end(),
            [](const Entry *a, const Entry *b) { return a->order < b->order; });
  std::vector<const ScriptLight *> out;
  out.reserve(entries.size());
  for (const auto *entry : entries)
    out.push_back(&entry->light);
  return out;
}

void script_lights_visit_gc_roots(const GcRootVisitor &visit) {
  for (const auto &[key, entry] : g_script_lights) {
    visit(key);
    visit(entry.light.parent);
  }
}
} // namespace slrr
