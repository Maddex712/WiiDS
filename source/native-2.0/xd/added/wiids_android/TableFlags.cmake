execute_process(COMMAND "${CMAKE_C_COMPILER}" -print-resource-dir
  OUTPUT_VARIABLE _xd_clang_res OUTPUT_STRIP_TRAILING_WHITESPACE)
set(_xd_android_inc "${CMAKE_SYSROOT}/usr/include")
set(_xd_linux_first "${CMAKE_BINARY_DIR}/linux_first")
file(MAKE_DIRECTORY "${_xd_linux_first}/sys")
file(WRITE "${_xd_linux_first}/stdbool.h" "#pragma once\n#include \"${_xd_clang_res}/include/stdbool.h\"\n")
file(WRITE "${_xd_linux_first}/sys/types.h" "#pragma once\n#include \"${_xd_android_inc}/sys/types.h\"\n")
file(WRITE "${_xd_dt_dir}/flags.json.new" "{
\"defines\": [\"AURORA\", \"AURORA_ENABLE_GX\", \"TARGET_PC=1\", \"WEBGPU_DAWN\", \"XD_PORT=1\", \"__ANDROID_API__=30\"],
\"includes\": [\"${PROJECT_SOURCE_DIR}/wiids_android\", \"${PROJECT_SOURCE_DIR}/wiids_companion\", \"${PROJECT_SOURCE_DIR}/platform/compat/include\", \"${PROJECT_SOURCE_DIR}/third_party/aurora/include\", \"${XD_SNAPSHOT_DIR}/include\", \"${PROJECT_SOURCE_DIR}/platform/sdk/include\"],
\"force_includes\": [\"${CMAKE_CURRENT_SOURCE_DIR}/src/game_code_seg.h\", \"${PROJECT_SOURCE_DIR}/platform/compat/include/cw_compat.h\", \"${PROJECT_SOURCE_DIR}/platform/sdk/include/sdk_port.h\"],
\"first_includes\": [\"${_xd_linux_first}\"],
\"system_includes\": [\"${CMAKE_SYSROOT}/usr/include/c++/v1\", \"${_xd_clang_res}/include\", \"${_xd_android_inc}/aarch64-linux-android\", \"${_xd_android_inc}\"],
\"gc_system_includes\": [\"${CMAKE_SYSROOT}/usr/include/c++/v1\", \"${_xd_clang_res}/include\", \"${_xd_android_inc}/arm-linux-androideabi\", \"${_xd_android_inc}\"],
\"pc_target\": \"aarch64-linux-android30\", \"gc_target\": \"armv7a-linux-androideabi30\"}")
