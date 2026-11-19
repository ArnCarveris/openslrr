// Real D3D11 pixels through production vehicle/scenery shaders and bindings.
#include "../src/render/bgfx_lib_backend.hpp"
#include "../src/render/shadow_cascades.hpp"
#include "save_io_fixture.hpp"
#include <bx/math.h>
using namespace slrr;
namespace slrr {
struct SceneShadowRegression {
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
