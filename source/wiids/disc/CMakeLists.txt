cmake_minimum_required(VERSION 3.20)
project(wiids_disc C CXX)

# GameCube/Wii image reader + extractor shared by every WiiDS game.
# Vendored decoders (see third_party/*/LICENSE|COPYING): liblzma (public domain, Dolphin's copy),
# bzip2, Zstandard (decompression only). zlib comes from the platform (Android NDK libz) or
# WIIDS_DISC_ZLIB_TARGET when the host has none.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_subdirectory(${TP}/liblzma ${CMAKE_CURRENT_BINARY_DIR}/liblzma EXCLUDE_FROM_ALL)

add_library(wiids_bz2 STATIC
  ${TP}/bzip2/blocksort.c ${TP}/bzip2/bzlib.c ${TP}/bzip2/compress.c ${TP}/bzip2/crctable.c
  ${TP}/bzip2/decompress.c ${TP}/bzip2/huffman.c ${TP}/bzip2/randtable.c)
target_include_directories(wiids_bz2 PUBLIC ${TP}/bzip2)
if(MSVC)
  target_compile_definitions(wiids_bz2 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

file(GLOB WIIDS_ZSTD_SRCS ${TP}/zstd/common/*.c ${TP}/zstd/decompress/*.c)
add_library(wiids_zstd STATIC ${WIIDS_ZSTD_SRCS})
target_include_directories(wiids_zstd PUBLIC ${TP}/zstd)
target_compile_definitions(wiids_zstd PRIVATE ZSTD_DISABLE_ASM ZSTD_MULTITHREAD=0 ZSTD_LEGACY_SUPPORT=0)

add_library(wiids_disc STATIC src/wiids_disc.cpp src/aes128.cpp)
target_include_directories(wiids_disc PUBLIC include)
target_link_libraries(wiids_disc PRIVATE lzma wiids_bz2 wiids_zstd)

if(WIIDS_DISC_ZLIB_TARGET)
  target_link_libraries(wiids_disc PRIVATE ${WIIDS_DISC_ZLIB_TARGET})
else()
  find_package(ZLIB REQUIRED)
  target_link_libraries(wiids_disc PRIVATE ZLIB::ZLIB)
endif()

option(WIIDS_DISC_TOOL "Build the wiids_disc_tool host test program" OFF)
if(WIIDS_DISC_TOOL)
  add_executable(wiids_disc_tool tools/wiids_disc_tool.cpp)
  target_link_libraries(wiids_disc_tool PRIVATE wiids_disc)
endif()
