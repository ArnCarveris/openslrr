#include "bgfx_lib_backend.hpp"
#include <algorithm>
#include <bx/math.h>
#include <cmath>
namespace slrr {
void LibBgfxBackend::destroy_ao_targets() {
  for (auto *fb : {&ao_depth_fb_, &ao_raw_fb_, &ao_filter_fb_}) {
    if (bgfx::isValid(*fb))
      bgfx::destroy(*fb);
    *fb = BGFX_INVALID_HANDLE;
  }
  ao_depth_tex_ = ao_raw_tex_ = ao_filter_tex_ = BGFX_INVALID_HANDLE;
  ao_width_ = ao_height_ = ao_screen_width_ = ao_screen_height_ = 0;
  ao_active_ = false;
  ao_world_view_ = UINT16_MAX;
}
void LibBgfxBackend::bind_ambient_occlusion(bool enabled) {
  const auto &options = host_enhanced_graphics();
  const bool active = enabled && ao_active_ && options.ao_method > 0;
  const float sampling[4] = {
      active ? std::clamp(options.ao_strength, 0, 200) * .01f : 0.f,
      ao_screen_width_ ? 1.f / ao_screen_width_ : 0.f,
      ao_screen_height_ ? 1.f / ao_screen_height_ : 0.f, 0};
  const float field[4] = {ao_width_ ? 1.f / ao_width_ : 0.f,
                          ao_height_ ? 1.f / ao_height_ : 0.f,
                          std::clamp(options.ao_radius, 25, 300) * .01f, 0};
  bgfx::setUniform(u_ao_sampling_, sampling);
  bgfx::setUniform(u_ao_field_size_, field);
  bgfx::setUniform(u_ao_projection_, ao_projection_.data());
  constexpr uint32_t point = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                             BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
  bgfx::setTexture(9, s_ao_field_, active ? ao_raw_tex_ : white_tex_, point);
  bgfx::setTexture(10, s_ao_depth_, active ? ao_depth_tex_ : depth_white_,
                   point);
}
void LibBgfxBackend::update_ambient_occlusion(const float *view,
                                              const float *projection,
                                              uint16_t width, uint16_t height,
                                              uint16_t world_view) {
  const auto &options = host_enhanced_graphics();
  if (options.ao_method <= 0 || options.ao_strength <= 0 || !width || !height ||
      !bgfx::isValid(ao_program_)) {
    destroy_ao_targets();
    return;
  }
  const int quality = std::clamp(options.ao_quality, 0, 2);
  const uint16_t aw = quality == 2 ? width : uint16_t((width + 1) / 2);
  const uint16_t ah = quality == 2 ? height : uint16_t((height + 1) / 2);
  if (ao_screen_width_ != width || ao_screen_height_ != height ||
      ao_width_ != aw || ao_height_ != ah)
    destroy_ao_targets();
  auto target = [&](bgfx::TextureFormat::Enum format, uint16_t w, uint16_t h,
                    bgfx::TextureHandle &texture, bgfx::FrameBufferHandle &fb) {
    texture = bgfx::createTexture2D(w, h, false, 1, format,
                                    BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                        BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(texture))
      return false;
    fb = bgfx::createFrameBuffer(1, &texture, true);
    if (bgfx::isValid(fb))
      return true;
    bgfx::destroy(texture);
    texture = BGFX_INVALID_HANDLE;
    return false;
  };
  if (!bgfx::isValid(ao_depth_fb_)) {
    if (!target(bgfx::TextureFormat::D32F, width, height, ao_depth_tex_,
                ao_depth_fb_) ||
        !target(bgfx::TextureFormat::R8, aw, ah, ao_raw_tex_, ao_raw_fb_) ||
        !target(bgfx::TextureFormat::R8, aw, ah, ao_filter_tex_,
                ao_filter_fb_)) {
      destroy_ao_targets();
      return;
    }
    ao_screen_width_ = width;
    ao_screen_height_ = height;
    ao_width_ = aw;
    ao_height_ = ah;
  }
  const auto *caps = bgfx::getCaps();
  float depth_projection[16];
  std::copy(projection, projection + 16, depth_projection);
  for (int i : {2, 6, 10, 14})
    depth_projection[i] = caps->homogeneousDepth
                              ? -projection[i]
                              : projection[i + 1] - projection[i];
  ao_projection_ = {depth_projection[10], depth_projection[14],
                    caps->homogeneousDepth ? 1.f : 0.f,
                    caps->originBottomLeft ? 1.f : 0.f};
  ao_world_view_ = world_view;
  ao_active_ = true;
  // A camera-matched, single-sample reversed-Z depth prepass. No swapchain
  // read, MSAA depth resolve, post-darkening of direct light or UI processing.
  bgfx::setViewFrameBuffer(140, ao_depth_fb_);
  bgfx::setViewRect(140, 0, 0, width, height);
  bgfx::setViewClear(140, BGFX_CLEAR_DEPTH, 0, 0.f);
  bgfx::setViewTransform(140, view, depth_projection);
  bgfx::touch(140);
  const float no_apertures[4]{};
  bgfx::setUniform(u_alpha_test_, no_apertures);
  bgfx::setUniform(u_aperture_params_, no_apertures);
  auto draw = [&](const ScenePart &part, const float *matrix,
                  const SceneInstance *object) {
    if (!bgfx::isValid(part.vbh) || !bgfx::isValid(part.ibh))
      return;
    const bool skin = object && object->gpu_skin && !object->skin.empty() &&
                      bgfx::isValid(part.skin_vbh);
    if (object && object->gpu_skin && !skin)
      return;
    for (const auto &group : part.shadow_groups) {
      const auto surface = scx_surface(part.geo.info.version, group);
      const float threshold = !surface.cutout ? -1.f
                              : part.geo.info.version == 4
                                  ? group.alpha_reference / 255.f
                                  : .5f;
      const float params[4] = {0, threshold, float(group.base_map.channel), 0};
      bgfx::setUniform(u_static_shadow_params_, params);
      bgfx::setTransform(matrix);
      if (skin)
        bgfx::setUniform(u_skin_bones_, object->skin.data(), 48);
      bgfx::setVertexBuffer(0, skin ? part.skin_vbh : part.vbh);
      bgfx::setIndexBuffer(part.ibh, group.index_start, group.index_count);
      const auto slot = group.texture_index;
      bgfx::setTexture(0, s_tex_,
                       slot < part.tex.size() && bgfx::isValid(part.tex[slot])
                           ? part.tex[slot]
                           : white_tex_);
      bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_GREATER);
      bgfx::submit(140, skin ? ao_skin_depth_program_ : static_shadow_program_);
    }
  };
  for (const auto &object : instances_) {
    if (!object.geometry || object.sky || object.emissive_overlay ||
        object.opacity < .99f)
      continue;
    const auto found = instance_meshes_.find(object.geometry.get());
    if (found != instance_meshes_.end())
      draw(found->second, object.transform.data(), &object);
  }
  float identity[16];
  bx::mtxIdentity(identity);
  if (garage_)
    for (const auto &part : parts_)
      draw(part, identity, nullptr);
  const bool saved_local = static_shadow_local_;
  static_shadow_local_ = true;
  draw_prop_batches(140, prop_batches_, true, false, true);
  static_shadow_local_ = saved_local;
  const float screen[4] = {1.f / width, 1.f / height, projection[0],
                           projection[5]};
  const float config[4] = {float(std::clamp(options.ao_method, 1, 2)),
                           float(quality),
                           std::clamp(options.ao_radius, 25, 300) * .01f, 0};
  constexpr uint32_t sampler = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                               BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
  for (uint16_t pass = 0; pass < 3; ++pass) {
    const uint16_t id = 141 + pass;
    const auto fb = pass == 1 ? ao_filter_fb_ : ao_raw_fb_;
    bgfx::setViewFrameBuffer(id, fb);
    bgfx::setViewRect(id, 0, 0, aw, ah);
    bgfx::setViewClear(id, BGFX_CLEAR_COLOR, 0xffffffff);
    bgfx::touch(id);
    if (bgfx::getAvailTransientVertexBuffer(3, layout_) < 3) {
      ao_active_ = false;
      return;
    }
    bgfx::TransientVertexBuffer vb;
    bgfx::allocTransientVertexBuffer(&vb, 3, layout_);
    auto *vertices = reinterpret_cast<MeshVertex *>(vb.data);
    const float v0 = caps->originBottomLeft ? 0.f : 1.f,
                v1 = caps->originBottomLeft ? 2.f : -1.f;
    vertices[0] = {-1, -1, 0, 0, 0, 1, 0, v0};
    vertices[1] = {3, -1, 0, 0, 0, 1, 2, v0};
    vertices[2] = {-1, 3, 0, 0, 0, 1, 0, v1};
    const float params[4] = {float(pass), 1.f / aw, 1.f / ah, 0};
    bgfx::setUniform(u_ao_screen_, screen);
    bgfx::setUniform(u_ao_projection_, ao_projection_.data());
    bgfx::setUniform(u_ao_config_, config);
    bgfx::setUniform(u_ao_params_, params);
    bgfx::setTexture(0, s_ao_depth_, ao_depth_tex_, sampler);
    bgfx::setTexture(1, s_ao_input_,
                     pass == 0   ? white_tex_
                     : pass == 1 ? ao_raw_tex_
                                 : ao_filter_tex_,
                     sampler);
    bgfx::setVertexBuffer(0, &vb);
    bgfx::setState(BGFX_STATE_WRITE_RGB);
    bgfx::submit(id, ao_program_);
  }
}
} // namespace slrr
