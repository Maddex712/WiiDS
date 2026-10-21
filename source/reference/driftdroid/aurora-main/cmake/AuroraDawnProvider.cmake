include_guard(GLOBAL)

# Switch: Dawn is built separately for devkitA64 with only its OpenGL ES backend, which runs on
# devkitPro's switch-mesa (EGL + GLES 3.x over nouveau). Source is google/dawn at the exact commit
# the other platforms' prebuilt packages use, plus small Switch patches (platform detection, EGL
# surface from nwindowGetDefault(), abseil newlib fixes).
# This is the first-playable renderer; a deko3d backend (aurora-main/lib/deko3d/) replaces it
# later for 60fps.
if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
  set(AURORA_SWITCH_DAWN_SOURCE_DIR "$ENV{HOME}/switch-dawn/dawn" CACHE PATH "Patched Dawn source for Switch")
  set(AURORA_SWITCH_DAWN_BUILD_DIR "$ENV{HOME}/switch-dawn/build-switch" CACHE PATH "Dawn build tree for Switch")
  set(_switch_dawn_lib "${AURORA_SWITCH_DAWN_BUILD_DIR}/src/dawn/native/libwebgpu_dawn.a")
  # uam: deko3d's GLSL -> DKSH compiler, used by Dawn's deko3d backend to build shaders at runtime.
  set(AURORA_SWITCH_UAM_LIB "$ENV{HOME}/uam/build-switch/libuamcore.a" CACHE FILEPATH "uam static library for Switch")
  if (NOT EXISTS "${_switch_dawn_lib}")
    message(FATAL_ERROR "Switch Dawn library not found at ${_switch_dawn_lib}; build it first.")
  endif ()
  if (NOT TARGET dawn::webgpu_dawn)
    add_library(aurora_switch_dawn STATIC IMPORTED GLOBAL)
    set_target_properties(aurora_switch_dawn PROPERTIES IMPORTED_LOCATION "${_switch_dawn_lib}")
    target_include_directories(aurora_switch_dawn INTERFACE
      "${AURORA_SWITCH_DAWN_SOURCE_DIR}/include"
      "${AURORA_SWITCH_DAWN_BUILD_DIR}/gen/include")
    # Mesa's static archives reference each other, hence the group.
    # deko3d ships a debug build (deko3dd) and a release build (deko3d). The debug one validates
    # every API call and aborts on failure - useful while bringing the backend up, but it is pure
    # CPU overhead on a renderer that issues thousands of calls a frame, and this port is
    # CPU-bound. Default to release; set AURORA_SWITCH_DEKO3D_DEBUG=ON to get the validation (and
    # the "dkmem" style aborts) back when diagnosing a graphics problem.
    option(AURORA_SWITCH_DEKO3D_DEBUG "Link the validating debug build of deko3d" OFF)
    if (AURORA_SWITCH_DEKO3D_DEBUG)
      set(_aurora_deko3d_lib deko3dd)
    else ()
      set(_aurora_deko3d_lib deko3d)
    endif ()
    target_link_libraries(aurora_switch_dawn INTERFACE
      -Wl,--start-group "${AURORA_SWITCH_UAM_LIB}" ${_aurora_deko3d_lib} EGL GLESv2 glapi drm_nouveau -Wl,--end-group)
    target_link_directories(aurora_switch_dawn INTERFACE "$ENV{DEVKITPRO}/portlibs/switch/lib")
    add_library(dawn::webgpu_dawn ALIAS aurora_switch_dawn)
  endif ()
  add_library(dawn_dawncpp_headers INTERFACE IMPORTED GLOBAL)
  target_include_directories(dawn_dawncpp_headers INTERFACE
    "${AURORA_SWITCH_DAWN_SOURCE_DIR}/include"
    "${AURORA_SWITCH_DAWN_BUILD_DIR}/gen/include")
  add_library(dawn::dawncpp_headers ALIAS dawn_dawncpp_headers)
  set(DAWN_ENABLE_D3D12 OFF CACHE INTERNAL "")
  set(DAWN_ENABLE_D3D11 OFF CACHE INTERNAL "")
  set(DAWN_ENABLE_VULKAN OFF CACHE INTERNAL "")
  set(DAWN_ENABLE_METAL OFF CACHE INTERNAL "")
  set(DAWN_ENABLE_DESKTOP_GL OFF CACHE INTERNAL "")
  set(DAWN_ENABLE_OPENGLES ON CACHE INTERNAL "")
  set(DAWN_ENABLE_NULL OFF CACHE INTERNAL "")
  set(AURORA_DAWN_IS_SHARED FALSE PARENT_SCOPE)
  return()
