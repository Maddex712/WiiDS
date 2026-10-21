cmake_minimum_required(VERSION 3.20)
project(wiids_art CXX)

# Builds each game's companion-screen art on the device from the player's imported disc.
# zlib comes from the platform (Android NDK libz) or WIIDS_ART_ZLIB_TARGET.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(wiids_art STATIC src/art_core.cpp src/art_util.cpp src/profile_nsmb_sms.cpp src/art_layout.cpp src/profile_mkw.cpp src/art_audio.cpp src/art_banner.cpp src/art_canvas.cpp src/art_cases.cpp src/art_maps.cpp)
# Pixels and samples must match the PC tools bit for bit: no fused multiply-add.
if(NOT MSVC)
  target_compile_options(wiids_art PRIVATE -ffp-contract=off)
endif()
target_include_directories(wiids_art PUBLIC include)
if(WIIDS_ART_ZLIB_TARGET)
  target_link_libraries(wiids_art PRIVATE ${WIIDS_ART_ZLIB_TARGET})
else()
  find_package(ZLIB REQUIRED)
  target_link_libraries(wiids_art PRIVATE ZLIB::ZLIB)
endif()

option(WIIDS_ART_TOOL "Build the wiids_art_tool host test program" OFF)
if(WIIDS_ART_TOOL)
  add_executable(wiids_art_tool tools/wiids_art_tool.cpp)
  target_link_libraries(wiids_art_tool PRIVATE wiids_art)
endif()
