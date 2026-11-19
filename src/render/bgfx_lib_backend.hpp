#pragma once

// Internal to the libbgfx renderer. The public surface stays
// slrr/render_backend.hpp. Method bodies live in the sibling cpp files.

#include "debug_overlay.hpp"
#include "osd_text_batch.hpp"
#include "scene_mesh_data.hpp"
#include "slrr/dds_image.hpp"
#include "slrr/gameref.hpp"
#include "slrr/host_game_options.hpp"
#include "slrr/osd_registry.hpp"
#include "slrr/render_backend.hpp"
#include "slrr/scene_instances.hpp"
#include "slrr/scene_light.hpp"
#include "slrr/scene_sun.hpp"
#include <optional>

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace slrr {
struct VehicleHudState;

struct MeshVertex {
  float x, y, z;
  float nx, ny, nz;
  float u, v;
  float u2 = 0, v2 = 0, u3 = 0, v3 = 0;
  uint32_t rgba = 0xffffffffu;
};

class LibBgfxBackend final : public IRenderBackend {

  // Map cascades use views 0/2/3/4; vehicle depth uses view 1.
  // The world is view 5 or 6, depending on the FMV backdrop. Previews start at 8. The
  // interface stays on kOsdView, above those rects. Point shadows alternatively
  // use 0/1/2/3/4/64; reserved view 64 is reordered before the world.
  static constexpr uint16_t kShadowResolution = 2048;
  static constexpr uint16_t kStaticShadowView = 0;
  static constexpr uint16_t kVehicleShadowFirst = 1;
  static constexpr uint16_t kWorldView = 5;
  static constexpr uint16_t kFmvLayer = 6;
  static constexpr uint16_t kOsdUnder = 7;
  static constexpr uint16_t kPreview0 = 8;
  static constexpr uint16_t kOsdView = 70;

 public:
  const char* name() const override { return "bgfx"; }

  bool open(int32_t width, int32_t height, const char* title) override;


  void close() override;


  bool ready() const override { return ready_; }

  void pump(int32_t /*ms*/) override;


  bool quit_requested() const override { return quit_; }
  void request_quit() override { quit_ = true; }
  bool request_screenshot(const std::string& path) override;

  void flush() override;


  int32_t width() const override { return width_; }
  int32_t height() const override { return height_; }

  void set_scx_smoke(const std::string& path);


  void set_garage_viewport(bool enabled, int32_t osd_buttons);


  void set_osd_overlay(bool enabled) { osd_overlay_ = enabled; }
  bool osd_draw_enabled() const { return garage_ || osd_overlay_; }

  void* native_hwnd() const { return hwnd_; }

  void set_scx_mesh(int32_t version, int32_t groups, int32_t vertices,
                    int32_t triangles, const float bbox_min[3],
                    const float bbox_max[3]);


  void set_scx_geometry(ScxGeometry geo);


  void clear_scene();
  void set_debug_context(Jvm& jvm, const SectionMachine& sections) {
    debug_overlay_.set_context(jvm, sections);
  }
  void set_debug_startup(bool vsync, bool overlay) {
    debug_overlay_.render_options().vsync = vsync;
    debug_overlay_.set_visible(overlay);
  }


  void add_scx_geometry(ScxGeometry geo);


 private:
  friend struct SceneMeshCacheRegression;
  friend struct SceneShadowRegression;
  DebugOverlay debug_overlay_;
  bool debug_was_visible_ = false;
  struct ScenePart {
    SceneMeshData geo;
    std::vector<ScxGroup> shadow_groups;
    bool merge_groups = false;
    std::shared_ptr<const ScxGeometry> source;
    bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle skin_vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
    std::vector<bgfx::TextureHandle> tex;
    std::vector<std::string> shared_texture_paths;
    uint32_t revision = 0;
  };
  struct SharedMapTexture {
    bgfx::TextureHandle handle;
    size_t users;
  };
  std::unordered_map<std::string, SharedMapTexture> map_textures_;
  struct Preview { GameRefState camera; GameRefState viewport; std::vector<SceneInstance> instances; };
  std::vector<SceneInstance> instances_;
  std::vector<SceneBatch> prop_batches_;
  std::vector<Preview> previews_;
  std::unordered_map<const ScxGeometry*,ScenePart> instance_meshes_;
  std::unordered_map<void*, const ScxGeometry*> deformed_meshes_;

  void cache_scene_mesh(const std::shared_ptr<const ScxGeometry>& source,
                        void* owner, bool map_prop);
  void sync_instances(const float* view, const float* projection);
  static SceneFrustum view_frustum(const float* view, const float* projection);
  void apply_video_mode(const VideoMode& mode);
  void update_static_shadow(const float* eye, const VideoOptions& video);

  // Diffuse * baked lightmap, the way the exe draws a map. No lightmap
  // (OSD, FMV, a mesh without the slot) keeps the existing shading.
  uint32_t world_sampler() const {
    return host_enhanced_graphics().anisotropic_filtering
        ? BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC : BGFX_SAMPLER_NONE;
  }
  void bind_scene_shadow(bool receive, uint8_t stage);
  void bind_baked_light(const ScxGroup* group,
                        const std::vector<bgfx::TextureHandle>* tex,
                        bgfx::TextureHandle mix = BGFX_INVALID_HANDLE,
                        bool receive_static_shadow = false, const SceneSun* sun = nullptr, const float* sky = nullptr);


  void draw_instance(uint16_t view_id,const SceneInstance& instance,const float* matrix,bool preview,int pass);
  void draw_prop_batches(uint16_t view_id, const std::vector<SceneBatch>& batches,
                         bool shadow, bool conservative = false);
  void refresh_distance_fog(const float render_eye[3]);

  void draw_sky(uint16_t view_id, const float* view, const float* projection);
  void draw_dynamic_world(uint16_t view_id);
  void draw_sun_flare(const float* view, const float* projection, uint16_t view_id);

  void draw_previews();


  void expand_scene_bbox(const float mn[3], const float mx[3]);


  void destroy_part(ScenePart& p);

  void build_camera(float* view, float* proj);


  void upload_part(ScenePart& part);
  void upload_skin(ScenePart& part);
  static bool is_logo_texture_slot(const ScxGeometry& geo, uint16_t tex_i);


  static bool is_logo_group(const ScxGroup& g, const ScxGeometry& geo);


  // Horizontal mirror (u → 1-u) for banner/logo groups only.
  static void flip_logo_uv_horizontal(ScxGeometry& geo);


  void draw_scene(uint16_t view_id);


  void load_part_textures(ScenePart& part, bool share_map_textures = false);
  // A mesh's texture files, one per slot ("" for a slot without a file).
  static std::vector<std::string> part_texture_paths(const ScxGeometry& geo);
  // Decodes, on the job pool, the files that load_part_textures will read for
  // these new meshes (geometry, shares map textures). It then takes each
  // image from here instead of reading the file on the host thread.
  void prefetch_part_textures(const std::vector<std::pair<const ScxGeometry*, bool>>& meshes);
  struct PrefetchedTexture {
    DdsImage image;
    size_t uses = 0;  // meshes still to load it; the image is freed at zero
  };
  std::unordered_map<std::string, PrefetchedTexture> prefetched_textures_;


#ifdef _WIN32
  static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

  bgfx::TextureHandle tex_for_osd_icon(const std::string& path);


  void draw_osd_quad(const OsdWidget& w, float cr = 1.f, float cg = 1.f,
                     float cb = 1.f, float ca = 1.f);


  bgfx::TextureHandle tex_for_font(void* font_key);


  void draw_osd_texts(int32_t only_layer, bool filter_layer, bool behind_ui);


  // One OSD line. y is the glyph top (y+ down). align 1 centers on x.
  void draw_osd_line(void* font_key, const std::string& text, float x, float y,
                     int align, uint32_t argb);


  // Osd.animate builds this after idleFor(1). The host never runs that
  // loop except for the credits scroller, so the plate is drawn here.
  // Row titles live in tip too; they are already painted beside the slider.
  void draw_tooltip();


  void draw_fmv(uint16_t view_id);


  void draw_osd();
  void draw_vehicle_hud(const VehicleHudState& hud);
  std::string gps_hud_path_;
  std::unordered_map<std::string, ScxGeometry> hud_preview_meshes_;


  HWND hwnd_ = nullptr;
  bool shown_ = false;
  int mouse_x_ = 0;
  int mouse_y_ = 0;
  int32_t tip_hover_ = -2;
  float tip_since_ = 0.f;
  bool tip_font_tried_ = false;
  int lmb_x_ = 0;
  int lmb_y_ = 0;
  bool have_mouse_ = false;
  bool raw_mouse_ = false;
  bool camera_cursor_clipped_ = false;
  bool lmb_down_ = false;
  int32_t slider_drag_index_ = -1;
  Object* inventory_drag_source_ = nullptr;
  bool inventory_dragging_ = false;
  char inventory_drag_cursor_ = 'J';
#endif

  bool ready_ = false;
  bool quit_ = false;
  bool garage_ = false;
  bool osd_overlay_ = false;
  // Openingpic stays up after the boot movie. Once the menu or its movie
  // has covered it, a later empty frame must not bring that still back.
  bool opening_retired_ = false;
  bool scx_ok_ = false;
  bool mesh_ok_ = false;
  int32_t osd_buttons_ = 0;
  uint16_t osd_view_ = kOsdUnder;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int32_t frames_ = 0;
  float mesh_min_[3] = {0, 0, 0};
  float mesh_max_[3] = {0, 0, 0};
  std::vector<ScenePart> parts_;
  bgfx::VertexLayout layout_;
  bgfx::VertexLayout text_layout_;
  bgfx::ProgramHandle text_program_ = BGFX_INVALID_HANDLE;
  std::vector<OsdTextVertex> text_vertices_;
  std::vector<uint16_t> text_indices_;
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle inst_program_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle inst_shadow_program_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle skin_program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_skin_bones_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle white_tex_ = BGFX_INVALID_HANDLE;
  // 1x1 far depth. The mesh shader's shadow sampler is a comparison
  // sampler, including the draws that do not receive a shadow.
  bgfx::TextureHandle depth_white_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle fmv_tex_ = BGFX_INVALID_HANDLE;
  int32_t fmv_tex_w_ = 0;
  uint64_t fmv_tex_serial_ = 0;
  int32_t fmv_tex_h_ = 0;
  bgfx::UniformHandle s_tex_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_lightmap_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_mesh_mix_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_lightmap_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_lightmap_uv_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_mesh_base_uv_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_mesh_mix_uv_ = BGFX_INVALID_HANDLE;
  std::unordered_map<std::string, bgfx::TextureHandle> osd_tex_;
  std::unordered_map<std::string, bgfx::TextureHandle> font_tex_;
  bgfx::UniformHandle u_alpha_test_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_color_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle vehicle_program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_mix_=BGFX_INVALID_HANDLE,s_env_=BGFX_INVALID_HANDLE,s_bake_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_layers_=BGFX_INVALID_HANDLE,u_channels_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_base_uv_=BGFX_INVALID_HANDLE,u_mix_uv_=BGFX_INVALID_HANDLE,u_env_uv_=BGFX_INVALID_HANDLE,u_diffuse_=BGFX_INVALID_HANDLE,u_spec_=BGFX_INVALID_HANDLE,u_sun_diff_=BGFX_INVALID_HANDLE,u_sun_amb_=BGFX_INVALID_HANDLE,u_sun_spec_=BGFX_INVALID_HANDLE,u_sun_dir_=BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle static_shadow_program_ = BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle static_shadow_fb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle static_shadow_tex_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_static_shadow_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_static_shadow_mtx_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_static_shadow_params_ = BGFX_INVALID_HANDLE;
  std::array<float, 16> static_shadow_matrix_{};
  bgfx::UniformHandle u_cloud_wind_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_cloud_sky_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fog_color_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fog_range_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fog_eye_ = BGFX_INVALID_HANDLE;
  bool fog_enabled_ = false;
  float fog_near_ = 1500.f;
  float fog_far_ = 3000.f;
  float fog_color_[3] = {0.57f, 0.73f, 0.86f};
  float fog_eye_[3] = {};
  bool outdoor_lighting_ = false;
  bgfx::UniformHandle u_local_light_ = BGFX_INVALID_HANDLE;
  std::array<float, 4> local_light_{};
  void update_local_light();
  void update_ambient_occlusion(const float* view,const float* projection,uint16_t width,uint16_t height,uint16_t world_view);
  void bind_ambient_occlusion(bool enabled);
  void destroy_ao_targets();
  bgfx::ProgramHandle ao_program_=BGFX_INVALID_HANDLE,ao_skin_depth_program_=BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle ao_depth_fb_=BGFX_INVALID_HANDLE,ao_raw_fb_=BGFX_INVALID_HANDLE,ao_filter_fb_=BGFX_INVALID_HANDLE;
  bgfx::TextureHandle ao_depth_tex_=BGFX_INVALID_HANDLE,ao_raw_tex_=BGFX_INVALID_HANDLE,ao_filter_tex_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_ao_depth_=BGFX_INVALID_HANDLE,s_ao_input_=BGFX_INVALID_HANDLE,s_ao_field_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_ao_screen_=BGFX_INVALID_HANDLE,u_ao_projection_=BGFX_INVALID_HANDLE,u_ao_config_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_ao_params_=BGFX_INVALID_HANDLE,u_ao_sampling_=BGFX_INVALID_HANDLE,u_ao_field_size_=BGFX_INVALID_HANDLE;
  std::array<float,4> ao_projection_{};
  uint16_t ao_width_=0,ao_height_=0,ao_screen_width_=0,ao_screen_height_=0,ao_world_view_=UINT16_MAX;
  bool ao_active_=false;

  static constexpr size_t kFixtureLights=12;
  std::vector<SpotLight> fixture_lights_;
  void update_fixture_shadows(const VideoOptions& video);
  void bind_fixture_lights(bool enabled);
  bgfx::FrameBufferHandle fixture_shadow_fb_=BGFX_INVALID_HANDLE;
  bgfx::TextureHandle fixture_shadow_tex_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_fixture_shadow_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fixture_params_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fixture_positions_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fixture_colors_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fixture_directions_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fixture_matrices_=BGFX_INVALID_HANDLE;
  std::array<std::array<float,16>,kFixtureLights> fixture_shadow_matrices_{},fixture_cached_matrices_{};
  bool fixture_shadow_active_=false;
  uint16_t fixture_shadow_tile_=512;
  uint64_t fixture_shadow_revision_=0;

  bool bind_point_light(bool enabled);
  std::optional<PointLight> point_light_, cached_point_light_;
  bgfx::UniformHandle u_point_light_color_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_point_shadow_faces_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_point_shadow_params_ = BGFX_INVALID_HANDLE;
  std::array<std::array<float, 16>, 6> point_shadow_matrices_{};
  bool point_shadow_active_ = false;
  void update_interior_sun(const VideoOptions& video);
  void bind_interior_sun(bool enabled, uint8_t stage);
  bgfx::FrameBufferHandle interior_sun_fb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle interior_sun_tex_ = BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle interior_aperture_fb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle interior_aperture_tex_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_sun_aperture_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_interior_sun_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_interior_sun_mtx_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_interior_sun_dir_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_interior_sun_color_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_aperture_params_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_aperture_matrices_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_aperture_uv_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_aperture_edge_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_aperture_ = BGFX_INVALID_HANDLE;
  std::array<float,16> interior_sun_matrix_{};
  SceneSun interior_sun_;
  bool interior_sun_active_ = false;
  uint16_t interior_sun_size_=1024;
  uint16_t interior_sun_mask_ = UINT16_MAX;
  std::array<float,16> interior_sun_cached_matrix_{};

  void update_reflections(const float* eye);
  void destroy_reflections();
  std::array<bgfx::TextureHandle, 2> reflection_cubes_{{
      BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
  size_t reflection_write_ = 0;
  bgfx::TextureHandle reflection_depth_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle reflection_color_ = BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle reflection_capture_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_reflection_cube_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_dynamic_reflection_ = BGFX_INVALID_HANDLE;
  std::array<float, 3> reflection_origin_{}, reflection_sample_origin_{};
  std::array<float, 3> reflection_box_min_{}, reflection_box_max_{};
  bool reflection_box_valid_ = false;
  bgfx::UniformHandle u_reflection_probe_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_reflection_box_min_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_reflection_box_max_ = BGFX_INVALID_HANDLE;
  void* reflection_world_ = nullptr;
  size_t reflection_face_ = 0;
  bool reflection_ready_ = false;
  bool reflection_scene_ = false;
  bgfx::UniformHandle u_shadow_cascades_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_shadow_origin_ = BGFX_INVALID_HANDLE;
  std::array<float,3> shadow_origin_{};
  void* shadow_origin_world_ = nullptr;
  bgfx::UniformHandle u_shadow_filter_=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_shadow_ranges_ = BGFX_INVALID_HANDLE;
  std::array<std::array<float,16>,4> shadow_cascade_matrices_{};
  struct ShadowCasterStamp {
    const ScxGeometry* source = nullptr;
    uint32_t revision = 0;
    uint16_t vertex = UINT16_MAX, index = UINT16_MAX;
    std::array<float,16> transform{};
    bool operator==(const ShadowCasterStamp&) const = default;
  };
  std::vector<ShadowCasterStamp> fixture_shadow_casters_;
  std::vector<ShadowCasterStamp> interior_sun_casters_;
  std::vector<ShadowCasterStamp> shadow_cached_casters_;
  std::array<float,3> shadow_cached_origin_{};
  bool shadow_cache_valid_ = false;
  uint64_t shadow_atlas_revision_ = 0;
  bool shadow_cascades_active_ = false;
  std::unordered_set<const ScxGeometry*> shadow_meshes_;
  bool static_shadow_local_ = false; // Positional-light field, rather than sun visibility.
  bool static_shadow_active_ = false;
  bgfx::FrameBufferHandle car_depth_fb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle car_depth_tex_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_car_depth_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_car_depth_mtx_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_car_depth_params_ = BGFX_INVALID_HANDLE;
  std::array<float, 16> car_depth_matrix_{};
  bool car_depth_active_ = false;
  uint16_t car_depth_size_=2048;
  void update_car_depth(const float* position, const VideoOptions& video);
  uint16_t static_shadow_size_ = 0;
  float static_shadow_distance_ = 0.f;
  bgfx::UniformHandle u_static_shadow_eye_ = BGFX_INVALID_HANDLE;
  std::array<float, 3> static_shadow_eye_{};
  VideoMode applied_video_mode_{};
  uint32_t reset_flags_ = BGFX_RESET_VSYNC;
  float cull_eye_[3]{};
  float cull_view_[16]{};
  float cull_proj_[16]{};
  bool screen_size_visible(const float center[3], float radius) const;
  static bool screen_size_keep(const float center[3], float radius, void* user);

};

}  // namespace slrr
