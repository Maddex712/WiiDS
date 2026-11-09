# gcn-recomp: a GameCube static-recompilation toolkit. A game project adds this directory
# and calls gcn_add_game() with what is specific to its game; everything else -- the
# PowerPC -> C recompiler, the hardware runtime, the renderer and the frontends -- is here.
cmake_minimum_required(VERSION 3.20)
project(gcn_recomp C CXX)

# Language levels are set per target: gcn_add_game() runs in the game project's scope,
# where directory-wide settings made here would not apply.
function(_gcn_set_standards target)
  set_target_properties(${target} PROPERTIES C_STANDARD 11 CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endfunction()

# GCN_BENCH_ONLY builds just the headless benchmark: no renderer, no SDL2, no GL. That
# is what a cross-build for a device uses, since it only needs a C++ toolchain and the
# recompiled code.
option(GCN_BENCH_ONLY "Build only the headless benchmark (no SDL2, no renderer)" OFF)
# See ENTER()/RET() in runtime/recomp.h. ON maintains a guest call stack, which answers
# "which guest function did that?" and gives a crash dump something to print.
option(GCN_TRACE_CALLS "Maintain a guest call stack (diagnostics)" OFF)
option(GCN_GUEST_CHECKS "Call the game project's consistency checks at every poll, DMA and frame (diagnostics)" OFF)

# PC VR: the desktop executable's --vr. Windows only for now: the frontend binds OpenXR to
# the window's WGL context (openxr/xr_desktop.cpp), and no other desktop binding is written.
if(WIN32 AND NOT GCN_BENCH_ONLY)
  set(_gcn_openxr_default ON)
else()
  set(_gcn_openxr_default OFF)
endif()
option(GCN_OPENXR "Build the desktop executable's OpenXR frontend (--vr)" ${_gcn_openxr_default})

# visionOS: the Vision Pro app in visionos/, configured with CMake's Xcode generator and
# -DCMAKE_SYSTEM_NAME=visionOS (see gcn_add_game's VISIONOS_BUNDLE_ID). Its launcher is
# Swift and its frontend Objective-C++, languages a project has to enable at its top level
# before targets use them, which is what gcn_enable_languages() is for.
# A cache entry, since gcn_add_game() runs in the game project's scope, not this one.
if(CMAKE_SYSTEM_NAME STREQUAL "visionOS")
  set(GCN_VISIONOS ON CACHE INTERNAL "")
else()
  set(GCN_VISIONOS OFF CACHE INTERNAL "")
endif()
macro(gcn_enable_languages)
  if(CMAKE_SYSTEM_NAME STREQUAL "visionOS")
    enable_language(OBJCXX)
    enable_language(Swift)
  endif()
endmacro()

# Khronos' OpenXR headers alone, for a platform with no OpenXR loader to link: visionOS,
# whose OpenXR is the frontend's own (visionos/xr_runtime.mm). SOURCE_SUBDIR names a
# directory that does not exist so that nothing of the SDK is built.
macro(_gcn_fetch_openxr_headers)
  include(FetchContent)
  FetchContent_Declare(openxr_headers
    URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.63.tar.gz
    URL_HASH SHA256=5b8dac0608f49498fe8e95f562fe6bcf0bc781e561f137348a8c251d06a88b6f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR headers-only)
  FetchContent_MakeAvailable(openxr_headers)
  set(GCN_OPENXR_INCLUDE ${openxr_headers_SOURCE_DIR}/include)
endmacro()

# SDL2 for Windows, which has no prebuilt ARM64 binaries: built from source once per
# compiler and configuration into GCN_SDL2_PREFIX (under %LOCALAPPDATA%\gcn-recomp), which
# every project's desktop builds share. Built inside each build directory, it was half of
# a desktop build's compile time on a two-core machine. A lock keeps two configures from
# building it at once; delete the directory to build it again.
function(_gcn_sdl2_shared out_target)
  set(_ver 2.30.9)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_config Debug)  # the debug C runtime, which a library built for release cannot share
  else()
    set(_config Release)
  endif()
  string(MAKE_C_IDENTIFIER "${CMAKE_C_COMPILER_ID}${CMAKE_C_COMPILER_VERSION}_${CMAKE_SYSTEM_PROCESSOR}_${_config}" _tag)
  set(_root "$ENV{LOCALAPPDATA}/gcn-recomp")
  if(NOT DEFINED ENV{LOCALAPPDATA})
    set(_root ${CMAKE_BINARY_DIR})
  endif()
  file(TO_CMAKE_PATH "${_root}" _root)
  set(GCN_SDL2_PREFIX "${_root}/SDL2-${_ver}-${_tag}" CACHE PATH "SDL2 built once for this compiler, shared by every project")
  set(_prefix ${GCN_SDL2_PREFIX})
  if(NOT EXISTS ${_prefix}/built)
    file(MAKE_DIRECTORY ${_root})
    file(LOCK ${_prefix}.lock GUARD FUNCTION TIMEOUT 3600)
    if(NOT EXISTS ${_prefix}/built)
      message(STATUS "Building SDL2 ${_ver} into ${_prefix} (once for every project)")
      set(_work ${_prefix}-work)
      file(REMOVE_RECURSE ${_work} ${_prefix})
      file(DOWNLOAD https://github.com/libsdl-org/SDL/releases/download/release-${_ver}/SDL2-${_ver}.tar.gz
           ${_work}/SDL2.tar.gz
           EXPECTED_HASH SHA256=24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4
           STATUS _st)
      list(GET _st 0 _code)
      if(NOT _code EQUAL 0)
        message(FATAL_ERROR "downloading SDL2 failed: ${_st}")
      endif()
      file(ARCHIVE_EXTRACT INPUT ${_work}/SDL2.tar.gz DESTINATION ${_work})
      set(_args -S ${_work}/SDL2-${_ver} -B ${_work}/build -G ${CMAKE_GENERATOR}
                -DCMAKE_BUILD_TYPE=${_config} -DCMAKE_INSTALL_PREFIX=${_prefix}
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF)
      foreach(v CMAKE_MAKE_PROGRAM CMAKE_RC_COMPILER CMAKE_MSVC_RUNTIME_LIBRARY)
        if(${v})
          list(APPEND _args -D${v}=${${v}})
        endif()
      endforeach()
      execute_process(COMMAND ${CMAKE_COMMAND} ${_args} RESULT_VARIABLE _r OUTPUT_QUIET)
      if(_r EQUAL 0)
        execute_process(COMMAND ${CMAKE_COMMAND} --build ${_work}/build --target install RESULT_VARIABLE _r OUTPUT_QUIET)
      endif()
      if(NOT _r EQUAL 0)
        message(FATAL_ERROR "building SDL2 in ${_work} failed")
      endif()
      file(REMOVE_RECURSE ${_work})
      file(WRITE ${_prefix}/built "${_ver}\n")
    endif()
    file(LOCK ${_prefix}.lock RELEASE)
  endif()
  find_package(SDL2 ${_ver} EXACT CONFIG REQUIRED PATHS ${_prefix} NO_DEFAULT_PATH)
  set(${out_target} SDL2::SDL2-static PARENT_SCOPE)
