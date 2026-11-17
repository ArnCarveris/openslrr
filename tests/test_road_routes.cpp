#include "race_fixture.hpp"
#include "slrr/gameref.hpp"
#include "slrr/race_runtime.hpp"
#include "slrr/road_routes.hpp"
#include <cmath>
using namespace slrr;
RoadSegment straight(RoadPoint a, RoadPoint b) {
  RoadSegment s;
  s.start = a;
  s.end = b;
  for (int i = 0; i < 3; ++i)
    s.start_tangent[i] = s.end_tangent[i] = b[i] - a[i];
  s.length = road_distance(a, b);
  return s;
}
RoadNetwork street(bool reverse) {
  RoadNetwork n;
  RoadNode a, b;
  a.id = 1;
  b.id = 2;
  b.position = {100, 0, 0};
  a.links = {{4, 0}};
  b.links = {{4, 1}};
  if (reverse) {
    a.links.push_back({5, 1});
    b.links.push_back({5, 0});
  }
  n.nodes = {a, b};
  RoadPath p;
  p.id = 3;
  p.refs[1] = 1;
  p.refs[2] = 2;
  p.segments = {straight(a.position, b.position)};
  n.paths = {p};
  RoadOther r;
  r.id = 4;
  r.refs[0] = 3;
  n.others = {r};
  if (reverse) {
    r.id = 5;
    n.others.push_back(r);
  }
  return n;
}
void directions() {
  RoadRoutes one(street(false)), two(street(true));
  auto r = one.query({20, 0, 0}, {80, 0, 0});
  SLRR_REQUIRE(r && std::fabs(r->length - 60) < .1f && r->direction[0] > .99f);
  SLRR_REQUIRE(!one.query({80, 0, 0}, {20, 0, 0}));
  r = two.query({80, 0, 0}, {20, 0, 0});
  SLRR_REQUIRE(r && std::fabs(r->length - 60) < .1f && r->direction[0] < -.99f);
  SLRR_REQUIRE(road_distance(road_at(*r, 30), {50, 0, 0}) < .1f);
  auto aligned = two.project({50, 3, 4});
  SLRR_REQUIRE(aligned && std::fabs(aligned->position[0] - 50) < .1f &&
               std::fabs(aligned->distance - 5) < .1f);
}
void cross_radius() {
  RoadNetwork n;
  RoadNode a, b;
  a.id = 1;
  a.position = {4, 0, 0};
  b.id = 2;
  b.position = {12, 0, 0};
  n.nodes = {a, b};
  RoadRoutes g(n);
  auto p = g.nearest_cross({}, 10);
  SLRR_REQUIRE(p && (*p)[0] == 12);
  p = g.nearest_cross({}, -10);
  SLRR_REQUIRE(p && (*p)[0] == 4);
  SLRR_REQUIRE(!g.query({}, {}));
}
void real_city() {
  Fixture f;
  f.packs.set_root(fs::absolute("game").string());
  auto pack = f.packs.open_lib("maps/city.rpk");
  SLRR_REQUIRE(pack);
  Object ground;
  auto *n = gameref_upsert(&ground, GameRefKind::GroundRef);
  n->type_id = RpkIndex::make_full_id(pack, 1);
  auto graph = race_ground_roads(f.packs, &ground);
  SLRR_REQUIRE(graph && !graph->edges().empty());
  const auto &e = graph->edges().front();
  auto route = graph->query(e.points.front(), e.points.back());
  SLRR_REQUIRE(route && route->length > 0 && std::isfinite(route->length));
  std::printf("City graph: %zu directed edges, first route %.1f metres\n",
              graph->edges().size(), route->length);
  gameref_remove(&ground);
  SLRR_REQUIRE(!race_last_route(&ground));
}
void route_native_contract() {
  RaceFixture f;
  register_renderref_natives(f.vm, f.packs, f.sections);
  auto graph = race_ground_roads(f.packs, &f.ground);
  SLRR_REQUIRE(graph);
  const auto &e = graph->edges().front();
  auto vector = [&](RoadPoint p) {
    auto *v = f.vm.new_object("java.lang.Vector3");
    for (int k = 0; k < 3; ++k)
      v->fields[k == 0 ? "x" : k == 1 ? "y" : "z"] = Value::make_float(p[k]);
    return v;
  };
  auto *a = vector(e.points.front()), *b = vector(e.points.back());
  auto length = f.call("findRoute", "(Ljava.lang.Vector3;Ljava.lang.Vector3;)F",
                       {Value::make_obj(a), Value::make_obj(b)})
                    .v.f;
  SLRR_REQUIRE(length > 0 && f.call("getRouteLength", "()F").v.f == length);
  auto *aligned =
      f.call("alignToRoad", "(Ljava.lang.Vector3;)[Ljava.lang.Vector3;",
             {Value::make_obj(a)})
          .v.o;
  SLRR_REQUIRE(aligned && aligned->is_array && aligned->slots.size() == 2 &&
               aligned->slots[0].v.o != a &&
               aligned->slots[0].v.o != aligned->slots[1].v.o);
  auto *middle = f.call("getRoutePos", "(F)Ljava.lang.Vector3;",
                        {Value::make_float(length * .5f)})
                     .v.o;
  SLRR_REQUIRE(middle);
  auto at = f.call("getRouteDist", "(Ljava.lang.Vector3;)F",
                   {Value::make_obj(middle)})
                .v.f;
  SLRR_REQUIRE(std::fabs(at - length * .5f) < .1f);
  Object navigator, line;
  gameref_upsert(&navigator, GameRefKind::Dummy);
  auto *scale = vector({1, 1, 1});
  auto plotted =
      f.registry.invoke("java.util.resource.RenderRef", "plotRoute",
                        "(Ljava.util.resource.ResourceRef;Ljava.util.resource."
                        "ResourceRef;IFLjava.lang.Vector3;)I",
                        &line,
                        {Value::make_obj(&navigator), Value::make_obj(nullptr),
                         Value::make_int(0xff00ff00), Value::make_float(2),
                         Value::make_obj(scale)});
  SLRR_REQUIRE(plotted.v.i == 1 && gameref_get(&line)->parent == &navigator &&
               gameref_get(&line)->line_geometry &&
               !gameref_get(&line)->line_geometry->indices.empty());
  f.call("findRoute", "(Ljava.lang.Vector3;Ljava.lang.Vector3;)F",
         {Value::make_obj(nullptr), Value::make_obj(b)});
  SLRR_REQUIRE(f.call("getRouteLength", "()F").v.f == -1 && !race_plot_route());
  gameref_remove(&line);
  gameref_remove(&navigator);
  std::puts("PASS route native arrays, metre-based cache queries, navigator "
            "ribbon and failed-query invalidation");
}
int main() {
  const standalone::Test tests[] = {directions, cross_radius, real_city,
                                    route_native_contract};
  return standalone::run("road_routes", tests);
}
