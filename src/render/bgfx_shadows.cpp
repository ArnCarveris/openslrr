// Active cached map/garage shadows and vehicle depth maps.
#include "bgfx_lib_backend.hpp"
#include "point_shadows.hpp"
#include "shadow_quality.hpp"
#include "shadow_cascades.hpp"
#include "slrr/frame_profiler.hpp"

#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace slrr {
namespace {
// Characters cast into the sun cascades up to this distance from the camera.
// Past it a pedestrian covers about one texel of the 320 m cascade.
constexpr float kSkinnedShadowRange = 100.f;
uint32_t skin_palette_hash(const std::vector<float> &palette) {
  uint32_t hash = 2166136261u;
  for (const float value : palette) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    hash = (hash ^ bits) * 16777619u;
  }
  return hash;
}
} // namespace

void LibBgfxBackend::update_fixture_shadows(const VideoOptions &video) {
  bool was_active = fixture_shadow_active_;
  fixture_shadow_active_ = false;
  if (fixture_lights_.empty() || !garage_ || video.shadows == 0 ||
      !debug_overlay_.render_options().map_shadows) {
    fixture_shadow_casters_.clear();
    return;
  }
  const uint16_t tile=local_shadow_resolution(),width=tile*4,height=tile*3;
  if(bgfx::isValid(fixture_shadow_fb_) && fixture_shadow_tile_!=tile) {
    bgfx::destroy(fixture_shadow_fb_);fixture_shadow_fb_=BGFX_INVALID_HANDLE;
    fixture_shadow_tex_=BGFX_INVALID_HANDLE;was_active=false;
  }
  fixture_shadow_tile_=tile;
  if (!bgfx::isValid(fixture_shadow_fb_)) {
    const auto depth = bgfx::createTexture2D(
        width, height, false, 1, bgfx::TextureFormat::D32F,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(depth))
      return;
    fixture_shadow_fb_ = bgfx::createFrameBuffer(1, &depth, true);
    if (!bgfx::isValid(fixture_shadow_fb_)) {
      bgfx::destroy(depth);
      return;
    }
    fixture_shadow_tex_ = depth;
  }
  std::array<std::array<float, 16>, kFixtureLights> views{}, projections{};
  fixture_shadow_matrices_ = {};
  const auto *caps = bgfx::getCaps();
  const float crop[16] = {.5f,
                          0,
                          0,
                          0,
                          0,
                          caps->originBottomLeft ? .5f : -.5f,
                          0,
                          0,
                          0,
                          0,
                          caps->homogeneousDepth ? .5f : 1.f,
                          0,
                          .5f,
                          .5f,
                          caps->homogeneousDepth ? .5f : 0.f,
                          1};
  for (size_t i = 0; i < fixture_lights_.size(); ++i) {
    const auto &light = fixture_lights_[i];
    const bx::Vec3 position{light.position[0], light.position[1],
                            light.position[2]};
    const bx::Vec3 direction{light.direction[0], light.direction[1],
                             light.direction[2]};
    const bx::Vec3 up =
        std::abs(direction.y) > .98f ? bx::Vec3{0, 0, 1} : bx::Vec3{0, 1, 0};
    bx::mtxLookAt(views[i].data(), position, bx::add(position, direction), up);
    bx::mtxProj(projections[i].data(),
                2.f * std::acos(light.outer_cos) * 57.2957795f, 1.f, .05f,
                light.range, caps->homogeneousDepth);
    float clip[16];
    bx::mtxMul(clip, views[i].data(), projections[i].data());
    bx::mtxMul(fixture_shadow_matrices_[i].data(), clip, crop);
  }
  std::vector<ShadowCasterStamp> stamps;
  for (const auto &instance : instances_) {
    if (!instance.geometry || instance.sky || instance.light_flags ||
        instance.emissive_overlay || instance.gpu_skin ||
        (instance.vehicle_material && !debug_overlay_.render_options().shadows))
      continue;
    cache_scene_mesh(instance.geometry, instance.key, false);
    const auto &part = instance_meshes_.at(instance.geometry.get());
    stamps.push_back({instance.geometry.get(), instance.geometry->revision,
                      part.vbh.idx, part.ibh.idx, instance.transform});
  }
  fixture_shadow_active_ = true;
  if (was_active && fixture_cached_matrices_ == fixture_shadow_matrices_ &&
      stamps == fixture_shadow_casters_)
    return;
  const float no_apertures[4]{};
  bgfx::setUniform(u_aperture_params_, no_apertures);
  for (size_t i = 0; i < fixture_lights_.size(); ++i) {
    const uint16_t view = uint16_t(128 + i);
    bgfx::setViewFrameBuffer(view, fixture_shadow_fb_);
    bgfx::setViewRect(view, uint16_t(i % 4 * tile), uint16_t(i / 4 * tile),
                      tile, tile);
    bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, 0, 1.f);
    bgfx::setViewTransform(view, views[i].data(), projections[i].data());
    bgfx::touch(view);
    const auto frustum = view_frustum(views[i].data(), projections[i].data());
    for (const auto &stamp : stamps) {
      const auto &part = instance_meshes_.at(stamp.source);
      if (!bgfx::isValid(part.vbh) || !bgfx::isValid(part.ibh) ||
          !shadow_bounds_visible(*stamp.source, stamp.transform, frustum))
        continue;
      for (const auto &group : part.shadow_groups) {
        const auto surface = scx_surface(part.geo.info.version, group);
        const float threshold = !surface.cutout ? -1.f
                                : part.geo.info.version == 4
                                    ? group.alpha_reference / 255.f
                                    : .5f;
        const float params[4] = {0, threshold, float(group.base_map.channel),
                                 0};
        const auto slot = group.texture_index;
        bgfx::setUniform(u_static_shadow_params_, params);
        bgfx::setTexture(0, s_tex_,
                         slot < part.tex.size() && bgfx::isValid(part.tex[slot])
                             ? part.tex[slot]
                             : white_tex_);
        bgfx::setVertexBuffer(0, part.vbh);
        bgfx::setIndexBuffer(part.ibh, group.index_start, group.index_count);
        bgfx::setTransform(stamp.transform.data());
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(view, static_shadow_program_);
      }
    }
  }
  fixture_shadow_casters_ = std::move(stamps);
  fixture_cached_matrices_ = fixture_shadow_matrices_;
  ++fixture_shadow_revision_;
}

