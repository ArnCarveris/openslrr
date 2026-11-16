#include "save_io_fixture.hpp"
#include "slrr/gameref.hpp"
#include "slrr/scene_instances.hpp"
#include "slrr/scene_sun.hpp"
int main(){using namespace slrr;Fixture f;f.packs.set_root(fs::absolute("game").string());f.packs.open_all();auto* pack=f.packs.pack_by_path("objects.rpk");if(!pack)return 2;
 Object root,shell;gameref_upsert(&root);auto* node=gameref_upsert(&shell,GameRefKind::RenderRef);node->type_id=RpkIndex::make_full_id(pack->pack_id,0x1ad);gameref_set_parent(&shell,&root);
 scene_instances_set_resources(&f.packs);scene_instances_select(&root,nullptr,false);
 GroundMapData map;map.render_id=node->type_id;GroundMapData::Placement p;p.type=RpkIndex::make_full_id(pack->pack_id,0x1a1);p.seed=0;map.placements.push_back(p);scene_instances_add_ground_props(map);
 bool emissive_ok=true;
 auto count=[&](float hour){scene_sun_set_time(hour*3600);std::vector<SceneBatch>b;scene_instances_collect_batches(nullptr,b);size_t night=0,total=0;for(auto& batch:b){total+=batch.instances.size();if(batch.geometry->info.path.find("night")!=std::string::npos)night+=batch.instances.size();}for(auto& i:scene_instances_collect(nullptr)){if(i.key==&shell)continue;++total;if(i.geometry->info.path.find("night")!=std::string::npos){++night;emissive_ok=emissive_ok&&i.emissive_overlay;
 size_t masked=0,unmasked=0;for(const auto& g:i.geometry->groups){if(g.emission_index!=0xffff){++masked;emissive_ok=emissive_ok&&g.emission_index<i.geometry->textures.size();}else ++unmasked;}
 std::printf("night materials masked=%zu unmasked=%zu\n",masked,unmasked);emissive_ok=emissive_ok&&masked>0&&unmasked>0;}}std::printf("hour=%.2f total=%zu night=%zu\n",hour,total,night);return std::pair{total,night};};
 auto day=count(12),night=count(22),dawn=count(6),before=count(18.49f),dusk=count(18.5f),again=count(12);
 bool ok=emissive_ok&&day.first>0&&day.second==0&&night.second>0&&night.first>day.first&&dawn==day&&before==day&&dusk==night&&again==day;
 gameref_remove(&shell);gameref_remove(&root);scene_instances_set_resources(nullptr);scene_sun_set_time(43200);return ok?0:1;}
