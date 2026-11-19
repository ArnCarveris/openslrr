// Real D3D11 pixels through production vehicle/scenery shaders and bindings.
#include "../src/render/bgfx_lib_backend.hpp"
#include "../src/render/shadow_cascades.hpp"
#include "save_io_fixture.hpp"
#include "slrr/mainloop.hpp"
#include <bx/math.h>
namespace slrr {
void spawn_ground_shell(Jvm &, const RpkIndex &, Object *);
}
using namespace slrr;
namespace slrr {
struct SceneShadowRegression {
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
    b.local_light_ = {0, 3.3f, 0, 8};
    b.update_static_shadow(eye, video);
    bgfx::frame();
    SLRR_REQUIRE(b.static_shadow_active_ && !b.static_shadow_local_);
    b.garage_ = false;
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
