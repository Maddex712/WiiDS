include(FetchContent)

# Don't build tests for third party dependencies
set(BUILD_TESTING OFF CACHE INTERNAL "Build tests")
set(SKIP_INSTALL_ALL ON)
if (NOT DEFINED BUILD_SHARED_LIBS)
  set(_USE_SHARED ON)
else ()
  set(_USE_SHARED OFF)
endif ()

function(aurora_find_package_global)
  set(_PREV_FIND_PACKAGE_TARGETS_GLOBAL ${CMAKE_FIND_PACKAGE_TARGETS_GLOBAL})
  set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
  find_package(${ARGV})
  set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ${_PREV_FIND_PACKAGE_TARGETS_GLOBAL})
endfunction()

if (AURORA_ENABLE_GX)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/AuroraDawnProvider.cmake)
endif ()

# Abseil is needed for core libraries. It normally comes via Dawn's vendor build.
# Otherwise prefer a system package and only fetch it as a last resort.
if (NOT TARGET absl::flat_hash_map OR NOT TARGET absl::btree)
  aurora_find_package_global(absl CONFIG QUIET GLOBAL)
endif ()

if (TARGET absl::flat_hash_map AND TARGET absl::btree)
  message(STATUS "aurora: Using system/existing abseil")
else ()
  message(STATUS "aurora: Fetching abseil-cpp (standalone)")
  if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL$")
    set(ABSL_MSVC_STATIC_RUNTIME OFF CACHE INTERNAL "Link static runtime libraries")
  else()
    set(ABSL_MSVC_STATIC_RUNTIME ON CACHE INTERNAL "Link static runtime libraries")
  endif()
  set(ABSL_PROPAGATE_CXX_STD ON)
  set(_aurora_abseil_patch_command "")
  if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    # Three separate Switch/devkitA64 gaps, bundled into one patch (see the patch file itself for
    # full rationale on each): (1) struct tm has no tm_gmtoff member under any name (newlib never
    # defines __TM_GMTOFF) - cctz's time_zone_libc.cc SFINAE detection fails, fixed by extending
    # the existing __native_client__/__myriad2__/__EMSCRIPTEN__ branch, which already uses the
    # matching '_timezone'/'tzname' fallback. (2) sysinfo.cc's GetTID() fallback assumes pthread_t
    # is arithmetic; devkitA64's is a pointer, and (unlike the __native_client__ branch this file
    # already has for that) sizeof(pid_t) != sizeof(pthread_t) here - uses libnx's
    # threadGetCurHandle() instead. (3) elf_mem_image.h needs <link.h> (glibc's dynamic-linker
    # ELF introspection), which doesn't exist on a target with no dynamic linking at all - added
    # to the existing platform exclusion list alongside OpenBSD/QNX/NaCl/etc.
    set(_aurora_abseil_patch_command PATCH_COMMAND git apply --verbose
      "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/patches/abseil-nintendo-switch-platform.patch")
  endif ()
  FetchContent_Declare(abseil-cpp
    URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
    ${_aurora_abseil_patch_command}
  )
  FetchContent_MakeAvailable(abseil-cpp)
  if (NOT TARGET absl::flat_hash_map OR NOT TARGET absl::btree)
    message(FATAL_ERROR "Failed to make abseil-cpp available")
  endif ()
endif ()

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/AuroraSDL3Provider.cmake)

if (NOT TARGET xxhash)
  message(STATUS "aurora: Fetching xxhash")
  FetchContent_Declare(xxhash
    URL https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.3.tar.gz
    URL_HASH SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80
    SOURCE_SUBDIR cmake_unofficial
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
  )
  set(XXHASH_BUILD_XXHSUM OFF CACHE INTERNAL "Build the xxhsum binary")
  # xxhash uses CMAKE_DEPENDENT_OPTION(BUILD_SHARED_LIBS ...) which writes
  # a sticky CACHE INTERNAL entry on reconfigure. Undo it afterward.
  set(_aurora_xxhash_saved_bsl "${BUILD_SHARED_LIBS}")
  FetchContent_MakeAvailable(xxhash)
  unset(BUILD_SHARED_LIBS CACHE)
  set(BUILD_SHARED_LIBS "${_aurora_xxhash_saved_bsl}")
  unset(_aurora_xxhash_saved_bsl)
  if (NOT TARGET xxhash)
    message(FATAL_ERROR "Failed to make xxhash available")
  endif ()
