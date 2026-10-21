cmake_minimum_required(VERSION 3.22)
project(driftdroid_android_native)

# Placeholder libwii.so target for the P1 toolchain de-risking step.
# The real runtime (runtime/ + aurora-main) gets wired in here during P3/P4
# via add_subdirectory, replacing this hello-world stub.

add_library(wii SHARED
    hello.cpp
)

target_compile_features(wii PRIVATE cxx_std_20)

find_library(log-lib log)
target_link_libraries(wii PRIVATE ${log-lib})
# 16 KiB pages keep the library loadable on Android 15+ devices that use them.
target_link_options(wii PRIVATE "LINKER:-z,max-page-size=16384")

# libwiidsdisc.so: reads the player's own disc images (ISO, WBFS, RVZ, WIA, GCZ, CISO) for the
# Games screen and extracts them. Separate from the game runtimes so they stay untouched.
set(WIIDS_DISC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../wiids/disc)
find_library(z-lib z)
set(WIIDS_DISC_ZLIB_TARGET ${z-lib})
add_subdirectory(${WIIDS_DISC_DIR} wiids_disc)
# The companion-screen art builder (wiids/art) lives in the same library.
set(WIIDS_ART_DIR ${WIIDS_DISC_DIR}/../art)
set(WIIDS_ART_ZLIB_TARGET ${z-lib})
add_subdirectory(${WIIDS_ART_DIR} wiids_art)
add_library(wiidsdisc SHARED ${WIIDS_DISC_DIR}/jni/wiids_disc_jni.cpp ${WIIDS_ART_DIR}/jni/wiids_art_jni.cpp)
target_link_libraries(wiidsdisc PRIVATE wiids_disc wiids_art)
target_link_options(wiidsdisc PRIVATE "LINKER:-z,max-page-size=16384")