// A separate directional field: punctual shadow visibility must never occlude
// sunlight, nor may the sky's ambient term replace the direct source.
void LibBgfxBackend::update_interior_sun(const VideoOptions &video) {
  bool was_active = interior_sun_active_;
  interior_sun_active_ = false;
  if (!garage_ || video.shadows == 0 ||
      !host_enhanced_graphics().dynamic_lighting ||
      !debug_overlay_.render_options().map_shadows) {
    interior_sun_casters_.clear();
    return;
  }
  struct Aperture {
    std::array<float, 16> inverse;
    std::array<float, 4> uv, edge;
  };
  std::vector<Aperture> apertures;
  bgfx::TextureHandle window_texture = BGFX_INVALID_HANDLE;
  // Stock compatibility adapter: "ablak" is the authored window section.
  // Do not infer light transmission from arbitrary blended signs or glass cars.
  for (const auto &instance : instances_) {
    if (!instance.geometry || instance.sky || instance.vehicle_material)
      continue;
    const auto &geo = *instance.geometry;
    for (const auto &group : geo.groups) {
      if (group.name != "ablak" ||
          !scx_surface(geo.info.version, group).blend ||
          group.texture_index >= geo.textures.size())
        continue;
      const auto texture = tex_for_osd_icon(geo.textures[group.texture_index]);
      if (!bgfx::isValid(texture) || texture.idx == white_tex_.idx)
        continue;
      // One texture and at most 32 triangles per interior are budgeted. Reject
      // unsupported mixed masks rather than opening an unmasked wall.
      if (bgfx::isValid(window_texture) && texture.idx != window_texture.idx)
        continue;
      window_texture = texture;
      const auto &uv = group.base_map.channel == 2   ? geo.uv3
                       : group.base_map.channel == 1 ? geo.uv2
                                                     : geo.uvs;
      for (size_t i = group.index_start;
           i + 2 < group.index_start + group.index_count &&
           apertures.size() < 32;
           i += 3) {
        bx::Vec3 v[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        float t[3][2];
        bool valid = true;
        for (int k = 0; k < 3; ++k) {
          const auto index = geo.indices[i + k];
          if (index * 3 + 2 >= geo.positions.size() ||
              index * 2 + 1 >= uv.size()) {
            valid = false;
            break;
          }
          const float scale = geo.position_scale;
          v[k] = bx::mul({geo.positions[index * 3] * scale,
                          geo.positions[index * 3 + 1] * scale,
                          -geo.positions[index * 3 + 2] * scale},
                         instance.transform.data());
          t[k][0] =
              uv[index * 2] * group.base_map.scale_u + group.base_map.offset_u;
          t[k][1] = uv[index * 2 + 1] * group.base_map.scale_v +
                    group.base_map.offset_v;
        }
        if (!valid)
          continue;
        const auto a = bx::sub(v[1], v[0]), b = bx::sub(v[2], v[0]);
        const auto cross = bx::cross(a, b);
        if (bx::dot(cross, cross) < 1e-8f)
          continue;
        const auto n = bx::normalize(cross);
        const float basis[16] = {a.x, a.y, a.z, 0, b.x,    b.y,    b.z,    0,
                                 n.x, n.y, n.z, 0, v[0].x, v[0].y, v[0].z, 1};
        Aperture aperture;
        bx::mtxInverse(aperture.inverse.data(), basis);
        aperture.uv = {t[0][0], t[0][1], t[1][0] - t[0][0], t[1][1] - t[0][1]};
        aperture.edge = {t[2][0] - t[0][0], t[2][1] - t[0][1], 0, 0};
        apertures.push_back(aperture);
      }
    }
  }
  if (apertures.empty()) {
    interior_sun_casters_.clear();
    return;
  }
  interior_sun_ = SceneSun{};
  scene_sun_apply_day_cycle(interior_sun_);
  if (interior_sun_.direction[1] >= 0)
    return;
  const uint16_t size=sun_shadow_resolution();
  if(interior_sun_size_!=size) {
    if(bgfx::isValid(interior_sun_fb_)) bgfx::destroy(interior_sun_fb_);
    if(bgfx::isValid(interior_aperture_fb_)) bgfx::destroy(interior_aperture_fb_);
    interior_sun_fb_=BGFX_INVALID_HANDLE;interior_sun_tex_=BGFX_INVALID_HANDLE;
    interior_aperture_fb_=BGFX_INVALID_HANDLE;interior_aperture_tex_=BGFX_INVALID_HANDLE;
    was_active=false;
  }
  interior_sun_size_=size;
  if (!bgfx::isValid(interior_sun_fb_)) {
    const auto depth = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::D32F,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(depth))
      return;
    interior_sun_fb_ = bgfx::createFrameBuffer(1, &depth, true);
    if (!bgfx::isValid(interior_sun_fb_)) {
      bgfx::destroy(depth);
      return;
    }
    interior_sun_tex_ = depth;
  }
  if(!bgfx::isValid(interior_aperture_fb_)) {
    const auto transmission=bgfx::createTexture2D(size,size,false,1,bgfx::TextureFormat::RG32F,
        BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
    const auto depth=bgfx::createTexture2D(size,size,false,1,bgfx::TextureFormat::D32F,BGFX_TEXTURE_RT);
    if(!bgfx::isValid(transmission) || !bgfx::isValid(depth)) {
      if(bgfx::isValid(transmission)) bgfx::destroy(transmission);
      if(bgfx::isValid(depth)) bgfx::destroy(depth);
      return;
    }
    const bgfx::TextureHandle attachments[]={transmission,depth};
    interior_aperture_fb_=bgfx::createFrameBuffer(2,attachments,true);
    if(!bgfx::isValid(interior_aperture_fb_)) {bgfx::destroy(transmission);bgfx::destroy(depth);return;}
    interior_aperture_tex_=transmission;
  }
  float center[3]{};
  float radius = 1;
  for (const auto &instance : instances_) {
    if (!instance.geometry || !instance.geometry->ground_polygons)
      continue;
    const auto &geo = *instance.geometry;
    const float scale = geo.position_scale;
    auto c =
        bx::mul({(geo.info.bbox_min[0] + geo.info.bbox_max[0]) * scale * .5f,
                 (geo.info.bbox_min[1] + geo.info.bbox_max[1]) * scale * .5f,
                 -(geo.info.bbox_min[2] + geo.info.bbox_max[2]) * scale * .5f},
                instance.transform.data());
    center[0] = c.x;
    center[1] = c.y;
    center[2] = c.z;
    radius = std::max(radius,
                      std::hypot(geo.info.bbox_max[0] - geo.info.bbox_min[0],
                                 geo.info.bbox_max[2] - geo.info.bbox_min[2]) *
                              scale * .5f +
                          4);
    break;
  }
  // A metre-scale interior field, independent of the outdoor kilometre splits.
  const float travel[3] = {interior_sun_.direction[0],
                           interior_sun_.direction[1],
                           -interior_sun_.direction[2]};
  const auto *caps = bgfx::getCaps();
  const auto projection =
      shadow_projection(center, travel, radius, size, caps->homogeneousDepth,
                        caps->originBottomLeft);
  interior_sun_matrix_ = projection.sample;
  std::vector<ShadowCasterStamp> stamps;
  for (const auto &instance : instances_) {
    if (!instance.geometry || instance.sky || instance.light_flags ||
        instance.emissive_overlay || instance.gpu_skin ||
        (instance.vehicle_material && !debug_overlay_.render_options().shadows))
      continue;
    cache_scene_mesh(instance.geometry, instance.key, false);
    const auto &part = instance_meshes_.at(instance.geometry.get());
    stamps.push_back({instance.geometry.get(), instance.geometry->revision,
                      part.vbh.idx, part.ibh.idx, instance.transform});
  }
  interior_sun_active_ = true;
  if (was_active && interior_sun_mask_ == window_texture.idx &&
      interior_sun_cached_matrix_ == interior_sun_matrix_ &&
      stamps == interior_sun_casters_)
    return;
  constexpr uint16_t view = 65;
  bgfx::setViewFrameBuffer(view, interior_sun_fb_);
  bgfx::setViewRect(view, 0, 0, size, size);
  bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, 0, 1.f, 0);
  bgfx::setViewTransform(view, projection.view.data(),
                         projection.projection.data());
  bgfx::touch(view);
  std::array<std::array<float, 16>, 32> matrices{};
  std::array<std::array<float, 4>, 32> uvs{}, edges{};
  for (size_t i = 0; i < apertures.size(); ++i) {
    matrices[i] = apertures[i].inverse;
    uvs[i] = apertures[i].uv;
    edges[i] = apertures[i].edge;
  }
  for (const auto &stamp : stamps) {
    const auto &part = instance_meshes_.at(stamp.source);
    if (!bgfx::isValid(part.vbh) || !bgfx::isValid(part.ibh))
      continue;
    // The overlay and the wall may be coincident. Cut only the shell within
    // 8 cm of the actual window plane, never furniture or the opposite wall.
    const float aperture_params[4] = {
        part.geo.ground_polygons ? float(apertures.size()) : 0.f, .08f, 0, 0};
    for (const auto &group : part.geo.groups) {
      const auto surface = scx_surface(part.geo.info.version, group);
      if (surface.blend && group.name != "ablak")
        continue;
      const bool window = surface.blend && group.name == "ablak" &&
          group.texture_index < part.geo.textures.size() &&
          tex_for_osd_icon(part.geo.textures[group.texture_index]).idx == window_texture.idx;
      const float threshold = surface.blend     ? (window ? .5f : -1.f)
                              : !surface.cutout ? -1.f
                              : part.geo.info.version == 4
                                  ? group.alpha_reference / 255.f
                                  : .5f;
      const float params[4] = {0, threshold, float(group.base_map.channel), 0};
      bgfx::setUniform(u_static_shadow_params_, params);
      bgfx::setUniform(u_aperture_params_, aperture_params);
      if (part.geo.ground_polygons) {
        bgfx::setUniform(u_aperture_matrices_, matrices.data(),
                         uint16_t(apertures.size()));
        bgfx::setUniform(u_aperture_uv_, uvs.data(),
                         uint16_t(apertures.size()));
        bgfx::setUniform(u_aperture_edge_, edges.data(),
                         uint16_t(apertures.size()));
      }
      bgfx::setTexture(1, s_aperture_, window_texture,
                       BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
      const auto slot = group.texture_index;
      bgfx::setTexture(0, s_tex_,
                       window ? window_texture
                       : surface.blend ? white_tex_
                       : slot < part.tex.size() && bgfx::isValid(part.tex[slot])
                           ? part.tex[slot]
                           : white_tex_);
      bgfx::setTransform(stamp.transform.data());
      bgfx::setVertexBuffer(0, part.vbh);
      bgfx::setIndexBuffer(part.ibh, group.index_start, group.index_count);
      bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
      bgfx::submit(view, static_shadow_program_);
    }
  }
  const float none[4]{};
  bgfx::setUniform(u_aperture_params_, none);
  // Only authored apertures may admit direct sunlight into this interior.
  // Store their light-space depth as well as transmission, so a window
  // behind the receiver cannot illuminate it. The regular depth field still
  // supplies occlusion by frames, roof, walls, vehicles and furniture.
  bgfx::setViewFrameBuffer(66,interior_aperture_fb_);
  bgfx::setViewRect(66,0,0,size,size);
  bgfx::setViewClear(66,BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,uint32_t(0),1.f);
  bgfx::setViewTransform(66,projection.view.data(),projection.projection.data());
  bgfx::touch(66);
  for(const auto& stamp:stamps) {
    const auto& part=instance_meshes_.at(stamp.source);
    for(const auto& group:part.geo.groups) {
      if(group.name!="ablak" || !scx_surface(part.geo.info.version,group).blend ||
         group.texture_index>=part.geo.textures.size() ||
         tex_for_osd_icon(part.geo.textures[group.texture_index]).idx!=window_texture.idx) continue;
      bind_baked_light(&group,nullptr);
      const float mode[4]={0,4,0,0};
      const auto& uv=group.base_map;
      const float transform[4]={uv.scale_u,uv.scale_v,uv.offset_u,uv.offset_v};
      bgfx::setUniform(u_alpha_test_,mode);
      bgfx::setUniform(u_mesh_base_uv_,transform);
      bgfx::setTexture(0,s_tex_,window_texture);
      bgfx::setTransform(stamp.transform.data());
      bgfx::setVertexBuffer(0,part.vbh);bgfx::setIndexBuffer(part.ibh,group.index_start,group.index_count);
      bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS);
      bgfx::submit(66,program_);
    }
  }
  const float reset_mode[4]{};bgfx::setUniform(u_alpha_test_,reset_mode);
  interior_sun_mask_ = window_texture.idx;
  interior_sun_casters_ = std::move(stamps);
  interior_sun_cached_matrix_ = interior_sun_matrix_;
}

