// Car lights (#279): each headlight part owns one spot beam and each brake
// light part one brake light, from the part's own cfg `render` light
// children. Lights follow their part: a removed headlight takes its beam
// away. Headlights light at night with the engine running, brake lights
// while the car brakes. Uses the installed game data, read-only.
#include "save_io_fixture.hpp"
#include "slrr/gameref.hpp"
#include "slrr/part_runtime.hpp"
#include "slrr/race_runtime.hpp"
#include "slrr/scene_instances.hpp"
#include <cmath>
namespace slrr {
void spawn_ground_shell(Jvm &, const RpkIndex &, Object *);
}
using namespace slrr;
namespace {
size_t count(const std::vector<CarLight> &lights, CarLightKind kind) {
  size_t n = 0;
  for (const auto &light : lights)
    n += light.kind == kind;
  return n;
}
const CarLight *find(const std::vector<CarLight> &lights, CarLightKind kind, size_t nth = 0) {
  for (const auto &light : lights)
    if (light.kind == kind && nth-- == 0)
      return &light;
  return nullptr;
}
void set_time(Jvm &vm, float seconds) {
  vm.set_static("java.game.GameLogic", "time", Value::make_float(seconds));
}
} // namespace

void stock_baiern_lights() {
  Fixture f;
  const auto game = fs::absolute("game");
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
  gameref_upsert(&world, GameRefKind::Dummy)->flags = kWorldTreeRoot;
  auto city = f.packs.open_lib("maps/city.rpk");
  auto *g = gameref_upsert(&ground, GameRefKind::GroundRef);
  g->type_id = RpkIndex::make_full_id(city, 1);
  gameref_set_parent(&ground, &world);
  gameref_upsert(&brain);
  scene_instances_set_resources(&f.packs);
  scene_instances_select(&world, nullptr, false);
  ground.class_name = "java.util.resource.GroundRef";
  spawn_ground_shell(f.vm, f.packs, &ground);
  scene_instances_enable_static_floor(false);
  const auto pack = f.packs.open_lib("cars/racers/Baiern.rpk");
  uint32_t type = 0;
  for (const auto &e : f.packs.pack_by_id(pack)->entries) {
    const auto id = RpkIndex::make_full_id(pack, e.type_id);
    const auto cfg = load_part_config(f.packs, id);
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
  const auto start = e.points.front(), finish = e.points.back();
  const auto toward = e.points[1];
  const float yaw = std::atan2(-(toward[0] - start[0]), -(toward[2] - start[2]));
  gameref_set_matrix(car, start[0], start[1], start[2], yaw, 0, 0);
  part_park_chassis(car);

  // Ownership: the stock Baiern has two headlight parts and two taillight
  // parts; each owns exactly one light of its kind.
  auto all = part_car_lights(false);
  const size_t heads = count(all, CarLightKind::Headlight);
  const size_t brakes = count(all, CarLightKind::Brake);
  std::printf("CAR LIGHTS Baiern: %zu headlights, %zu brake lights (all parts)\n", heads, brakes);
  SLRR_REQUIRE(heads == 2 && brakes == 2);
  const CarLight left = *find(all, CarLightKind::Headlight, 0);
  const CarLight right = *find(all, CarLightKind::Headlight, 1);
  SLRR_REQUIRE(left.part != right.part && left.car == car && right.car == car);
  SLRR_REQUIRE(find(all, CarLightKind::Brake, 0)->part != find(all, CarLightKind::Brake, 1)->part);
  for (const auto &light : all)
    SLRR_REQUIRE(light.part != car);
  // The headlights are the `type spot` resource (diffuse 0.2, range 400),
  // the brake lights the red glow-only `fakespot` (range 5).
  SLRR_REQUIRE(left.range == 400.f && left.theta > 1.8f && left.diffuse[0] < .5f);
  const CarLight rear = *find(all, CarLightKind::Brake);
  SLRR_REQUIRE(rear.diffuse[0] > .9f && rear.diffuse[1] < .1f && rear.range == 5.f);
  // Placement: beams leave the front of the car, forward and level within
  // about 15 degrees; brake lights sit behind the headlights.
  PartMatrix m;
  SLRR_REQUIRE(scene_instances_render_matrix(car, m.data()));
  const std::array<float, 3> centre{m[12], m[13], -m[14]};
  for (const auto *light : {&left, &right}) {
    float out = 0, ahead = 0;
    for (int i = 0; i < 3; ++i) {
      out += (light->position[i] - centre[i]) * light->direction[i];
      ahead += (light->position[i] - rear.position[i]) * light->direction[i];
    }
    std::printf("CAR LIGHTS headlight at %.2f %.2f %.2f dir %.3f %.3f %.3f (%.2f m ahead of the centre, %.2f of the brake light)\n",
                light->position[0], light->position[1], light->position[2],
                light->direction[0], light->direction[1], light->direction[2], out, ahead);
    SLRR_REQUIRE(out > .8f && ahead > 2.f && std::fabs(light->direction[1]) < .26f);
  }
  SLRR_REQUIRE(std::hypot(left.position[0] - right.position[0], left.position[2] - right.position[2]) > .8f);

  // Gating: parked, engine off, midday: nothing is lit.
  set_time(f.vm, 12 * 3600.f);
  SLRR_REQUIRE(part_car_lights(true).empty());
  // Driving at night under autonomous control: headlights lit, no brake.
  race_brain_command(f.packs, &brain, "controllable " + std::to_string(gameref_make_live(car)));
  f.vm.set_static("java.util.resource.GameRef", "EVENT_COMMAND", Value::make_int(0x10));
  auto *bot = f.vm.new_object("java.game.Bot");
  bot->fields["car"] = Value::make_obj(vehicle);
  bot->fields["brain"] = Value::make_obj(&brain);
  bot->fields["world"] = Value::make_obj(&ground);
  bot->fields["traffic_id"] = Value::make_int(0);
  auto *racer = f.vm.new_object("java.game.Racer");
  racer->fields["car"] = Value::make_obj(vehicle);
  auto *destination = f.vm.new_object("java.lang.Vector3");
  destination->fields["x"] = Value::make_float(finish[0]);
  destination->fields["y"] = Value::make_float(finish[1]);
  destination->fields["z"] = Value::make_float(finish[2]);
  f.vm.invoke("java.game.Bot", "startRace", "(Ljava.lang.Vector3;Ljava.game.Racer;)V",
              {Value::make_obj(bot), Value::make_obj(destination), Value::make_obj(racer)}, false);
  for (int i = 0; i < 240; ++i)
    part_physics_advance(1. / 60.);
  set_time(f.vm, 12 * 3600.f);
  const auto day = part_car_lights(true);
  set_time(f.vm, 23 * 3600.f);
  auto lit = part_car_lights(true);
  PartPhysicsState state;
  SLRR_REQUIRE(part_physics_state(car, state));
  std::printf("CAR LIGHTS driving %.1f km/h: day %zu lit, night %zu headlights %zu brake lights\n",
              state.speed_kph, day.size(), count(lit, CarLightKind::Headlight),
              count(lit, CarLightKind::Brake));
  SLRR_REQUIRE(count(day, CarLightKind::Headlight) == 0);
  SLRR_REQUIRE(count(lit, CarLightKind::Headlight) == 2 && count(lit, CarLightKind::Brake) == 0);
  // Braking: the brake light parts light up (day or night), and their
  // lamp glass with them.
  race_brain_command(f.packs, &brain, "AI_stop");
  for (int i = 0; i < 20; ++i)
    part_physics_advance(1. / 60.);
  lit = part_car_lights(true);
  std::printf("CAR LIGHTS braking: %zu brake lights, %zu headlights\n",
              count(lit, CarLightKind::Brake), count(lit, CarLightKind::Headlight));
  SLRR_REQUIRE(count(lit, CarLightKind::Brake) == 2 && count(lit, CarLightKind::Headlight) == 2);
  SLRR_REQUIRE(part_lamp_lit(rear.part, 1u, false) && !part_lamp_lit(rear.part, 1u, true));
  set_time(f.vm, 12 * 3600.f);
  SLRR_REQUIRE(count(part_car_lights(true), CarLightKind::Brake) == 2);
  set_time(f.vm, 23 * 3600.f);

  // A lost headlight takes only its own beam away.
  SLRR_REQUIRE(part_destroy(left.part));
  all = part_car_lights(false);
  lit = part_car_lights(true);
  std::printf("CAR LIGHTS one headlight removed: %zu headlights (%zu lit), %zu brake lights\n",
              count(all, CarLightKind::Headlight), count(lit, CarLightKind::Headlight),
              count(all, CarLightKind::Brake));
  SLRR_REQUIRE(count(all, CarLightKind::Headlight) == 1 && count(lit, CarLightKind::Headlight) == 1);
  SLRR_REQUIRE(find(lit, CarLightKind::Headlight)->part == right.part);
  SLRR_REQUIRE(count(all, CarLightKind::Brake) == 2);

  // Wear: condition = wear x tear. Past kLampBrokenCondition the lamp is
  // broken (no beam, no lit glass); between it and kLampFullCondition it
  // dims from 40 %; a repaired lamp is at full power again.
  const auto wear = [&](Object *part, float value) {
    f.vm.natives().invoke("java.game.parts.Part", "setWear", "(F)V", part,
                          {Value::make_float(value)});
  };
  const auto beam = [&]() -> float {
    const auto *head = find(part_car_lights(true), CarLightKind::Headlight);
    return head ? head->power : 0.f;
  };
  SLRR_REQUIRE(beam() == 1.f);
  wear(right.part, .2f);
  const float broken = beam();
  const bool glass = part_lamp_lit(right.part, 4u, false);
  wear(right.part, .4f);
  const float dim = beam();
  wear(right.part, 1.f);
  const float repaired = beam();
  std::printf("CAR LIGHTS headlight wear 0.2 -> power %.2f (glass %d), 0.4 -> %.2f, 1.0 -> %.2f\n",
              broken, int(glass), dim, repaired);
  SLRR_REQUIRE(broken == 0.f && !glass && find(part_car_lights(false), CarLightKind::Headlight));
  SLRR_REQUIRE(std::fabs(dim - part_lamp_power(.4f)) < 1e-6f && dim > .4f && dim < 1.f);
  SLRR_REQUIRE(repaired == 1.f && part_lamp_power(kLampBrokenCondition) == 0.f &&
               part_lamp_power(kLampFullCondition) == 1.f);
  // The same rule for brake lights, through tear.
  const auto *taillight = find(part_car_lights(true), CarLightKind::Brake);
  SLRR_REQUIRE(taillight);
  Object *const torn_tail = taillight->part;
  f.vm.natives().invoke("java.game.parts.Part", "setTear", "(F)V", torn_tail,
                        {Value::make_float(.1f)});
  lit = part_car_lights(true);
  std::printf("CAR LIGHTS taillight torn to 0.1: %zu brake lights\n", count(lit, CarLightKind::Brake));
  SLRR_REQUIRE(count(lit, CarLightKind::Brake) == 1 && find(lit, CarLightKind::Brake)->part != torn_tail);
  SLRR_REQUIRE(!part_lamp_lit(torn_tail, 1u, false));

  // A headlight torn off and lying loose on the ground gives no light.
  SLRR_REQUIRE(part_tear_off(right.part));
  for (int i = 0; i < 60; ++i)
    part_physics_advance(1. / 60.);
  all = part_car_lights(false);
  lit = part_car_lights(true);
  std::printf("CAR LIGHTS remaining headlight torn off: %zu headlights reported (%zu lit)\n",
              count(all, CarLightKind::Headlight), count(lit, CarLightKind::Headlight));
  SLRR_REQUIRE(count(all, CarLightKind::Headlight) == 0 && count(lit, CarLightKind::Headlight) == 0);
  SLRR_REQUIRE(!part_lamp_lit(right.part, 4u, false));
  SLRR_REQUIRE(part_destroy(right.part));  // still a live (loose) part until now
  // A car taken apart reports no light.
  SLRR_REQUIRE(part_destroy(car));
  SLRR_REQUIRE(part_car_lights(false).empty());

  scene_instances_select(nullptr, nullptr, false);
  scene_instances_set_resources(nullptr);
  scene_instances_enable_static_floor(false);
  gameref_clear();
  rpk_set_active(nullptr);
  std::puts("PASS car lights belong to their headlight and brake light parts");
}
int main() {
  const standalone::Test tests[] = {stock_baiern_lights};
  return standalone::run("car_lights", tests);
}
