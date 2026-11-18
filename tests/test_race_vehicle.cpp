#include "save_io_fixture.hpp"
#include "slrr/gameref.hpp"
#include "slrr/part_runtime.hpp"
#include "slrr/race_runtime.hpp"
#include "slrr/scene_instances.hpp"
#include <cmath>
using namespace slrr;
void physical_stock_opponent(const char *pack_name) {
  Fixture f;
  const auto game = fs::absolute("game");
  // Copy bytecode only; file natives remain confined to the temporary root.
  for (const auto &e : fs::recursive_directory_iterator(game))
    if (e.is_regular_file() && e.path().extension() == ".class") {
      auto out = f.root / fs::relative(e.path(), game);
      fs::create_directories(out.parent_path());
      fs::copy_file(e.path(), out);
    }
  f.packs.set_root(game.string());
  f.packs.open_all();
  rpk_set_active(&f.packs);
  SLRR_REQUIRE(f.vm.bootstrap_classpath());
  gameref_clear();
  register_lang_natives(f.vm, f.packs, f.sections);
  register_gameref_natives(f.vm, f.packs, f.sections);
  register_resref_natives(f.vm, f.packs, f.sections);
  register_renderref_natives(f.vm, f.packs, f.sections);
  register_part_natives(f.vm, f.packs);
  register_race_natives(f.vm, f.packs, f.sections);
  Object world, ground, brain;
  auto *w = gameref_upsert(&world, GameRefKind::Dummy);
  w->flags = kWorldTreeRoot;
  auto city = f.packs.open_lib("maps/city.rpk");
  auto *g = gameref_upsert(&ground, GameRefKind::GroundRef);
  g->type_id = RpkIndex::make_full_id(city, 1);
  gameref_set_parent(&ground, &world);
  gameref_upsert(&brain);
  scene_instances_set_resources(&f.packs);
  scene_instances_select(&world, nullptr, false);
  ScxGeometry floor;
  floor.info.ok = true;
  floor.info.version = 3;
  floor.position_scale = 1;
  floor.positions = {-3000, 0, -3000, 3000,  0, -3000,
                     3000,  0, 3000,  -3000, 0, 3000};
  floor.indices = {0, 2, 1, 0, 3, 2};
  scene_instances_add_static_mesh(floor);
  scene_instances_enable_static_floor(true);
  auto pack = f.packs.open_lib(pack_name);
  uint32_t type = 0;
  for (const auto &e : f.packs.pack_by_id(pack)->entries) {
    auto id = RpkIndex::make_full_id(pack, e.type_id);
    auto cfg = load_part_config(f.packs, id);
    if (cfg.valid && cfg.native_kind == "car") {
      type = id;
      break;
    }
  }
  SLRR_REQUIRE(type);
  auto *vehicle = vehicle_spawn_stock(f.vm, f.packs, &ground, type);
  SLRR_REQUIRE(vehicle);
  auto *car = part_chassis(vehicle);
  SLRR_REQUIRE(car);
  auto graph = race_ground_roads(f.packs, &ground);
  SLRR_REQUIRE(graph);
  const auto &e = graph->edges().front();
  auto start = e.points.front(), finish = e.points.back();
  start[1] = finish[1] = 0;
  const auto direction = e.points[1];
  float yaw =
      std::atan2(-(direction[0] - start[0]), -(direction[2] - start[2]));
  gameref_set_matrix(car, start[0], 0, start[2], yaw, 0, 0);
  part_park_chassis(car);
  race_brain_command(f.packs, &brain,
                     "controllable " + std::to_string(gameref_make_live(car)));
  f.vm.set_static("java.util.resource.GameRef", "EVENT_COMMAND",
                  Value::make_int(0x10));
  auto *bot = f.vm.new_object("java.game.Bot");
  bot->fields["car"] = Value::make_obj(vehicle);
  bot->fields["brain"] = Value::make_obj(&brain);
  bot->fields["world"] = Value::make_obj(&ground);
  bot->fields["traffic_id"] = Value::make_int(0);
  auto *racer = f.vm.new_object("java.game.Racer");
  racer->fields["car"] = Value::make_obj(vehicle);
  auto *destination = f.vm.new_object("java.lang.Vector3");
  for (int k = 0; k < 3; ++k)
    destination->fields[k == 0   ? "x"
                        : k == 1 ? "y"
                                 : "z"] = Value::make_float(finish[k]);
  f.vm.invoke("java.game.Bot", "startRace",
              "(Ljava.lang.Vector3;Ljava.game.Racer;)V",
              {Value::make_obj(bot), Value::make_obj(destination),
               Value::make_obj(racer)},
              false);
  RaceDriveInput input;
  SLRR_REQUIRE(race_drive_input(car, start, yaw, 0, .02f, input) &&
               input.throttle > 0);
  for (int i = 0; i < 600; ++i)
    part_physics_advance(1. / 60.);
  PartPhysicsState state;
  SLRR_REQUIRE(part_physics_state(car, state));
  PartMatrix m;
  SLRR_REQUIRE(scene_instances_script_matrix(car, m.data()));
  std::printf("STOCK type=%x parts=%zu speed=%.3f rpm=%.1f gear=%d "
              "displacement=%.3f height=%.3f ticks=%llu\n",
              type, part_installed_tree(car).size(), state.speed_kph, state.rpm,
              state.gear, std::hypot(m[12] - start[0], m[14] - start[2]),
              state.height, state.ticks);
  SLRR_REQUIRE(state.active && state.ticks > 0 &&
               std::hypot(m[12] - start[0], m[14] - start[2]) > 5);
  for (int i = 0; i < 900; ++i)
    part_physics_advance(1. / 60.);
  part_physics_state(car, state);
  scene_instances_script_matrix(car, m.data());
  std::printf("ARRIVAL speed=%.3f remaining=%.3f\n", state.speed_kph,
              std::hypot(m[12] - finish[0], m[14] - finish[2]));
  SLRR_REQUIRE(std::fabs(state.speed_kph) < 2 &&
               std::hypot(m[12] - finish[0], m[14] - finish[2]) < 5);
  // Exercise the original MWM ROC command builder, including argument ordering.
  std::ifstream spline_file(game / "maps/ROC_Track/race.spl");
  RoadPoint roc;
  SLRR_REQUIRE(bool(spline_file >> roc[0] >> roc[1] >> roc[2]));
  gameref_set_matrix(car, roc[0], roc[1], roc[2], 0, 0, 0);
  auto *path = f.vm.new_string("maps/ROC_Track/race.spl");
  f.vm.invoke("java.game.Bot", "followSplineTrack", "(FLjava.lang.String;I)V",
              {Value::make_obj(bot), Value::make_float(5),
               Value::make_obj(path), Value::make_int(0)},
              false);
  SLRR_REQUIRE(race_drive_input(car, roc, 0, 0, .02f, input) &&
               input.throttle > 0);
  RoadPoint tangent;
  spline_file >> tangent[0] >> tangent[1] >> tangent[2];
  const float roc_yaw =
      0; // Deliberately face away from the first ROC spline
         // target.gameref_set_matrix(car,roc[0],0,roc[2],roc_yaw,0,0);part_park_chassis(car);
  f.vm.invoke("java.game.Bot", "followSplineTrack", "(FLjava.lang.String;I)V",
              {Value::make_obj(bot), Value::make_float(5),
               Value::make_obj(path), Value::make_int(0)},
              false);
  double max_request = 0, max_wheel = 0, max_speed = 0, max_yaw = 0;
  for (int i = 0; i < 7200; ++i) {
    part_physics_advance(1. / 60.);
    part_physics_state(car, state);
    max_request = std::max(max_request, std::fabs(state.steering_request));
    for (auto &w : state.tires)
      max_wheel = std::max(max_wheel, std::fabs(w.steer));
    max_speed = std::max(max_speed, state.speed_kph);
    scene_instances_script_matrix(car, m.data());
    float heading = std::atan2(m[8], m[10]);
    max_yaw = std::max(
        max_yaw, double(std::fabs(std::atan2(std::sin(heading - roc_yaw),
                                             std::cos(heading - roc_yaw)))));
  }
  std::printf("ROC request=%.4f wheel=%.4f yaw=%.4f speed=%.3f height=%.3f\n",
              max_request, max_wheel, max_yaw, max_speed, state.height);
  std::printf("ROC final x=%.2f z=%.2f\n", m[12], m[14]);
  SLRR_REQUIRE(max_request > .02 && max_wheel > .01 && max_yaw > .1);
  const auto before_brake = state.speed_kph;
  race_brain_command(f.packs, &brain, "AI_stop");
  for (int i = 0; i < 300; ++i)
    part_physics_advance(1. / 60.);
  part_physics_state(car, state);
  std::printf("BRAKE before=%.3f after=%.3f\n", before_brake, state.speed_kph);
  SLRR_REQUIRE(before_brake > 5 && std::fabs(state.speed_kph) < 2);
  part_destroy(car);
  scene_instances_select(nullptr, nullptr, false);
  scene_instances_set_resources(nullptr);
  scene_instances_enable_static_floor(false);
  gameref_clear();
  rpk_set_active(nullptr);
  std::puts("PASS real stock assembly moves under autonomous controls with "
            "mechanical physics");
}
void baiern() { physical_stock_opponent("cars/racers/Baiern.rpk"); }
void enula() { physical_stock_opponent("cars/racers/Enula.rpk"); }
int main() {
  const standalone::Test tests[] = {baiern, enula};
  return standalone::run("race_vehicle", tests);
}