void LibBgfxBackend::update_static_shadow(const float *eye,
                                          const VideoOptions &video) {
  const uint16_t point_shadow_tile=local_shadow_resolution();
  const uint16_t point_shadow_width=point_shadow_tile*3,point_shadow_height=point_shadow_tile*2;
  const uint16_t resolution=scenery_shadow_resolution();
  perf::Scope perf_scope(perf::Zone::Shadows);
  const float no_apertures[4]{};
  bgfx::setUniform(u_aperture_params_, no_apertures);
  static_shadow_active_ = false;
  static_shadow_local_ = false;
  shadow_cascades_active_ = false;
  shadow_meshes_.clear();
  point_shadow_active_ = false;
  if (point_light_ && point_light_->valid() && point_light_->casts_shadows &&
      video.shadows != 0 && debug_overlay_.render_options().map_shadows &&
      bgfx::isValid(static_shadow_program_)) {
    const auto &light = *point_light_;
    if (!bgfx::isValid(static_shadow_fb_) ||
        static_shadow_size_ != point_shadow_width) {
      shadow_cache_valid_ = false;
      if (bgfx::isValid(static_shadow_fb_))
        bgfx::destroy(static_shadow_fb_);
      static_shadow_fb_ = BGFX_INVALID_HANDLE;
      static_shadow_tex_ = BGFX_INVALID_HANDLE;
      const auto depth = bgfx::createTexture2D(
          point_shadow_width, point_shadow_height, false, 1,
          bgfx::TextureFormat::D32F,
          BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
      if (!bgfx::isValid(depth))
        return;
      static_shadow_fb_ = bgfx::createFrameBuffer(1, &depth, true);
      if (!bgfx::isValid(static_shadow_fb_)) {
        bgfx::destroy(depth);
        return;
      }
      static_shadow_tex_ = depth;
      static_shadow_size_ = point_shadow_width;
    }
    shadow_origin_ = light.position;
    shadow_origin_world_ = scene_instances_world();
    std::vector<const SceneInstance *> casters;
    std::vector<ShadowCasterStamp> stamps;
    for (const auto &instance : instances_) {
      if (!instance.geometry || instance.sky || instance.light_flags ||
          instance.gpu_skin || instance.emissive_overlay ||
          (instance.vehicle_material &&
           !debug_overlay_.render_options().shadows))
        continue;
      cache_scene_mesh(instance.geometry, instance.key, false);
      const auto &part = instance_meshes_.at(instance.geometry.get());
      if (!bgfx::isValid(part.vbh) || !bgfx::isValid(part.ibh))
        continue;
      casters.push_back(&instance);
      shadow_meshes_.insert(instance.geometry.get());
      stamps.push_back({instance.geometry.get(), instance.geometry->revision,
                        part.vbh.idx, part.ibh.idx, instance.transform});
    }
    static_shadow_active_ = true;
    static_shadow_local_ = true;
    point_shadow_active_ = true;
    static_shadow_eye_ = light.position;
    static_shadow_distance_ = light.range;
    // Colour/intensity/ambient updates do not change the depth field.
    const bool same_light = cached_point_light_ &&
                            cached_point_light_->position == light.position &&
                            cached_point_light_->range == light.range;
    if (shadow_cache_valid_ && same_light && stamps == shadow_cached_casters_)
      return;
    const auto *caps = bgfx::getCaps();
    for (unsigned face = 0; face < 6; ++face) {
      const auto projection = point_shadow_projection(
          face, light.range, caps->homogeneousDepth, caps->originBottomLeft);
      point_shadow_matrices_[face] = projection.sample;
      const auto frustum =
          view_frustum(projection.view.data(), projection.projection.data());
      const auto view = point_shadow_views[face];
      bgfx::setViewFrameBuffer(view, static_shadow_fb_);
      bgfx::setViewRect(view, uint16_t(face % 3 * point_shadow_tile),
                        uint16_t(face / 3 * point_shadow_tile),
                        point_shadow_tile, point_shadow_tile);
      bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, uint32_t(0), 1.f, 0);
      bgfx::setViewTransform(view, projection.view.data(),
                             projection.projection.data());
      bgfx::touch(view);
      for (const auto *instance : casters) {
        auto matrix = instance->transform;
        for (int axis = 0; axis < 3; ++axis)
          matrix[12 + axis] -= light.position[axis];
        if (!shadow_bounds_visible(*instance->geometry, matrix, frustum))
          continue;
        const auto &part = instance_meshes_.at(instance->geometry.get());
        for (const auto &group : part.shadow_groups) {
          const auto surface = scx_surface(part.geo.info.version, group);
          if (surface.blend)
            continue;
          const auto slot = group.texture_index;
          const auto texture =
              slot < part.tex.size() && bgfx::isValid(part.tex[slot])
                  ? part.tex[slot]
                  : white_tex_;
          const float threshold = !surface.cutout ? -1.f
                                  : part.geo.info.version == 4
                                      ? group.alpha_reference / 255.f
                                      : .5f;
          const float params[4] = {0, threshold, float(group.base_map.channel),
                                   0};
          bgfx::setUniform(u_static_shadow_params_, params);
          bgfx::setTexture(0, s_tex_, texture);
          bgfx::setTransform(matrix.data());
          bgfx::setVertexBuffer(0, part.vbh);
          bgfx::setIndexBuffer(part.ibh, group.index_start, group.index_count);
          bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
          bgfx::submit(view, static_shadow_program_);
        }
      }
    }
    static_shadow_matrix_ = point_shadow_matrices_[3];
    cached_point_light_ = point_light_;
    shadow_cached_casters_ = std::move(stamps);
    shadow_cache_valid_ = true;
    ++shadow_atlas_revision_;
    return;
  }
  if (cached_point_light_)
    shadow_cache_valid_ = false;
  cached_point_light_.reset();
  if (!eye || video.shadows == 0 || !scene_instances_outdoor()) {
    shadow_cache_valid_ = false;
    if (bgfx::isValid(static_shadow_fb_)) bgfx::destroy(static_shadow_fb_);
    static_shadow_fb_ = BGFX_INVALID_HANDLE;
    static_shadow_tex_ = BGFX_INVALID_HANDLE;
    return;
  }
  if (!debug_overlay_.render_options().map_shadows || !bgfx::isValid(static_shadow_program_))
    return;
  // The selected world's light is independent of visible terrain geometry.
  // Dealers/addons may contain only ordinary SCX meshes, or no ground in view.
  const auto sun = scene_instances_light();
  if (!sun.has_direction || sun.direction[1] >= 0.02f) return;
  const uint16_t size = resolution * 2;
  const bool conservative =
      (bgfx::getCaps()->supported & BGFX_CAPS_CONSERVATIVE_RASTER) != 0;
  if (size != static_shadow_size_ && bgfx::isValid(static_shadow_fb_)) {
    shadow_cache_valid_ = false;
    bgfx::destroy(static_shadow_fb_);
    static_shadow_fb_ = BGFX_INVALID_HANDLE;
    static_shadow_tex_ = BGFX_INVALID_HANDLE;
  }
  if (!bgfx::isValid(static_shadow_fb_)) {
    const auto depth = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::D32F,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(depth))
      return;
    static_shadow_fb_ = bgfx::createFrameBuffer(1, &depth, true);
    if (!bgfx::isValid(static_shadow_fb_)) {
      bgfx::destroy(depth);
      return;
    }
    static_shadow_tex_ = depth;
    static_shadow_size_ = size;
  }
  const auto *caps = bgfx::getCaps();
  const float focus[3] = {eye[0], eye[1], -eye[2]};
  const float direction[3] = {sun.direction[0], sun.direction[1],
                              -sun.direction[2]};
  static_shadow_eye_ = {focus[0], focus[1], focus[2]};
  // Refresh moving sunlight every frame; texel-snapped projections remove
  // camera swimming without an eight-metre cache jump.
  if(shadow_origin_world_!=scene_instances_world()) {
    shadow_cache_valid_=false;
    shadow_origin_world_=scene_instances_world();
    shadow_origin_={focus[0],focus[1],focus[2]};
  }
  shadow_rebase_origin(shadow_origin_,focus,direction,resolution);
  const float relative_focus[3]={focus[0]-shadow_origin_[0],focus[1]-shadow_origin_[1],focus[2]-shadow_origin_[2]};
  const auto outer = shadow_projection(relative_focus,direction,shadow_radii.back(),resolution,
      caps->homogeneousDepth,caps->originBottomLeft);
  auto outer_world_view=outer.view;
  for(int row=0;row<3;++row) for(int axis=0;axis<3;++axis)
    outer_world_view[12+row]-=outer.view[axis*4+row]*shadow_origin_[axis];
  const auto outer_frustum=view_frustum(outer_world_view.data(),outer.projection.data());
  // One scene traversal; nested light volumes reuse these immutable draw records.
  const auto casters=scene_instances_collect(&outer_frustum,nullptr,false);
  std::vector<SceneBatch> outer_batches;
  scene_instances_collect_batches(&outer_frustum,outer_batches);
  for(const auto& instance:casters) {
    if(!instance.geometry || instance.sky || instance.vehicle_material || instance.gpu_skin ||
       instance.light_flags || instance.emissive_overlay) continue;
    shadow_meshes_.insert(instance.geometry.get());
    cache_scene_mesh(instance.geometry,nullptr,false);
  }
  for(const auto& batch:outer_batches) if(batch.geometry) {
    shadow_meshes_.insert(batch.geometry.get());cache_scene_mesh(batch.geometry,nullptr,true);
  }
  // Skinned pedestrians/drivers cast from the main-view records, whose bone
  // palettes are already complete. The outer collection deliberately skips
  // palette completion; recomputing it for off-camera characters would bring
  // back that CPU cost, so off-screen characters do not cast.
  std::vector<const SceneInstance*> skinned_casters;
  std::vector<float> skinned_distance;
  for(const auto& instance:instances_) {
    if(!instance.geometry || !instance.gpu_skin || instance.skin.size()<48u*16u ||
       instance.light_flags || instance.emissive_overlay || instance.opacity<.99f) continue;
    const float dx=instance.transform[12]-focus[0],dy=instance.transform[13]-focus[1],
                dz=instance.transform[14]-focus[2];
    const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(distance>kSkinnedShadowRange) continue;
    const auto found=instance_meshes_.find(instance.geometry.get());
    if(found==instance_meshes_.end() || !bgfx::isValid(found->second.skin_vbh) ||
       !bgfx::isValid(found->second.ibh)) continue;
    skinned_casters.push_back(&instance);
    skinned_distance.push_back(distance);
  }
  std::array<ShadowProjection,4> projections;
  bool same_projection=shadow_cached_origin_==shadow_origin_;
  for(size_t i=0;i<4;++i) {
    projections[i]=shadow_projection(relative_focus,direction,shadow_radii[i],resolution,
        caps->homogeneousDepth,caps->originBottomLeft);
    same_projection=same_projection && projections[i].sample==shadow_cascade_matrices_[i];
  }
  // Moving sunlight cannot reuse the atlas: avoid building a costly snapshot
  // on that path. Once it stops, one redraw establishes an exact caster snapshot.
  std::vector<ShadowCasterStamp> stamps;
  if(same_projection) {
    auto stamp=[&](const auto& geo,const auto& matrix) {
      const auto& part=instance_meshes_.at(geo.get());
      stamps.push_back({geo.get(),geo->revision,part.vbh.idx,part.ibh.idx,matrix});
    };
    for(const auto& instance:casters)
      if(instance.geometry && !instance.sky && !instance.vehicle_material && !instance.gpu_skin &&
         !instance.light_flags && !instance.emissive_overlay) stamp(instance.geometry,instance.transform);
    for(const auto& batch:outer_batches)
      for(const auto& instance:batch.instances) stamp(batch.geometry,instance.transform);
    // A pose change must redraw the atlas even when the root does not move.
    for(const auto* instance:skinned_casters) {
      stamp(instance->geometry,instance->transform);
      stamps.back().revision^=skin_palette_hash(instance->skin);
    }
  }
  const bool unchanged=shadow_cache_valid_ && same_projection && stamps==shadow_cached_casters_;
  if(unchanged) {
    static_shadow_active_=true;shadow_cascades_active_=true;return;
  }
  for (size_t cascade = 0; cascade < shadow_radii.size(); ++cascade) {
    const auto& projection=projections[cascade];
    shadow_cascade_matrices_[cascade] = projection.sample;
    auto world_view=projection.view;
    for(int row=0;row<3;++row) for(int axis=0;axis<3;++axis)
      world_view[12+row]-=projection.view[axis*4+row]*shadow_origin_[axis];
    const auto frustum=view_frustum(world_view.data(),projection.projection.data());
    const auto view_id = shadow_views[cascade];
    bgfx::setViewFrameBuffer(view_id, static_shadow_fb_);
    bgfx::setViewRect(view_id, uint16_t(cascade % 2 * resolution),
                      uint16_t(cascade / 2 * resolution),
                      resolution, resolution);
    bgfx::setViewClear(view_id, BGFX_CLEAR_DEPTH, 0, 1.f, 0);
    bgfx::setViewTransform(view_id, projection.view.data(),
                           projection.projection.data());
    bgfx::touch(view_id);
    for (const auto &instance : casters) {
      // Vehicles retain their separate nearby depth map. The scenery depth
      // shader has no skin palette; skinned casters need their own path.
      // Ground POLY also contains buildings, stairs and track structures.
      // It must cast as well as receive; receiver-plane bias handles
      // self-shadow.
      if (!instance.geometry || instance.sky || instance.vehicle_material ||
          instance.gpu_skin || instance.light_flags ||
          instance.emissive_overlay)
        continue;
      if(!shadow_bounds_visible(*instance.geometry,instance.transform,frustum)) continue;
      const auto found = instance_meshes_.find(instance.geometry.get());
      if (found == instance_meshes_.end())
        continue;
      const auto &part = found->second;
      if (!bgfx::isValid(part.vbh) || !bgfx::isValid(part.ibh))
        continue;
      for (const auto &group : part.shadow_groups) {
        const bool v4 = part.geo.info.version == 4;
        const auto surface = scx_surface(part.geo.info.version, group);
        if (surface.blend)
          continue;
        const bool cutout = surface.cutout;
        const auto slot = group.texture_index;
        const auto tex = slot < part.tex.size() && bgfx::isValid(part.tex[slot])
                             ? part.tex[slot]
                             : white_tex_;
        const float threshold = !cutout ? -1.f
                                : v4    ? group.alpha_reference / 255.f
                                        : .5f;
        const float params[4] = {
            0.f, threshold, static_cast<float>(group.base_map.channel), 0.f};
        bgfx::setUniform(u_static_shadow_params_, params);
        bgfx::setTexture(0, s_tex_, tex);
        auto relative=instance.transform;
        for(int axis=0;axis<3;++axis) relative[12+axis]-=shadow_origin_[axis];
        bgfx::setTransform(relative.data());
        bgfx::setVertexBuffer(0, part.vbh);
        bgfx::setIndexBuffer(part.ibh, group.index_start, group.index_count);
        // Preserve narrow casters that overlap a texel without covering its
        // centre.
        const uint64_t coverage =
            conservative ? BGFX_STATE_CONSERVATIVE_RASTER : 0;
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                       coverage);
        bgfx::submit(view_id, static_shadow_program_);
      }
    }
    std::vector<SceneBatch> shadow_batches;
    for(const auto& batch:outer_batches) {
      SceneBatch filtered;filtered.geometry=batch.geometry;filtered.lighting=batch.lighting;
      for(const auto& instance:batch.instances)
        if(shadow_bounds_visible(*batch.geometry,instance.transform,frustum)) filtered.instances.push_back(instance);
      if(!filtered.instances.empty()) shadow_batches.push_back(std::move(filtered));
    }
    draw_prop_batches(view_id, shadow_batches, true, conservative);
    if(bgfx::isValid(ao_skin_depth_program_))
      for(size_t i=0;i<skinned_casters.size();++i) {
        // Skinned bounds describe the rest pose, not the animated limbs. Cull
        // by the camera-centred cascade radius instead (plus a body margin).
        if(skinned_distance[i]>shadow_radii[cascade]+3.f) continue;
        const auto* instance=skinned_casters[i];
        const auto& part=instance_meshes_.at(instance->geometry.get());
        auto relative=instance->transform;
        for(int axis=0;axis<3;++axis) relative[12+axis]-=shadow_origin_[axis];
        for(const auto& group:part.shadow_groups) {
          const auto surface=scx_surface(part.geo.info.version,group);
          if(surface.blend) continue;
          const float threshold=!surface.cutout ? -1.f
                                : part.geo.info.version==4 ? group.alpha_reference/255.f : .5f;
          const float params[4]={0.f,threshold,static_cast<float>(group.base_map.channel),0.f};
          bgfx::setUniform(u_static_shadow_params_,params);
          const auto slot=group.texture_index;
          bgfx::setTexture(0,s_tex_,slot<part.tex.size() && bgfx::isValid(part.tex[slot])
                                        ? part.tex[slot] : white_tex_);
          bgfx::setTransform(relative.data());
          bgfx::setUniform(u_skin_bones_,instance->skin.data(),48);
          bgfx::setVertexBuffer(0,part.skin_vbh);
          bgfx::setIndexBuffer(part.ibh,group.index_start,group.index_count);
          bgfx::setState(BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS|
                         (conservative ? BGFX_STATE_CONSERVATIVE_RASTER : 0));
          bgfx::submit(view_id,ao_skin_depth_program_);
        }
      }
  }
  ++shadow_atlas_revision_;
  shadow_cached_casters_=std::move(stamps);
  shadow_cached_origin_=shadow_origin_;shadow_cache_valid_=same_projection;
  static_shadow_distance_ = shadow_radii.back();
  static_shadow_matrix_ = shadow_cascade_matrices_[0];
  static_shadow_active_ = true;
  shadow_cascades_active_ = true;
}

