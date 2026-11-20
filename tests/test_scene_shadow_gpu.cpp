// Real D3D11 pixels through production vehicle/scenery shaders and bindings.
#include "../src/render/bgfx_lib_backend.hpp"
#include "../src/render/reflection_capture.hpp"
#include "../src/render/point_shadows.hpp"
#include "../src/render/shadow_cascades.hpp"
#include "save_io_fixture.hpp"
#include "slrr/mainloop.hpp"
#include "slrr/script_lights.hpp"
#include <bx/math.h>
#include <limits>
namespace slrr {
void spawn_ground_shell(Jvm &, const RpkIndex &, Object *);
}
using namespace slrr;
namespace slrr {
struct SceneShadowRegression {
  static void ambient_occlusion(LibBgfxBackend& b) {
    Fixture f;Object root;
    gameref_upsert(&root)->flags=kWorldTreeRoot;
    scene_instances_select(&root,nullptr,false,SceneEnvironment::Interior);
    const auto saved=host_enhanced_graphics();
    auto saved_parts=std::move(b.parts_);auto saved_batches=std::move(b.prop_batches_);
    b.parts_.clear();b.prop_batches_.clear();
    host_enhanced_graphics().dynamic_lighting=true;
    host_enhanced_graphics().ao_strength=150;host_enhanced_graphics().ao_radius=150;
    b.garage_=true;b.outdoor_lighting_=false;b.fog_enabled_=false;
    b.fixture_lights_.clear();b.interior_sun_active_=false;b.static_shadow_active_=false;b.car_depth_active_=false;
    PointLight ambient;ambient.intensity=0;ambient.ambient={.7f,.7f,.7f};b.point_light_=ambient;
    auto floor=std::make_shared<ScxGeometry>();floor->info.ok=true;floor->info.version=4;floor->position_scale=1;
    floor->positions={-4,0,0,4,0,0,-4,0,-8,4,0,-8};floor->normals={0,1,0,0,1,0,0,1,0,0,1,0};
    floor->uvs.resize(8);floor->indices={0,2,1,1,2,3};ScxGroup group;group.index_count=6;floor->groups={group};
    auto cube=std::make_shared<ScxGeometry>();*cube=*floor;
    cube->positions={-.5f,0,-2.5f,.5f,0,-2.5f,.5f,1,-2.5f,-.5f,1,-2.5f,-.5f,0,-3.5f,.5f,0,-3.5f,.5f,1,-3.5f,-.5f,1,-3.5f};
    cube->indices={0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,3,2,6,3,6,7,0,3,7,0,7,4,1,5,6,1,6,2};
    cube->normals.resize(24);cube->uvs.resize(16);
    for(size_t i=0;i<8;++i) {cube->normals[i*3]=0;cube->normals[i*3+1]=1;cube->normals[i*3+2]=0;}
    cube->groups[0].index_count=uint32_t(cube->indices.size());
    SceneInstance receiver,blocker;receiver.geometry=floor;blocker.geometry=cube;
    bx::mtxIdentity(receiver.transform.data());bx::mtxIdentity(blocker.transform.data());
    receiver.lighting.set=receiver.lighting.has_direction=true;receiver.lighting.direction[1]=-1;
    blocker.lighting=receiver.lighting;
    b.instances_={receiver,blocker};b.cache_scene_mesh(floor,nullptr,false);b.cache_scene_mesh(cube,nullptr,false);
    constexpr uint16_t width=160,height=120;
    const auto color=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
    const auto depth=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::D32F,BGFX_TEXTURE_RT);
    const bgfx::TextureHandle attachments[]={color,depth};const auto fb=bgfx::createFrameBuffer(2,attachments,true);
    const auto readback=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
    float view[16],projection[16],render[16];
    bx::mtxLookAt(view,{3,3,-4},{0,0,3});bx::mtxProj(projection,60.f,float(width)/height,.05f,100.f,bgfx::getCaps()->homogeneousDepth);
    std::copy(projection,projection+16,render);
    for(int i:{2,6,10,14}) render[i]=bgfx::getCaps()->homogeneousDepth?-render[i]:render[i+1]-render[i];
    uint16_t draw_view=5;bool draw_blocker=true;
    auto capture=[&](int method,int quality) {
      host_enhanced_graphics().ao_method=method;host_enhanced_graphics().ao_quality=quality;
      b.update_ambient_occlusion(view,projection,width,height,5);b.draw_previews();
      bgfx::setViewFrameBuffer(draw_view,fb);bgfx::setViewRect(draw_view,0,0,width,height);
      bgfx::setViewClear(draw_view,BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,uint32_t(0xff),0.f);
      bgfx::setViewTransform(draw_view,view,render);
      b.draw_instance(draw_view,receiver,receiver.transform.data(),false,0);
      if(draw_blocker) b.draw_instance(draw_view,blocker,blocker.transform.data(),false,0);
      bgfx::TextureRegion dst,src;dst.init(readback);src.init(color);bgfx::blit(100,dst,src);bgfx::frame();
      std::vector<uint8_t> bytes(width*height*4);const auto ready=bgfx::read(dst,bytes.data());
      for(int i=0;i<16;++i) if(bgfx::frame()>=ready) return bytes;
      throw std::runtime_error("AO readback timeout");
    };
    b.instances_={receiver};draw_blocker=false;
    const auto flat_plain=capture(0,1);
    for(int method=1;method<=2;++method) for(int quality=0;quality<3;++quality) {
      const auto flat=capture(method,quality);unsigned dark=0,visible=0;uint64_t total=0,reference=0;
      for(size_t i=0;i<flat.size();i+=4) if(flat_plain[i]>10) {
        ++visible;total+=flat[i];reference+=flat_plain[i];
        if(int(flat_plain[i])-int(flat[i])>3) ++dark;
      }
      std::printf("AO flat method=%d dark=%u/%u average=%g reference=%g\n",method,dark,visible,double(total)/visible,double(reference)/visible);
      SLRR_REQUIRE(visible>100 && dark==0);
    }
    // Near-plane precision must not turn a distant, unobstructed road into AO bands.
    const auto near_positions=floor->positions;
    floor->positions={-300,0,0,300,0,0,-300,0,-1500,300,0,-1500};
    ++floor->revision;b.cache_scene_mesh(floor,nullptr,false);
    bx::mtxLookAt(view,{3,1.6f,-4},{0,1.6f,50});
    bx::mtxProj(projection,60.f,float(width)/height,.05f,3000.f,bgfx::getCaps()->homogeneousDepth);
    std::copy(projection,projection+16,render);
    for(int i:{2,6,10,14}) render[i]=bgfx::getCaps()->homogeneousDepth?-render[i]:render[i+1]-render[i];
    const auto distant_plain=capture(0,2);
    for(int method=1;method<=2;++method) for(int quality=0;quality<3;++quality) {
      const auto image=capture(method,quality);unsigned dark=0,visible=0;
      for(size_t i=0;i<image.size();i+=4) if(distant_plain[i]>10) {
        ++visible;if(int(distant_plain[i])-int(image[i])>3) ++dark;
      }
      std::printf("AO distant road method=%d quality=%d false occlusion=%u/%u pixels\n",method,quality,dark,visible);
      SLRR_REQUIRE(visible>100 && dark==0);
    }
    floor->positions=near_positions;++floor->revision;b.cache_scene_mesh(floor,nullptr,false);
    bx::mtxLookAt(view,{3,3,-4},{0,0,3});
    bx::mtxProj(projection,60.f,float(width)/height,.05f,100.f,bgfx::getCaps()->homogeneousDepth);
    std::copy(projection,projection+16,render);
    for(int i:{2,6,10,14}) render[i]=bgfx::getCaps()->homogeneousDepth?-render[i]:render[i+1]-render[i];
    b.instances_={receiver,blocker};draw_blocker=true;
    const auto plain=capture(0,1);
    std::vector<uint8_t> methods[2];
    for(int method=1;method<=2;++method) for(int quality=0;quality<3;++quality) {
      const auto image=capture(method,quality);unsigned shaded=0;
      for(size_t i=0;i<image.size();i+=4) if(int(plain[i])-int(image[i])>3) ++shaded;
      std::printf("AO method=%d quality=%d contact pixels=%u dimensions=%ux%u\n",method,quality,shaded,b.ao_width_,b.ao_height_);
      SLRR_REQUIRE(shaded>10 && shaded<width*height/2);
      SLRR_REQUIRE(b.ao_width_==(quality==2?width:width/2));
      methods[method-1]=image;
    }
    SLRR_REQUIRE(methods[0]!=methods[1]);
    b.instances_={receiver};
    SceneBatch batch;batch.geometry=cube;SceneBatchInstance placement;bx::mtxIdentity(placement.transform.data());
    batch.instances.push_back(placement);b.prop_batches_={batch};
    const auto instanced=capture(2,1);unsigned prop_shaded=0;
    for(size_t i=0;i<instanced.size();i+=4) if(int(plain[i])-int(instanced[i])>3) ++prop_shaded;
    SLRR_REQUIRE(prop_shaded>10);b.prop_batches_.clear();b.instances_={receiver,blocker};
    std::printf("AO reversed-depth instanced blocker contact=%u pixels\n",prop_shaded);

