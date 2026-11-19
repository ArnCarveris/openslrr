// Active cached map/garage shadows and vehicle depth maps.
#include "bgfx_lib_backend.hpp"
#include "shadow_cascades.hpp"
#include "slrr/frame_profiler.hpp"

#include <bx/math.h>
#include <algorithm>
#include <cmath>

namespace slrr {

void LibBgfxBackend::update_static_shadow(const float* eye, const VideoOptions& video) {
  perf::Scope perf_scope(perf::Zone::Shadows);
  static_shadow_active_ = false;
  static_shadow_local_ = false;
  shadow_cascades_active_ = false;
  shadow_meshes_.clear();
  // The garage has no sun map. The neon reuses this same view and looks down
  // on the bay, so the car and furniture cast onto the room without a new pass.
  if (garage_ && local_light_[3] > 0.f && video.shadows != 0 &&
      debug_overlay_.render_options().map_shadows &&
      bgfx::isValid(static_shadow_program_)) {
    shadow_cache_valid_ = false;
    shadow_origin_world_ = nullptr;
    constexpr uint16_t size = kShadowResolution;
    if (!bgfx::isValid(static_shadow_fb_) || size != static_shadow_size_) {
      if (bgfx::isValid(static_shadow_fb_)) bgfx::destroy(static_shadow_fb_);
      static_shadow_fb_ = BGFX_INVALID_HANDLE;
      static_shadow_tex_ = BGFX_INVALID_HANDLE;
      constexpr uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
          BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
      const auto depth = bgfx::createTexture2D(size, size, false, 1,
                                              bgfx::TextureFormat::D32F, flags);
      if (!bgfx::isValid(depth)) return;
      static_shadow_fb_ = bgfx::createFrameBuffer(1, &depth, true);
      if (!bgfx::isValid(static_shadow_fb_)) {
        bgfx::destroy(depth);
        return;
      }
      static_shadow_tex_ = depth;
      static_shadow_size_ = size;
    }
    const auto* caps = bgfx::getCaps();
    const bx::Vec3 light{local_light_[0], local_light_[1], local_light_[2]};
    float view[16], proj[16], clip[16];
    bx::mtxLookAt(view, light, bx::Vec3{light.x, light.y - 1.f, light.z}, {0.f, 0.f, 1.f});
    float extent = 10.f;
    // Garage shells include floor, walls and some furniture. Fit their actual
    // footprint; a fixed 20m-wide field leaves much of a stock bay unshadowed.
    for (const auto& instance : instances_) {
      if (!instance.geometry || !instance.geometry->ground_polygons) continue;
      const auto& mesh = *instance.geometry;
      const auto& m = instance.transform;
      for (int corner = 0; corner < 8; ++corner) {
        float p[3];
        for (int axis = 0; axis < 3; ++axis)
          p[axis] = ((corner & (1 << axis)) ? mesh.info.bbox_max[axis]
                                           : mesh.info.bbox_min[axis]) * mesh.position_scale;
        p[2] = -p[2];
        const float x = m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12]-light.x;
        const float z = m[2]*p[0]+m[6]*p[1]+m[10]*p[2]+m[14]-light.z;
        if (std::isfinite(x) && std::isfinite(z))
          extent = std::max(extent, std::max(std::abs(x),std::abs(z))+.5f);
      }
    }
    bx::mtxOrtho(proj, -extent, extent, -extent, extent, 0.15f, 16.f, 0.f,
                 caps->homogeneousDepth);
    bx::mtxMul(clip, view, proj);
    const float crop[16] = {
        .5f, 0.f, 0.f, 0.f, 0.f, caps->originBottomLeft ? .5f : -.5f, 0.f, 0.f,
        0.f, 0.f, caps->homogeneousDepth ? .5f : 1.f, 0.f, .5f, .5f,
        caps->homogeneousDepth ? .5f : 0.f, 1.f};
    bx::mtxMul(static_shadow_matrix_.data(), clip, crop);
    bgfx::setViewFrameBuffer(kStaticShadowView, static_shadow_fb_);
    bgfx::setViewRect(kStaticShadowView, 0, 0, size, size);
    bgfx::setViewClear(kStaticShadowView, BGFX_CLEAR_DEPTH, 0, 1.f, 0);
    bgfx::setViewTransform(kStaticShadowView, view, proj);
    bgfx::touch(kStaticShadowView);
    const uint64_t state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    auto cast = [&](bgfx::VertexBufferHandle vb, bgfx::IndexBufferHandle ib,
                    const auto& groups, const float* matrix) {
      if (!bgfx::isValid(vb) || !bgfx::isValid(ib)) return;
      auto submit = [&](uint32_t start, uint32_t count) {
        const float opaque[4] = {0.f, -1.f, 0.f, 0.f};
        bgfx::setUniform(u_static_shadow_params_, opaque);
        bgfx::setTexture(0, s_tex_, white_tex_);
        bgfx::setTransform(matrix);
        bgfx::setVertexBuffer(0, vb);
        bgfx::setIndexBuffer(ib, start, count);
        bgfx::setState(state);
        bgfx::submit(kStaticShadowView, static_shadow_program_);
      };
      if (groups.empty()) return;
      for (const auto& group : groups) {
        if ((group.material_flags & 0x40000000u) || group.diffuse[3] < .99f) continue;
        submit(group.index_start, group.index_count);
      }
    };
    // Include built-in room geometry and furniture. The light near plane
    // clips ceilings above the emitter; excluding the entire POLY shell also
    // removed legitimate interior casters.
    for (const auto& instance : instances_) {
      if (instance.sky || instance.light_flags || !instance.geometry ||
          instance.gpu_skin || instance.emissive_overlay) continue;
      const auto found = instance_meshes_.find(instance.geometry.get());
      if (found == instance_meshes_.end()) continue;
      cast(found->second.vbh, found->second.ibh, found->second.geo.groups,
           instance.transform.data());
    }
    static_shadow_eye_ = {light.x, light.y, light.z};
    // The fitted interior field is bounded by its projection, not by a
    // legacy distance/UV fade which relights the edges of the room.
    static_shadow_distance_ = 0.f;
    static_shadow_active_ = true;
    // The authored neon is directional. Its visibility must attenuate that
    // direct light, not a weak extra point light over an unshadowed fallback.
    static_shadow_local_ = false;
    return;
  }
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
  constexpr uint16_t size = kShadowResolution * 2;
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
  shadow_rebase_origin(shadow_origin_,focus,direction,kShadowResolution);
  const float relative_focus[3]={focus[0]-shadow_origin_[0],focus[1]-shadow_origin_[1],focus[2]-shadow_origin_[2]};
  const auto outer = shadow_projection(relative_focus,direction,shadow_radii.back(),kShadowResolution,
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
  std::array<ShadowProjection,4> projections;
  bool same_projection=shadow_cached_origin_==shadow_origin_;
  for(size_t i=0;i<4;++i) {
    projections[i]=shadow_projection(relative_focus,direction,shadow_radii[i],kShadowResolution,
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
    bgfx::setViewRect(view_id, uint16_t(cascade % 2 * kShadowResolution),
                      uint16_t(cascade / 2 * kShadowResolution),
                      kShadowResolution, kShadowResolution);
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

  constexpr uint16_t size = kShadowResolution;
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