void LibBgfxBackend::update_car_depth(const float* position, const VideoOptions& video) {
  perf::Scope perf_scope(perf::Zone::Shadows);
  car_depth_active_ = false;
  if (!position || video.shadows == 0 || !debug_overlay_.render_options().shadows ||
      !bgfx::isValid(static_shadow_program_)) return;

  const uint16_t size=scenery_shadow_resolution();
  if(bgfx::isValid(car_depth_fb_) && car_depth_size_!=size) {
    bgfx::destroy(car_depth_fb_);car_depth_fb_=BGFX_INVALID_HANDLE;car_depth_tex_=BGFX_INVALID_HANDLE;
  }
  car_depth_size_=size;
  if (!bgfx::isValid(car_depth_fb_)) {
    const auto texture = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::D32F,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    if (!bgfx::isValid(texture)) return;
    car_depth_fb_ = bgfx::createFrameBuffer(1, &texture, true);
    if (!bgfx::isValid(car_depth_fb_)) {
      bgfx::destroy(texture);
      return;
    }
    car_depth_tex_ = texture;
  }

  const auto sun = scene_instances_light();
  const bx::Vec3 direction = sun.has_direction
      ? bx::Vec3{sun.direction[0], sun.direction[1], -sun.direction[2]}
      : bx::Vec3{0.f, -1.f, 0.f};
  const auto travel = bx::normalize(direction);
  if (travel.y >= 0.02f) return;
  const bx::Vec3 centre{position[0], position[1], -position[2]};
  const bx::Vec3 up = std::abs(travel.y) > .98f ? bx::Vec3{0,0,1} : bx::Vec3{0,1,0};
  const auto* caps = bgfx::getCaps();
  float view[16], proj[16], clip[16];
  bx::mtxLookAt(view, bx::sub(centre, bx::mul(travel, 20.f)), centre, up);
  bx::mtxOrtho(proj, -14.f, 14.f, -14.f, 14.f, 0.f, 48.f, 0.f, caps->homogeneousDepth);
  bx::mtxMul(clip, view, proj);
  const float crop[16] = {
      .5f, 0.f, 0.f, 0.f, 0.f, caps->originBottomLeft ? .5f : -.5f, 0.f, 0.f,
      0.f, 0.f, caps->homogeneousDepth ? .5f : 1.f, 0.f, .5f, .5f,
      caps->homogeneousDepth ? .5f : 0.f, 1.f};
  bx::mtxMul(car_depth_matrix_.data(), clip, crop);
  constexpr uint16_t id = kVehicleShadowFirst;
  bgfx::setViewFrameBuffer(id, car_depth_fb_);
  bgfx::setViewRect(id, 0, 0, size, size);
  bgfx::setViewClear(id, BGFX_CLEAR_DEPTH, 0, 1.f, 0);
  bgfx::setViewTransform(id, view, proj);
  bgfx::touch(id);

  for (const auto& instance : instances_) {
    if (!instance.vehicle_material || instance.light_flags) continue;
    const auto found = instance_meshes_.find(instance.geometry.get());
    if (found == instance_meshes_.end()) continue;
    const auto& part = found->second;
    if (!bgfx::isValid(part.vbh) || !bgfx::isValid(part.ibh)) continue;
    for (const auto& group : part.geo.groups) {
      if ((group.material_flags & 0x40000000u) || group.diffuse[3] < .99f) continue;
      const float opaque[4] = {0.f, -1.f, 0.f, 0.f};
      bgfx::setUniform(u_static_shadow_params_, opaque);
      bgfx::setTexture(0, s_tex_, white_tex_);
      bgfx::setTransform(instance.transform.data());
      bgfx::setVertexBuffer(0, part.vbh);
      bgfx::setIndexBuffer(part.ibh, group.index_start, group.index_count);
      bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
      bgfx::submit(id, static_shadow_program_);
    }
  }
  car_depth_active_ = true;
}

}  // namespace slrr