else ()
  message(STATUS "aurora: Using existing xxhash")
endif ()

if (NOT TARGET fmt)
  message(STATUS "aurora: Fetching fmt")
  FetchContent_Declare(fmt
    URL https://github.com/fmtlib/fmt/archive/refs/tags/11.1.4.tar.gz
    URL_HASH SHA256=ac366b7b4c2e9f0dde63a59b3feb5ee59b67974b14ee5dc9ea8ad78aa2c1ee1e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(fmt)
  if (NOT TARGET fmt)
    message(FATAL_ERROR "Failed to make fmt available")
  endif ()
else ()
  message(STATUS "aurora: Using existing fmt")
endif ()

# fmt 11.1 drops its libc++ transitive includes but still calls malloc/free without <cstdlib>.
# Force the missing header for fmt and its consumers until the pinned release is updated.
if (MINGW AND TARGET fmt)
  target_compile_options(fmt PUBLIC
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:cstdlib>)
endif ()

if (AURORA_ENABLE_GX)
  if (NOT TARGET ZLIB::ZLIB)
    aurora_find_package_global(ZLIB QUIET)
  endif ()
  if (NOT TARGET ZLIB::ZLIB)
    message(STATUS "aurora: Fetching zlib")
    set(_AURORA_ZLIB_SHARED ${_USE_SHARED})
    # ClangCL injects Clang's C headers into the MSVC resource compiler path, and zlib's DLL version
    # resource then hits syntax rc.exe cannot parse. Use the resource-free static target here.
    if (MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      set(_AURORA_ZLIB_SHARED OFF)
    endif ()
    set(ZLIB_BUILD_SHARED ${_AURORA_ZLIB_SHARED} CACHE BOOL "Enable building zlib shared library" FORCE)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "Enable Zlib Examples as tests" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "Enable installation of zlib" FORCE)
    FetchContent_Declare(
      ZLIB
      URL https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
      URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(ZLIB)
    if (NOT TARGET ZLIB::ZLIB AND TARGET ZLIB::ZLIBSTATIC)
      add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif ()
    if (NOT TARGET ZLIB::ZLIB)
      message(FATAL_ERROR "Failed to make zlib available")
    endif ()
    # libpng calls find_package(ZLIB) even when zlib was already provided by FetchContent.
    file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/ZLIBConfig.cmake"
      "set(ZLIB_FOUND TRUE)\n"
      "set(ZLIB_INCLUDE_DIR \"${zlib_SOURCE_DIR}\")\n"
      "set(ZLIB_INCLUDE_DIRS \"${zlib_BINARY_DIR};${zlib_SOURCE_DIR}\")\n"
    )
  endif ()

  if (NOT TARGET PNG::PNG)
    if (TARGET png_shared AND _USE_SHARED)
      add_library(PNG::PNG ALIAS png_shared)
    elseif (TARGET png_static)
      add_library(PNG::PNG ALIAS png_static)
    else ()
      aurora_find_package_global(PNG QUIET)
    endif ()
  endif ()
  if (NOT TARGET PNG::PNG)
    message(STATUS "aurora: Fetching libpng")
    set(PNG_SHARED ${_USE_SHARED} CACHE BOOL "Build libpng as a shared library" FORCE)
    set(PNG_TESTS OFF CACHE BOOL "Build the libpng tests" FORCE)
    set(PNG_TOOLS OFF CACHE BOOL "Build the libpng tools" FORCE)
    FetchContent_Declare(
      PNG
      URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.58.tar.gz
      URL_HASH SHA256=A9D4DF463D36A6E5F9C29BD6F4967312D17E996C1854F3511F833924EB1993CF
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(PNG)
    if (TARGET png_shared AND _USE_SHARED)
      add_library(PNG::PNG ALIAS png_shared)
    elseif (TARGET png_static)
      add_library(PNG::PNG ALIAS png_static)
    else ()
      message(FATAL_ERROR "Failed to make libpng available")
    endif ()
  else ()
    message(STATUS "aurora: Using existing libpng")
  endif ()

  aurora_find_package_global(Freetype QUIET)
  if (NOT TARGET Freetype::Freetype)
    message(STATUS "aurora: Fetching Freetype")
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    FetchContent_Declare(Freetype
      URL https://files.twilitrealm.dev/freetype-2.14.3.tar.gz
      URL_HASH SHA256=e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
  else ()
    message(STATUS "aurora: Using existing Freetype")
  endif ()

  if (NOT TARGET imgui)
    message(STATUS "aurora: Fetching imgui")
    FetchContent_Declare(imgui
      URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz
      URL_HASH SHA256=466fdef9b18de15f0bb6e288e3d00ffa3d82200ec458ce5e4f724a161d9528a5
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(imgui)

    add_library(imgui STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_demo.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    )
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

    add_library(imgui_backends STATIC
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
    )
    target_compile_definitions(imgui_backends PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
    target_link_libraries(imgui_backends PRIVATE imgui ${AURORA_SDL3_TARGET} dawn::webgpu_dawn)
    target_link_libraries(imgui PUBLIC imgui_backends)

    if (TARGET Freetype::Freetype)
      target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
      target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
      target_link_libraries(imgui PRIVATE Freetype::Freetype)
    endif ()
  else ()
    message(STATUS "aurora: Using existing imgui")
  endif ()

  if (TARGET imgui)
    target_include_directories(imgui PUBLIC ${PROJECT_SOURCE_DIR}/include)
    target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="aurora/imgui_config.h")
    target_sources(imgui PRIVATE ${PROJECT_SOURCE_DIR}/lib/imgui_config.cpp)
    target_link_libraries(imgui PRIVATE ${AURORA_SDL3_TARGET})
  endif ()

  if (NOT TARGET sqlite3)
    message(STATUS "aurora: Fetching sqlite3")
    FetchContent_Declare(sqlite3
      URL https://sqlite.org/2026/sqlite-amalgamation-3510300.zip
      URL_HASH SHA256=acb1e6f5d832484bf6d32b681e858c38add8b2acdfd42ac5df24b8afb46552b4
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
      PATCH_COMMAND patch -p1 -N -r - < ${CMAKE_CURRENT_LIST_DIR}/../cmake/patches/sqlite3-nintendo-switch-platform.patch || ${CMAKE_COMMAND} -E true
    )
    FetchContent_MakeAvailable(sqlite3)

    add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
    if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
      # devkitA64/newlib has no mmap() at all (no <sys/mman.h> - same root cause already hit with
      # libco's mprotect() in Phase 2, see [[switch-port-effort]] memory). sqlite3.c pulls in
      # <sys/mman.h> whenever SQLITE_MAX_MMAP_SIZE>0 OR WAL mode isn't compiled out (WAL's
      # shared-memory wal-index needs real mmap, not just the optional mmap-I/O fast path) - so
      # SQLITE_MAX_MMAP_SIZE=0 alone isn't enough, WAL itself must go too. Single-process homebrew
      # has no use for WAL's concurrent-reader/writer performance semantics anyway; the default
      # rollback-journal mode this falls back to is functionally complete.
      # No dlfcn.h either - devkitA64/libnx homebrew doesn't support dynamic linking at all (same
      # reason SDL3's own dynapi is disabled for this target, see SDL_dynapi.h's SDL_PLATFORM_SWITCH
      # branch) - SQLITE_OMIT_LOAD_EXTENSION drops sqlite3_load_extension() and its <dlfcn.h> use.
      # newlib's fcntl() has no POSIX advisory locks, so the default "unix" VFS fails its first
      # SHARED lock with SQLITE_IOERR ("disk I/O error"). Only this process ever opens these
      # caches, so the lock-free "unix-none" VFS is safe.
      target_compile_definitions(sqlite3 PRIVATE SQLITE_MAX_MMAP_SIZE=0 SQLITE_OMIT_WAL
        SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_UNIX_VFS="unix-none")
    endif ()
  else ()
    message(STATUS "aurora: Using existing sqlite3")
  endif ()

  if (AURORA_CACHE_USE_ZSTD)
    if (NOT TARGET libzstd_static)
      message(STATUS "aurora: Fetching zstd")
      set(ZSTD_BUILD_STATIC ON)
      set(ZSTD_BUILD_SHARED OFF)
      set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "BUILD PROGRAMS" FORCE)
      set(ZSTD_BUILD_TESTS OFF CACHE BOOL "BUILD TESTS" FORCE)

      if (WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(C_FLAG_WALL TRUE CACHE INTERNAL "")
        set(CXX_FLAG_WALL TRUE CACHE INTERNAL "")
        set(C_FLAG_WEXTRA TRUE CACHE INTERNAL "")
        set(CXX_FLAG_WEXTRA TRUE CACHE INTERNAL "")
        set(C_FLAG_WUNDEF TRUE CACHE INTERNAL "")
        set(CXX_FLAG_WUNDEF TRUE CACHE INTERNAL "")
        set(C_FLAG_WSHADOW TRUE CACHE INTERNAL "")
        set(CXX_FLAG_WSHADOW TRUE CACHE INTERNAL "")
        set(C_FLAG_WCAST_ALIGN TRUE CACHE INTERNAL "")
        set(CXX_FLAG_WCAST_ALIGN TRUE CACHE INTERNAL "")
        set(C_FLAG_WCAST_QUAL TRUE CACHE INTERNAL "")
        set(CXX_FLAG_WCAST_QUAL TRUE CACHE INTERNAL "")
        set(C_FLAG_WSTRICT_PROTOTYPES TRUE CACHE INTERNAL "")
        set(LD_FLAG_WL_Z_NOEXECSTACK FALSE CACHE INTERNAL "")
        set(C_FLAG_QUNUSED_ARGUMENTS TRUE CACHE INTERNAL "")
        set(CXX_FLAG_QUNUSED_ARGUMENTS TRUE CACHE INTERNAL "")
        set(C_FLAG_WA_NOEXECSTACK TRUE CACHE INTERNAL "")
        set(CXX_FLAG_WA_NOEXECSTACK TRUE CACHE INTERNAL "")
      endif()

      FetchContent_Declare(zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR build/cmake
        EXCLUDE_FROM_ALL
      )
      FetchContent_MakeAvailable(zstd)
    else ()
      message(STATUS "aurora: Using existing zstd")
    endif ()
  endif ()
endif ()

if (NOT TARGET TracyClient)
  message(STATUS "aurora: Fetching Tracy")

  # Default to off, users can enable the cache variable if they want to use it.
  set(TRACY_ENABLE OFF CACHE BOOL "Enable Tracy profiling")

  set(_aurora_tracy_patch_command "")
  if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    # TracySystem.cpp's thread-ID/user-login platform detection has no Switch case at all
    # (#error "Unsupported platform!" for the former, missing getlogin_r() for the latter) - see
    # the patch file for the two small additions (threadGetCurHandle(), a "(?)" placeholder login
    # matching the existing __ANDROID__ treatment). TRACY_ENABLE is OFF by default just above, but
    # this file still needs to compile regardless - these platform checks aren't gated on it.
    set(_aurora_tracy_patch_command PATCH_COMMAND git apply --verbose
      "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/patches/tracy-nintendo-switch-platform.patch")
  endif ()
  FetchContent_Declare(
          tracy
          URL https://github.com/wolfpld/tracy/archive/a64b9a20294d59421a2f57aeca3c6383d8c48169.tar.gz
          URL_HASH SHA256=24d342b5127d7f659dc3cf94f24347b348cc736f25b17a691fd7f69541937658
          DOWNLOAD_EXTRACT_TIMESTAMP TRUE
          EXCLUDE_FROM_ALL
          ${_aurora_tracy_patch_command}
  )
  FetchContent_MakeAvailable(tracy)

  if (NOT TARGET TracyClient)
    message(FATAL_ERROR "Failed to make TracyClient available")
  endif ()
endif ()
