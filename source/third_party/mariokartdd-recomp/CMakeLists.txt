cmake_minimum_required(VERSION 3.20)
project(mkdd C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Everything that is not specific to Mario Kart: Double Dash!! lives in gcn-recomp (a
# submodule): the recompiler, the GameCube runtime, the renderer and the frontends. See its
# README.
add_subdirectory(gcn-recomp)
gcn_enable_languages()

gcn_add_game(
  NAME mkdd
  TITLE "Mario Kart: Double Dash!!"
  LABEL "Double Dash Recomp"
  DISC_ID GM4E01
  DOL_SHA1 e389e50357d4a5076ab3d38672204658f411123d
  SYMBOLS ${CMAKE_SOURCE_DIR}/analysis/symbols.txt
  TABLES ${CMAKE_SOURCE_DIR}/recomp
  FPS 60
  # The shader keys a play-through uses, compiled on a fresh install before the game starts:
  # made by gcn-recomp/tools/shader_keys from the Vision Pro's cache (docs/dev/vr.md).
  SHADER_KEYS ${CMAKE_SOURCE_DIR}/src/shader_keys.bin
  # The game's half of the headset and the motion rig: when to be in stereo, the world's
  # scale, what the eyes leave out, and the kart's motion (docs/dev/vr.md); and where its
  # sticks respond, for controls.txt (docs/dev/controls.md).
  SOURCES ${CMAKE_SOURCE_DIR}/src/race.cpp ${CMAKE_SOURCE_DIR}/src/vr_mkdd.cpp ${CMAKE_SOURCE_DIR}/src/motion.cpp
          ${CMAKE_SOURCE_DIR}/src/controls.cpp
  # The headset app and its harness, on an Android build.
  ANDROID_PACKAGE com.ethannicholas.mkdd
  # The Vision Pro app, on a visionOS build: see build-visionos.sh.
  VISIONOS_BUNDLE_ID com.ethannicholas.mkdd)
