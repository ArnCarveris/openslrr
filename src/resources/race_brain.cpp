// Adapted from SLRR_rev v221 cbb99147 engine/race-driver.ts.
#include "slrr/gameref.hpp"
#include "slrr/part_runtime.hpp"
#include "slrr/race_runtime.hpp"
#include "slrr/rpk_index.hpp"
#include "slrr/scene_instances.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>
namespace slrr {
namespace {
struct Brain {
  void *key = nullptr;
  uint64_t incarnation = 0;
  Object *car = nullptr;
  uint64_t car_incarnation = 0;
  const RpkIndex *resources = nullptr;
  std::string mode;
  RoadRoute route;
  float progress = 0, steering = 0, level = 1, replan = 0;
  int32_t target = 0;
  RoadPoint offset{};
};
std::unordered_map<void *, Brain> brains;
Object *live_car(int32_t id) {
  auto *n = gameref_find_live(id);
  if (!n)
    return nullptr;
  return part_chassis(static_cast<Object *>(n->key));
}
bool read_point(std::istream &in, RoadPoint &p) {
  char a = 0, b = 0;
  return bool(in >> p[0] >> a >> p[1] >> b >> p[2]) && a == ',' && b == ',' &&
         std::all_of(p.begin(), p.end(),
                     [](float x) { return std::isfinite(x); });
}
void *car_ground(Object *car) {
  auto *n = gameref_get(car);
  for (int i = 0; n && i < 32; ++i) {
    if (n->kind == GameRefKind::GroundRef)
      return n->key;
    n = gameref_get(n->parent);
  }
  return nullptr;
}
RoadPoint car_position(Object *car) {
  PartMatrix m;
  if (!scene_instances_script_matrix(car, m.data()))
    return {};
  return {m[12], m[13], m[14]};
}
RoadRoute spline(const RpkIndex &rpk, const std::string &file,
                 const RoadPoint &start) {
  std::string path = file;
  std::replace(path.begin(), path.end(), '\\', '/');
  std::filesystem::path relative(path);
  if (relative.is_absolute())
    return {};
  for (const auto &p : relative)
    if (p == "..")
      return {};
  std::ifstream in(std::filesystem::path(rpk.game_root()) / relative);
  RoadRoute r;
  RoadPoint p, d;
  std::vector<std::pair<RoadPoint, RoadPoint>> keys;
  std::string line;
  while (std::getline(in, line) && keys.size() < 100000) {
    std::istringstream row(line);
    if (row >> p[0] >> p[1] >> p[2] >> d[0] >> d[1] >> d[2]) {
      if (!std::all_of(p.begin(), p.end(),
                       [](float v) { return std::isfinite(v); }) ||
          !std::all_of(d.begin(), d.end(),
                       [](float v) { return std::isfinite(v); }))
        return {};
      keys.push_back({p, d});
    }
  }
  if (keys.size() < 2)
    return {};
  size_t first = 0;
  float nearest = std::numeric_limits<float>::infinity();
  for (size_t i = 0; i < keys.size(); ++i) {
    float distance = road_distance(start, keys[i].first);
    if (distance < nearest) {
      nearest = distance;
      first = i;
    }
  }
  r.points.push_back(start);
  for (size_t i = first; i + 1 < keys.size(); ++i) {
    RoadSegment s;
    s.start = keys[i].first;
    s.end = keys[i + 1].first;
    s.length = road_distance(s.start, s.end);
    for (int k = 0; k < 3; ++k) {
      s.start_tangent[k] = keys[i].second[k] * s.length;
      s.end_tangent[k] = keys[i + 1].second[k] * s.length;
    }
    for (int j = 0; j < 16; ++j)
      r.points.push_back(trc_sample(s, float(j) / 16));
  }
  r.points.push_back(keys.back().first);
  for (size_t i = 1; i < r.points.size(); ++i)
    r.length += road_distance(r.points[i - 1], r.points[i]);
  return r;
}
void traffic_route(Brain &b, const RoadPoint &position) {
  auto graph = race_ground_roads(*b.resources, car_ground(b.car));
  auto p = graph ? graph->project(position) : std::nullopt;
  if (!p)
    return;
  const auto &e = graph->edges()[p->edge];
  b.route = {};
  b.route.points.push_back(position);
  for (size_t i = 0; i < e.points.size(); ++i)
    if (e.distances[i] > p->along + .5f)
      b.route.points.push_back(e.points[i]);
  if (b.route.points.size() < 3) {
    for (const auto &next : graph->edges())
      if (next.from == e.to && next.to != e.from) {
        b.route.points.insert(b.route.points.end(), next.points.begin(),
                              next.points.end());
        break;
      }
  }
  for (size_t i = 1; i < b.route.points.size(); ++i)
    b.route.length += road_distance(b.route.points[i - 1], b.route.points[i]);
  b.progress = 0;
}
} // namespace
void race_brain_forget(void *key) {
  brains.erase(key);
  std::erase_if(brains,
                [&](const auto &entry) { return entry.second.car == key; });
}
void race_brains_reset() { brains.clear(); }
bool race_brain_command(const RpkIndex &rpk, void *key,
                        const std::string &command) {
  std::istringstream input(command);
  std::string verb;
  input >> verb;
  if (verb != "controllable" && verb != "renderinstance" && verb != "leave" &&
      verb != "AI_level" && verb != "AI_NightRace" && verb != "AI_BeginRace" &&
      verb != "AI_stop" && verb != "AI_suspend" && verb != "AI_race" &&
      verb != "AI_spline" && verb != "AI_follow" && verb != "AI_GoToTraffic" &&
      verb != "AI_GoToTarget")
    return false;
  auto *n = gameref_get(key);
  if (!n)
    return false;
  auto &b = brains[n->key];
  if (b.incarnation != n->incarnation) {
    b = {};
    b.key = n->key;
    b.incarnation = n->incarnation;
  }
  b.resources = &rpk;
  if (verb == "controllable") {
    int32_t id = 0;
    if (input >> id) {
      b.car = live_car(id);
      auto *car = gameref_get(b.car);
      b.car_incarnation = car ? car->incarnation : 0;
    }
    return true;
  }
  if (verb == "renderinstance")
    return true;
  if (verb == "AI_level") {
    float level = 1;
    if (input >> level && std::isfinite(level))
      b.level = std::clamp(level, .1f, 2.f);
    return true;
  }
  if (verb == "AI_NightRace")
    return true; // Mode hint; AI_race supplies the actual finish.
  b.mode = verb;
  b.route = {};
  b.progress = 0;
  b.replan = 0;
  if (!b.car)
    return true;
  if (verb == "AI_race" || verb == "AI_GoToTarget") {
    RoadPoint finish;
    if (read_point(input, finish)) {
      auto route =
          race_query_route(rpk, car_ground(b.car), car_position(b.car), finish);
      if (route)
        b.route = *route;
    }
  } else if (verb == "AI_spline") {
    float width = 0;
    std::string path;
    if (input >> width >> path && std::isfinite(width) && width > 0)
      b.route = spline(rpk, path, car_position(b.car));
  } else if (verb == "AI_follow") {
    if (read_point(input, b.offset))
      input >> b.target;
  } else if (verb == "AI_GoToTraffic")
    traffic_route(b, car_position(b.car));
  if (!b.route.points.empty() || verb == "AI_follow")
    part_command(b.car, "start");
  return true;
}
void race_set_traffic_mode(const RpkIndex &rpk, Object *car) {
  for (auto &entry : brains)
    if (entry.second.car == car) {
      race_brain_command(rpk, entry.first, "AI_GoToTraffic");
      return;
    }
  auto id = gameref_make_live(car);
  race_brain_command(rpk, car, "controllable " + std::to_string(id));
  race_brain_command(rpk, car, "AI_GoToTraffic");
}
bool race_drive_input(Object *car, const RoadPoint &position, float yaw,
                      float speed, float dt, RaceDriveInput &out) {
  Brain *b = nullptr;
  auto *n = gameref_get(car);
  if (!n)
    return false;
  for (auto &entry : brains)
    if (entry.second.car == car &&
        entry.second.car_incarnation == n->incarnation) {
      b = &entry.second;
      break;
    }
  if (!b)
    return false;
  out = {};
  if (b->mode == "AI_GoToTraffic" &&
      race_traffic_halted(car_ground(car), position))
    return true;
  if (b->mode == "AI_follow") {
    b->replan -= dt;
    if (b->replan <= 0) {
      b->replan = 1;
      auto *target = live_car(b->target);
      if (target) {
        auto finish = car_position(target);
        for (int k = 0; k < 3; ++k)
          finish[k] += b->offset[k];
        auto r =
            race_query_route(*b->resources, car_ground(car), position, finish);
        b->route = r ? *r : RoadRoute{};
        b->progress = 0;
      }
    }
  }
  if (b->mode == "AI_GoToTraffic" &&
      (b->route.points.size() < 2 ||
       road_distance(position, b->route.points.back()) < 8))
    traffic_route(*b, position);
  if (b->route.points.size() < 2)
    return true;
  const auto &points = b->route.points;
  float along = 0, best = std::numeric_limits<float>::infinity(),
        projected = b->progress;
  for (size_t i = 1; i < points.size(); ++i) {
    auto a = points[i - 1], q = points[i];
    float length = road_distance(a, q);
    if (length < 1e-6f)
      continue;
    if (along + length < b->progress - 2) {
      along += length;
      continue;
    }
    if (along > b->progress + 50)
      break;
    float t = 0;
    for (int k = 0; k < 3; ++k)
      t += (position[k] - a[k]) * (q[k] - a[k]);
    t = std::clamp(t / (length * length), 0.f, 1.f);
    RoadPoint near;
    for (int k = 0; k < 3; ++k)
      near[k] = a[k] + t * (q[k] - a[k]);
    float d = road_distance(position, near);
    if (d < best) {
      best = d;
      projected = along + t * length;
    }
    along += length;
  }
  b->progress = std::max(b->progress, projected);
  auto finish = points.back();
  if (b->progress >= std::max(0.f, b->route.length - 12.f) &&
      std::hypot(position[0] - finish[0], position[2] - finish[2]) < 3) {
    b->route = {};
    return true;
  }
  const auto target =
      road_at(b->route,
              b->progress + std::clamp(std::fabs(speed) * .8f + 4, 4.f, 18.f));
  RoadPoint error;
  for (int k = 0; k < 3; ++k)
    error[k] = target[k] - position[k];
  float target_yaw = std::atan2(-error[0], -error[2]),
        angle =
            std::atan2(std::sin(target_yaw - yaw), std::cos(target_yaw - yaw));
  float curvature =
      2 * std::sin(angle) / std::max(2.f, road_distance(error, {}));
  float request = std::clamp(std::atan(2.7f * curvature) / .6f, -1.f, 1.f);
  b->steering += std::clamp(request - b->steering, -dt * 3, dt * 3);
  float desired =
      std::min({32.f * std::clamp(b->level, .5f, 1.5f),
                std::sqrt(4.5f / std::max(.005f, std::fabs(curvature))),
                std::sqrt(8 * std::max(road_distance(position, finish),
                                       b->route.length - b->progress))});
  if (b->mode == "AI_GoToTraffic")
    desired = std::min(desired, 12.f);
  out.brake = std::clamp((speed - desired) * .35f, 0.f, 1.f);
  out.throttle =
      out.brake > .05f ? 0 : std::clamp((desired - speed) * .5f, 0.f, 1.f);
  out.steer = b->steering;
  out.start = true;
  return true;
}
} // namespace slrr
