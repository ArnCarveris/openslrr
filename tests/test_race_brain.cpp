#include "race_fixture.hpp"
#include <cmath>
using namespace slrr;
void spline_wait_and_arbitration() {
  RaceFixture f;
  auto *car = f.car();
  Object brain;
  gameref_upsert(&brain);
  race_brain_command(f.packs, &brain,
                     "controllable " + std::to_string(gameref_make_live(car)));
  std::ifstream in(fs::absolute("game/maps/ROC_Track/race.spl"));
  RoadPoint start;
  SLRR_REQUIRE(bool(in >> start[0] >> start[1] >> start[2]));
  gameref_set_matrix(car, start[0], start[1], start[2], 0, 0, 0);
  RaceDriveInput input;
  race_brain_command(f.packs, &brain, "AI_BeginRace 0.5");
  SLRR_REQUIRE(race_drive_input(car, start, 0, 0, .02f, input) &&
               input.throttle == 0 && input.brake == 1);
  SLRR_REQUIRE(race_brain_command(f.packs, &brain,
                                  "AI_spline 5 maps/ROC_Track/race.spl 0"));
  SLRR_REQUIRE(race_drive_input(car, start, 0, 0, .02f, input) &&
               input.throttle > 0 && input.start);
  SLRR_REQUIRE(!race_brain_command(f.packs, &brain, "AI_unknown"));
  SLRR_REQUIRE(race_drive_input(car, start, 0, 0, .02f, input) &&
               input.throttle > 0);
  race_brain_command(f.packs, &brain, "AI_suspend");
  SLRR_REQUIRE(race_drive_input(car, start, 0, 0, .02f, input) &&
               input.throttle == 0 && input.brake == 1);
  gameref_remove(&brain);
  SLRR_REQUIRE(!race_drive_input(car, start, 0, 0, .02f, input));
  part_destroy(car);
  std::puts("PASS ROC closed spline starts; countdown/suspend hold; command "
            "order and destruction");
}
void city_route_drives_and_halts() {
  RaceFixture f;
  auto *car = f.car();
  Object brain;
  gameref_upsert(&brain);
  auto graph = race_ground_roads(f.packs, &f.ground);
  SLRR_REQUIRE(graph && !graph->edges().empty());
  const auto &e = graph->edges().front();
  auto start = e.points.front(), finish = e.points.back();
  gameref_set_matrix(car, start[0], start[1], start[2], 0, 0, 0);
  race_brain_command(f.packs, &brain,
                     "controllable " + std::to_string(gameref_make_live(car)));
  std::string command = "AI_race " + std::to_string(finish[0]) + "," +
                        std::to_string(finish[1]) + "," +
                        std::to_string(finish[2]) + " 0";
  race_brain_command(f.packs, &brain, command);
  RaceDriveInput input;
  SLRR_REQUIRE(race_drive_input(car, start, 0, 0, .02f, input) &&
               input.throttle > 0);
  auto direction = race_last_route(&f.ground)->direction;
  float away = std::atan2(direction[0], direction[2]);
  SLRR_REQUIRE(race_drive_input(car, start, away, 20, .02f, input) &&
               input.throttle == 0 && input.brake > .9f &&
               std::fabs(input.steer) > .05f);
  race_brain_command(f.packs, &brain, "AI_GoToTraffic");
  race_halt_path(f.packs, &f.ground, start, finish);
  SLRR_REQUIRE(race_drive_input(car, start, 0, 0, .02f, input) &&
               input.throttle == 0 && input.brake == 1);
  race_clear_halts(&f.ground);
  gameref_remove(&brain);
  part_destroy(car);
  std::puts("PASS city route supplies throttle; traffic respects race path "
            "exclusions");
}
void preview_bend_brakes() {
  RaceFixture f;
  auto *car = f.car();
  gameref_set_matrix(car, 0, 0, 0, 0, 0, 0);
  Object brain;
  gameref_upsert(&brain);
  std::ofstream(f.root / "bend.spl")
      << "0 0 0 0 0 -1\n0 0 -30 0 0 -1\n10 0 -40 1 0 0\n40 0 -40 1 0 0\n";
  RpkIndex routes;
  routes.set_root(f.root.string());
  race_brain_command(routes, &brain,
                     "controllable " + std::to_string(gameref_make_live(car)));
  race_brain_command(routes, &brain, "AI_spline 5 bend.spl 0");
  RaceDriveInput input;
  SLRR_REQUIRE(race_drive_input(car, {0, 0, 0}, 0, 20, .02f, input) &&
               input.throttle == 0 && input.brake > .1f);
  for (int i = 0; i < 10; ++i)
    race_drive_input(car, {0, 0, -28}, 0, 5, .02f, input);
  SLRR_REQUIRE(std::fabs(input.steer) > .1f);
  gameref_remove(&brain);
  part_destroy(car);
  std::puts("PASS anticipation brakes before a sharp spline bend and turns at "
            "the bend");
}
int main() {
  const standalone::Test tests[] = {spline_wait_and_arbitration,
                                    city_route_drives_and_halts,
                                    preview_bend_brakes};
  return standalone::run("race_brain", tests);
}
