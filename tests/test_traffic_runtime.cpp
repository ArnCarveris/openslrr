#include "race_fixture.hpp"
#include <cmath>
#include <limits>
#include <sstream>
namespace slrr {
void spawn_ground_shell(Jvm &, const RpkIndex &, Object *);
}
using namespace slrr;
Object *vec(Jvm &vm, const RoadPoint &p) {
  auto *v = vm.new_object("java.lang.Vector3");
  for (int i = 0; i < 3; ++i)
    v->fields[i == 0 ? "x" : i == 1 ? "y" : "z"] = Value::make_float(p[i]);
  return v;
}
void lifecycle_and_stream() {
  RaceFixture f;
  f.packs.open_all();
  auto graph = race_ground_roads(f.packs, &f.ground);
  SLRR_REQUIRE(graph && !graph->edges().empty());
  auto start = graph->edges()[0].points.front();
  f.player = f.car();
  gameref_set_matrix(f.player, start[0], start[1], start[2], 0, 0, 0);
  f.controller.class_name = "java.io.Controller";
  gameref_upsert(&f.controller);
  auto *command = f.vm.new_string("controllable " +
                                  std::to_string(gameref_make_live(f.player)));
  f.registry.invoke("java.util.resource.GameRef", "queueEvent",
                    "(Ljava.util.resource.ResourceRef;ILjava.lang.String;)V",
                    &f.controller,
                    {Value::make_obj(nullptr), Value::make_int(0x10),
                     Value::make_obj(command)});
  SLRR_REQUIRE(part_controlled_chassis() == f.player);
  fs::create_directories(f.root / "probe/src");
  std::ofstream(f.root / "probe/src/GameRef.java")
      << "package java.util.resource; public class GameRef {}";
  std::ofstream(f.root / "probe/src/String.java")
      << "package java.lang; public class String {}";
  std::ofstream(f.root / "probe/src/Listener.java")
      << "package probe; import java.util.resource.*; import java.lang.*; "
         "public class Listener { public native void handleEvent(GameRef "
         "source,int event,String param); }";
  auto report = script_compile_all(f.root.string(), "probe");
  SLRR_REQUIRE(report.failed == 0);
  for (const auto &e : fs::recursive_directory_iterator(f.root / "probe"))
    if (e.path().extension() == ".class")
      SLRR_REQUIRE(f.vm.load_class_file(e.path().string()));
  std::vector<std::string> events;
  f.registry.register_method(
      "probe.Listener", "handleEvent",
      "(Ljava.util.resource.GameRef;ILjava.lang.String;)V",
      [&](Object *, const std::vector<Value> &a) {
        SLRR_REQUIRE(a[0].v.o == &f.ground && a[1].v.i == 0x10);
        events.push_back(a[2].v.o->str);
        return Value::make_void();
      });
  auto *listener = f.vm.new_object("probe.Listener");
  gameref_set_event_mask(listener, 0x10);
  f.sections.adopt_actual(listener);
  auto *rival = f.car();
  auto point = start;
  point[0] += 2;
  auto *where = vec(f.vm, point);
  auto add = [&]() {
    return f
        .call("addTrafficCar",
              "(Ljava.util.resource.GameRef;Ljava.lang.Vector3;)I",
              {Value::make_obj(rival), Value::make_obj(where)})
        .v.i;
  };
  auto id = add();
  SLRR_REQUIRE(id > 0 && add() == id && race_traffic_pointer(rival) == id);
  f.call("notifyTrafficCar", "(II)I",
         {Value::make_int(id), Value::make_int(1)});
  race_traffic_tick(f.vm, .1f);
  SLRR_REQUIRE(events.size() == 1 && events[0].starts_with("car_add ") &&
               !gameref_get(rival)->streamed_hidden);
  race_traffic_tick(f.vm, .1f);
  SLRR_REQUIRE(events.size() == 1);
  f.call("notifyTrafficCar", "(II)I",
         {Value::make_int(id), Value::make_int(0)});
  race_traffic_tick(f.vm, .1f);
  SLRR_REQUIRE(events.size() == 2 && events.back().starts_with("car_rem "));
  f.call("notifyTrafficCar", "(II)I",
         {Value::make_int(id), Value::make_int(1)});
  race_traffic_tick(f.vm, .1f);
  SLRR_REQUIRE(events.size() == 3);
  f.call("remTrafficCar", "(I)V", {Value::make_int(id)});
  race_traffic_tick(f.vm, .1f);
  SLRR_REQUIRE(gameref_get(rival) && race_traffic_pointer(rival) == 0 &&
               events.size() == 4);
  auto *taxi = f.packs.pack_by_name("cars/traffic/Taxi.rpk");
  if (!taxi)
    taxi = f.packs.pack_by_path("cars/traffic/Taxi.rpk");
  SLRR_REQUIRE(taxi);
  Object type;
  type.fields["id"] = Value::make_int(RpkIndex::make_full_id(taxi->pack_id, 6));
  auto positioned = f.call(
      "addTrafficP", "(Ljava.util.resource.GameRef;Ljava.lang.Vector3;IFFF)I",
      {Value::make_obj(&type), Value::make_obj(where), Value::make_int(1),
       Value::make_float(4), Value::make_float(4), Value::make_float(2.5f)});
  SLRR_REQUIRE(positioned.v.i == 1);
  race_traffic_tick(f.vm, 0);
  SLRR_REQUIRE(events.size() == 4);
  race_traffic_tick(f.vm, .01f);
  SLRR_REQUIRE(events.size() == 5);
  std::istringstream inserted(events.back());
  std::string inserted_verb;
  int32_t inserted_id = 0;
  inserted >> inserted_verb >> inserted_id;
  auto *seed = gameref_find_live(inserted_id);
  SLRR_REQUIRE(seed && road_distance(start, {seed->pos[0], seed->pos[1],
                                             seed->pos[2]}) < 10);
  std::vector<std::array<float, 3>> supports;
  SLRR_REQUIRE(scene_instances_supports(seed->key, supports) &&
               supports.size() >= 5);
  float bottom = std::numeric_limits<float>::infinity();
  for (auto &point : supports)
    bottom = std::min(bottom, point[1]);
  std::printf("TRAFFIC bottom=%.3f road=%.3f origin=%.3f\n", bottom, start[1],
              seed->pos[1]);
  SLRR_REQUIRE(std::fabs(bottom - start[1]) < .25f);
  race_traffic_tick(f.vm, .01f);
  SLRR_REQUIRE(events.size() == 5);
  f.call("delTraffic", "()V");
  race_traffic_tick(f.vm, 0);
  SLRR_REQUIRE(events.size() == 6);
  events.clear();
  auto model =
      f.call("addTrafficN", "(Ljava.util.resource.GameRef;IFFF)I",
             {Value::make_obj(&type), Value::make_int(10), Value::make_float(4),
              Value::make_float(4), Value::make_float(2.5f)});
  SLRR_REQUIRE(model.v.i > 0);
  race_traffic_tick(f.vm, .1f);
  SLRR_REQUIRE(events.size() > 0);
  std::istringstream first(events[0]);
  std::string verb;
  int32_t car_id = 0, registration = 0;
  first >> verb >> car_id >> registration;
  auto *traffic = gameref_find_live(car_id);
  SLRR_REQUIRE(traffic && registration > 0);
  RoadPoint before{traffic->pos[0], traffic->pos[1], traffic->pos[2]};
  race_traffic_tick(f.vm, 3);
  SLRR_REQUIRE(road_distance(before, {traffic->pos[0], traffic->pos[1],
                                      traffic->pos[2]}) > 1);
  SLRR_REQUIRE(!scene_instances_collect_root(&f.world).empty());
  f.call("delTraffic", "()V");
  race_traffic_tick(f.vm, .1f);
  SLRR_REQUIRE(!gameref_find_live(car_id));
  size_t added = 0, removed = 0;
  for (const auto &e : events) {
    added += e.starts_with("car_add ");
    removed += e.starts_with("car_rem ");
  }
  SLRR_REQUIRE(added == removed);
  SLRR_REQUIRE(gameref_get(rival));
  part_destroy(rival);
  part_destroy(f.player);
  gameref_remove(&type);
  gameref_remove(listener);
  std::printf("PASS traffic IDs, Controller routing, balanced events, real "
              "Taxi movement/rendering and cleanup (%zu adds)\n",
              added);
}
void red_light_stop_and_release() {
  RaceFixture f;
  f.packs.open_all();
  auto graph = race_ground_roads(f.packs, &f.ground);
  SLRR_REQUIRE(graph && graph->signals().size() > 400);
  size_t selected = graph->edges().size();
  RoadPoint start;
  for (size_t i = 0; i < graph->edges().size(); ++i) {
    const auto &e = graph->edges()[i];
    if (e.length < 40 || graph->edge_signal(i, 0) != 0)
      continue;
    RoadRoute road;
    road.points = e.points;
    auto point = road_at(road, e.length - 13);
    auto projected = graph->project(point);
    if (projected && projected->edge == i) {
      selected = i;
      start = point;
      break;
    }
  }
  SLRR_REQUIRE(selected < graph->edges().size());
  f.player = f.car();
  gameref_set_matrix(f.player, start[0], start[1], start[2], 0, 0, 0);
  f.controller.class_name = "java.io.Controller";
  gameref_upsert(&f.controller);
  part_command(&f.controller,
               "controllable " + std::to_string(gameref_make_live(f.player)));
  Object type;
  auto taxi = f.packs.open_lib("cars/traffic/Taxi.rpk");
  type.fields["id"] = Value::make_int(RpkIndex::make_full_id(taxi, 6));
  auto *where = vec(f.vm, start);
  SLRR_REQUIRE(f.call("addTrafficP",
                      "(Ljava.util.resource.GameRef;Ljava.lang.Vector3;IFFF)I",
                      {Value::make_obj(&type), Value::make_obj(where),
                       Value::make_int(1), Value::make_float(4),
                       Value::make_float(4), Value::make_float(2.5f)})
                   .v.i == 1);
  race_traffic_tick(f.vm, .01f);
  Object *car = nullptr;
  for (const auto &p : part_vehicle_positions())
    if (p.chassis != f.player && race_traffic_pointer(p.chassis))
      car = p.chassis;
  SLRR_REQUIRE(car);
  for (int i = 0; i < 180; ++i)
    race_traffic_tick(f.vm, 1.f / 60);
  auto *n = gameref_get(car);
  SLRR_REQUIRE(n);
  RoadPoint stopped{n->pos[0], n->pos[1], n->pos[2]};
  for (int i = 0; i < 120; ++i)
    race_traffic_tick(f.vm, 1.f / 60);
  SLRR_REQUIRE(road_distance(stopped, {n->pos[0], n->pos[1], n->pos[2]}) <
                   .01 &&
               std::hypot(n->vel[0], n->vel[2]) < .01);
  race_traffic_tick(f.vm, 0);
  SLRR_REQUIRE(road_distance(stopped, {n->pos[0], n->pos[1], n->pos[2]}) < .01);
  bool resumed = false;
  for (int i = 0; i < 5000 && !resumed; ++i) {
    race_traffic_tick(f.vm, 1.f / 60);
    resumed = road_distance(stopped, {n->pos[0], n->pos[1], n->pos[2]}) > 3;
  }
  SLRR_REQUIRE(resumed);
  f.call("delTraffic", "()V");
  part_destroy(f.player);
  std::puts("PASS actual City signal approach stops at red, stays still during "
            "pause and resumes on green");
}
void shared_signal_render_cache() {
  RaceFixture f;
  f.packs.open_all();
  f.ground.class_name = "java.util.resource.GroundRef";
  spawn_ground_shell(f.vm, f.packs, &f.ground);
  Object type;
  type.fields["id"] = Value::make_int(
      RpkIndex::make_full_id(f.packs.open_lib("cars/traffic/Taxi.rpk"), 6));
  f.call("addTrafficN", "(Ljava.util.resource.GameRef;IFFF)I",
         {Value::make_obj(&type), Value::make_int(0), Value::make_float(4),
          Value::make_float(4), Value::make_float(2.5f)});
  auto collect = []() {
    std::vector<std::shared_ptr<const ScxGeometry>> meshes;
    for (const auto &instance : scene_instances_collect())
      if (instance.emissive_overlay && !instance.geometry->groups.empty() &&
          instance.geometry->groups.front().name == "traffic signal") {
        SLRR_REQUIRE(instance.geometry->groups.size() <= 3);
        meshes.push_back(instance.geometry);
      }
    return meshes;
  };
  auto before = collect();
  SLRR_REQUIRE(!before.empty() && before.size() < 5);
  size_t triangles = 0;
  for (auto &mesh : before)
    triangles += mesh->indices.size() / 3;
  SLRR_REQUIRE(triangles > 400 * 16);
  auto unchanged = collect();
  SLRR_REQUIRE(unchanged == before);
  race_traffic_tick(f.vm, 0);
  SLRR_REQUIRE(collect() == before);
  race_traffic_tick(f.vm, 13.1f);
  auto after = collect();
  SLRR_REQUIRE(after.size() == before.size() && after != before);
  std::puts("PASS real City signal lenses share cached emission meshes; phases "
            "refresh groups without per-head draws");
}
int main() {
  const standalone::Test tests[] = {lifecycle_and_stream,
                                    red_light_stop_and_release,
                                    shared_signal_render_cache};
  return standalone::run("traffic_runtime", tests);
}