endfunction()

# The OpenXR loader, from Khronos' SDK source at the version the Android build's AAR is.
# Static, and nothing but the loader.
macro(_gcn_fetch_openxr_loader)
  if(NOT TARGET openxr_loader)
    include(FetchContent)
    set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_ALL_EXTENSIONS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(openxr_sdk
      URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.63.tar.gz
      URL_HASH SHA256=5b8dac0608f49498fe8e95f562fe6bcf0bc781e561f137348a8c251d06a88b6f
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(openxr_sdk)
  endif()
endmacro()

# Build the game once without the modules it assembles at run time (GCN_BOOTSTRAP), run
# that headless with GCN_REL_DUMP until the game links each of `missing`, and keep what it
# linked in ${REL_RUNTIME_DIR}. The game's own code produces them, from the user's disc.
macro(_gcn_bootstrap_rels missing)
  set(_boot ${CMAKE_BINARY_DIR}/bootstrap)
  message(STATUS "Bootstrapping ${missing}: building and running the game without them")
  set(_boot_args -S ${CMAKE_SOURCE_DIR} -B ${_boot} -G ${CMAKE_GENERATOR} -DGCN_BOOTSTRAP=ON
                 -DGCN_BENCH_ONLY=ON -DCMAKE_BUILD_TYPE=Release "-DGAME_ISO=${GAME_ISO}"
                 -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
  if(CMAKE_OSX_SYSROOT)
    list(APPEND _boot_args -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT})
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} ${_boot_args} RESULT_VARIABLE _r OUTPUT_QUIET)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "bootstrap: configuring ${_boot} failed")
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} --build ${_boot} --target ${G_NAME}_bench
                  RESULT_VARIABLE _r OUTPUT_QUIET)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "bootstrap: building ${_boot} failed")
  endif()
  file(MAKE_DIRECTORY ${REL_RUNTIME_DIR})
  string(REPLACE ";" "," _want "${missing}")
  execute_process(COMMAND ${CMAKE_COMMAND} -E env GCN_REL_DUMP=${REL_RUNTIME_DIR} GCN_REL_DUMP_WANT=${_want}
                          ${_boot}/${G_NAME}_bench --seconds=600 "${GAME_ISO}"
                  TIMEOUT 600 RESULT_VARIABLE _r OUTPUT_QUIET ERROR_VARIABLE _err)
  foreach(m ${missing})
    if(NOT EXISTS ${REL_RUNTIME_DIR}/${m}.rel)
      message(FATAL_ERROR "bootstrap: the game did not link ${m} (exit ${_r}):\n${_err}")
    endif()
  endforeach()
endmacro()

