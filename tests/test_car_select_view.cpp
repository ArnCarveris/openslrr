#include "save_io_fixture.hpp"
#include "slrr/mainloop.hpp"
#include "slrr/gameref.hpp"
#include "slrr/scene_instances.hpp"
using namespace slrr;
void inset_preview_keeps_page_and_pose(){
 Fixture f;gameref_clear();scene_instances_select(nullptr,nullptr,false);
 Object catalog,world,camera,viewport,cursor,cursor_camera,cursor_view;
 catalog.class_name="java.game.Catalog";cursor.class_name="java.io.MouseCursor";
 catalog.fields["CarScene"]=Value::make_obj(&world);catalog.fields["Cam"]=Value::make_obj(&camera);
 auto* root=gameref_upsert(&world,GameRefKind::Dummy);root->flags=kWorldTreeRoot;
 auto* cam=gameref_upsert(&camera,GameRefKind::Camera);gameref_set_parent(&camera,&world);
 auto* vp=gameref_upsert(&viewport,GameRefKind::Viewport);vp->vp_w=.45f;vp->vp_h=.4f;vp->vp_active=true;vp->vp_pri=12;
 gameref_activate_camera(*cam,*vp,1);cam->aim_look=cam->aim_move=true;cam->aim_eye[0]=-2;cam->aim_eye[1]=1;cam->aim_eye[2]=-4;cam->aim_at[0]=.3f;cam->aim_at[1]=-.6f;
 auto expected=*cam;
 f.sections.adopt_actual(&catalog);auto render=create_render_backend(RenderBackendKind::Null);
 mainloop_sync_section_render(*render,f.sections);
 SLRR_REQUIRE(scene_instances_world()==nullptr && fullscreen_script_camera()==nullptr);
 SLRR_REQUIRE(cam->aim_eye[0]==expected.aim_eye[0] && cam->aim_eye[1]==expected.aim_eye[1] && cam->aim_eye[2]==expected.aim_eye[2]);
 // A tooltip uses a fullscreen cursor camera at a higher viewport priority.
 auto* cr=gameref_upsert(&cursor,GameRefKind::GameType);cr->class_name=cursor.class_name;cr->flags=kWorldTreeRoot;
 auto* cc=gameref_upsert(&cursor_camera,GameRefKind::Camera);gameref_set_parent(&cursor_camera,&cursor);
 auto* cv=gameref_upsert(&cursor_view,GameRefKind::Viewport);cv->vp_active=true;cv->vp_pri=127;gameref_activate_camera(*cc,*cv,0);
 for(int i=0;i<20;++i){mainloop_sync_section_render(*render,f.sections);SLRR_REQUIRE(scene_instances_world()==nullptr && fullscreen_script_camera()==nullptr);}
 const auto cameras=gameref_active_cameras();SLRR_REQUIRE(std::find(cameras.begin(),cameras.end(),cam)!=cameras.end());
 // Fullscreen showroom remains a main scene, and cursor cannot steal it.
 vp->vp_w=vp->vp_h=1.f;mainloop_sync_section_render(*render,f.sections);
 SLRR_REQUIRE(scene_instances_world()==&world && fullscreen_script_camera()==cam);
 scene_instances_select(nullptr,nullptr,false);gameref_clear();
 std::puts("PASS catalog inset retains script pose and preview camera; cursor hover cannot replace world; fullscreen showroom preserved");
}
void cursor_camera_is_ui_only(){
 Fixture f;gameref_clear();scene_instances_select(nullptr,nullptr,false);
 Object cursor,camera,viewport;
 auto* root=gameref_upsert(&cursor,GameRefKind::GameType);root->class_name="java.io.MouseCursor";root->flags=kWorldTreeRoot;
 auto* cam=gameref_upsert(&camera,GameRefKind::Camera);gameref_set_parent(&camera,&cursor);
 auto* vp=gameref_upsert(&viewport,GameRefKind::Viewport);vp->vp_active=true;vp->vp_pri=127;gameref_activate_camera(*cam,*vp,0);
 SLRR_REQUIRE(fullscreen_script_camera()==nullptr);
 SLRR_REQUIRE(gameref_active_cameras().size()==1); // UI camera remains usable by UI
 gameref_clear();std::puts("PASS cursor-only UI cannot become the main 3D camera");
}
int main(){const standalone::Test tests[]={inset_preview_keeps_page_and_pose,cursor_camera_is_ui_only};return standalone::run("car_select_view",tests);}
