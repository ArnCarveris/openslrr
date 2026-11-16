#include "save_io_fixture.hpp"
#include "slrr/script_compiler.hpp"
#include "slrr/host_game_options.hpp"
#include <thread>
using namespace slrr;
int main() {
 Fixture f;
 fs::create_directories(f.root/"probe/src");
 std::ofstream(f.root/"probe/src/String.java") << "package java.lang; public class String {}";
 std::ofstream(f.root/"probe/src/Config.java") << R"JAVA(package java.util;
 import java.lang.String;
 public class Config { public Config(int mode){} public native int saveConfig(String name); }
 )JAVA";
 auto report=script_compile_all(f.root.string(),"probe");
 SLRR_REQUIRE(report.failed==0 && report.succeeded>0);
 fs::path compiled;for(auto& e:fs::recursive_directory_iterator(f.root))if(e.path().filename()=="Config.class")compiled=e.path();
 SLRR_REQUIRE(f.vm.load_class_file(compiled.string()));
 int attempts=0;bool succeed=false;
 f.registry.register_method("java.util.Config","saveConfig","(Ljava.lang.String;)I",
   [&](Object*,const std::vector<Value>&){++attempts;return Value::make_int(succeed);});
 host_game_options_load(f.vm);
 host_game_options_dirty();
 host_game_options_update(f.vm);SLRR_REQUIRE(attempts==0);
 std::this_thread::sleep_for(std::chrono::milliseconds(550));
 host_game_options_update(f.vm);SLRR_REQUIRE(attempts==1);
 std::this_thread::sleep_for(std::chrono::milliseconds(550));
 for(int i=0;i<100;++i)host_game_options_update(f.vm);
 SLRR_REQUIRE(attempts==1);
 // A real setting change permits another debounced automatic attempt.
 f.vm.set_static("java.util.Config","gpsMode",Value::make_int(2));
 host_game_options_apply(f.vm);
 host_game_options_update(f.vm);SLRR_REQUIRE(attempts==1);
 std::this_thread::sleep_for(std::chrono::milliseconds(550));
 host_game_options_update(f.vm);SLRR_REQUIRE(attempts==2);
 host_game_options_update(f.vm,true);SLRR_REQUIRE(attempts==3);
 succeed=true;
 host_game_options_update(f.vm,true);SLRR_REQUIRE(attempts==4);
 host_game_options_update(f.vm,true);SLRR_REQUIRE(attempts==4);
 host_game_options_dirty();host_game_options_update(f.vm,true);SLRR_REQUIRE(attempts==5);
 host_game_options_update(f.vm,true);SLRR_REQUIRE(attempts==5);
 std::puts("PASS debounce; failed saves suspend automatic retries; real changes rearm; forced retry and success clear dirty");
}
