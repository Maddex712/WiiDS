add_library(aurora_core STATIC
        lib/aurora.cpp
        lib/input.cpp
        lib/window.cpp
        lib/logging.cpp
        lib/system_info.cpp
        lib/system_info.hpp
)
add_library(aurora::core ALIAS aurora_core)
set_target_properties(aurora_core PROPERTIES FOLDER "aurora")

target_compile_definitions(aurora_core PUBLIC AURORA TARGET_PC)
target_include_directories(aurora_core PUBLIC include)
target_link_libraries(aurora_core PUBLIC fmt::fmt ${AURORA_SDL3_TARGET} xxhash)
target_link_libraries(aurora_core PRIVATE absl::btree absl::flat_hash_map sqlite3 TracyClient)
if (AURORA_ENABLE_GX AND AURORA_CACHE_USE_ZSTD)
    target_compile_definitions(aurora_core PRIVATE AURORA_CACHE_USE_ZSTD)
    target_link_libraries(aurora_core PRIVATE libzstd_static)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL Windows)
    # stuff for fetching system info.
    target_link_libraries(aurora_core PRIVATE ntdll dxgi advapi32 user32)
elseif (APPLE)
    target_sources(aurora_core PRIVATE lib/system_info_mac.mm)
endif ()

if (AURORA_ENABLE_GX)
    target_sources(aurora_core PRIVATE lib/imgui.cpp)
    target_link_libraries(aurora_core PUBLIC imgui)
endif ()

if (AURORA_ENABLE_GX)
    target_compile_definitions(aurora_core PUBLIC AURORA_ENABLE_GX WEBGPU_DAWN)
    # BackendBinding.cpp does platform surface setup specifically for Dawn's own backend
    # selection (X11/Wayland/Win32/Cocoa) - no Switch equivalent, genuinely excluded.
    # gpu.cpp turns out to be almost entirely portable wgpu:: C++ calls already - only one small
    # block (inside initialize(), already gated `#if defined(WEBGPU_DAWN) && !defined(__MINGW32__)`)
    # touches dawn/native/DawnNative.h directly (for a Dawn-specific instance-descriptor tweak);
    # that block now also excludes __SWITCH__ (see the guard in gpu.cpp itself), so the file
    # compiles for Switch unmodified otherwise - confirmed by re-including it here.
    # gpu_cache.cpp stays unconditional -
    # it's a generic SQLite blob cache with zero direct wgpu::/dawn:: dependency, reusable by any
    # backend including deko3d's future uam-shader cache.
    target_sources(aurora_core PRIVATE lib/webgpu/gpu_cache.cpp lib/webgpu/gpu.cpp)
    if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
        target_sources(aurora_core PRIVATE lib/switch/surface_switch.cpp lib/switch/gl_proc_table.c
            lib/switch/input_switch.cpp lib/switch/deko3d_selftest.cpp)
    else ()
        target_sources(aurora_core PRIVATE lib/dawn/BackendBinding.cpp)
    endif ()
    target_link_libraries(aurora_core PRIVATE dawn::webgpu_dawn)
    if (DAWN_ENABLE_VULKAN)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_VULKAN)
    endif ()
    if (DAWN_ENABLE_METAL)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_METAL)
        target_sources(aurora_core PRIVATE lib/dawn/MetalBinding.mm)
        set_source_files_properties(lib/dawn/MetalBinding.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
        target_link_options(aurora_core PUBLIC "LINKER:-weak_framework,Metal")
    endif ()
    if (DAWN_ENABLE_D3D11)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_D3D11)
    endif ()
    if (DAWN_ENABLE_D3D12)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_D3D12)
    endif ()
    if (DAWN_ENABLE_DESKTOP_GL OR DAWN_ENABLE_OPENGLES)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_OPENGL)
        if (DAWN_ENABLE_DESKTOP_GL)
            target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_DESKTOP_GL)
        endif ()
        if (DAWN_ENABLE_OPENGLES)
            target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_OPENGLES)
        endif ()
    endif ()
    if (DAWN_ENABLE_NULL)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_NULL)
    endif ()
endif ()
