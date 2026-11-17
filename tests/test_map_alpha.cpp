#include "save_io_fixture.hpp"
#include "slrr/scx_mesh.hpp"
using namespace slrr;
void cutout_assets() {
 for(const char* path:{"game/objects/meshes/roadtest26_egyedi/roadtest26_egyedi.scx",
     "game/objects/meshes/area_200_egyedi/area_200_egyedi.scx",
     "game/objects/meshes/area_19_egyedi/area_19_egyedi.scx"}) {
  const auto mesh=load_scx_geometry(path);
  SLRR_REQUIRE(mesh.info.ok && mesh.info.version==4);
  size_t count=0;
  for(const auto& group:mesh.groups) {
   if(!(group.material_flags & 0x20000000u))continue;
   const auto surface=scx_surface(mesh.info.version,group);
   SLRR_REQUIRE(surface.cutout && !surface.blend && !surface.color_mix);
   if(group.material_type==3)++count;
  }
  SLRR_REQUIRE(count>0);
  std::printf("PASS %s: %zu ColorMixTex sections retain authored alpha holes\n",path,count);
 }
}
void dealer_rail_alpha() {
 const auto mesh=load_scx_geometry("game/misc/dealer/meshes/marketkulso.scx");
 SLRR_REQUIRE(mesh.info.ok && mesh.info.version==4);
 bool rail=false;
 for(const auto& group:mesh.groups) {
  if(group.name!="korlat")continue;
  SLRR_REQUIRE(group.material_type==0 && (group.material_flags & 0x40000000u));
  const auto surface=scx_surface(mesh.info.version,group);
  SLRR_REQUIRE(surface.blend && !surface.cutout && !surface.color_mix);rail=true;
 }
 SLRR_REQUIRE(rail);
 std::puts("PASS actual dealer type-0 rail uses texture alpha without a glass colour function");
}
void opaque_paint_and_legacy() {
 ScxGroup group;group.material_type=3;group.material_flags=0x80000000u;
 auto surface=scx_surface(4,group);
 SLRR_REQUIRE(surface.color_mix && !surface.cutout && !surface.blend);
 for(uint32_t kind:{0u,3u,4u,0x33u}) {
  group.material_type=kind;group.material_flags=0xa0000000u;surface=scx_surface(4,group);
  SLRR_REQUIRE(surface.cutout && !surface.blend && !surface.color_mix);
  group.material_flags=0xc0000000u;surface=scx_surface(4,group);
  SLRR_REQUIRE(surface.blend && !surface.cutout && !surface.color_mix);
 }
 group.material_flags=1;surface=scx_surface(3,group);
 SLRR_REQUIRE(surface.cutout && !surface.blend);
 group.material_flags=0x101;surface=scx_surface(3,group);
 SLRR_REQUIRE(surface.blend && !surface.cutout);
 group.material_flags=0x100;surface=scx_surface(3,group);
 SLRR_REQUIRE(!surface.blend && !surface.cutout && !surface.color_mix);
 std::puts("PASS opaque paint and legacy SCX3 cutout/blended/vertex-paint contracts");
}
int main(){const standalone::Test tests[]={cutout_assets,dealer_rail_alpha,opaque_paint_and_legacy};return standalone::run("map_alpha",tests);}