endif ()

# Resolve Dawn/WebGPU dependency based on AURORA_DAWN_PROVIDER and AURORA_DAWN_LINKAGE.
#
# After this module runs:
#   - dawn::webgpu_dawn target is available
#   - dawn::dawncpp_headers target is available (for tests)
#   - DAWN_ENABLE_* backend variables are set for the target platform
#   - AURORA_DAWN_IS_SHARED is set to TRUE/FALSE

# When using a non-vendored Dawn, we don't get DAWN_ENABLE_* from its build.
# Infer from the target platform instead.
function(_aurora_dawn_set_platform_backends)
  if (WIN32)
    set(DAWN_ENABLE_D3D12 ON CACHE INTERNAL "")
    # Disable D3D11 because this Aurora fork does not support it.
    set(DAWN_ENABLE_D3D11 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_VULKAN ON CACHE INTERNAL "")
    set(DAWN_ENABLE_METAL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_OPENGLES OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_NULL ON CACHE INTERNAL "")
  elseif (APPLE)
    set(DAWN_ENABLE_D3D12 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_D3D11 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_VULKAN OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_METAL ON CACHE INTERNAL "")
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_OPENGLES OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_NULL ON CACHE INTERNAL "")
  elseif (ANDROID)
    # No case existed for Android before this, so it fell into the "Linux / other" branch below
    # and inherited DAWN_ENABLE_NULL ON - a desktop CI/testing default that has no business in an
    # end-user mobile build. Confirmed as the real root cause of a device crash (GitHub issue #3,
    # PowerVR B-Series GPU): when that device's Vulkan adapter request failed (a driver problem
    # this codebase can't fix), Aurora's backend fallback loop (aurora-main/lib/aurora.cpp,
    # PreferredBackendOrder) happily tried Null next since it was compiled in, "succeeded" at
    # creating a no-op CPU backend, and only crashed later with a confusing low-level WebGPU
    # surface-configuration error instead of ever surfacing "Vulkan isn't available" clearly.
    # With DAWN_ENABLE_NULL OFF here, Null is never in PreferredBackendOrder on Android at all, so
    # a real Vulkan failure fails cleanly instead of masquerading as a worse failure downstream.
    set(DAWN_ENABLE_D3D12 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_D3D11 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_VULKAN ON CACHE INTERNAL "")
    set(DAWN_ENABLE_METAL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE INTERNAL "")
    # The real fallback for the devices in issue #3: a phone whose Vulkan driver is missing or
    # broken can still run GLES. The prebuilt Dawn for Android already contains this backend.
    set(DAWN_ENABLE_OPENGLES ON CACHE INTERNAL "")
    set(DAWN_ENABLE_NULL OFF CACHE INTERNAL "")
  else () # Linux / other
    set(DAWN_ENABLE_D3D12 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_D3D11 OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_VULKAN ON CACHE INTERNAL "")
    set(DAWN_ENABLE_METAL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_OPENGLES OFF CACHE INTERNAL "")
    set(DAWN_ENABLE_NULL ON CACHE INTERNAL "")
  endif ()
endfunction()

# ── Auto: resolve provider based on platform availability ──
set(_aurora_dawn_provider "${AURORA_DAWN_PROVIDER}")
if (_aurora_dawn_provider STREQUAL "auto")
  # Prebuilt Dawn packages available for: windows-{amd64,arm64}, linux-{x86_64,aarch64}, darwin-{arm64,x86_64}
  set(_has_package FALSE)
  if (WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|ARM64|aarch64)$")
    set(_has_package TRUE)
  elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|aarch64)$")
    set(_has_package TRUE)
  elseif (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|x86_64)$")
    set(_has_package TRUE)
  endif ()

  if (_has_package)
    set(_aurora_dawn_provider "package")
  else ()
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    find_package(Dawn QUIET)
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL OFF)
    if (Dawn_FOUND)
      set(_aurora_dawn_provider "system")
    else ()
      set(_aurora_dawn_provider "vendor")
    endif ()
  endif ()
  set(AURORA_DAWN_PROVIDER "${_aurora_dawn_provider}" CACHE STRING "" FORCE)
  message(STATUS "aurora: Dawn auto-resolved provider: ${_aurora_dawn_provider}")
endif ()