    draw_view=72;SLRR_REQUIRE(capture(2,1)==plain);draw_view=5;
    // AO can only affect ambient diffuse: retain exactly the same direct light.
    b.point_light_.reset();b.garage_=false;b.outdoor_lighting_=true;
    receiver.lighting.ambient[0]=receiver.lighting.ambient[1]=receiver.lighting.ambient[2]=0;
    blocker.lighting=receiver.lighting;
    const auto direct=capture(0,1);SLRR_REQUIRE(capture(2,1)==direct);
    b.update_ambient_occlusion(view,projection,80,60,5);
    SLRR_REQUIRE(b.ao_screen_width_==80 && b.ao_width_==40);
    host_enhanced_graphics().ao_method=0;b.update_ambient_occlusion(view,projection,width,height,5);
    SLRR_REQUIRE(!b.ao_active_ && !bgfx::isValid(b.ao_depth_fb_) && !bgfx::isValid(b.ao_raw_fb_));
    bgfx::frame();bgfx::destroy(fb);bgfx::destroy(readback);
    host_enhanced_graphics()=saved;b.instances_.clear();b.garage_=b.outdoor_lighting_=false;
    b.parts_=std::move(saved_parts);b.prop_batches_=std::move(saved_batches);
    gameref_remove(&root);scene_instances_set_resources(nullptr);
    std::puts("PASS HBAO/GTAO quality/contact, indirect-only, reflection isolation, resize and off cleanup");
  }
  // #237: a street lamp from the scenery light registry lights the road at
  // night through the production fs_mesh path, not by day, and option 4
  // switches it off. Fake (glow-only) and non-city groups never light.
  static void scenery_lamps(LibBgfxBackend& b) {
    Fixture f;scene_instances_set_resources(&f.packs);
    Object world,shell;
    gameref_upsert(&world)->flags=kWorldTreeRoot;
    gameref_upsert(&shell,GameRefKind::RenderRef)->empty=false;
    gameref_set_parent(&shell,&world);gameref_make_live(&shell);
    gameref_set_matrix(&shell,0,0,0,0,0,0);
    scene_instances_select(&world,nullptr,false,SceneEnvironment::Exterior);
    auto lights=std::make_shared<std::vector<SceneryLight>>();
    SceneryLight lamp;
    lamp.position={0,7.8f,0};lamp.color={.972f,.888f,.615f};lamp.range=500;
    lamp.attenuation={1,.03f,0};lamp.group=kLightGroupCity;lamp.lights_scene=true;
    lights->push_back(lamp);
    SceneryLight glow=lamp;glow.lights_scene=false;glow.position={6,5.5f,0};lights->push_back(glow);
    SceneryLight brake=lamp;brake.group=1;brake.position={-6,5.5f,0};lights->push_back(brake);
    SLRR_REQUIRE(scenery_lights_attach(&shell,lights));
    const auto saved=host_enhanced_graphics();const float old_time=scene_sun_time();
    auto saved_parts=std::move(b.parts_);auto saved_batches=std::move(b.prop_batches_);
    b.parts_.clear();b.prop_batches_.clear();
    host_enhanced_graphics().local_lights=host_enhanced_graphics().dynamic_lighting=true;
    host_enhanced_graphics().ao_method=0;
    b.garage_=false;b.outdoor_lighting_=true;b.fog_enabled_=false;
    b.interior_sun_active_=false;b.static_shadow_active_=false;b.car_depth_active_=false;
    b.fixture_shadow_active_=false;b.point_light_.reset();b.local_light_={};
    b.cull_eye_[0]=0;b.cull_eye_[1]=2;b.cull_eye_[2]=12;
    scene_sun_set_time(22*3600.f);b.update_local_light();
    SLRR_REQUIRE(b.fixture_lights_.size()==1);
    const auto street=b.fixture_lights_.front();
    SLRR_REQUIRE(street.omni && street.valid() && std::abs(street.position[1]-7.8f)<1e-4f);
    SLRR_REQUIRE(street.range==kSceneryLightMaxRange && street.intensity==kSceneryLightIntensity);
    SLRR_REQUIRE(street.attenuation[1]==.03f && street.color[2]==.615f);
    host_enhanced_graphics().local_lights=false;b.update_local_light();
    SLRR_REQUIRE(b.fixture_lights_.empty());
    host_enhanced_graphics().local_lights=true;
    scene_sun_set_time(12*3600.f);b.update_local_light();
    SLRR_REQUIRE(b.fixture_lights_.empty());
    auto floor=std::make_shared<ScxGeometry>();floor->info.ok=true;floor->info.version=4;floor->position_scale=1;
    floor->positions={-30,0,30,30,0,30,-30,0,-30,30,0,-30};floor->normals={0,1,0,0,1,0,0,1,0,0,1,0};
    floor->uvs.resize(8);floor->indices={0,2,1,1,2,3};ScxGroup group;group.index_count=6;floor->groups={group};
    SceneInstance road;road.geometry=floor;bx::mtxIdentity(road.transform.data());
    road.lighting.set=road.lighting.has_direction=true;road.lighting.direction[1]=-1;
    for(int i=0;i<3;++i){road.lighting.diffuse[i]=0;road.lighting.ambient[i]=.08f;road.lighting.specular[i]=0;}
    b.instances_={road};b.cache_scene_mesh(floor,nullptr,false);
    constexpr uint16_t width=160,height=120;
    const auto color=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
    const auto depth=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::D32F,BGFX_TEXTURE_RT);
    const bgfx::TextureHandle attachments[]={color,depth};const auto fb=bgfx::createFrameBuffer(2,attachments,true);
    const auto readback=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
    float view[16],projection[16],render[16];
    bx::mtxLookAt(view,{0,14,-26},{0,0,0});bx::mtxProj(projection,60.f,float(width)/height,.05f,200.f,bgfx::getCaps()->homogeneousDepth);
    std::copy(projection,projection+16,render);
    for(int i:{2,6,10,14}) render[i]=bgfx::getCaps()->homogeneousDepth?-render[i]:render[i+1]-render[i];
    auto capture=[&]() {
      bgfx::setViewFrameBuffer(5,fb);bgfx::setViewRect(5,0,0,width,height);
      bgfx::setViewClear(5,BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,uint32_t(0xff),0.f);
      bgfx::setViewTransform(5,view,render);
      b.draw_instance(5,road,road.transform.data(),false,0);
      bgfx::TextureRegion dst,src;dst.init(readback);src.init(color);bgfx::blit(100,dst,src);bgfx::frame();
      std::vector<uint8_t> bytes(width*height*4);const auto ready=bgfx::read(dst,bytes.data());
      for(int i=0;i<16;++i) if(bgfx::frame()>=ready) return bytes;
      throw std::runtime_error("scenery lamp readback timeout");
    };
    const auto day=capture();  // update_local_light at noon: no lamp
    scene_sun_set_time(22*3600.f);b.update_local_light();
    SLRR_REQUIRE(b.fixture_lights_.size()==1);
    const auto night=capture();
    b.fixture_lights_.clear();
    const auto unlit=capture();
    SLRR_REQUIRE(day==unlit);
    unsigned lit=0;int centre_gain=0,edge_gain=0;
    const auto at=[&](const std::vector<uint8_t>& image,int x,int y){return int(image[(size_t(y)*width+x)*4]);};
    for(size_t i=0;i<night.size();i+=4) if(int(night[i])-int(unlit[i])>10) ++lit;
    centre_gain=at(night,width/2,height/2)-at(unlit,width/2,height/2);
    edge_gain=at(night,2,height-3)-at(unlit,2,height-3);
    std::printf("SCENERY lamp lit pixels=%u of %u, centre gain=%d, corner gain=%d\n",
                lit,unsigned(width*height),centre_gain,edge_gain);
    SLRR_REQUIRE(lit>2000 && centre_gain>40 && centre_gain>edge_gain);
    bgfx::frame();bgfx::destroy(fb);bgfx::destroy(readback);
    host_enhanced_graphics()=saved;scene_sun_set_time(old_time);
    b.instances_.clear();b.fixture_lights_.clear();b.outdoor_lighting_=false;
    b.parts_=std::move(saved_parts);b.prop_batches_=std::move(saved_batches);
    gameref_remove(&shell);SLRR_REQUIRE(scenery_lights_count()==0);
    gameref_remove(&world);scene_instances_set_resources(nullptr);
    std::puts("PASS scenery lamp lights the road at night only, option 4 and groups");
  }
  static void reflection_occupants() {
    Fixture f;Object car,seat,person,pedestrian,world;
    auto* c=gameref_upsert(&car);c->vehicle_material=true;
    gameref_upsert(&seat);gameref_upsert(&person);gameref_upsert(&pedestrian);gameref_upsert(&world);
    gameref_set_parent(&person,&seat);gameref_set_parent(&seat,&car);
    gameref_set_parent(&pedestrian,&world);
    SceneInstance occupant;occupant.key=&person;
    SLRR_REQUIRE(!reflection_capture_visible(occupant));
    occupant.key=&pedestrian;SLRR_REQUIRE(reflection_capture_visible(occupant));
    gameref_set_parent(&person,&world);occupant.key=&person;
    SLRR_REQUIRE(reflection_capture_visible(occupant));
    for(auto* key:{&person,&pedestrian,&seat,&car,&world}) gameref_remove(key);
    std::puts("PASS reflection excludes seated subtree but preserves independent pedestrians");
  }

  static void point_lifetime(LibBgfxBackend &b) {
    Fixture f;
    scene_instances_set_resources(&f.packs);
    Object world, other, neon;
    gameref_upsert(&world)->flags = kWorldTreeRoot;
    gameref_upsert(&other)->flags = kWorldTreeRoot;
    auto *node = gameref_upsert(&neon, GameRefKind::RenderRef);
    node->type_id = 0x1d;
    node->directional_light.set = node->directional_light.has_direction = true;
    gameref_set_parent(&neon, &world);
    gameref_make_live(&neon);
    gameref_register_light(&neon);
    const auto saved = host_enhanced_graphics();
    host_enhanced_graphics().local_lights =
        host_enhanced_graphics().dynamic_lighting = true;
    b.garage_ = true;
    scene_instances_select(&world, nullptr, false, SceneEnvironment::Interior);
    b.update_local_light();
    SLRR_REQUIRE(b.point_light_ && b.point_light_->valid() &&
                 b.point_light_->owner == &neon);
    gameref_set_matrix(&neon, 2, 1, 3, 0, 0, 0);
    b.update_local_light();
    SLRR_REQUIRE(b.point_light_->position[0] == 2 &&
                 std::abs(b.point_light_->position[1] - 4.3f) < .0001f &&
                 b.point_light_->position[2] == -3);
    auto invalid = *b.point_light_;
    invalid.range = 0;
    SLRR_REQUIRE(!invalid.valid());
    invalid = *b.point_light_;
    invalid.intensity = std::numeric_limits<float>::quiet_NaN();
    SLRR_REQUIRE(!invalid.valid());
    host_enhanced_graphics().local_lights = false;
    b.update_local_light();
    SLRR_REQUIRE(!b.point_light_);
    host_enhanced_graphics().local_lights = true;
    b.update_local_light();
    SLRR_REQUIRE(b.point_light_);
    scene_instances_select(&other, nullptr, false, SceneEnvironment::Interior);
    b.update_local_light();
    SLRR_REQUIRE(!b.point_light_ && b.local_light_[3] == 0);
    host_enhanced_graphics() = saved;
    b.garage_ = false;
    gameref_remove(&neon);
    gameref_remove(&world);
    gameref_remove(&other);
    scene_instances_set_resources(nullptr);
    std::puts("PASS point-light source lifetime, invalid data, option toggle "
              "and world isolation");
  }
  static void vehicle_wall(LibBgfxBackend &b) {
    Fixture f;
    scene_instances_set_resources(&f.packs);
    Object root;
    gameref_upsert(&root)->flags = kWorldTreeRoot;
    scene_instances_select(&root, nullptr, false, SceneEnvironment::Exterior);
    scene_sun_set_time(9 * 3600.f);
    auto car = std::make_shared<ScxGeometry>();
    car->info.ok = true;
    car->info.version = 4;
    car->position_scale = 1;
    car->positions = {-.6f, 0, -1, .6f, 0, -1, .6f, 1.5f, -1, -.6f, 1.5f, -1,
                      -.6f, 0, 1,  .6f, 0, 1,  .6f, 1.5f, 1,  -.6f, 1.5f, 1};
    car->indices = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                    3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
    car->info.bbox_min[0] = -.6f;
    car->info.bbox_min[1] = 0;
    car->info.bbox_min[2] = -1;
    car->info.bbox_max[0] = .6f;
    car->info.bbox_max[1] = 1.5f;
    car->info.bbox_max[2] = 1;
    car->normals.resize(car->positions.size(), 0);
    car->uvs.resize(16, 0);
    ScxGroup group;
    group.index_count = uint32_t(car->indices.size());
    car->groups.push_back(group);
    b.cache_scene_mesh(car, nullptr, false);
    SceneInstance caster;
    caster.geometry = car;
    caster.vehicle_material = true;
    bx::mtxIdentity(caster.transform.data());
    b.instances_ = {caster};
    b.garage_ = false;
    b.outdoor_lighting_ = true;
    b.local_light_ = {};
    b.static_shadow_active_ = false;
    VideoOptions video;
    video.shadows = 2;
    const float centre[3] = {0, 0, 0};
    b.update_car_depth(centre, video);
    bgfx::frame();
    SLRR_REQUIRE(b.car_depth_active_);
    auto wall = std::make_shared<ScxGeometry>();
    wall->info.ok = true;
    wall->info.version = 4;
    wall->position_scale = 1;
    wall->positions = {-1.3f, 0, -2, -1.3f, 2, -2, -1.3f, 0, 2, -1.3f, 2, 2};
    wall->normals = {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0};
    wall->uvs.resize(8, 0);
    wall->indices = {0, 1, 2, 2, 1, 3};
    group.index_count = 6;
    wall->groups = {group};
    b.cache_scene_mesh(wall, nullptr, false);
    SceneInstance receiver;
    receiver.geometry = wall;
    receiver.lighting = scene_instances_light();
    bx::mtxIdentity(receiver.transform.data());
    constexpr uint16_t size = 128;
    auto color = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
    auto depth = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT);
    bgfx::TextureHandle attachments[] = {color, depth};
    auto fb = bgfx::createFrameBuffer(2, attachments, true);
    auto readback =
        bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    float identity[16];
    bx::mtxIdentity(identity);
    float projection[16]{};
    projection[8] = .5f;
    projection[5] = 1;
    projection[13] = -1;
    projection[14] = .5f;
    projection[15] = 1;
    bool point_mode = false;
    uint16_t view = 5;
    float receiver_view[16];
    bx::mtxIdentity(receiver_view);
    auto capture = [&](bool active) {
      if (point_mode) {
        b.car_depth_active_ = false;
        b.static_shadow_active_ = active;
      } else
        b.car_depth_active_ = active;
      bgfx::setViewFrameBuffer(view, fb);
      bgfx::setViewRect(view, 0, 0, size, size);
      bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                         uint32_t(0xff), 0.f);
      bgfx::setViewTransform(view, receiver_view, projection);
      b.draw_instance(view, receiver, receiver.transform.data(), false, 0);
      bgfx::TextureRegion dst, src;
      dst.init(readback);
      src.init(color);
      bgfx::blit(100, dst, src);
      bgfx::frame();
      std::vector<uint8_t> bytes(size * size * 4);
      auto ready = bgfx::read(dst, bytes.data());
      for (int i = 0; i < 16; ++i)
        if (bgfx::frame() >= ready)
          return bytes;
      throw std::runtime_error("wall readback timeout");
    };
    auto lit = capture(false), shadow = capture(true);
    unsigned changed = 0;
    for (size_t i = 0; i < lit.size(); i += 4)
      if (int(lit[i]) - int(shadow[i]) > 10)
        ++changed;
    std::printf("Vehicle production depth -> vertical wall: %u shadow pixels\n",
                changed);
    SLRR_REQUIRE(changed > 100);
    PointLight point;
    point.owner = &root;
    point.position = {0, 0, 0};
    b.point_light_ = point;
    point_mode = true;
    b.outdoor_lighting_ = false;
    constexpr bx::Vec3 axes[] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                 {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    for (unsigned face = 0; face < 6; ++face) {
      const auto axis = axes[face];
      const auto normal = bx::mul(axis, -1.f);
      const bx::Vec3 up =
          std::abs(normal.y) > .9f ? bx::Vec3{0, 0, 1} : bx::Vec3{0, 1, 0};
      const auto side = bx::cross(normal, up);
      bx::mtxIdentity(receiver.transform.data());
      for (int row = 0; row < 3; ++row) {
        receiver.transform[row] = (&normal.x)[row];
        receiver.transform[4 + row] = (&up.x)[row];
        receiver.transform[8 + row] = (&side.x)[row];
        receiver.transform[12 + row] = 2.7f * (&axis.x)[row] - (&up.x)[row];
      }
      bx::mtxInverse(receiver_view, receiver.transform.data());
      b.instances_[0].transform[12] = axis.x * 2;
      b.instances_[0].transform[13] = axis.y * 2 - .75f;
      b.instances_[0].transform[14] = axis.z * 2;
      b.update_static_shadow(centre, video);
      b.draw_previews(); // production ordering must place face/view 64 before
                         // world 5
      SLRR_REQUIRE(b.point_shadow_active_ && b.static_shadow_local_);
      auto occluded = capture(true), plain = capture(false);
      unsigned coverage = 0;
      for (size_t i = 0; i < plain.size(); i += 4)
        if (int(plain[i]) - int(occluded[i]) > 10)
          ++coverage;
      std::printf("Point light face %u: %u blocked wall pixels\n", face,
                  coverage);
      SLRR_REQUIRE(coverage > 100);
      if (face == 5) {
        view = 72;
        auto reflection = capture(true);
        SLRR_REQUIRE(reflection == occluded);
        view = 5;
      }
      // This must be a moving occluder, not a dark atlas or an unlit wall.
      for (int row = 0; row < 3; ++row)
        b.instances_[0].transform[12 + row] += (&up.x)[row] * 4;
      b.update_static_shadow(centre, video);
      bgfx::frame();
      auto revealed = capture(true);
      unsigned remaining = 0;
      for (size_t i = 0; i < plain.size(); i += 4)
        if (int(plain[i]) - int(revealed[i]) > 10)
          ++remaining;
      std::printf("Point light face %u: %u pixels blocked after lateral move\n",
                  face, remaining);
      SLRR_REQUIRE(remaining < coverage / 20);
      const auto revision = b.shadow_atlas_revision_;
      b.update_static_shadow(centre, video);
      bgfx::frame();
      SLRR_REQUIRE(b.shadow_atlas_revision_ == revision);
    }
    b.point_light_->position[0] += .1f;
    auto revision = b.shadow_atlas_revision_;
    b.update_static_shadow(centre, video);
    bgfx::frame();
    SLRR_REQUIRE(b.shadow_atlas_revision_ == revision + 1);
    revision = b.shadow_atlas_revision_;
    b.point_light_->color = {.1f, .2f, 1.f};
    b.point_light_->intensity = 2;
    b.update_static_shadow(centre, video);
    bgfx::frame();
    SLRR_REQUIRE(b.shadow_atlas_revision_ == revision);
    const auto blue = capture(false);
    const size_t pixel = (size / 2 * size + size / 2) * 4;
    SLRR_REQUIRE(blue[pixel + 2] > blue[pixel] + 10);
    b.point_light_->range = 1;
    b.update_static_shadow(centre, video);
    bgfx::frame();
    const auto outside = capture(false);
    SLRR_REQUIRE(std::abs(int(outside[pixel + 2]) - int(outside[pixel])) <= 1);
    std::puts("PASS point colour/intensity, radial range cutoff and "
              "unchanged-depth reuse");
    // A lit vertical body panel is also in its own wide-cone shadow map.
    // Grazing projection/PCF must not invent stripes across that same plane.
    auto panel=std::make_shared<ScxGeometry>();
    panel->info.ok=true;panel->info.version=4;panel->position_scale=1;
    panel->positions={-1.3f,0,-2,-1.3f,3,-2,-1.3f,0,2,-1.3f,3,2};
    panel->normals={1,0,0,1,0,0,1,0,0,1,0,0};panel->uvs.resize(8,0);
    panel->indices={0,1,2,2,1,3};panel->groups={group};panel->groups[0].index_count=6;
    panel->info.bbox_min[0]=panel->info.bbox_max[0]=-1.3f;
    panel->info.bbox_min[2]=-2;panel->info.bbox_max[2]=2;panel->info.bbox_max[1]=3;
    b.cache_scene_mesh(panel,nullptr,false);
    receiver.geometry=panel;receiver.vehicle_material=true;
    bx::mtxIdentity(receiver.transform.data());bx::mtxIdentity(receiver_view);
    std::fill(std::begin(projection),std::end(projection),0.f);
    projection[8]=.5f;projection[5]=2.f/3;projection[13]=-1;
    projection[14]=.5f;projection[15]=1;view=5;
    b.instances_={receiver};b.garage_=true;
    SpotLight lamp;lamp.position={1,6.5f,0};lamp.intensity=24;
    b.fixture_lights_={lamp};
    PointLight ambient;ambient.intensity=0;ambient.casts_shadows=false;
    b.point_light_=ambient;b.point_shadow_active_=false;b.car_depth_active_=false;
    b.update_fixture_shadows(video);b.draw_previews();bgfx::frame();
    b.fixture_shadow_active_=false;const auto clean_panel=capture(false);
    b.fixture_shadow_active_=true;const auto shadowed_panel=capture(false);
    unsigned acne=0;
    for(size_t i=0;i<clean_panel.size();i+=4)
      if(int(clean_panel[i])-int(shadowed_panel[i])>8) ++acne;
    std::printf("NEON grazing panel self-shadow stripes=%u pixels\n",acne);
    SLRR_REQUIRE(acne<size*size/100);
    // Put a vehicle 20 cm from the wall: its shadow must climb onto the
    // actual vertical receiver, not merely appear on a horizontal floor.
    receiver.vehicle_material=false;
    bx::mtxIdentity(caster.transform.data());caster.transform[12]=-.5f;
    b.instances_={receiver,caster};b.update_fixture_shadows(video);
    b.draw_previews();bgfx::frame();
    b.fixture_shadow_active_=false;const auto bare_wall=capture(false);
    b.fixture_shadow_active_=true;const auto car_wall=capture(false);
    unsigned wall_pixels=0;
    for(size_t i=0;i<bare_wall.size();i+=4)
      if(int(bare_wall[i])-int(car_wall[i])>8) ++wall_pixels;
    std::printf("NEON vehicle shadow on vertical wall=%u pixels\n",wall_pixels);
    SLRR_REQUIRE(wall_pixels>100);
    const auto saved_shadow_options=host_enhanced_graphics();
    for(int quality=0;quality<3;++quality) {
      host_enhanced_graphics().shadow_quality=quality;
      b.update_fixture_shadows(video);b.draw_previews();bgfx::frame();
      SLRR_REQUIRE(b.fixture_shadow_tile_==(256u<<quality));
      const auto revision=b.fixture_shadow_revision_;
      b.update_fixture_shadows(video);SLRR_REQUIRE(b.fixture_shadow_revision_==revision);
      std::vector<uint8_t> filters[3];
      for(int filter=0;filter<3;++filter) {
        host_enhanced_graphics().shadow_smoothing=filter;
        filters[filter]=capture(false);unsigned shaded=0;
        for(size_t i=0;i<bare_wall.size();i+=4)
          if(int(bare_wall[i])-int(filters[filter][i])>8) ++shaded;
        SLRR_REQUIRE(shaded>100);
        b.update_fixture_shadows(video);SLRR_REQUIRE(b.fixture_shadow_revision_==revision);
      }
      SLRR_REQUIRE(filters[0]!=filters[1] && filters[1]!=filters[2]);
    }
    host_enhanced_graphics()=saved_shadow_options;
    std::puts("PASS shadow quality reallocates, smoothing changes pixels without depth redraw");
    caster.transform[14]=5; b.instances_={receiver,caster};
    b.update_fixture_shadows(video);b.draw_previews();bgfx::frame();
    const auto cleared_wall=capture(false);unsigned remaining_wall=0;
    for(size_t i=0;i<bare_wall.size();i+=4)
      if(int(bare_wall[i])-int(cleared_wall[i])>8) ++remaining_wall;
    SLRR_REQUIRE(remaining_wall<wall_pixels/10);
    b.fixture_lights_.clear();b.fixture_shadow_active_=false;b.garage_=false;
    b.point_light_.reset();
    b.point_shadow_active_ = false;
    bgfx::destroy(readback);
    bgfx::destroy(fb);
    gameref_remove(&root);
    scene_instances_set_resources(nullptr);
    b.instances_.clear();
    b.car_depth_active_ = false;
  }
  static void garage_geometry(LibBgfxBackend &b) {
    Fixture f;
    f.packs.set_root(fs::absolute("game").string());
    f.packs.open_all();
    scene_instances_set_resources(&f.packs);
    const auto pack = f.packs.open_lib("misc/garage.rpk");
    Object root, stuff;
    root.class_name = "java.util.resource.GroundRef";
    gameref_upsert(&root, GameRefKind::GroundRef)->type_id =
        RpkIndex::make_full_id(pack, 0x7a);
    spawn_ground_shell(f.vm, f.packs, &root);
    gameref_upsert(&stuff, GameRefKind::RenderRef)->type_id =
        RpkIndex::make_full_id(pack, 0x42);
    gameref_set_parent(&stuff, &root);
    scene_instances_select(&root, nullptr, false, SceneEnvironment::Interior);
    b.instances_ = scene_instances_collect();
    for (const auto &obj : b.instances_)
      b.cache_scene_mesh(obj.geometry, nullptr, false);
    auto realFloor =
        std::make_shared<ScxGeometry>(*b.instances_.front().geometry);
    realFloor->indices.clear();
    realFloor->groups.clear();
    const auto &source = *b.instances_.front().geometry;
    for (const auto &original : source.groups) {
      auto group = original;
      group.index_start = uint32_t(realFloor->indices.size());
      for (size_t i = original.index_start;
           i + 2 < original.index_start + original.index_count; i += 3) {
        bool floor = true;
        for (int k = 0; k < 3; ++k) {
          auto v = source.indices[i + k] * 3;
          floor &= std::abs(source.positions[v + 1]) < .2f &&
                   source.normals[v + 1] > .5f;
        }
        if (floor)
          realFloor->indices.insert(realFloor->indices.end(),
                                    source.indices.begin() + i,
                                    source.indices.begin() + i + 3);
      }
      group.index_count =
          uint32_t(realFloor->indices.size()) - group.index_start;
      if (group.index_count)
        realFloor->groups.push_back(group);
    }
    SLRR_REQUIRE(!realFloor->indices.empty());
    b.garage_ = true;
    b.outdoor_lighting_ = false;
    b.local_light_ = {0, 3.3f, 0, 32};
    PointLight fixture_light;
    fixture_light.position = {0, 3.3f, 0};
    b.point_light_ = fixture_light;
    VideoOptions video;
    video.shadows = 2;
    const float eye[3] = {0, 2, -4};
    b.update_static_shadow(eye, video);
    bgfx::frame();
    // All room corners must lie inside the actual fitted shadow footprint.
    for (int corner = 0; corner < 4; ++corner) {
      bx::Vec3 point{
          corner & 1 ? source.info.bbox_max[0] : source.info.bbox_min[0], 0,
          -(corner & 2 ? source.info.bbox_max[2] : source.info.bbox_min[2])};
      const std::array<float, 3> delta{point.x - fixture_light.position[0],
                                       point.y - fixture_light.position[1],
                                       point.z - fixture_light.position[2]};
      const auto face = point_shadow_face(delta);
      const auto uv = bx::mulH({delta[0], delta[1], delta[2]},
                               b.point_shadow_matrices_[face].data());
      SLRR_REQUIRE(uv.x > 0 && uv.x < 1 && uv.y > 0 && uv.y < 1);
    }
    SLRR_REQUIRE(b.point_shadow_active_ && b.static_shadow_local_);
    constexpr uint16_t size = 256;
    const auto color = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
    const auto depth = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT);
    const bgfx::TextureHandle attachments[] = {color, depth};
    const auto fb = bgfx::createFrameBuffer(2, attachments, true);
    const auto readback =
        bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    auto floor = realFloor;
    b.cache_scene_mesh(floor, nullptr, false);
    SceneInstance receiver;
    receiver.geometry = floor;
    bx::mtxIdentity(receiver.transform.data());
    receiver.lighting.set = receiver.lighting.has_direction = true;
    for (int i = 0; i < 3; ++i) {
      receiver.lighting.diffuse[i] = .8f;
      receiver.lighting.ambient[i] = .1f;
    }
    float identity[16];
    bx::mtxIdentity(identity);
    float projection[16]{};
    projection[0] = projection[9] = .05f;
    projection[6] = .01f;
    projection[14] = .5f;
    projection[15] = 1;
    uint16_t capture_view=5;
    auto capture = [&](bool enabled) {
      b.static_shadow_active_ = enabled;
      bgfx::setViewFrameBuffer(capture_view, fb);
      bgfx::setViewRect(capture_view, 0, 0, size, size);
      bgfx::setViewClear(capture_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, uint32_t(0xff),
                         0.f);
      bgfx::setViewTransform(capture_view, identity, projection);
      b.draw_instance(capture_view, receiver, identity, false, 0);
      bgfx::TextureRegion dst, src;
      dst.init(readback);
      src.init(color);
      bgfx::blit(100, dst, src);
      bgfx::frame();
      std::vector<uint8_t> bytes(size * size * 4);
      auto ready = bgfx::read(dst, bytes.data());
      for (int i = 0; i < 16; ++i)
        if (bgfx::frame() >= ready)
          return bytes;
      throw std::runtime_error("garage readback timeout");
    };
    const auto lit = capture(false), shadow = capture(true);
    unsigned changed = 0;
    for (size_t i = 0; i < lit.size(); i += 4)
      if (int(lit[i]) - int(shadow[i]) > 10)
        ++changed;
    std::printf("GARAGE real geometry shadow coverage=%u pixels\n", changed);
    SLRR_REQUIRE(changed > 20 && changed < size * size / 2);
    // Move the actual furniture mesh: baked texture pixels stay identical,
    // while its depth-map footprint must move across the real garage floor.
    for (auto &instance : b.instances_)
      if (instance.key == &stuff)
        instance.transform[12] += 1.f;
    b.update_static_shadow(eye, video);
    bgfx::frame();
    const auto moved = capture(true);
    unsigned moving = 0;
    for (size_t i = 0; i < moved.size(); i += 4)
      if (std::abs(int(moved[i]) - int(shadow[i])) > 10)
        ++moving;
    std::printf("GARAGE furniture motion changes %u real-floor shadow pixels\n",
                moving);
    SLRR_REQUIRE(moving > 20);
    // Restore the stock window positions: the furniture SCX also contains
    // the window overlays, so the preceding movement test translated those.
    for (auto& instance : b.instances_) if (instance.key == &stuff) instance.transform[12]-=1.f;
    b.update_static_shadow(eye,video);
    // Daylight is an independent directional source: adding it must keep the
    // point source/shadow field, and changing only clock time moves its
    // footprint.
    const auto point_before = b.point_light_;
    const float old_time = scene_sun_time();
    scene_sun_set_time(15.f * 3600);
    b.update_interior_sun(video);
    SLRR_REQUIRE(b.interior_sun_active_);
    SLRR_REQUIRE(b.point_light_ == point_before && b.point_shadow_active_);
    b.draw_previews();
    bgfx::frame();
    const auto solar = capture(true);
    b.interior_sun_active_ = false;
    const auto neon_only = capture(true);
    unsigned sunlight_pixels = 0;
    for (size_t i = 0; i < solar.size(); i += 4)
      if (int(solar[i]) - int(neon_only[i]) > 10)
        ++sunlight_pixels;
    std::printf("GARAGE directional sunlight through alpha windows=%u pixels\n",
                sunlight_pixels);
    SLRR_REQUIRE(sunlight_pixels > 20 && sunlight_pixels < size * size / 2);
    // Same mesh and camera, but an opaque alpha mask must seal the window.
    std::string mask_path;
    for (const auto &obj : b.instances_)
      for (const auto &g : obj.geometry->groups)
        if (g.name == "ablak")
          mask_path = obj.geometry->textures.at(g.texture_index);
    SLRR_REQUIRE(!mask_path.empty());
    const auto original_mask = b.osd_tex_.at(mask_path);
    const uint32_t opaque_pixel = 0xffffffff;
    const auto opaque_mask =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                              bgfx::copy(&opaque_pixel, 4));
    b.osd_tex_[mask_path] = opaque_mask;
    b.update_interior_sun(video);
    b.draw_previews();
    bgfx::frame();
    const auto sealed = capture(true);
    unsigned masked_pixels = 0;
    for (size_t i = 0; i < solar.size(); i += 4)
      if (int(solar[i]) - int(sealed[i]) > 10)
        ++masked_pixels;
    std::printf("GARAGE opaque window alpha blocks %u sunlight pixels\n",
                masked_pixels);
    SLRR_REQUIRE(masked_pixels > 20);
    unsigned leaked_sun=0;
    for(size_t i=0;i<sealed.size();i+=4)
      if(int(sealed[i])-int(neon_only[i])>1) ++leaked_sun;
    std::printf("GARAGE sealed-aperture sunlight leaks=%u pixels\n",leaked_sun);
    SLRR_REQUIRE(leaked_sun==0);
    b.osd_tex_[mask_path] = original_mask;
    bgfx::destroy(opaque_mask);
    const auto afternoon_matrix = b.interior_sun_matrix_;
    scene_sun_set_time(16.f * 3600);
    b.update_interior_sun(video);
    SLRR_REQUIRE(b.interior_sun_matrix_ != afternoon_matrix);
    b.draw_previews();
    bgfx::frame();
    const auto late_solar = capture(true);
    unsigned solar_motion = 0;
    for (size_t i = 0; i < solar.size(); i += 4)
      if (std::abs(int(solar[i]) - int(late_solar[i])) > 10)
        ++solar_motion;
    std::printf("GARAGE clock-only sunlight motion=%u pixels\n", solar_motion);
    SLRR_REQUIRE(solar_motion > 20);
    auto flare_capture = [&](const bx::Vec3 &camera) {
      const auto &sun = b.interior_sun_;
      const bx::Vec3 direction{-sun.direction[0], -sun.direction[1],
                               sun.direction[2]};
      float view[16], proj[16];
      bx::mtxLookAt(view, camera, bx::add(camera, direction));
      bx::mtxProj(proj, 60.f, 1.f, .1f, 100.f,
                  bgfx::getCaps()->homogeneousDepth);
      bgfx::setViewFrameBuffer(5, fb);
      bgfx::setViewRect(5, 0, 0, size, size);
      bgfx::setViewClear(5, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, uint32_t(0xff),
                         0.f);
      bgfx::setViewTransform(5, view, proj);
      bgfx::touch(5);
      b.draw_sun_flare(view, proj, 5);
      bgfx::TextureRegion dst, src;
      dst.init(readback);
      src.init(color);
      bgfx::blit(100, dst, src);
      bgfx::frame();
      std::vector<uint8_t> bytes(size * size * 4);
      const auto ready = bgfx::read(dst, bytes.data());
      for (int i = 0; i < 16; ++i)
        if (bgfx::frame() >= ready) {
          uint64_t sum = 0;
          for (size_t j = 0; j < bytes.size(); j += 4)
            sum += bytes[j] + bytes[j + 1] + bytes[j + 2];
          return sum;
        }
      throw std::runtime_error("flare readback timeout");
    };
    const auto visible_flare = flare_capture({-17, 2.5f, -8.5f});
    const auto blocked_flare = flare_capture({0, 1.5f, 0});
    const auto frame_flare = flare_capture({-17, 2.5f, -9.46f});
    std::printf("GARAGE frame flare=%llu\n",static_cast<unsigned long long>(frame_flare));
    SLRR_REQUIRE(frame_flare == 0);
    std::printf("GARAGE camera-to-sun flare: window=%llu roof=%llu\n",
                static_cast<unsigned long long>(visible_flare),
                static_cast<unsigned long long>(blocked_flare));
    SLRR_REQUIRE(visible_flare > blocked_flare + 1000 && blocked_flare == 0);
    scene_sun_set_time(0);
    b.update_interior_sun(video);
    SLRR_REQUIRE(!b.interior_sun_active_);
    scene_sun_set_time(old_time);
    b.interior_sun_active_=false;
    Object neon;
    auto* neon_node=gameref_upsert(&neon,GameRefKind::RenderRef);
    neon_node->type_id=RpkIndex::make_full_id(pack,0x1d);
    neon_node->directional_light.set=neon_node->directional_light.has_direction=true;
    gameref_set_parent(&neon,&root);gameref_make_live(&neon);gameref_register_light(&neon);
    const auto graphics=host_enhanced_graphics();
    host_enhanced_graphics().local_lights=host_enhanced_graphics().dynamic_lighting=true;
    b.update_local_light();
    std::printf("NEON actual fixture count=%zu\n",b.fixture_lights_.size());
    SLRR_REQUIRE(b.fixture_lights_.size()==9);
    SLRR_REQUIRE(b.point_light_ && b.point_light_->intensity==0 && !b.point_light_->casts_shadows);
    for(const auto& light:b.fixture_lights_) {
      SLRR_REQUIRE(light.valid() && light.position[1]>6.4f && light.position[1]<6.5f);
      SLRR_REQUIRE(light.owner==&neon && light.direction[1]<-.99f);
    }
    b.update_static_shadow(eye,video);b.update_fixture_shadows(video);
    SLRR_REQUIRE(b.fixture_shadow_active_);
    b.draw_previews();bgfx::frame();
    const auto fixtures=b.fixture_lights_;
    {
      // #235: the patched Garage.java creates one SpotLight per fitting,
      // parented to the stuff mesh at these local anchors. They must give the
      // adapter's world positions and replace it.
      const float anchors[9][3]={{-14.05739f,6.43138f,5.03007f},{-8.68388f,6.43138f,5.03007f},
          {-3.03047f,6.43138f,5.03007f},{2.63350f,6.43138f,5.03007f},{8.82223f,6.43138f,5.03007f},
          {5.77201f,6.43138f,-4.00419f},{-5.88736f,6.43138f,-4.00419f},
          {12.71575f,6.43138f,1.23414f},{12.71575f,6.43138f,-5.19158f}};
      Object scripted[9];
      for(int i=0;i<9;++i) {
        SLRR_REQUIRE(script_lights_create(&scripted[i],&stuff,ScriptLightKind::Spot));
        auto* light=script_lights_get(&scripted[i]);
        light->position={anchors[i][0],anchors[i][1],anchors[i][2]};
        light->outer_degrees=80;
      }
      b.update_local_light();
      SLRR_REQUIRE(b.fixture_lights_.size()==fixtures.size());
      for(size_t i=0;i<fixtures.size();++i) {
        const auto& got=b.fixture_lights_[i];const auto& want=fixtures[i];
        for(int axis=0;axis<3;++axis) {
          SLRR_REQUIRE(std::abs(got.position[axis]-want.position[axis])<1e-3f);
          SLRR_REQUIRE(std::abs(got.direction[axis]-want.direction[axis])<1e-5f);
        }
        SLRR_REQUIRE(got.owner==&scripted[i] && got.intensity==want.intensity &&
                     got.range==want.range && std::abs(got.outer_cos-want.outer_cos)<1e-5f);
      }
      // The neon stays an ambient-only record next to script fixtures.
      SLRR_REQUIRE(b.point_light_ && b.point_light_->intensity==0 && !b.point_light_->casts_shadows);
      std::printf("NEON script fixtures match adapter=%zu\n",b.fixture_lights_.size());
      // Disabled script lights still replace the adapter: the garage is dark.
      for(auto& light:scripted) script_lights_get(&light)->enabled=false;
      b.update_local_light();SLRR_REQUIRE(b.fixture_lights_.empty());
      // The neon stays ambient-only: no shadowed point light comes back.
      SLRR_REQUIRE(b.point_light_ && b.point_light_->owner==&neon &&
                   b.point_light_->intensity==0 && !b.point_light_->casts_shadows);
      // A script point light takes the point record and keeps the neon ambient.
      Object bulb;SLRR_REQUIRE(script_lights_create(&bulb,&stuff,ScriptLightKind::Point));
      script_lights_get(&bulb)->position={0,3,0};
      b.update_local_light();
      SLRR_REQUIRE(b.point_light_ && b.point_light_->owner==&bulb && b.point_light_->intensity==24);
      SLRR_REQUIRE(std::abs(b.point_light_->position[1]-3)<1e-4f);
      for(auto& light:scripted) script_lights_destroy(&light);
      script_lights_destroy(&bulb);
      b.update_local_light();
      SLRR_REQUIRE(b.fixture_lights_.size()==fixtures.size() && b.fixture_lights_[0].owner==&neon);
      b.fixture_lights_=fixtures;
    }
    const auto fixture_image=capture(false);
    capture_view=72;
    SLRR_REQUIRE(capture(false)==fixture_image);
    capture_view=5;
    b.fixture_lights_.clear();
    const auto ambient_image=capture(false);
    unsigned neon_pixels=0;
    for(size_t i=0;i<fixture_image.size();i+=4) if(int(fixture_image[i])-int(ambient_image[i])>10) ++neon_pixels;
    std::printf("NEON real-floor illuminated pixels=%u\n",neon_pixels);
    SLRR_REQUIRE(neon_pixels>100);
    b.fixture_lights_=fixtures;
    auto revision=b.fixture_shadow_revision_;
    b.update_fixture_shadows(video);
    SLRR_REQUIRE(b.fixture_shadow_revision_==revision);
    // Each atlas tile/source must contribute independently, including > 6.
    for(size_t lamp=0;lamp<fixtures.size();++lamp) {
      b.fixture_lights_=fixtures;
      for(size_t j=0;j<fixtures.size();++j) if(j!=lamp) b.fixture_lights_[j].intensity=0;
      const auto image=capture(false);unsigned pixels=0;
      for(size_t i=0;i<image.size();i+=4) if(int(image[i])-int(ambient_image[i])>3) ++pixels;
      std::printf("NEON source %zu contributes %u pixels\n",lamp,pixels);
      SLRR_REQUIRE(pixels>10);
      b.update_fixture_shadows(video);SLRR_REQUIRE(b.fixture_shadow_revision_==revision);
    }
    b.fixture_lights_=fixtures;
    b.fixture_shadow_active_=false;
    const auto unshadowed=capture(false);
    unsigned blocked=0;
    for(size_t i=0;i<fixture_image.size();i+=4) if(int(unshadowed[i])-int(fixture_image[i])>5) ++blocked;
    std::printf("NEON occluded pixels=%u\n",blocked);SLRR_REQUIRE(blocked>10);
    b.update_fixture_shadows(video);b.draw_previews();bgfx::frame();
    revision=b.fixture_shadow_revision_;
    b.fixture_lights_[0].position[0]+=1.f;
    b.update_fixture_shadows(video);SLRR_REQUIRE(b.fixture_shadow_revision_==revision+1);
    b.draw_previews();bgfx::frame();
    const auto shifted=capture(false);unsigned moving_neon=0;
    for(size_t i=0;i<shifted.size();i+=4) if(std::abs(int(shifted[i])-int(fixture_image[i]))>5) ++moving_neon;
    SLRR_REQUIRE(moving_neon>10);
    // The luminous tube faces keep their original white atlas sample and do
    // not depend on ambient, punctual shadows or the sunlight enable state.
    auto tubes=std::make_shared<ScxGeometry>();
    for(const auto& obj:b.instances_) if(!obj.geometry->ceiling_light_anchors.empty()) {
      *tubes=*obj.geometry;tubes->indices.clear();tubes->groups.clear();
      for(auto group:obj.geometry->groups) if(group.self_illuminated) {
        auto first=group.index_start;group.index_start=uint32_t(tubes->indices.size());
        tubes->indices.insert(tubes->indices.end(),obj.geometry->indices.begin()+first,
            obj.geometry->indices.begin()+first+group.index_count);tubes->groups.push_back(group);
      }
      break;
    }
    SLRR_REQUIRE(tubes->indices.size()==432*3);
    b.cache_scene_mesh(tubes,nullptr,false);
    receiver.geometry=tubes;
    const auto tubes_on=capture(false);
    b.fixture_lights_.clear();b.point_light_.reset();
    const auto tubes_off=capture(false);
    SLRR_REQUIRE(tubes_on==tubes_off);
    unsigned white=0;
    for(size_t i=0;i<tubes_off.size();i+=4) if(tubes_off[i]>180 && tubes_off[i+1]>180 && tubes_off[i+2]>180) ++white;
    std::printf("NEON white tube pixels retained without lights=%u\n",white);SLRR_REQUIRE(white>10);
    for(auto& object:b.instances_) if(object.key==&stuff) object.transform[12]+=2.f;
    b.update_local_light();
    SLRR_REQUIRE(b.fixture_lights_.size()==fixtures.size());
    for(size_t i=0;i<fixtures.size();++i)
      SLRR_REQUIRE(std::abs(b.fixture_lights_[i].position[0]-fixtures[i].position[0]-2.f)<.001f);
    for(auto& object:b.instances_) if(object.key==&stuff) object.transform[12]-=2.f;
    scene_instances_select(nullptr,nullptr,false,SceneEnvironment::Interior);
    b.update_local_light();SLRR_REQUIRE(b.fixture_lights_.empty());
    scene_instances_select(&root,nullptr,false,SceneEnvironment::Interior);
    host_enhanced_graphics().local_lights=false;b.update_local_light();
    SLRR_REQUIRE(b.fixture_lights_.empty());
    host_enhanced_graphics()=graphics;
    for(int quality=0;quality<3;++quality) {
      host_enhanced_graphics().shadow_quality=quality;
      b.update_interior_sun(video);b.draw_previews();bgfx::frame();
      SLRR_REQUIRE(b.interior_sun_size_==(512u<<quality));
      SLRR_REQUIRE(bgfx::isValid(b.interior_sun_fb_));
    }
    host_enhanced_graphics()=graphics;
    b.fixture_shadow_active_=false;
    gameref_remove(&neon);
    bgfx::destroy(readback);
    bgfx::destroy(fb);
    auto children = gameref_get(&root)->children;
    for (auto child : children)
      gameref_remove(child);
    gameref_remove(&root);
    scene_instances_set_resources(nullptr);
    b.instances_.clear();
    b.garage_ = false;
    b.point_light_.reset();
    b.local_light_ = {};
  }
  // #235: the three club garages. Garage.java places one SpotLight per
  // twin-tube fitting of the stuff mesh. The loader must turn exactly the tube
  // faces into unlit emitters (they stay white without any light), each anchor
  // must sit 8 cm under them, the registry must place the spots there, and the
  // fittings must light the real floor at Garage.java's brightness.
  static void garage_fixtures(LibBgfxBackend &b) {
    struct Room { uint32_t ground, stuff; const char* lamps; std::vector<std::array<float,3>> anchors; };
    const Room rooms[] = {
      {0x7a, 0x42, "texturak", {{-14.05739f,6.43138f,5.03007f},{-8.68388f,6.43138f,5.03007f},
        {-3.03047f,6.43138f,5.03007f},{2.63350f,6.43138f,5.03007f},{8.82223f,6.43138f,5.03007f},
        {5.77201f,6.43138f,-4.00419f},{-5.88736f,6.43138f,-4.00419f},
        {12.71575f,6.43138f,1.23414f},{12.71575f,6.43138f,-5.19158f}}},
      {0x06, 0x18, "neon", {{-7.04596f,5.42426f,-3.26745f},{-7.04596f,5.42426f,4.32187f},
        {0.02651f,5.42426f,4.32187f},{0.02651f,5.42426f,-3.26745f},{7.12191f,5.42426f,4.32187f},
        {7.12191f,5.42426f,-3.26745f},{14.09471f,5.42426f,-3.26745f},{14.09471f,5.42426f,4.32187f},
        {-13.63311f,5.42426f,-3.26745f},{-13.63311f,5.42426f,4.32187f}}},
      {0xc7, 0x3c, "Material #11345", {{-12.53611f,5.69371f,-0.75343f},{20.72244f,5.52448f,6.01213f},
        {-6.72907f,6.41467f,9.38976f},{20.72244f,5.53801f,10.85484f},{-12.53611f,5.69371f,-6.80128f},
        {-0.08248f,6.43135f,-3.24062f},{-0.08248f,6.43135f,1.71936f},{3.34185f,6.41467f,9.38976f},
        {9.71543f,6.43135f,-6.30282f},{9.71543f,6.43135f,-1.04061f},{13.13120f,6.41467f,9.38976f},
        {20.72244f,5.55018f,1.16555f}}}};
    for (const auto& room : rooms) {
      Fixture f;
      f.packs.set_root(fs::absolute("game").string());
      f.packs.open_all();
      scene_instances_set_resources(&f.packs);
      const auto pack = f.packs.open_lib("misc/garage.rpk");
      Object root, stuff;
      root.class_name = "java.util.resource.GroundRef";
      gameref_upsert(&root, GameRefKind::GroundRef)->type_id = RpkIndex::make_full_id(pack, room.ground);
      spawn_ground_shell(f.vm, f.packs, &root);
      gameref_upsert(&stuff, GameRefKind::RenderRef)->type_id = RpkIndex::make_full_id(pack, room.stuff);
      gameref_set_parent(&stuff, &root);
      scene_instances_select(&root, nullptr, false, SceneEnvironment::Interior);
      b.instances_ = scene_instances_collect();
      const ScxGeometry* mesh = nullptr;
      for (const auto& obj : b.instances_) if (obj.key == &stuff) mesh = obj.geometry.get();
      SLRR_REQUIRE(mesh);
      // The loader's adapter finds the same fittings as Garage.java.
      SLRR_REQUIRE(mesh->ceiling_light_anchors.size() == room.anchors.size());
      for (const auto& anchor : room.anchors) {
        bool found = false;
        for (const auto& a : mesh->ceiling_light_anchors)
          found |= std::abs(a[0] - anchor[0]) < 1e-4f && std::abs(a[1] - anchor[1]) < 1e-4f &&
                   std::abs(a[2] - anchor[2]) < 1e-4f;
        SLRR_REQUIRE(found);
      }
      // Only faces of the named lamp group become emitters, two tubes each.
      size_t emitting = 0;
      for (const auto& group : mesh->groups) if (group.self_illuminated) {
        SLRR_REQUIRE(group.name == room.lamps);
        emitting += group.index_count / 3;
      }
      SLRR_REQUIRE(emitting >= room.anchors.size() * 2 * 12);
      // Derivation check: the emitting tube faces lie 8 cm above each anchor,
      // along a 3 m tube through it.
      for (const auto& anchor : room.anchors) {
        float lowest = 1e9f;
        for (const auto& group : mesh->groups) if (group.self_illuminated)
          for (uint32_t i = group.index_start; i < group.index_start + group.index_count; ++i) {
            const auto v = mesh->indices[i] * 3;
            const float dx = std::abs(mesh->positions[v] * mesh->position_scale - anchor[0]);
            const float dz = std::abs(mesh->positions[v + 2] * mesh->position_scale - anchor[2]);
            if (std::max(dx, dz) < 1.6f && std::min(dx, dz) < .2f)
              lowest = std::min(lowest, mesh->positions[v + 1] * mesh->position_scale);
          }
        SLRR_REQUIRE(std::abs(lowest - .08f - anchor[1]) < .002f);
      }
      std::vector<Object> lights(room.anchors.size());
      // Garage.java's brightness for every garage (the first version used the
      // registry defaults, 24 and 20 m).
      constexpr float intensity = 30.f, range = 26.f;
      for (size_t i = 0; i < lights.size(); ++i) {
        SLRR_REQUIRE(script_lights_create(&lights[i], &stuff, ScriptLightKind::Spot));
        auto* light = script_lights_get(&lights[i]);
        light->position = room.anchors[i];
        light->intensity = intensity;
        light->range = range;
      }
      for (const auto& obj : b.instances_) b.cache_scene_mesh(obj.geometry, nullptr, false);
      b.garage_ = true; b.outdoor_lighting_ = false;
      const auto graphics = host_enhanced_graphics();
      host_enhanced_graphics().local_lights = true;
      b.update_local_light();
      SLRR_REQUIRE(b.fixture_lights_.size() == room.anchors.size());
      for (size_t i = 0; i < lights.size(); ++i) {
        const auto& light = b.fixture_lights_[i];
        SLRR_REQUIRE(light.owner == &lights[i] && light.valid() && light.direction[1] < -.99f &&
                     light.intensity == intensity && light.range == range);
        SLRR_REQUIRE(std::abs(light.position[0] - room.anchors[i][0]) < 1e-3f &&
                     std::abs(light.position[1] - room.anchors[i][1]) < 1e-3f &&
                     std::abs(light.position[2] + room.anchors[i][2]) < 1e-3f);
      }
      // The fittings light the actual floor of this garage.
      auto floor = std::make_shared<ScxGeometry>(*b.instances_.front().geometry);
      floor->indices.clear(); floor->groups.clear();
      const auto& source = *b.instances_.front().geometry;
      for (auto group : source.groups) {
        const auto first = group.index_start, count = group.index_count;
        group.index_start = uint32_t(floor->indices.size());
        for (uint32_t i = first; i + 2 < first + count; i += 3) {
          bool flat = true;
          for (int k = 0; k < 3; ++k) {
            const auto v = source.indices[i + k] * 3;
            flat &= std::abs(source.positions[v + 1] * source.position_scale) < .3f && source.normals[v + 1] > .5f;
          }
          if (flat) floor->indices.insert(floor->indices.end(), source.indices.begin() + i, source.indices.begin() + i + 3);
        }
        group.index_count = uint32_t(floor->indices.size()) - group.index_start;
        if (group.index_count) floor->groups.push_back(group);
      }
      SLRR_REQUIRE(!floor->indices.empty());
      b.cache_scene_mesh(floor, nullptr, false);
      SceneInstance receiver;
      receiver.geometry = floor;
      bx::mtxIdentity(receiver.transform.data());
      receiver.lighting.set = receiver.lighting.has_direction = true;
      for (int i = 0; i < 3; ++i) { receiver.lighting.diffuse[i] = .8f; receiver.lighting.ambient[i] = .1f; }
      constexpr uint16_t size = 256;
      const auto color = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
      const auto depth = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT);
      const bgfx::TextureHandle attachments[] = {color, depth};
      const auto fb = bgfx::createFrameBuffer(2, attachments, true);
      const auto readback = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8,
                                                  BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
      float identity[16];
      bx::mtxIdentity(identity);
      float projection[16]{};
      projection[0] = projection[9] = .04f;
      projection[6] = .01f;
      projection[14] = .5f;
      projection[15] = 1;
      auto capture = [&]() {
        b.static_shadow_active_ = false;
        b.fixture_shadow_active_ = false;
        bgfx::setViewFrameBuffer(5, fb);
        bgfx::setViewRect(5, 0, 0, size, size);
        bgfx::setViewClear(5, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, uint32_t(0xff), 0.f);
        bgfx::setViewTransform(5, identity, projection);
        b.draw_instance(5, receiver, identity, false, 0);
        bgfx::TextureRegion dst, src;
        dst.init(readback);
        src.init(color);
        bgfx::blit(100, dst, src);
        bgfx::frame();
        std::vector<uint8_t> bytes(size * size * 4);
        auto ready = bgfx::read(dst, bytes.data());
        for (int i = 0; i < 16; ++i)
          if (bgfx::frame() >= ready) return bytes;
        throw std::runtime_error("garage fixture readback timeout");
      };
      const auto lit = capture();
      const auto fixtures = b.fixture_lights_;
      b.fixture_lights_.clear();
      const auto dark = capture();
      // Previous brightness, for the before/after report.
      b.fixture_lights_ = fixtures;
      for (auto& light : b.fixture_lights_) { light.intensity = 24.f; light.range = 20.f; }
      const auto before = capture();
      b.fixture_lights_ = fixtures;
      // 8-bit captures clip under the fittings, so also report the floor that
      // was dim before (gain under 60), where the change is not clipped.
      unsigned pixels = 0, floor_pixels = 0, dim_pixels = 0;
      double gain = 0, gain_before = 0, dim = 0, dim_before = 0;
      for (size_t i = 0; i < lit.size(); i += 4) {
        if (!dark[i] && !dark[i + 1] && !dark[i + 2]) continue; // black clear colour
        ++floor_pixels;
        const int now = int(lit[i]) - int(dark[i]), old = int(before[i]) - int(dark[i]);
        gain += now;
        gain_before += old;
        if (old < 60) { ++dim_pixels; dim += now; dim_before += old; }
        if (now > 10) ++pixels;
      }
      gain /= std::max(floor_pixels, 1u);
      gain_before /= std::max(floor_pixels, 1u);
      dim /= std::max(dim_pixels, 1u);
      dim_before /= std::max(dim_pixels, 1u);
      std::printf("GARAGE 0x%x script fittings=%zu lit floor pixels=%u/%u mean floor gain 24/20m=%.1f 30/26m=%.1f;"
                  " dim floor (%u px) %.1f -> %.1f\n", room.ground, fixtures.size(), pixels, floor_pixels,
                  gain_before, gain, dim_pixels, dim_before, dim);
      SLRR_REQUIRE(pixels > 100 && gain >= gain_before);
      SLRR_REQUIRE(!dim_pixels || dim > dim_before * 1.15);
      // The tube faces keep their texture without any light (#235): white.
      auto tubes = std::make_shared<ScxGeometry>(*mesh);
      tubes->indices.clear(); tubes->groups.clear();
      for (auto group : mesh->groups) if (group.self_illuminated) {
        const auto first = group.index_start;
        group.index_start = uint32_t(tubes->indices.size());
        tubes->indices.insert(tubes->indices.end(), mesh->indices.begin() + first,
                              mesh->indices.begin() + first + group.index_count);
        tubes->groups.push_back(group);
      }
      b.cache_scene_mesh(tubes, nullptr, false);
      receiver.geometry = tubes;
      b.fixture_lights_.clear();
      b.point_light_.reset();
      const auto tubes_off = capture();
      unsigned white = 0;
      for (size_t i = 0; i < tubes_off.size(); i += 4)
        if (tubes_off[i] > 180 && tubes_off[i + 1] > 180 && tubes_off[i + 2] > 180) ++white;
      std::printf("GARAGE 0x%x emitting tube triangles=%zu white tube pixels without lights=%u\n",
                  room.ground, emitting, white);
      SLRR_REQUIRE(white > 10);
      host_enhanced_graphics() = graphics;
      bgfx::destroy(readback);
      bgfx::destroy(fb);
      for (auto& light : lights) script_lights_destroy(&light);
      auto children = gameref_get(&root)->children;
      for (auto child : children) gameref_remove(child);
      gameref_remove(&root);
      scene_instances_set_resources(nullptr);
      b.instances_.clear();
      b.fixture_lights_.clear();
      b.garage_ = false;
      b.outdoor_lighting_ = true;
      b.point_light_.reset();
      b.local_light_ = {};
    }
  }
  static void addon_bounds(LibBgfxBackend& b) {
    if (!fs::exists("game/maps/Addon_Maps/Drag_Track.rpk")) {
      std::puts("SKIP optional Drag Track asset regression: addon not installed");
      return;
    }
    Fixture f;
    f.packs.set_root(fs::absolute("game").string());
    f.packs.open_all();
    scene_instances_set_resources(&f.packs);
    const auto id=RpkIndex::make_full_id(
        f.packs.open_lib("maps/Addon_Maps/Drag_Track.rpk"),1);
    Object root;
    root.class_name="java.util.resource.GroundRef";
    gameref_upsert(&root,GameRefKind::GroundRef)->type_id=id;
    spawn_ground_shell(f.vm,f.packs,&root);
    scene_instances_select(&root,nullptr,false,SceneEnvironment::Exterior);
    scene_sun_set_time(13*3600.f);
    b.instances_=scene_instances_collect();
    SLRR_REQUIRE(!b.instances_.empty());
    const auto& instance=b.instances_.front();
    const auto& g=*instance.geometry;
    SLRR_REQUIRE(g.ground_polygons && g.positions.size()>200000);
    for(int axis=0;axis<3;++axis) {
      float minimum=g.positions[axis],maximum=minimum;
      for(size_t i=axis;i<g.positions.size();i+=3) {
        minimum=std::min(minimum,g.positions[i]);
        maximum=std::max(maximum,g.positions[i]);
      }
      SLRR_REQUIRE(g.info.bbox_min[axis]==minimum && g.info.bbox_max[axis]==maximum);
    }
    const float eye[3]={27,2,-36};
    const auto light=scene_instances_light();
    const float focus[3]={eye[0],eye[1],-eye[2]};
    const float dir[3]={light.direction[0],light.direction[1],-light.direction[2]};
    for(float radius:shadow_radii) {
      const auto p=shadow_projection(focus,dir,radius,2048,false,false);
      const auto fr=b.view_frustum(p.view.data(),p.projection.data());
      SLRR_REQUIRE(shadow_bounds_visible(g,instance.transform,fr));
    }
    VideoOptions video;video.shadows=2;
    b.garage_=false;b.local_light_={};
    b.update_static_shadow(eye,video);bgfx::frame();
    SLRR_REQUIRE(b.static_shadow_active_ && b.shadow_meshes_.contains(&g));
    const auto children=gameref_get(&root)->children;
    for(auto child:children)gameref_remove(child);
    gameref_remove(&root);
    scene_instances_set_resources(nullptr);b.instances_.clear();
    std::puts("PASS real Drag Track: decoded bounds match vertices and retain map in all four shadow levels");
  }
  static void scene_environment(LibBgfxBackend &b) {
    Fixture f;
    scene_instances_set_resources(&f.packs);
    Object world, sky, mesh, light, unrelated, other_light, section;
    gameref_upsert(&world)->flags = kWorldTreeRoot;
    gameref_upsert(&sky, GameRefKind::RenderRef);
    gameref_set_parent(&sky, &world);
    auto geometry = std::make_shared<ScxGeometry>();
    geometry->info.ok = true;
    geometry->info.version = 4;
    geometry->position_scale = 1;
    geometry->positions = {-1, 0, 0, 1, 0, 0, 0, 2, 0};
    geometry->normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    geometry->uvs = {0, 0, 1, 0, .5f, 1};
    geometry->indices = {0, 1, 2};
    geometry->info.bbox_min[0] = -1;
    geometry->info.bbox_max[0] = 1;
    geometry->info.bbox_max[1] = 2;
    ScxGroup group;
    group.index_count = 3;
    geometry->groups.push_back(group);
    gameref_upsert(&mesh, GameRefKind::RenderRef)->line_geometry = geometry;
    gameref_set_parent(&mesh, &world);
    auto make_light = [](Object *key, Object *root, float red) {
      auto *node = gameref_upsert(key, GameRefKind::RenderRef);
      node->type_id = 1;
      node->directional_light.set = node->directional_light.has_direction = true;
      node->directional_light.diffuse[0] = red;
      node->directional_light.direction[0] = .2f;
      node->directional_light.direction[1] = -.8f;
      node->directional_light.direction[2] = .3f;
      gameref_set_parent(key, root);
      gameref_make_live(key);
      gameref_register_light(key);
    };
    make_light(&light, &world, .25f);
    gameref_upsert(&unrelated)->flags = kWorldTreeRoot;
    make_light(&other_light, &unrelated, .9f);
    section.fields["map"] = Value::make_obj(&world);
    section.fields["skydome"] = Value::make_obj(&sky);
    section.fields["internalScene"] = Value::make_int(1);
    f.sections.adopt_actual(&section);
    mainloop_sync_section_render(b, f.sections);
    SLRR_REQUIRE(!scene_instances_has_ground_props() && !scene_instances_outdoor());
    scene_sun_set_time(12 * 3600.f);
    auto indoor = scene_instances_light();
    SLRR_REQUIRE(indoor.diffuse[0] == .25f && indoor.direction[0] == .2f);
    scene_sun_set_time(0);
    SLRR_REQUIRE(scene_instances_light().direction[0] == .2f);
    // A skydome also exists in internalScene: it must not enable sunlight.
    VideoOptions video;
    video.shadows = 2;
    const float eye[3] = {0, 1, 4};
    b.instances_ = scene_instances_collect();
    b.update_static_shadow(eye, video);
    SLRR_REQUIRE(!b.static_shadow_active_);
    // CarMarket inherits an unassigned int: the host object is sparse, but
    // Scene.addSceneElements has created its sun. Exercise that real layout.
    section.fields.erase("internalScene");
    section.fields["sun"] = Value::make_obj(&light);
    mainloop_sync_section_render(b, f.sections);
    scene_sun_set_time(12 * 3600.f);
    SLRR_REQUIRE(scene_instances_outdoor());
    SLRR_REQUIRE(scene_instances_light().direction[1] < -.8f);
    // No main-view ground or even visible instances are needed to find the sun.
    b.instances_.clear();
    b.update_static_shadow(eye, video);
    bgfx::frame();
    SLRR_REQUIRE(b.static_shadow_active_ && b.shadow_cascades_active_);
    SLRR_REQUIRE(b.shadow_meshes_.contains(geometry.get()));
    // Garage uses the authored downwards neon for direct shading/visibility,
    // and furniture must cast too, not only vehicle_material instances.
    section.fields["internalScene"] = Value::make_int(1);
    section.class_name = "java.game.Garage";
    mainloop_sync_section_render(b, f.sections);
    b.instances_ = scene_instances_collect();
    b.cache_scene_mesh(geometry, nullptr, false);
    b.local_light_ = {0, 3.3f, 0, 32};
    PointLight fixture_light;
    fixture_light.position = {0, 3.3f, 0};
    b.point_light_ = fixture_light;
    b.update_static_shadow(eye, video);
    bgfx::frame();
    SLRR_REQUIRE(b.static_shadow_active_ && b.static_shadow_local_ &&
                 b.point_shadow_active_);
    b.garage_ = false;
    b.point_light_.reset();
    b.local_light_ = {};
    b.instances_.clear();
    scene_instances_select(&world, &sky, true, SceneEnvironment::Exterior);
    SLRR_REQUIRE(!scene_instances_outdoor());
    b.update_static_shadow(eye, video);
    SLRR_REQUIRE(!b.static_shadow_active_);
    scene_instances_select(&world, &sky, false);
    SLRR_REQUIRE(!scene_instances_outdoor());
    f.sections.adopt_actual(nullptr);
    for (Object *object : {&other_light, &unrelated, &light, &mesh, &sky, &world})
      gameref_remove(object);
    scene_instances_set_resources(nullptr);
    std::puts("PASS scene intent: mesh-only exterior shadows, interior clock/sky "
              "isolation, root-scoped lights, preview and automatic fallback");
  }

  static void real_world_cache(LibBgfxBackend &b) {
    Fixture f;
    f.packs.set_root(fs::absolute("game").string());
    f.packs.open_all();
    scene_instances_set_resources(&f.packs);
    const auto id =
        RpkIndex::make_full_id(f.packs.open_lib("maps/test_track.rpk"), 1);
    auto *root = f.vm.new_object("java.util.resource.GroundRef");
    gameref_upsert(root, GameRefKind::GroundRef)->type_id = id;
    spawn_ground_shell(f.vm, f.packs, root);
    scene_instances_select(root, nullptr, false);
    scene_sun_set_time(13 * 3600.f);
    b.instances_ = scene_instances_collect();
    b.garage_ = false;
    b.point_light_.reset();
    b.local_light_ = {};
    VideoOptions video;
    video.shadows = 2;
    const float eye[3] = {1000, 5, -2000};
    b.update_static_shadow(eye, video);
    bgfx::frame();
    SLRR_REQUIRE(b.static_shadow_active_);
    // One stable redraw establishes the caster snapshot after moving light.
    b.update_static_shadow(eye,video);bgfx::frame();
    SLRR_REQUIRE(b.shadow_cache_valid_);
    auto revision = b.shadow_atlas_revision_;
    b.update_static_shadow(eye, video);
    bgfx::frame();
    SLRR_REQUIRE(b.shadow_atlas_revision_ == revision);
    scene_sun_set_time(scene_sun_time() + .01f);
    b.update_static_shadow(eye, video);
    bgfx::frame();
    SLRR_REQUIRE(b.shadow_atlas_revision_ == revision + 1);
    revision = b.shadow_atlas_revision_;
    const auto children = gameref_get(root)->children;
    SLRR_REQUIRE(!children.empty());
    auto *shell = gameref_get(children.front());
    shell->local_matrix[12] += .25f;
    shell->matrix_override = true;
    b.update_static_shadow(eye, video);
    bgfx::frame();
    SLRR_REQUIRE(b.shadow_atlas_revision_ == revision + 1);
    revision = b.shadow_atlas_revision_;
    for (auto &object : b.instances_)
      if (object.geometry->ground_polygons) {
        // The fixture's scene cache owns mutable geometry behind this const
        // view.
        ++std::const_pointer_cast<ScxGeometry>(object.geometry)->revision;
        break;
      }
    b.update_static_shadow(eye, video);
    bgfx::frame();
    SLRR_REQUIRE(b.shadow_atlas_revision_ == revision + 1);
    for (auto child : children)
      gameref_remove(child);
    gameref_remove(root);
    scene_instances_set_resources(nullptr);
    b.instances_.clear();
    std::puts("PASS real map: unchanged atlas reused, small sun/caster motion "
              "invalidates");
  }

  static void run(LibBgfxBackend &b) {
    constexpr uint16_t size = 64;
    const auto color = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
    const auto depth = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT);
    const bgfx::TextureHandle attachments[] = {color, depth};
    const auto output = bgfx::createFrameBuffer(2, attachments, true);
    const auto readback =
        bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    const auto shadow = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::D32F,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    SLRR_REQUIRE(bgfx::isValid(output) && bgfx::isValid(readback) &&
                 bgfx::isValid(shadow));
    b.static_shadow_fb_ = bgfx::createFrameBuffer(1, &shadow, true);
    SLRR_REQUIRE(bgfx::isValid(b.static_shadow_fb_));
    b.static_shadow_tex_ = shadow;
    b.static_shadow_size_ = size;
    b.static_shadow_distance_ = 100;
    b.static_shadow_eye_ = {0, 0, 0};
    b.debug_overlay_.render_options().map_shadows = true;
    b.outdoor_lighting_ = true;
    float identity[16];
    bx::mtxIdentity(identity);
    bx::mtxIdentity(b.static_shadow_matrix_.data());
    b.static_shadow_matrix_[0] = b.static_shadow_matrix_[5] = .1f;
    b.static_shadow_matrix_[12] = b.static_shadow_matrix_[13] = .5f;
    auto geo = std::make_shared<ScxGeometry>();
    geo->info.ok = true;
    geo->info.version = 4;
    geo->position_scale = 1;
    geo->positions = {-1, -1, -.5f, 3, -1, -.5f, -1, 3, -.5f};
    geo->normals = {0, 0, -1, 0, 0, -1, 0, 0, -1};
    geo->uvs = {0, 0, 1, 0, 0, 1};
    geo->uv2 = geo->uv3 = geo->uvs;
    geo->vertex_colors = {0xffffffff, 0xffffffff, 0xffffffff};
    geo->indices = {0, 1, 2};
    ScxGroup group;
    group.index_count = 3;
    group.material_flags = 0x02000000u;
    group.diffuse[0] = group.diffuse[1] = group.diffuse[2] = group.diffuse[3] =
        1;
    group.specular[0] = group.specular[1] = group.specular[2] = 0;
    geo->groups.push_back(group);
    b.cache_scene_mesh(geo, nullptr, false);
    SceneInstance instance;
    instance.geometry = geo;
    instance.lighting.set = instance.lighting.has_direction = true;
    instance.lighting.direction[0] = instance.lighting.direction[1] = 0;
    instance.lighting.direction[2] =
        1; // CPU converts travel to +Z toward-light.
    for (int i = 0; i < 3; ++i) {
      instance.lighting.ambient[i] = .1f;
      instance.lighting.diffuse[i] = .6f;
      instance.lighting.specular[i] = 0;
    }
    int darkTile = -1;
    int stripeX = -1;
    bool sloped_self_receiver=false;
    auto pixel = [&](bool vehicle, bool enabled, float shadowDepth,
                     uint16_t view, bool preview = false, bool lamp = false) {
      instance.vehicle_material = vehicle;
      instance.light_flags = lamp ? 1 : 0;
      b.static_shadow_active_ = enabled;
      if (b.shadow_cascades_active_) {
        for (size_t i = 0; i < 4; ++i) {
          const auto id = shadow_views[i];
          bgfx::setViewFrameBuffer(id, b.static_shadow_fb_);
          bgfx::setViewRect(id, uint16_t(i % 2 * size), uint16_t(i / 2 * size),
                            size, size);
          bgfx::setViewClear(id, BGFX_CLEAR_DEPTH, uint32_t(0),
                             int(i) == darkTile ? shadowDepth : .9f);
          bgfx::touch(id);
        }
      } else {
        bgfx::setViewFrameBuffer(0, b.static_shadow_fb_);
        bgfx::setViewRect(0, 0, 0, size, size);
        bgfx::setViewClear(0, BGFX_CLEAR_DEPTH, uint32_t(0), shadowDepth);
        bgfx::touch(0);
      }
      if(sloped_self_receiver) {
        float sloped_projection[16];bx::mtxIdentity(sloped_projection);
        sloped_projection[2]=.2f;sloped_projection[6]=.15f;sloped_projection[10]=0;sloped_projection[14]=.5f;
        if(bgfx::getCaps()->homogeneousDepth) for(int row:{2,6,10,14}) sloped_projection[row]=2*sloped_projection[row]-sloped_projection[row+1];
        bgfx::setViewTransform(0,identity,sloped_projection);
        const auto& part=b.instance_meshes_.at(geo.get());
        const float params[4]={0,-1,0,0};bgfx::setUniform(b.u_static_shadow_params_,params);
        const float opaque[4]{};bgfx::setUniform(b.u_alpha_test_,opaque);bgfx::setUniform(b.u_aperture_params_,opaque);
        bgfx::setTransform(identity);bgfx::setVertexBuffer(0,part.vbh);bgfx::setIndexBuffer(part.ibh);
        bgfx::setState(BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(0,b.static_shadow_program_);
      }
      if (stripeX >= 0) {
        bgfx::setViewFrameBuffer(1, b.static_shadow_fb_);
        bgfx::setViewRect(1, uint16_t(stripeX), 0, 1, size);
        bgfx::setViewClear(1, BGFX_CLEAR_DEPTH, uint32_t(0), .2f);
        bgfx::touch(1);
      }
      bgfx::setViewFrameBuffer(view, output);
      bgfx::setViewRect(view, 0, 0, size, size);
      bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                         uint32_t(0x000000ff), 0.f);
      bgfx::setViewTransform(view, identity, identity);
      bgfx::touch(view);
      b.draw_instance(view, instance, identity, preview, 0);
      bgfx::TextureRegion dst, src;
      dst.init(readback);
      src.init(color);
      bgfx::blit(100, dst, src);
      bgfx::frame();
      std::vector<uint8_t> bytes(size * size * 4);
      const auto ready = bgfx::read(dst, bytes.data());
      bool completed = false;
      for (int i = 0; i < 16; ++i)
        if (bgfx::frame() >= ready) {
          completed = true;
          break;
        }
      SLRR_REQUIRE(completed);
      const auto pos = (size / 2 * size + size / 2) * 4;
      return int(bytes[pos]) + bytes[pos + 1] + bytes[pos + 2];
    };
    const auto lit = pixel(true, true, .9f, 2);
    const auto shaded = pixel(true, true, .2f, 2);
    const auto disabled = pixel(true, false, .2f, 2);
    const auto preview = pixel(true, true, .2f, 2, true);
    const auto lamp = pixel(true, true, .2f, 2, false, true);
    const auto meshLit = pixel(false, true, .9f, 2);
    const auto meshShadow = pixel(false, true, .2f, 2);
    const auto captureShadow = pixel(false, true, .2f, 72);
    b.garage_ = true;
    b.outdoor_lighting_ = false;
    b.static_shadow_local_ = false;
    const auto garageLit = pixel(false, true, .9f, 2);
    const auto garageShadow = pixel(false, true, .2f, 2);
    const auto garageCapture = pixel(false, true, .2f, 72);
    std::printf("Garage authored light sums: lit=%d shadow=%d capture=%d\n",
                garageLit, garageShadow, garageCapture);
    SLRR_REQUIRE(garageShadow * 2 < garageLit && garageCapture == garageShadow);
    b.garage_ = false;
    b.outdoor_lighting_ = true;
    b.static_shadow_local_ = true;
    const auto unrelatedSun = pixel(true, true, .2f, 2);
    b.local_light_ = {0, 0, 3, 8};
    const auto localLit = pixel(true, true, .9f, 2);
    const auto localShadow = pixel(true, true, .2f, 2);
    const auto localMeshLit = pixel(false, true, .9f, 2);
    const auto localMeshShadow = pixel(false, true, .2f, 72);
    std::printf("Local light sums: sun unaffected=%d vehicle lit=%d shadow=%d "
                "scenery lit=%d capture shadow=%d\n",
                unrelatedSun, localLit, localShadow, localMeshLit,
                localMeshShadow);
    SLRR_REQUIRE(std::abs(unrelatedSun - lit) <= 3);
    SLRR_REQUIRE(localLit > localShadow + 20 &&
                 std::abs(localShadow - lit) <= 3);
    SLRR_REQUIRE(localMeshLit > localMeshShadow + 20 &&
                 std::abs(localMeshShadow - meshLit) <= 3);
    std::printf("RGB sums: vehicle lit=%d shadow=%d disabled=%d preview=%d "
                "lamp=%d; scenery lit=%d shadow=%d capture=%d\n",
                lit, shaded, disabled, preview, lamp, meshLit, meshShadow,
                captureShadow);
    SLRR_REQUIRE(lit > 300 && shaded > 30 && shaded < lit / 2);
    SLRR_REQUIRE(std::abs(disabled - lit) <= 3 &&
                 std::abs(preview - lit) <= 3 && lamp > lit);
    SLRR_REQUIRE(meshLit > 300 && meshShadow > 30 && meshShadow < meshLit / 2);
    SLRR_REQUIRE(std::abs(captureShadow - meshShadow) <= 3);
    // PCF compares depths before interpolation. Sampling interpolated depth
    // and then step() produced binary popping at sub-texel car-shadow edges.
    b.static_shadow_local_ = false;
    b.local_light_ = {};
    b.car_depth_active_ = true;
    b.car_depth_tex_ = shadow;
    b.car_depth_matrix_ = b.static_shadow_matrix_;
    stripeX = 32;
    std::vector<int> carEdge;
    for (int i = 0; i < 12; ++i) {
      b.car_depth_matrix_[12] = .5f + float(i) * .08f / size;
      carEdge.push_back(pixel(false, false, .9f, 5));
    }
    auto uniqueCarEdge = carEdge;
    std::sort(uniqueCarEdge.begin(), uniqueCarEdge.end());
    uniqueCarEdge.erase(std::unique(uniqueCarEdge.begin(), uniqueCarEdge.end()),
                        uniqueCarEdge.end());
    std::printf("Car shadow sub-texel edge: %zu distinct coverage values\n",
                uniqueCarEdge.size());
    SLRR_REQUIRE(uniqueCarEdge.size() > 6);
    stripeX = -1;
    b.car_depth_active_ = false;
    b.car_depth_tex_ = BGFX_INVALID_HANDLE;
    // A depth edge moving by fractions of a shadow texel must not snap,
    // even in Sharp. Very Soft must retain fractional phase in its wider tent.
    const auto saved_filters=host_enhanced_graphics();
    b.fixture_shadow_tex_=shadow;b.fixture_shadow_active_=true;b.fixture_shadow_tile_=size/4;
    SpotLight probe;probe.position={0,0,5};probe.direction={0,0,-1};probe.intensity=2;
    b.fixture_lights_={probe};
    b.fixture_shadow_matrices_={};
    auto& fixture_matrix=b.fixture_shadow_matrices_[0];
    fixture_matrix[13]=.5f;fixture_matrix[14]=.5f;fixture_matrix[15]=1;
    stripeX=size/8;
    for(int filter=0;filter<3;++filter) {
      host_enhanced_graphics().shadow_smoothing=filter;
      std::vector<int> samples;int jump=0;
      for(int step=0;step<=32;++step) {
        fixture_matrix[12]=.5f+(-1.f+step/32.f)/(size/4);
        const int value=pixel(false,false,.9f,5);
        if(!samples.empty()) jump=std::max(jump,std::abs(value-samples.back()));
        samples.push_back(value);
      }
      auto distinct=samples;std::sort(distinct.begin(),distinct.end());
      distinct.erase(std::unique(distinct.begin(),distinct.end()),distinct.end());
      std::printf("Fixture filter %d fractional edge: %zu levels, max step %d\n",filter,distinct.size(),jump);
      SLRR_REQUIRE(distinct.size()>(filter==2?5:8) && jump<=8);
    }
    host_enhanced_graphics()=saved_filters;stripeX=-1;
    b.fixture_lights_.clear();b.fixture_shadow_active_=false;b.fixture_shadow_tex_=BGFX_INVALID_HANDLE;
    const auto flat_matrix=b.static_shadow_matrix_;
    bx::mtxIdentity(b.static_shadow_matrix_.data());
    b.static_shadow_matrix_[0]=.5f;b.static_shadow_matrix_[5]=bgfx::getCaps()->originBottomLeft?.5f:-.5f;
    b.static_shadow_matrix_[2]=.2f;b.static_shadow_matrix_[6]=.15f;b.static_shadow_matrix_[10]=0;
    b.static_shadow_matrix_[12]=b.static_shadow_matrix_[13]=b.static_shadow_matrix_[14]=.5f;
    sloped_self_receiver=true;
    for(int filter=0;filter<3;++filter) {
      host_enhanced_graphics().shadow_smoothing=filter;
      const auto self=pixel(false,true,1.f,5);
      std::printf("Outdoor grazing plane filter %d: lit=%d self=%d\n",filter,meshLit,self);
      SLRR_REQUIRE(std::abs(self-meshLit)<=3);
    }
    sloped_self_receiver=false;b.static_shadow_matrix_=flat_matrix;
    host_enhanced_graphics()=saved_filters;
    // Distinct depth in each tile detects wrong atlas selection or clears
    // accidentally overwriting other view rectangles on D3D11.
    bgfx::destroy(b.static_shadow_fb_);
    const auto atlas = bgfx::createTexture2D(
        size * 2, size * 2, false, 1, bgfx::TextureFormat::D32F,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    b.static_shadow_fb_ = bgfx::createFrameBuffer(1, &atlas, true);
    b.static_shadow_tex_ = atlas;
    b.static_shadow_size_ = size * 2;
    b.shadow_cascades_active_ = true;
    b.static_shadow_local_ = false;
    b.local_light_ = {};
    for (auto &matrix : b.shadow_cascade_matrices_)
      matrix = b.static_shadow_matrix_;
    const float distances[4] = {10, 70, 220, 700};
    for (int i = 0; i < 4; ++i) {
      darkTile = i;
      b.static_shadow_eye_ = {-distances[i], 0, 0};
      const auto dark = pixel(true, true, .2f, 5);
      const auto light = pixel(true, true, .9f, 5);
      const auto captured = pixel(false, true, .2f, 72);
      std::printf("Cascade %d distance %.0fm: lit=%d shadow=%d capture=%d\n", i,
                  distances[i], light, dark, captured);
      SLRR_REQUIRE(light > 300 && dark > 30 && dark < light / 2 &&
                   std::abs(captured - dark) <= 3);
    }
    darkTile = 0;
    b.static_shadow_eye_ = {-27.2f, 0, 0};
    const auto transitionStart = pixel(true, true, .2f, 5);
    b.static_shadow_eye_ = {-29.6f, 0, 0};
    const auto transitionMid = pixel(true, true, .2f, 5);
    b.static_shadow_eye_ = {-32.1f, 0, 0};
    const auto transitionEnd = pixel(true, true, .2f, 5);
    SLRR_REQUIRE(transitionStart < transitionMid &&
                 transitionMid < transitionEnd);
    darkTile = 3;
    b.static_shadow_eye_ = {-950, 0, 0};
    const auto fade = pixel(true, true, .2f, 5);
    b.static_shadow_eye_ = {-1001, 0, 0};
    const auto beyond = pixel(true, true, .2f, 5);
    SLRR_REQUIRE(fade > 100 && fade < lit && std::abs(beyond - lit) <= 3);
    std::printf("Cascade blend=%d/%d/%d; far fade=%d beyond 1km=%d\n",
                transitionStart, transitionMid, transitionEnd, fade, beyond);
    darkTile = -1;
    b.static_shadow_eye_ = {-10, 0, 0};
    int thin[5]{};
    for (int i = 0; i < 5; ++i) {
      stripeX = 30 + i;
      thin[i] = pixel(true, true, .9f, 5);
    }
    stripeX = -1;
    std::printf("Thin shadow footprint RGB sums: %d %d %d %d %d\n", thin[0],
                thin[1], thin[2], thin[3], thin[4]);
    SLRR_REQUIRE(*std::min_element(thin, thin + 5) > 75 &&
                 *std::min_element(thin, thin + 5) < 500);
    for (int i = 1; i < 5; ++i)
      SLRR_REQUIRE(std::abs(thin[i] - thin[i - 1]) < 230);
    // The receiver must subtract the same nonzero origin as caster passes.
    darkTile = 0;
    b.static_shadow_eye_ = {-10, 0, 0};
    b.shadow_origin_ = {100, 0, 100};
    const auto absoluteMatrices = b.shadow_cascade_matrices_;
    for (auto &matrix : b.shadow_cascade_matrices_) {
      matrix[12] += 10;
      matrix[14] += 100;
    }
    const auto rebasedPixel = pixel(true, true, .2f, 5);
    SLRR_REQUIRE(std::abs(rebasedPixel - shaded) <= 3);
    b.shadow_cascade_matrices_ = absoluteMatrices;
    b.shadow_origin_ = {};
    // Render-space bounds: SCX Z is reflected before the instance transform.
    ScxGeometry bounds;
    bounds.position_scale = 1;
    for (int i = 0; i < 3; ++i) {
      bounds.info.bbox_min[i] = 1;
      bounds.info.bbox_max[i] = 2;
    }
    SceneFrustum positiveZ{};
    positiveZ[0] = {0, 0, 1, 0};
    std::array<float, 16> pose;
    bx::mtxIdentity(pose.data());
    SLRR_REQUIRE(!shadow_bounds_visible(bounds, pose, positiveZ));
    pose[14] = 3;
    SLRR_REQUIRE(shadow_bounds_visible(bounds, pose, positiveZ));
    pose[0] = 2;
    pose[5] = .5f;
    SLRR_REQUIRE(shadow_bounds_visible(bounds, pose, positiveZ));
    const float origin[3] = {0, 0, 0}, tinyMove[3] = {.0001f, 0, 0};
    const float direction[3] = {.3f, -.8f, .5f},
                laterSun[3] = {.30001f, -.8f, .5f};
    const auto initial =
        shadow_projection(origin, direction, 32, 2048, false, false);
    const auto moved =
        shadow_projection(tinyMove, direction, 32, 2048, false, false);
    const auto sunMoved =
        shadow_projection(origin, laterSun, 32, 2048, false, false);
    SLRR_REQUIRE(std::abs(initial.sample[12] - moved.sample[12]) < 1e-7f &&
                 std::abs(initial.sample[13] - moved.sample[13]) < 1e-7f);
    // Stationary receiver/camera far from the map origin; only sun angle
    // varies.
    const float farEye[3] = {5000, 20, -7000}, localEye[3] = {0, 0, 0},
                localPoint[3] = {8, -2, 3};
    float oldPrevious[2]{}, newPrevious[2]{};
    double oldJump = 0, newJump = 0;
    for (int frame = 0; frame < 400; ++frame) {
      const float sun[3] = {.3f + frame * .000001f, -.8f, .5f};
      const auto oldGrid =
          shadow_projection(farEye, sun, 32, 2048, false, false);
      const auto localGrid =
          shadow_projection(localEye, sun, 32, 2048, false, false);
      for (int row = 0; row < 2; ++row) {
        float oldUV = oldGrid.sample[12 + row],
              newUV = localGrid.sample[12 + row];
        for (int axis = 0; axis < 3; ++axis) {
          oldUV += oldGrid.sample[axis * 4 + row] *
                   (farEye[axis] + localPoint[axis]);
          newUV += localGrid.sample[axis * 4 + row] * localPoint[axis];
        }
        if (frame) {
          oldJump = std::max(oldJump,
                             double(std::abs(oldUV - oldPrevious[row]) * 2048));
          newJump = std::max(newJump,
                             double(std::abs(newUV - newPrevious[row]) * 2048));
        }
        oldPrevious[row] = oldUV;
        newPrevious[row] = newUV;
      }
    }
    std::printf("Stationary moving-sun lattice max pixel jump: global=%.5f "
                "local=%.5f\n",
                oldJump, newJump);
    SLRR_REQUIRE(oldJump > .5 && newJump < .01);
    std::array<float, 3> anchor{0, 0, 0};
    const float longDrive[3] = {500, 3, -200};
    shadow_rebase_origin(anchor, longDrive, direction);
    SLRR_REQUIRE(std::hypot(anchor[0] - longDrive[0], anchor[1] - longDrive[1],
                            anchor[2] - longDrive[2]) < 10);
    float maximumRebaseJump = 0;
    const float relativeDrive[3] = {longDrive[0] - anchor[0],
                                    longDrive[1] - anchor[1],
                                    longDrive[2] - anchor[2]};
    for (float radius : shadow_radii) {
      const auto beforeRebase =
          shadow_projection(longDrive, direction, radius, 2048, false, false);
      const auto afterRebase = shadow_projection(relativeDrive, direction,
                                                 radius, 2048, false, false);
      for (int row = 0; row < 2; ++row) {
        float before = beforeRebase.sample[12 + row],
              after = afterRebase.sample[12 + row];
        for (int axis = 0; axis < 3; ++axis) {
          before += beforeRebase.sample[axis * 4 + row] * longDrive[axis];
          after += afterRebase.sample[axis * 4 + row] * relativeDrive[axis];
        }
        maximumRebaseJump =
            std::max(maximumRebaseJump, std::abs(before - after) * 2048);
      }
    }
    std::printf("All-cascade rebase phase error: %.5f texels\n",
                maximumRebaseJump);
    SLRR_REQUIRE(maximumRebaseJump < .1f);
    SLRR_REQUIRE(initial.sample != sunMoved.sample);
    std::puts(
        "PASS sub-texel camera stability and continuous small sun changes");
    bgfx::destroy(readback);
    bgfx::destroy(output);
  }
};
} // namespace slrr
int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  LibBgfxBackend backend;
  try {
    SLRR_REQUIRE(backend.open(320, 240, "Scene shadow integration"));
    SLRR_REQUIRE(bgfx::getRendererType() == bgfx::RendererType::Direct3D11);
    ShowWindow(static_cast<HWND>(backend.native_hwnd()), SW_HIDE);
    SceneShadowRegression::run(backend);
    SceneShadowRegression::real_world_cache(backend);
    SceneShadowRegression::addon_bounds(backend);
    SceneShadowRegression::scene_environment(backend);
    SceneShadowRegression::reflection_occupants();
    SceneShadowRegression::garage_geometry(backend);
    SceneShadowRegression::garage_fixtures(backend);
    SceneShadowRegression::vehicle_wall(backend);
    SceneShadowRegression::ambient_occlusion(backend);
    SceneShadowRegression::point_lifetime(backend);
    SceneShadowRegression::scenery_lamps(backend);
    backend.close();
    std::puts("PASS vehicle/scenery shadow visibility, preview/emission "
              "isolation and reflection-view parity");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "scene_shadow_gpu: %s\n", e.what());
    if (backend.ready())
      backend.close();
    return 1;
  }
}
