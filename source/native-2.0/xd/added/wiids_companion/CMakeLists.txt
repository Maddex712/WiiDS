cmake_minimum_required(VERSION 3.22)
project(wiids_xd_companion LANGUAGES CXX)
add_library(wiids_xd_companion SHARED xd_companion.cpp xd_chain.cpp)
target_compile_features(wiids_xd_companion PRIVATE cxx_std_17)
target_compile_options(wiids_xd_companion PRIVATE -Wall -Wextra -Werror)
# This is the companion transport, NOT a game executable or a substitute game runtime.
if(ANDROID)
  target_sources(wiids_xd_companion PRIVATE xd_jni.cpp)
endif()