if (_aurora_dawn_provider STREQUAL "vendor")
  # ── Vendor: FetchContent build from source ──
  if (NOT TARGET webgpu_dawn)
    message(STATUS "aurora: Fetching Dawn (provider=vendor, linkage=${AURORA_DAWN_LINKAGE})")

    if (AURORA_DAWN_LINKAGE STREQUAL "shared")
      set(DAWN_BUILD_MONOLITHIC_LIBRARY SHARED CACHE INTERNAL
        "Bundle all dawn components into a single shared library.")
    else ()
      set(DAWN_BUILD_MONOLITHIC_LIBRARY STATIC CACHE INTERNAL
        "Bundle all dawn components into a single static library.")
    endif ()
    set(DAWN_BUILD_SAMPLES OFF CACHE INTERNAL "Disable Dawn sample applications")
    set(DAWN_BUILD_BENCHMARKS OFF CACHE INTERNAL "Disable Dawn benchmarks")
    set(DAWN_SUPPORTS_GLFW_FOR_WINDOWING OFF CACHE INTERNAL "Disable Dawn GLFW windowing")
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE INTERNAL "Enable compilation of the OpenGL backend")
    set(DAWN_ENABLE_OPENGLES OFF CACHE INTERNAL "Enable compilation of the OpenGL ES backend")
    set(DAWN_FETCH_DEPENDENCIES ON CACHE INTERNAL
      "Use fetch_dawn_dependencies.py as an alternative to using depot_tools")
    if (CMAKE_SYSTEM_NAME STREQUAL Linux)
      set(DAWN_USE_WAYLAND ON CACHE INTERNAL "Enable support for Wayland surface")
    endif ()
    set(TINT_BUILD_TESTS OFF CACHE INTERNAL "Build tests")
    set(TINT_BUILD_CMD_TOOLS OFF CACHE INTERNAL "Build the Tint command line tools")
    if (NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL$")
      set(ABSL_MSVC_STATIC_RUNTIME OFF CACHE INTERNAL "Link static runtime libraries")
    else ()
      set(ABSL_MSVC_STATIC_RUNTIME ON CACHE INTERNAL "Link static runtime libraries")
    endif ()

    include(FetchContent)
    FetchContent_Declare(dawn
      URL "https://github.com/google/dawn/archive/refs/tags/${AURORA_DAWN_VERSION}.tar.gz"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(dawn)
    if (NOT TARGET webgpu_dawn)
      message(FATAL_ERROR "Failed to make dawn available")
    endif ()
  else ()
    message(STATUS "aurora: Using existing Dawn")
  endif ()

  if (AURORA_DAWN_LINKAGE STREQUAL "shared")
    set(AURORA_DAWN_IS_SHARED TRUE PARENT_SCOPE)
  else ()
    set(AURORA_DAWN_IS_SHARED FALSE PARENT_SCOPE)
  endif ()

elseif (_aurora_dawn_provider STREQUAL "system")
  # ── System: find_package(Dawn) ──
  message(STATUS "aurora: Using system Dawn (provider=system)")
  if (NOT Dawn_FOUND)
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    find_package(Dawn REQUIRED)
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL OFF)
  endif ()
  if (NOT TARGET dawn::webgpu_dawn)
    message(FATAL_ERROR "find_package(Dawn) succeeded but dawn::webgpu_dawn target not found")
  endif ()
  _aurora_dawn_set_platform_backends()

  get_target_property(_dawn_type dawn::webgpu_dawn TYPE)
  if (_dawn_type STREQUAL "SHARED_LIBRARY")
    set(AURORA_DAWN_IS_SHARED TRUE PARENT_SCOPE)
  else ()
    set(AURORA_DAWN_IS_SHARED FALSE PARENT_SCOPE)
  endif ()

elseif (_aurora_dawn_provider STREQUAL "package")
  # ── Package: download prebuilt Dawn install tree ──
  if (NOT AURORA_DAWN_PACKAGE_URL)
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _dawn_system)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _dawn_arch)
    if (_dawn_system STREQUAL "windows")
      if (_dawn_arch STREQUAL "x86_64")
        set(_dawn_arch "amd64")
      elseif (_dawn_arch STREQUAL "aarch64")
        set(_dawn_arch "arm64")
      endif ()
    endif ()
    set(AURORA_DAWN_PACKAGE_URL
      "https://github.com/encounter/dawn-build/releases/download/${AURORA_DAWN_VERSION}/dawn-${_dawn_system}-${_dawn_arch}.tar.gz")

    # A release asset is mutable: the same tag has already served two different windows-amd64 archives,
    # and a cached extraction is never re-verified. Pin the digest for the combinations we ship.
    if (NOT AURORA_DAWN_PACKAGE_URL_HASH
        AND AURORA_DAWN_VERSION STREQUAL "v20260603.191052"
        AND _dawn_system STREQUAL "windows" AND _dawn_arch STREQUAL "amd64")
      set(AURORA_DAWN_PACKAGE_URL_HASH
        "SHA256=7785373d569b3b0237918ec9c523239f7d0667857c5ea8242e3cdfde95e6aeab")
    endif ()
  endif ()
  message(STATUS "aurora: Fetching prebuilt Dawn package from ${AURORA_DAWN_PACKAGE_URL}")

  set(_dawn_prebuilt_hash_argument "")
  if (AURORA_DAWN_PACKAGE_URL_HASH)
    set(_dawn_prebuilt_hash_argument URL_HASH "${AURORA_DAWN_PACKAGE_URL_HASH}")
  endif ()

  include(FetchContent)
  FetchContent_Declare(dawn_prebuilt
    URL "${AURORA_DAWN_PACKAGE_URL}"
    ${_dawn_prebuilt_hash_argument}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(dawn_prebuilt)

  # Detect package layout: may be nested in a subdirectory
  set(_dawn_pkg_dir "${dawn_prebuilt_SOURCE_DIR}")
  file(GLOB _dawn_subdirs
    "${dawn_prebuilt_SOURCE_DIR}/dawn-*"
    "${dawn_prebuilt_SOURCE_DIR}/Dawn-*"
  )
  foreach (_sd IN LISTS _dawn_subdirs)
    if (IS_DIRECTORY "${_sd}")
      set(_dawn_pkg_dir "${_sd}")
      break()
    endif ()
  endforeach ()
  if (IS_DIRECTORY "${dawn_prebuilt_SOURCE_DIR}/dawn-install")
    set(_dawn_pkg_dir "${dawn_prebuilt_SOURCE_DIR}/dawn-install")
  endif ()

  # Static Dawn packages may link Threads::Threads
  find_package(Threads QUIET)

  # Find DawnConfig.cmake in the package
  set(_dawn_cmake_found FALSE)
  foreach (_cmake_path
    "${_dawn_pkg_dir}/lib/cmake/Dawn"
    "${_dawn_pkg_dir}/lib64/cmake/Dawn"
    "${_dawn_pkg_dir}/cmake/Dawn"
    "${_dawn_pkg_dir}/cmake"
  )
    if (EXISTS "${_cmake_path}/DawnConfig.cmake")
      set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
      find_package(Dawn REQUIRED CONFIG PATHS "${_cmake_path}" NO_DEFAULT_PATH)
      set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL OFF)
      set(_dawn_cmake_found TRUE)
      break()
    endif ()
  endforeach ()

  if (NOT _dawn_cmake_found OR NOT TARGET dawn::webgpu_dawn)
    message(FATAL_ERROR
      "Dawn package does not contain DawnConfig.cmake.\n"
      "Searched: ${_dawn_pkg_dir}/{lib,lib64,cmake}/cmake/Dawn/\n"
      "The package must be a Dawn install tree built with DAWN_ENABLE_INSTALL=ON.")
  endif ()

  _aurora_dawn_set_platform_backends()

  get_target_property(_dawn_pkg_type dawn::webgpu_dawn TYPE)
  if (_dawn_pkg_type STREQUAL "SHARED_LIBRARY")
    set(AURORA_DAWN_IS_SHARED TRUE PARENT_SCOPE)

  else ()
    set(AURORA_DAWN_IS_SHARED FALSE PARENT_SCOPE)
  endif ()

else ()
  message(FATAL_ERROR "Invalid AURORA_DAWN_PROVIDER: ${AURORA_DAWN_PROVIDER} "
    "(must be auto, vendor, system, or package)")
endif ()

# Ensure dawn::dawncpp_headers target exists (needed by tests).
# When Dawn is vendored, this target is created by Dawn's CMake.
# For package/system providers, create it from the same include dirs.
if (NOT TARGET dawn::dawncpp_headers)
  add_library(dawn_dawncpp_headers INTERFACE IMPORTED GLOBAL)
  get_target_property(_dawn_inc dawn::webgpu_dawn INTERFACE_INCLUDE_DIRECTORIES)
  if (NOT _dawn_inc)
    if (TARGET dawn::dawn_public_config)
      get_target_property(_dawn_inc dawn::dawn_public_config INTERFACE_INCLUDE_DIRECTORIES)
    endif ()
  endif ()
  if (_dawn_inc)
    target_include_directories(dawn_dawncpp_headers INTERFACE ${_dawn_inc})
  else ()
    target_link_libraries(dawn_dawncpp_headers INTERFACE dawn::webgpu_dawn)
  endif ()
  add_library(dawn::dawncpp_headers ALIAS dawn_dawncpp_headers)
endif ()
