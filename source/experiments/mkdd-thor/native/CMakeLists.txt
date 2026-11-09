cmake_minimum_required(VERSION 3.20)
project(wiids_mkdd_lab C CXX)
option(MKDD_PLUS_BUILD "Private pinned MKDD Plus translation and course-set feed" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT ANDROID)
  add_executable(lower_state_test lower_state.cpp ../tests/lower_state_test.cpp)
  enable_testing()
  add_test(NAME lower_state COMMAND lower_state_test)
  if(IS_DIRECTORY "${MKDD_RUNTIME}/gcn-recomp/runtime" AND EXISTS "${MKDD_GENERATED}/wiids_hud_profile.h")
    add_executable(widescreen_hud_test widescreen_hud.cpp lower_state.cpp ../tests/widescreen_hud_test.cpp)
    target_include_directories(widescreen_hud_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
      "${MKDD_RUNTIME}/gcn-recomp/runtime" "${MKDD_GENERATED}")
    add_test(NAME widescreen_hud COMMAND widescreen_hud_test)
  endif()
  return()
endif()

if(NOT IS_DIRECTORY "${MKDD_RUNTIME}/gcn-recomp/runtime" OR
   NOT EXISTS "${MKDD_GENERATED}/sources.cmake")
  message(FATAL_ERROR "Supply pinned MKDD_RUNTIME and private MKDD_GENERATED output")
endif()
set(RT "${MKDD_RUNTIME}/gcn-recomp/runtime")
set(AND_DIR "${MKDD_RUNTIME}/gcn-recomp/android")
set(GLAD "${RT}/gx/glad_es")
set(RECOMP_GEN_DIR "${MKDD_GENERATED}")
include("${MKDD_GENERATED}/sources.cmake")
add_library(game_recomp STATIC ${RECOMP_SOURCES})
target_include_directories(game_recomp PUBLIC "${RT}" "${MKDD_GENERATED}")
target_compile_definitions(game_recomp PUBLIC GCN_MEM_BASE=0x2000000000ull)
target_compile_options(game_recomp PRIVATE -O2 -fno-strict-aliasing -ffp-contract=off
  -Wno-unused-label -Wno-unused-variable -Wno-parentheses-equality)

# Same upstream core sources and excludes as gcn_add_game. The flat harness uses
# the Android GLES/AAudio path; no headset loader or VR app entry point.
file(GLOB_RECURSE CORE CONFIGURE_DEPENDS "${RT}/*.cpp")
list(REMOVE_ITEM CORE "${RT}/main.cpp" "${RT}/bench_main.cpp" "${RT}/input_map.cpp"
  "${RT}/ffb.cpp" "${RT}/audio_sdl.cpp" "${RT}/audio_stub.cpp" "${RT}/record.cpp"
  "${RT}/gx/render_stub.cpp")
add_library(mkdd_objects OBJECT ${CORE} "${AND_DIR}/audio_aaudio.cpp" "${GLAD}/src/gles2.c"
  lower_state.cpp lower_bridge.cpp frame_hook.cpp)
target_include_directories(mkdd_objects PUBLIC "${RT}" "${MKDD_GENERATED}" "${GLAD}/include" "${AND_DIR}")
target_compile_definitions(mkdd_objects PUBLIC GCN_GL_ES GCN_DISC_ID="GM4E01"
  GCN_GAME_TITLE="Mario Kart: Double Dash!!" GCN_LOG_TAG="WiiDS-MKDD")
if(MKDD_PLUS_BUILD)
  target_compile_definitions(mkdd_objects PUBLIC WIIDS_MKDD_PLUS=1)
endif()
target_compile_options(mkdd_objects PRIVATE -fno-strict-aliasing -ffp-contract=off)
target_link_libraries(mkdd_objects PUBLIC game_recomp)

add_library(wiids_mkdd SHARED $<TARGET_OBJECTS:mkdd_objects>)
target_link_libraries(wiids_mkdd PRIVATE game_recomp EGL GLESv3 log android aaudio)
target_link_options(wiids_mkdd PRIVATE -Wl,--no-undefined)

add_executable(mkdd_egl "${AND_DIR}/egl_main.cpp")
target_link_libraries(mkdd_egl PRIVATE mkdd_objects game_recomp EGL GLESv3 log android aaudio)
target_compile_definitions(mkdd_egl PRIVATE GCN_DEFAULT_ISO="game.iso" GCN_APP_LABEL="WiiDS Double Dash lab" GCN_GAME_FPS=60)

add_executable(lower_state_test lower_state.cpp ../tests/lower_state_test.cpp)

# The flat Thor game: NativeActivity loads this library and calls android_main (flat_main.cpp).
# The same library carries the JNI lower-screen reader, so Java's lower display shares the process.
set(GLUE "${ANDROID_NDK}/sources/android/native_app_glue")
add_library(wiids_mkdd_game SHARED $<TARGET_OBJECTS:mkdd_objects> flat_main.cpp
  widescreen_hud.cpp
  "${GLUE}/android_native_app_glue.c")
target_include_directories(wiids_mkdd_game PRIVATE "${GLUE}"
  $<TARGET_PROPERTY:mkdd_objects,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(wiids_mkdd_game PRIVATE
  $<TARGET_PROPERTY:mkdd_objects,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(wiids_mkdd_game PRIVATE game_recomp EGL GLESv3 log android aaudio)
target_link_options(wiids_mkdd_game PRIVATE -Wl,--no-undefined -u ANativeActivity_onCreate
  -Wl,-z,max-page-size=16384)

# The owner-disc fingerprint header is PRIVATE metadata, generated beside the translated C.
if(NOT EXISTS "${MKDD_GENERATED}/wiids_hud_profile.h")
  message(FATAL_ERROR "Run native/hud_profile.py with your verified disc and private generated output first")
endif()