# gcn_add_game(
#   NAME <target>              executable name; the benchmark is <target>_bench
#   TITLE <string>             the game's name, for messages ("put your <title> .iso in rom/")
#   LABEL <string>             what the window and the headset app are called; defaults
#                              to TITLE. A name of the project's own keeps the game's
#                              trademark off the app itself.
#   DISC_ID <6 chars>          the game's disc ID; the image and the booted disc are checked against it
#   DOL_SHA1 <hex>             the supported build's main.dol
#   SYMBOLS <file>             decomp-toolkit symbols.txt for that DOL
#   TABLES <dir>               hle.txt, names.txt, patches.txt, special_calls.txt, idle.txt
#   FPS <n>                    the game's frame rate, for the benchmark's percentage
#   [IMAGE_DIR <dir>]          where to look for the user's .iso/.ciso (default rom/)
#   [DOL_FIXUP <script>]       a Python script run as `script <main.dol> <out.dol> <image>`
#                              after the DOL is extracted; the recompiler reads its output
#                              instead. For a game whose executable is not laid out the way
#                              it runs: one that carries a second image inside and maps it
#                              elsewhere, or whose data files carry code (the script may
#                              also write `<out.dol>.modules`, see recomp/recomp.py).
#   [RELS <disc paths>...]     relocatable modules the game loads with OSLink, from the disc;
#                              each is compiled at a canonical address and runs wherever
#                              the game links it (docs/rels.md)
#   [REL_RUNTIME <names>...]   modules the game does not keep on the disc as plain files but
#                              builds in memory before linking them (e.g. "game.main"). The
#                              first configure builds the game without them, runs it
#                              headless until the game itself hands each to OSLink, and
#                              keeps them in <build>/rels-runtime/ (docs/rels.md)
#   [REL_SYMBOLS <dir>]        dtk's symbols for each module, <dir>/<module>.txt, with the
#                              splits file beside it as <module>.splits.txt
#   [SOURCES <files>...]       the game project's own runtime sources, compiled into every
#                              target that links the game, the benchmark included, since a
#                              patch in TABLES may call into them: hooks the runtime offers a
#                              game (render_set_eye_hook in runtime/gx/render_gl.h,
#                              vr::GameHooks in runtime/vr_game.h), installed from a static
#                              initializer. The benchmark has no renderer; the hook
#                              installers it may call are stubbed there
#                              (runtime/gx/render_stub.cpp), and anything more of the
#                              renderer a game source needs must go through a hook.
#   [SHADER_KEYS <file>]       the shader keys a play-through uses, as tools/shader_keys
#                              makes them from the caches runs leave behind. Built into
#                              every target, so that a fresh install compiles them before
#                              the game starts (render_set_shader_keys in
#                              runtime/gx/render_gl.h; docs/graphics.md, "Shader cache")
#   [ANDROID_PACKAGE <id>]     e.g. com.example.game. On an Android build, also build the
#                              headset app (lib<target>.so, a NativeActivity that
#                              tools/package-apk.ps1 packages) and <target>_egl, the
#                              renderer headless on a device; see android/
#   [VISIONOS_BUNDLE_ID <id>]  On a visionOS build, the Vision Pro app <target>.app under
#                              that bundle identifier; see visionos/. Signed for the team
#                              in GCN_APPLE_TEAM.
# )
# The user's image may also be given as -DGAME_ISO=<path>.
function(gcn_add_game)
  cmake_parse_arguments(G "" "NAME;TITLE;LABEL;DISC_ID;DOL_SHA1;SYMBOLS;TABLES;FPS;IMAGE_DIR;ANDROID_PACKAGE;VISIONOS_BUNDLE_ID;DOL_FIXUP;REL_SYMBOLS;SHADER_KEYS" "SOURCES;RELS;REL_RUNTIME" ${ARGN})
  if(NOT G_LABEL)
    set(G_LABEL "${G_TITLE}")
  endif()
  foreach(req NAME TITLE DISC_ID DOL_SHA1 SYMBOLS TABLES FPS)
    if(NOT G_${req})
      message(FATAL_ERROR "gcn_add_game: ${req} is required")
    endif()
  endforeach()
  if(NOT G_IMAGE_DIR)
    set(G_IMAGE_DIR ${CMAKE_SOURCE_DIR}/rom)
  endif()
  # This function runs in the game project's scope, so it locates its own files from
  # where it was defined rather than from any variable set alongside it.
  set(GCN_RECOMP_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  # The game image is supplied by the user: an uncompressed .iso or a .ciso.
  file(GLOB _rom_images ${G_IMAGE_DIR}/*.iso ${G_IMAGE_DIR}/*.ciso)
  list(LENGTH _rom_images _rom_count)
  if(_rom_count GREATER 0)
    list(GET _rom_images 0 _default_iso)
  else()
    set(_default_iso "")
  endif()
  set(GAME_ISO "${_default_iso}" CACHE FILEPATH "Your ${G_TITLE} disc image (.iso or .ciso)")

  # Without an image we can still compile the runtime, which is enough to check a
  # port. Only the recompiled game code and the final link need the disc.
  if(GAME_ISO AND EXISTS "${GAME_ISO}")
    set(HAVE_GAME_ISO TRUE)
  else()
    set(HAVE_GAME_ISO FALSE)
    if(GAME_ISO)
      message(WARNING "GAME_ISO is set to '${GAME_ISO}' but that file does not exist.")
    endif()
    message(WARNING
      "No game image found, so only the `runtime_check` target (compile the runtime, no link) "
      "is available. Put your own ${G_TITLE} .iso or .ciso in ${G_IMAGE_DIR}/ "
      "or pass -DGAME_ISO=/path/to/game.iso to build the game. See README.md.")
  endif()

  set(GAME_DOL ${CMAKE_BINARY_DIR}/main.dol)
  set(RECOMP_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
  set(RT ${GCN_RECOMP_DIR}/runtime)

  # ---- Recompiled game code ----
  if(HAVE_GAME_ISO)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${GCN_RECOMP_DIR}/recomp/extract_dol.py
                            "${GAME_ISO}" ${GAME_DOL} ${G_DISC_ID} ${G_DOL_SHA1}
                    RESULT_VARIABLE EXTRACT_RESULT)
    if(NOT EXTRACT_RESULT EQUAL 0)
      message(FATAL_ERROR "could not extract main.dol from ${GAME_ISO}")
    endif()

    file(GLOB RECOMP_TOOL_SOURCES ${GCN_RECOMP_DIR}/recomp/*.py ${G_TABLES}/*.txt)
    set(RECOMP_DOL ${GAME_DOL})
    if(G_DOL_FIXUP)
      set(RECOMP_DOL ${CMAKE_BINARY_DIR}/main_recomp.dol)
      execute_process(COMMAND ${Python3_EXECUTABLE} ${G_DOL_FIXUP} ${GAME_DOL} ${RECOMP_DOL} ${GAME_ISO}
                      RESULT_VARIABLE FIXUP_RESULT)
      if(NOT FIXUP_RESULT EQUAL 0)
        message(FATAL_ERROR "${G_DOL_FIXUP} failed")
      endif()
      list(APPEND RECOMP_TOOL_SOURCES ${G_DOL_FIXUP})
    endif()
    if(G_RELS)
      set(REL_DIR ${CMAKE_BINARY_DIR}/rels)
      file(REMOVE_RECURSE ${REL_DIR})
      execute_process(COMMAND ${Python3_EXECUTABLE} ${GCN_RECOMP_DIR}/recomp/rel.py extract
                              "${GAME_ISO}" ${REL_DIR} ${G_RELS}
                      RESULT_VARIABLE REL_RESULT)
      if(NOT REL_RESULT EQUAL 0)
        message(FATAL_ERROR "could not extract the modules from ${GAME_ISO}")
      endif()
      file(GLOB _rel_files ${REL_DIR}/*.rel)
      # Modules the game assembles in memory: taken from the game itself, by a first build
      # without them (GCN_BOOTSTRAP) that runs until it links each one.
      set(REL_RUNTIME_DIR ${CMAKE_BINARY_DIR}/rels-runtime)
      if(G_REL_RUNTIME AND NOT GCN_BOOTSTRAP)
        set(_missing "")
        foreach(m ${G_REL_RUNTIME})
          if(NOT EXISTS ${REL_RUNTIME_DIR}/${m}.rel)
            list(APPEND _missing ${m})
          endif()
        endforeach()
        if(_missing AND CMAKE_CROSSCOMPILING)
          # The bootstrap has to run here, and a cross build's binaries do not: take the
          # modules from a desktop build of the same project instead.
          set(GCN_HOST_BUILD_DIR ${CMAKE_SOURCE_DIR}/build CACHE PATH
              "A desktop build of this project, whose rels-runtime/ a cross build takes")
          foreach(m ${_missing})
            if(NOT EXISTS ${GCN_HOST_BUILD_DIR}/rels-runtime/${m}.rel)
              message(FATAL_ERROR "${m} is made by running the game, which a cross build cannot do here. "
                      "Build for the desktop first (it writes ${GCN_HOST_BUILD_DIR}/rels-runtime/), "
                      "or point -DGCN_HOST_BUILD_DIR at a desktop build that has.")
            endif()
            file(COPY ${GCN_HOST_BUILD_DIR}/rels-runtime/${m}.rel DESTINATION ${REL_RUNTIME_DIR})
          endforeach()
        elseif(_missing)
          _gcn_bootstrap_rels("${_missing}")
        endif()
        foreach(m ${G_REL_RUNTIME})
          list(APPEND _rel_files ${REL_RUNTIME_DIR}/${m}.rel)
        endforeach()
      endif()
      list(SORT _rel_files)
      set(_rel_in ${RECOMP_DOL})
      set(RECOMP_DOL ${CMAKE_BINARY_DIR}/main_rels.dol)
      execute_process(COMMAND ${Python3_EXECUTABLE} ${GCN_RECOMP_DIR}/recomp/rel.py link
                              ${_rel_in} ${RECOMP_DOL} "${G_REL_SYMBOLS}" ${_rel_files}
                      RESULT_VARIABLE REL_RESULT)
      if(NOT REL_RESULT EQUAL 0)
        message(FATAL_ERROR "could not link the modules")
      endif()
      if(G_REL_SYMBOLS)
        file(GLOB _rel_syms ${G_REL_SYMBOLS}/*.txt)
        list(APPEND RECOMP_TOOL_SOURCES ${_rel_syms})
      endif()
    endif()
    execute_process(COMMAND ${Python3_EXECUTABLE} ${GCN_RECOMP_DIR}/recomp/recomp.py
                            ${RECOMP_DOL} ${G_SYMBOLS} ${RECOMP_GEN_DIR} ${G_TABLES}
                    RESULT_VARIABLE RECOMP_RESULT)
    if(NOT RECOMP_RESULT EQUAL 0)
      message(FATAL_ERROR "recompilation failed")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RECOMP_TOOL_SOURCES} ${GAME_ISO} ${G_SYMBOLS})
    include(${RECOMP_GEN_DIR}/sources.cmake)

    add_library(game_recomp STATIC ${RECOMP_SOURCES})
    _gcn_set_standards(game_recomp)
    target_include_directories(game_recomp PUBLIC ${RT} ${RECOMP_GEN_DIR})
    # PUBLIC because the CPU struct grows with the call trace: the runtime and the
    # generated code must agree on the layout.
    if(GCN_TRACE_CALLS)
      target_compile_definitions(game_recomp PUBLIC GCN_TRACE_CALLS)
    endif()
    # Guest memory at a fixed address, so the generated code adds a constant rather than
    # loading a pointer at every access: see GCN_MEM in runtime/recomp.h. Android only so
    # far, where a 64-bit process's address space has room low down.
    if(ANDROID)
      target_compile_definitions(game_recomp PUBLIC GCN_MEM_BASE=0x2000000000ull)
    endif()
    if(MSVC)
      # Generated code is enormous and machine-written: keep the optimizer honest about
      # aliasing and floating point, and silence the noise it legitimately produces.
      target_compile_options(game_recomp PRIVATE /O2 /fp:precise /fp:except- /wd4101 /wd4102 /wd4723 /bigobj)
    else()
      target_compile_options(game_recomp PRIVATE -O2 -fno-strict-aliasing -Wno-unused-label -Wno-unused-variable
                             -Wno-parentheses-equality -ffp-contract=off)
      # Line tables only, where the build has debug info at all: a crash's frames still
      # resolve to a line (addr2line, a symbolized backtrace), and nobody reads a
      # recompiled function's locals. Full debug info took about 15% longer to compile and
      # made the binaries nearly twice the size (110 MB against 63 for an Android build).
      if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(game_recomp PRIVATE $<$<CONFIG:Debug,RelWithDebInfo>:-gline-tables-only>)
      else()
        target_compile_options(game_recomp PRIVATE $<$<CONFIG:Debug,RelWithDebInfo>:-g1>)
      endif()
    endif()
  endif()

  # ---- Runtime ----
  file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS ${RT}/*.cpp)

  # Alternative entry points and the stubs: none of these may land in a target that
  # already has main.cpp, audio.cpp or the renderer.
  list(REMOVE_ITEM RUNTIME_SOURCES ${RT}/bench_main.cpp ${RT}/audio_stub.cpp ${RT}/gx/render_stub.cpp)

  # Everything below the renderer and the SDL frontend -- the CPU, OS, hardware and GX
  # front end, including the batch queue in gx/xf.cpp -- is free of both GL and SDL.
  set(CORE_SOURCES ${RUNTIME_SOURCES})
  list(REMOVE_ITEM CORE_SOURCES
    ${RT}/main.cpp ${RT}/input_map.cpp ${RT}/ffb.cpp ${RT}/audio.cpp ${RT}/audio_sdl.cpp ${RT}/png.cpp ${RT}/record.cpp
    ${RT}/gx/render_gl.cpp ${RT}/gx/gl_load.cpp ${RT}/gx/shadergen.cpp)
  list(APPEND CORE_SOURCES ${RT}/audio_stub.cpp)
  # The benchmark compiles the game's SOURCES too (gcn_add_game), and a game's eye hook
  # installs itself into the renderer; render_stub.cpp answers those calls with nothing.
  set(BENCH_SOURCES ${CORE_SOURCES} ${RT}/bench_main.cpp ${RT}/gx/render_stub.cpp)

  # Render target: desktop OpenGL 3.3 core, or OpenGL ES 3.2 for Android and standalone
  # headsets, or Apple's OpenGL ES 3.0 on visionOS. One renderer source serves all three;
  # see runtime/gx/gl.h. GL entry points are resolved at runtime via a vendored glad loader;
  # macOS links the system framework directly and needs neither.
  if(ANDROID OR GCN_VISIONOS)
    set(_gcn_gl_es_default ON)
  else()
    set(_gcn_gl_es_default OFF)
  endif()
  option(GCN_GL_ES "Target OpenGL ES 3.2 instead of desktop OpenGL 3.3 core" ${_gcn_gl_es_default})
  if(GCN_GL_ES)
    set(GLAD_INCLUDE_DIR ${RT}/gx/glad_es/include)
    set(GLAD_SOURCE ${RT}/gx/glad_es/src/gles2.c)
    set(GCN_GL_DEFS GCN_GL_ES)
    if(GCN_VISIONOS)
      list(APPEND GCN_GL_DEFS GCN_GL_ES30)
    endif()
  else()
    set(GLAD_INCLUDE_DIR ${RT}/gx/glad/include)
    set(GLAD_SOURCE ${RT}/gx/glad/src/gl.c)
    set(GCN_GL_DEFS "")
  endif()
  if(NOT APPLE OR GCN_GL_ES)
    list(APPEND RUNTIME_SOURCES ${GLAD_SOURCE})
  endif()

  # The game's shader keys, in a generated source alongside the game's own.
  if(G_SHADER_KEYS)
    set(_keys_cpp ${CMAKE_BINARY_DIR}/shader_keys.cpp)
    add_custom_command(OUTPUT ${_keys_cpp}
      COMMAND ${CMAKE_COMMAND} -DIN=${G_SHADER_KEYS} -DOUT=${_keys_cpp} -P ${GCN_RECOMP_DIR}/tools/embed_shader_keys.cmake
      DEPENDS ${G_SHADER_KEYS} ${GCN_RECOMP_DIR}/tools/embed_shader_keys.cmake
      COMMENT "Embedding the shader keys")
    list(APPEND G_SOURCES ${_keys_cpp})
  endif()

  # Makes a key list for SHADER_KEYS from shader caches: `shader_keys OUT IN...`. Runs on
  # the build machine, so only in a build for it.
  if(NOT CMAKE_CROSSCOMPILING AND NOT GCN_BENCH_ONLY)
    add_executable(shader_keys EXCLUDE_FROM_ALL ${GCN_RECOMP_DIR}/tools/shader_keys.cpp ${RT}/gx/shadergen.cpp)
    _gcn_set_standards(shader_keys)
    target_include_directories(shader_keys PRIVATE ${RT} ${GLAD_INCLUDE_DIR})
    target_compile_definitions(shader_keys PRIVATE ${GCN_GL_DEFS})
  endif()

  # SDL2: pkg-config where it exists; on Windows there are no prebuilt ARM64 binaries,
  # so it is built from source, once for all projects (_gcn_sdl2_shared).
  if(GCN_BENCH_ONLY OR GCN_VISIONOS)
    set(GCN_SDL_TARGET "")
  elseif(WIN32)
    _gcn_sdl2_shared(GCN_SDL_TARGET)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(GCN_SDL_TARGET PkgConfig::SDL2)
  endif()

  if(MSVC)
    set(GCN_RUNTIME_WARNINGS /W3 /wd4244 /wd4267)
  else()
    set(GCN_RUNTIME_WARNINGS -Wall -Wno-unused-function)
  endif()

  # Platform libraries the runtime needs beyond SDL2. The benchmark links none of this.
  set(GCN_PLATFORM_LIBS "")
  if(GCN_BENCH_ONLY OR GCN_VISIONOS)
  elseif(APPLE)
    list(APPEND GCN_PLATFORM_LIBS "-framework OpenGL")
  elseif(WIN32)
    # opengl32 for the GL entry points, dbghelp to symbolize crash backtraces, ws2_32 for
    # telemetry's socket.
    list(APPEND GCN_PLATFORM_LIBS opengl32 dbghelp ws2_32)
  else()
    find_package(OpenGL REQUIRED)
    list(APPEND GCN_PLATFORM_LIBS OpenGL::GL)
  endif()

  # What the runtime knows about the game it was built for. The image, the label and the
  # frame rate are read only by the entry points (GCN_ENTRY_DEFS), which every target
  # compiles for itself; with them out of the rest, the runtime below the frontends is
  # compiled once for all of a build's targets (gcn_core), and changing one of them
  # compiles no more than the entry points again.
  set(GCN_GAME_DEFS
    GCN_GAME_TITLE="${G_TITLE}"
    GCN_DISC_ID="${G_DISC_ID}")
  if(GCN_GUEST_CHECKS)
    list(APPEND GCN_GAME_DEFS GCN_GUEST_CHECKS)
  endif()
  set(GCN_ENTRY_DEFS
    GCN_DEFAULT_ISO="${GAME_ISO}"
    GCN_APP_LABEL="${G_LABEL}"
    GCN_GAME_FPS=${G_FPS})

  # CORE_SOURCES but the audio stub, as objects every target links: the benchmark, the
  # desktop executable, the harness and the headset app each compiled their own copy
  # before, two or three times the work for the same objects.
  if(HAVE_GAME_ISO AND NOT GCN_VISIONOS)
    set(GCN_CORE_OBJ_SOURCES ${CORE_SOURCES})
    list(REMOVE_ITEM GCN_CORE_OBJ_SOURCES ${RT}/audio_stub.cpp)
    add_library(gcn_core OBJECT ${GCN_CORE_OBJ_SOURCES})
    _gcn_set_standards(gcn_core)
    target_include_directories(gcn_core PRIVATE ${RT} ${RECOMP_GEN_DIR})
    # For game_recomp's public definitions: the CPU struct's layout and the memory base.
    target_link_libraries(gcn_core PRIVATE game_recomp)
    target_compile_options(gcn_core PRIVATE ${GCN_RUNTIME_WARNINGS})
    target_compile_definitions(gcn_core PRIVATE ${GCN_GAME_DEFS})
    if(ANDROID)
      # The headset app is a shared library.
      set_target_properties(gcn_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
  endif()

  # Headless benchmark: reports how fast the guest advances, with no graphics or audio
  # in the way. Used to compare host CPUs.
  if(HAVE_GAME_ISO AND NOT GCN_VISIONOS)
    set(_bench_sources ${BENCH_SOURCES})
    list(REMOVE_ITEM _bench_sources ${GCN_CORE_OBJ_SOURCES})
    add_executable(${G_NAME}_bench ${_bench_sources} ${G_SOURCES})
    _gcn_set_standards(${G_NAME}_bench)
    target_include_directories(${G_NAME}_bench PRIVATE ${RT} ${RECOMP_GEN_DIR})
    target_link_libraries(${G_NAME}_bench PRIVATE gcn_core game_recomp)
    target_compile_options(${G_NAME}_bench PRIVATE ${GCN_RUNTIME_WARNINGS})
    target_compile_definitions(${G_NAME}_bench PRIVATE ${GCN_GAME_DEFS} ${GCN_ENTRY_DEFS})
    if(WIN32)
      target_link_libraries(${G_NAME}_bench PRIVATE dbghelp ws2_32)
    endif()
  endif()

  # Compile-only target: available even without a disc image, so the runtime and any
  # port work can be checked on its own. With an image present the game target already
  # compiles these sources, so keep it out of the default build; `ninja runtime_check`
  # still works.
  if(NOT GCN_BENCH_ONLY AND NOT GCN_VISIONOS)
    if(HAVE_GAME_ISO)
      add_library(runtime_check OBJECT EXCLUDE_FROM_ALL ${RUNTIME_SOURCES})
    else()
      add_library(runtime_check OBJECT ${RUNTIME_SOURCES})
    endif()
    _gcn_set_standards(runtime_check)
    target_include_directories(runtime_check PRIVATE ${RT} ${RECOMP_GEN_DIR} ${GLAD_INCLUDE_DIR})
    target_link_libraries(runtime_check PRIVATE ${GCN_SDL_TARGET})
    target_compile_options(runtime_check PRIVATE ${GCN_RUNTIME_WARNINGS})
    target_compile_definitions(runtime_check PRIVATE ${GCN_GAME_DEFS} ${GCN_ENTRY_DEFS} ${GCN_GL_DEFS})
  endif()

  if(HAVE_GAME_ISO AND NOT GCN_BENCH_ONLY AND NOT GCN_VISIONOS)
    set(_desktop_sources ${RUNTIME_SOURCES})
    list(REMOVE_ITEM _desktop_sources ${GCN_CORE_OBJ_SOURCES})
    add_executable(${G_NAME} ${_desktop_sources} ${G_SOURCES})
    _gcn_set_standards(${G_NAME})
    target_include_directories(${G_NAME} PRIVATE ${GLAD_INCLUDE_DIR})
    target_link_libraries(${G_NAME} PRIVATE gcn_core game_recomp ${GCN_SDL_TARGET} ${GCN_PLATFORM_LIBS})
    target_compile_options(${G_NAME} PRIVATE ${GCN_RUNTIME_WARNINGS})
    target_compile_definitions(${G_NAME} PRIVATE ${GCN_GAME_DEFS} ${GCN_ENTRY_DEFS} ${GCN_GL_DEFS})

    # PC VR (--vr): the headset frontend in openxr/, on the window's own GL context. The
    # loader is Khronos' own, built from source and linked statically, so the executable
    # finds whichever runtime the system has active with nothing beside it.
    if(GCN_OPENXR)
      _gcn_fetch_openxr_loader()
      target_sources(${G_NAME} PRIVATE ${GCN_RECOMP_DIR}/openxr/xr_frontend.cpp ${GCN_RECOMP_DIR}/openxr/xr_desktop.cpp)
      target_include_directories(${G_NAME} PRIVATE ${GCN_RECOMP_DIR}/openxr ${RT})
      target_link_libraries(${G_NAME} PRIVATE openxr_loader $<$<PLATFORM_ID:Windows>:ole32>)
      target_compile_definitions(${G_NAME} PRIVATE GCN_OPENXR)
    endif()
  endif()

  # ---- Android headset app and harness ----
  # Built from the same pieces as everything else, with the real audio mix and an AAudio
  # device in place of the stub, and the game's own sources. The OpenXR loader comes from
  # Khronos' official Android AAR on Maven Central: it carries both the headers and a
  # prebuilt libopenxr_loader.so, so nothing has to be committed as a binary.
  if(ANDROID AND HAVE_GAME_ISO AND G_ANDROID_PACKAGE)
    set(AND_DIR ${GCN_RECOMP_DIR}/android)
    set(ANDROID_DEFS ${GCN_GAME_DEFS} ${GCN_GL_DEFS} GCN_LOG_TAG="${G_NAME}")

    # What the harness and the app share past gcn_core -- the mix and its AAudio device, the
    # renderer and the game's sources -- compiled once for both.
    add_library(${G_NAME}_android OBJECT ${RT}/audio.cpp ${AND_DIR}/audio_aaudio.cpp
      ${RT}/gx/render_gl.cpp ${RT}/gx/shadergen.cpp ${RT}/gx/gl_load.cpp ${RT}/png.cpp
      ${GLAD_SOURCE} ${G_SOURCES})
    _gcn_set_standards(${G_NAME}_android)
    target_include_directories(${G_NAME}_android PRIVATE ${AND_DIR} ${RT} ${RECOMP_GEN_DIR} ${GLAD_INCLUDE_DIR})
    target_link_libraries(${G_NAME}_android PRIVATE game_recomp)
    target_compile_options(${G_NAME}_android PRIVATE ${GCN_RUNTIME_WARNINGS})
    target_compile_definitions(${G_NAME}_android PRIVATE ${ANDROID_DEFS})
    set_target_properties(${G_NAME}_android PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # Headless EGL frontend: the real renderer, offscreen, writing frames as PNGs. Not an
    # APK: it validates the GL ES renderer on a device over adb, and shows the stereo path
    # without anyone wearing the headset.
    add_executable(${G_NAME}_egl ${AND_DIR}/egl_main.cpp)
    _gcn_set_standards(${G_NAME}_egl)
    target_include_directories(${G_NAME}_egl PRIVATE ${AND_DIR} ${RT} ${RECOMP_GEN_DIR} ${GLAD_INCLUDE_DIR})
    target_link_libraries(${G_NAME}_egl PRIVATE gcn_core ${G_NAME}_android game_recomp EGL log aaudio)
    target_compile_options(${G_NAME}_egl PRIVATE ${GCN_RUNTIME_WARNINGS})
    target_compile_definitions(${G_NAME}_egl PRIVATE ${ANDROID_DEFS} ${GCN_ENTRY_DEFS})

    include(FetchContent)
    FetchContent_Declare(openxr_aar
      URL https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/1.1.63/openxr_loader_for_android-1.1.63.aar
      URL_HASH SHA256=622419d2f6741c3443a3beb4779af0764318edd01830de967f24c741ebcded73
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(openxr_aar)
    set(OPENXR_INCLUDE ${openxr_aar_SOURCE_DIR}/prefab/modules/headers/include)
    set(OPENXR_LIB ${openxr_aar_SOURCE_DIR}/prefab/modules/openxr_loader/libs/android.${ANDROID_ABI}/libopenxr_loader.so)

    add_library(native_app_glue STATIC
      ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
    target_include_directories(native_app_glue PUBLIC
      ${ANDROID_NDK}/sources/android/native_app_glue)

    # The installable app: android.app.NativeActivity loads lib<name>.so and calls
    # android_main(), so there is no Java source at all. The library name must match the
    # manifest's android.app.lib_name, which the template below fills in.
    add_library(${G_NAME} SHARED ${AND_DIR}/openxr_main.cpp ${GCN_RECOMP_DIR}/openxr/xr_frontend.cpp)
    _gcn_set_standards(${G_NAME})
    target_include_directories(${G_NAME} PRIVATE ${AND_DIR} ${GCN_RECOMP_DIR}/openxr ${RT} ${RECOMP_GEN_DIR} ${GLAD_INCLUDE_DIR} ${OPENXR_INCLUDE})
    target_link_libraries(${G_NAME} PRIVATE gcn_core ${G_NAME}_android game_recomp native_app_glue ${OPENXR_LIB} EGL android log aaudio)
    target_compile_options(${G_NAME} PRIVATE ${GCN_RUNTIME_WARNINGS})
    # No default image: the app finds game.iso or game.ciso in its own files directory.
    set(APP_DEFS ${ANDROID_DEFS} ${GCN_ENTRY_DEFS})
    list(FILTER APP_DEFS EXCLUDE REGEX "^GCN_DEFAULT_ISO=")
    target_compile_definitions(${G_NAME} PRIVATE ${APP_DEFS} GCN_DEFAULT_ISO="")
    # NativeActivity resolves this symbol by name; without it the linker drops the glue.
    target_link_options(${G_NAME} PRIVATE -u ANativeActivity_onCreate)

    configure_file(${AND_DIR}/AndroidManifest.xml.in ${CMAKE_BINARY_DIR}/AndroidManifest.xml @ONLY)
  endif()

  # ---- Vision Pro app ----
  # The OpenXR frontend's core on the runtime in visionos/xr_runtime.mm, with GL ES through
  # ANGLE, a Core Audio device and a SwiftUI launcher, as an app bundle Xcode signs and
  # installs. Configure with -G Xcode -DCMAKE_SYSTEM_NAME=visionOS -DGCN_APPLE_TEAM=<team>.
  # ANGLE is built from source by tools/build-angle-visionos.sh the first time, into
  # GCN_ANGLE_DIR.
  if(GCN_VISIONOS AND HAVE_GAME_ISO AND G_VISIONOS_BUNDLE_ID)
    set(VOS_DIR ${GCN_RECOMP_DIR}/visionos)
    _gcn_fetch_openxr_headers()
    set(GCN_ANGLE_DIR ${CMAKE_BINARY_DIR}/angle CACHE PATH "ANGLE for visionOS, as tools/build-angle-visionos.sh leaves it")
    if(NOT EXISTS ${GCN_ANGLE_DIR}/libANGLE-shared.dylib)
      message(STATUS "Building ANGLE for visionOS into ${GCN_ANGLE_DIR} (a few minutes)")
      execute_process(COMMAND sh ${GCN_RECOMP_DIR}/tools/build-angle-visionos.sh ${GCN_ANGLE_DIR} xros
                      RESULT_VARIABLE _r)
      if(NOT _r EQUAL 0)
        message(FATAL_ERROR "building ANGLE failed")
      endif()
    endif()
    set(GCN_APPLE_TEAM "" CACHE STRING "The Apple development team that signs the visionOS app")
    set(VOS_SOURCES ${CORE_SOURCES} ${RT}/audio.cpp ${RT}/gx/render_gl.cpp ${RT}/gx/shadergen.cpp
      ${RT}/gx/gl_load.cpp ${RT}/png.cpp ${GLAD_SOURCE} ${GCN_RECOMP_DIR}/openxr/xr_frontend.cpp
      ${VOS_DIR}/xr_runtime.mm ${VOS_DIR}/angle.mm ${VOS_DIR}/visionos_main.mm ${VOS_DIR}/audio_coreaudio.mm
      ${VOS_DIR}/App.swift
      ${G_SOURCES})
    list(REMOVE_ITEM VOS_SOURCES ${RT}/audio_stub.cpp)
    add_executable(${G_NAME} MACOSX_BUNDLE ${VOS_SOURCES})
    _gcn_set_standards(${G_NAME})
    target_include_directories(${G_NAME} PRIVATE ${VOS_DIR} ${GCN_RECOMP_DIR}/openxr ${RT} ${RECOMP_GEN_DIR}
      ${GLAD_INCLUDE_DIR} ${GCN_OPENXR_INCLUDE} ${GCN_ANGLE_DIR}/include)
    target_link_libraries(${G_NAME} PRIVATE game_recomp
      "-framework CompositorServices" "-framework ARKit" "-framework Metal" "-framework GameController"
      "-framework AudioToolbox" "-framework AVFAudio" "-framework Foundation")
    # On the C family's sources only: the Xcode generator does not split a target's
    # definitions by language, and Swift takes no valued ones.
    set(VOS_DEFS ${GCN_GAME_DEFS} ${GCN_ENTRY_DEFS} ${GCN_GL_DEFS})
    list(FILTER VOS_DEFS EXCLUDE REGEX "^GCN_DEFAULT_ISO=")
    list(APPEND VOS_DEFS GCN_DEFAULT_ISO="")
    set(VOS_C_SOURCES ${VOS_SOURCES})
    list(FILTER VOS_C_SOURCES EXCLUDE REGEX "\\.swift$")
    set_source_files_properties(${VOS_C_SOURCES} PROPERTIES COMPILE_DEFINITIONS "${VOS_DEFS}"
                                COMPILE_OPTIONS "${GCN_RUNTIME_WARNINGS}")
    configure_file(${VOS_DIR}/Info.plist.in ${CMAKE_BINARY_DIR}/visionos/Info.plist @ONLY)
    set_target_properties(${G_NAME} PROPERTIES
      OUTPUT_NAME ${G_NAME}
      XCODE_ATTRIBUTE_INFOPLIST_FILE ${CMAKE_BINARY_DIR}/visionos/Info.plist
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${G_VISIONOS_BUNDLE_ID}
      XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER ${VOS_DIR}/bridge.h
      XCODE_ATTRIBUTE_SWIFT_VERSION 5.0
      XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
      XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY 7
      XCODE_ATTRIBUTE_XROS_DEPLOYMENT_TARGET 26.0
      XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
      XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
      XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${GCN_APPLE_TEAM}"
      XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS ${VOS_DIR}/app.entitlements
      XCODE_ATTRIBUTE_ENABLE_BITCODE NO
      # ANGLE is opened from the bundle's Frameworks directory (visionos/angle.mm), signed
      # as it is copied in.
      XCODE_EMBED_FRAMEWORKS ${GCN_ANGLE_DIR}/libANGLE-shared.dylib
      XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY ON
      XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks")
  endif()

  # For a game project that adds frontends of its own -- a headset app, a headless harness
  # on a device -- the pieces such a target is assembled from. GCN_CORE_SOURCES is the
  # guest and the hardware with no renderer, no SDL and the silent audio stub;
  # GCN_AUDIO_SOURCES is the real mix (a frontend adds its own device backend);
  # GCN_RENDER_SOURCES is the GL renderer. Every target also needs ${GCN_GLAD_SOURCE} off
  # Apple, the include directories GCN_RT, GCN_GEN_DIR and GCN_GLAD_INCLUDE_DIR, and the
  # definitions GCN_GAME_DEFS and GCN_GL_DEFS; it links game_recomp.
  set(GCN_RT ${RT} PARENT_SCOPE)
  set(GCN_GEN_DIR ${RECOMP_GEN_DIR} PARENT_SCOPE)
  set(GCN_HAVE_GAME_ISO ${HAVE_GAME_ISO} PARENT_SCOPE)
  set(GCN_CORE_SOURCES ${CORE_SOURCES} PARENT_SCOPE)
  set(GCN_AUDIO_SOURCES ${RT}/audio.cpp PARENT_SCOPE)
  set(GCN_RENDER_SOURCES ${RT}/gx/render_gl.cpp ${RT}/gx/shadergen.cpp ${RT}/gx/gl_load.cpp ${RT}/png.cpp PARENT_SCOPE)
  set(GCN_GLAD_INCLUDE_DIR ${GLAD_INCLUDE_DIR} PARENT_SCOPE)
  set(GCN_GLAD_SOURCE ${GLAD_SOURCE} PARENT_SCOPE)
  set(GCN_RUNTIME_WARNINGS ${GCN_RUNTIME_WARNINGS} PARENT_SCOPE)
  set(GCN_GAME_DEFS ${GCN_GAME_DEFS} ${GCN_ENTRY_DEFS} PARENT_SCOPE)
  set(GCN_GL_DEFS ${GCN_GL_DEFS} PARENT_SCOPE)
endfunction()
