# Public WiiCompiled product graph.
#
# The translator owns the translated build graph. Mario Kart's profile-neutral
# functions are compiled once into mkw_base_shared; only callers whose direct
# ABI differs between profiles receive small base/RR variants.

set(DATA_INIT_FILE "${MKW_RUNTIME_SOURCE_DIR}/../generated/data_sections_init.cpp")
set(DATA_INIT_BLOB_ASM "${MKW_RUNTIME_SOURCE_DIR}/../generated/data_sections_init_blobs.S")
# WiiDS releases carry no game executable bytes: the sections are rebuilt from the player's own
# disc at startup and checked against SHA-256 pins (runtime/src/wiids_game_data.cpp). The manifest
# comes from experiments/release/gen_game_data_manifest.py. OFF restores the embedded blobs.
option(WIIDS_RUNTIME_GAME_DATA "Rebuild DOL/REL sections from the player's disc instead of embedding them" ON)
if(WIIDS_RUNTIME_GAME_DATA)
    set(DATA_INIT_FILE "${MKW_RUNTIME_SOURCE_DIR}/../generated/data_sections_runtime.cpp")
    set(DATA_INIT_BLOB_ASM "")
    if(NOT EXISTS "${DATA_INIT_FILE}")
        message(FATAL_ERROR "WIIDS_RUNTIME_GAME_DATA needs ${DATA_INIT_FILE}; run experiments/release/gen_game_data_manifest.py")
    endif()
endif()
if(EXISTS "${DATA_INIT_FILE}")
    list(APPEND SOURCES "${DATA_INIT_FILE}")
endif()
# Crash-report symbolization table emitted by generate-data-init. The stub
# (deliberately outside the globbed src/ tree so it is never picked up twice)
# keeps link succeeding when the generated table has not been produced yet.
set(GUEST_SYMBOL_TABLE_FILE "${MKW_RUNTIME_SOURCE_DIR}/../generated/guest_symbol_table.cpp")
if(EXISTS "${GUEST_SYMBOL_TABLE_FILE}")
    list(APPEND SOURCES "${GUEST_SYMBOL_TABLE_FILE}")
else()
    list(APPEND SOURCES "${MKW_RUNTIME_SOURCE_DIR}/cmake/guest_symbol_table_stub.cpp")
endif()
if(DATA_INIT_BLOB_ASM AND EXISTS "${DATA_INIT_BLOB_ASM}")
    enable_language(ASM)
    set_source_files_properties("${DATA_INIT_BLOB_ASM}" PROPERTIES LANGUAGE ASM SKIP_UNITY_BUILD_INCLUSION ON)
    list(APPEND SOURCES "${DATA_INIT_BLOB_ASM}")
endif()
list(REMOVE_DUPLICATES SOURCES)

function(mkw_apply_common_compile_options target)
    target_compile_options(${target} PRIVATE -O3 -ffast-math -w -pipe)
endfunction()

function(mkw_apply_translated_compile_options target)
    # -O2, NOT -O3 (2026-09-08): -O3 was tried twice to attack the confirmed CPU-bound bottleneck
    # (the single emulation thread runs pegged at ~100% CPU during gameplay)
    # and broke real gameplay correctness both times:
    # first attempt (-O3 alone) caused idling-with-stick-input to slide the character unnaturally;
    # second attempt added -fno-strict-aliasing -fwrapv (the standard mitigation for exactly this
    # class of recompiler bug - CpuContext's guest register file is read/written through raw
    # pointers of different types, which is the textbook strict-aliasing violation) and it STILL
    # broke - the character could move while just revving the engine, no stick input needed. That
    # second symptom means the bug isn't (only) strict-aliasing-related after all - something else
    # about -O3's codegen is unsafe for this translated corpus. Do not re-attempt -O3 here without
    # a real per-function bisection to find which specific translated function(s) need O2's more
    # conservative codegen, rather than flipping the whole corpus and hoping a blanket flag fixes
    # it. Explicitly deferred past the next update (UI-focused) - revisit later, not urgent.
    # -fno-slp-vectorize is Clang/LLVM's flag name; GCC's equivalent pass has a different name
    # (-fno-tree-slp-vectorize). Switch (devkitA64) is GCC-only - see runtime/CMakeLists.txt's
    # MKW_TARGET_SWITCH branch - so this needs to branch on compiler ID rather than assume Clang
    # the way every other platform this project has targeted so far could.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(MKW_NO_SLP_VECTORIZE_FLAG -fno-tree-slp-vectorize)
        # The shards are built -fPIC; GCC then treats every non-static function as interposable
        # and refuses to inline or specialise calls to it across the corpus. Nothing here is
        # interposed (the Switch NRO is one static executable), so opting out changes no computed
        # result - unlike -O3 above, this is codegen-neutral for guest semantics.
        list(APPEND MKW_NO_SLP_VECTORIZE_FLAG -fno-semantic-interposition)
    else()
        set(MKW_NO_SLP_VECTORIZE_FLAG -fno-slp-vectorize)
    endif()
    # One section per function so the linker can place the hot ones together; see
    # MKW_SYMBOL_ORDER_FILE below. Layout only - this changes no generated code.
    target_compile_options(${target} PRIVATE
        -O2 ${MKW_TRANSLATED_PPC_FP_OPTIONS} ${MKW_NO_SLP_VECTORIZE_FLAG} -w -pipe
        -ffunction-sections -fdata-sections)
endfunction()

function(mkw_configure_object_target target)
    target_include_directories(${target} PRIVATE
        "${MKW_RUNTIME_SOURCE_DIR}/include"
        "${MKW_RUNTIME_SOURCE_DIR}/src"
        # Workspace root, so translator output is spelled "generated/<x>.h"
        # instead of a ../ chain whose depth depends on the includer.
        "${MKW_RUNTIME_SOURCE_DIR}/.."
        "${MKW_RUNTIME_SOURCE_DIR}/../aurora-main/include")
    target_compile_definitions(${target} PRIVATE
        TARGET_PC)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
endfunction()

# Translated shard TUs are the memory-hungry compiles; everything else in the build is
# comparatively small. A dedicated Ninja job pool caps how many of them run at once so the
# global parallelism can use every core for the cheap TUs without the memory ceiling being a
# guess. The pool depth is per-machine (derived from installed RAM by LocalBuild.ps1) and is
# deliberately not part of the canonical flag set: it changes scheduling, never output bytes.
if(MKW_TRANSLATED_COMPILE_JOBS GREATER 0)
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS "mkw_translated=${MKW_TRANSLATED_COMPILE_JOBS}")
endif()

function(mkw_bound_translated_compiles target)
    if(MKW_TRANSLATED_COMPILE_JOBS GREATER 0)
        set_property(TARGET ${target} PROPERTY JOB_POOL_COMPILE mkw_translated)
    endif()
endfunction()

function(mkw_configure_translated_target target)
    mkw_configure_object_target(${target})
    mkw_apply_translated_compile_options(${target})
    mkw_bound_translated_compiles(${target})
endfunction()

add_library(mkw_runtime_common OBJECT ${SOURCES})
mkw_configure_object_target(mkw_runtime_common)
target_compile_features(mkw_runtime_common PRIVATE cxx_std_20)
target_compile_definitions(mkw_runtime_common PRIVATE
    SDL_MAIN_HANDLED
    _DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION)
target_link_libraries(mkw_runtime_common PRIVATE
    aurora::gx aurora::pad aurora::si aurora::vi aurora::mtx aurora::thp TracyClient)
target_link_libraries(mkw_runtime_common PRIVATE mkw::pugixml mkw::toml11 mkw::cryptopp mkw::mbedtls)
if(TARGET adrenotools)
    # Custom Vulkan driver loading (android_gpu_driver.cpp). Scoped to this target so the define
    # does not touch the translated shards' compile lines.
    target_link_libraries(mkw_runtime_common PRIVATE adrenotools)
    target_compile_definitions(mkw_runtime_common PRIVATE MKW_HAVE_ADRENOTOOLS)
endif()
if(WIN32)
    target_link_libraries(mkw_runtime_common PRIVATE shell32 windowsapp)
else()
    target_link_libraries(mkw_runtime_common PRIVATE mkw::libco)
endif()
if(MKW_CPPWINRT_INCLUDE_DIR)
    if(NOT EXISTS "${MKW_CPPWINRT_INCLUDE_DIR}/winrt/base.h")
        message(FATAL_ERROR
            "MKW_CPPWINRT_INCLUDE_DIR does not contain winrt/base.h: ${MKW_CPPWINRT_INCLUDE_DIR}")
    endif()
    target_include_directories(mkw_runtime_common PRIVATE "${MKW_CPPWINRT_INCLUDE_DIR}")
endif()

# GCC's profile-guided build treats a function whose code changed since the recording run exactly
# like one that never ran: it loses its counts and is then optimized for size, as cold code. For
# the hand-written runtime that punishes every edit to a hot function until the next recording.
# With this flag such functions are compiled as if there were no profile at all instead. Not
# applied to the translated shards, where "never ran" really means cold and size matters.
if(MKW_TARGET_SWITCH AND MKW_PGO STREQUAL "use")
    target_compile_options(mkw_runtime_common PRIVATE -fprofile-partial-training)
endif()

# Keep runtime unity units small and semantically related. The old generated-TU
# batch size put all 57 native runtime sources into one memory-heavy compiler job.
foreach(source IN LISTS SOURCES)
    get_filename_component(source_name "${source}" NAME_WE)
    string(REPLACE "\\" "/" source_normalized "${source}")
    if(source_normalized MATCHES "/hle/gx/")
        set(runtime_group "gx_bridge")
    elseif(source_name MATCHES "network|socket|dns|dwc|ios")
        set(runtime_group "network_ios")
    elseif(source_name MATCHES "os_|system|memory|fiber|scheduler")
        set(runtime_group "guest_system")
    elseif(source_name MATCHES "debug|trace|prof")
        set(runtime_group "diagnostics")
    else()
        string(SHA256 source_hash "${source_name}")
        string(SUBSTRING "${source_hash}" 0 4 source_hash_prefix)
        math(EXPR runtime_bucket "0x${source_hash_prefix} % 8")
        set(runtime_group "runtime_${runtime_bucket}")
    endif()
    set_source_files_properties("${source}" PROPERTIES UNITY_GROUP "${runtime_group}")
endforeach()
# These translation units implement guest-visible floating-point bit
# semantics.  Keep them out of the fast-math runtime unity groups and apply
# the same contraction/rounding policy as translated PPC shards.
set(MKW_PPC_SEMANTIC_RUNTIME_SOURCES
    "${MKW_RUNTIME_SOURCE_DIR}/src/ppc_helpers.cpp"
    "${MKW_RUNTIME_SOURCE_DIR}/src/fpu_helpers.cpp"
    # ax_effects.cpp previously relied solely on a `#if defined(__clang__) #pragma clang fp
    # contract(off) #endif` guard to suppress FMA contraction on top of this target's -ffast-math.
    # That pragma is a silent no-op under GCC (devkitA64/Switch has no Clang - see
    # runtime/CMakeLists.txt's MKW_TARGET_SWITCH branch), which would leave PowerPC's discrete
    # fmuls/fadds semantics unenforced there. The compile-flag override below is portable across
    # every compiler this project targets; the pragma is left in place too since it's harmless on
    # Clang platforms, but this flag is what actually guarantees the behavior on Switch.
    "${MKW_RUNTIME_SOURCE_DIR}/src/hle/audio/ax_effects.cpp")
set_source_files_properties(${MKW_PPC_SEMANTIC_RUNTIME_SOURCES} PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON
    SKIP_PRECOMPILE_HEADERS ON
    COMPILE_OPTIONS "${MKW_TRANSLATED_PPC_FP_OPTIONS}")
# guest_flat_memory.cpp defines MKW_GUEST_FLAT_BASE_WRITER before including guest_flat_memory.h
# (it owns the only write to the Switch guest base); a unity batch or the PCH would include the
# reader's const declaration first.
set_source_files_properties("${MKW_RUNTIME_SOURCE_DIR}/src/guest_flat_memory.cpp" PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON
    SKIP_PRECOMPILE_HEADERS ON)
set_target_properties(mkw_runtime_common PROPERTIES UNITY_BUILD ON UNITY_BUILD_MODE GROUP)
target_precompile_headers(mkw_runtime_common PRIVATE "${MKW_RUNTIME_SOURCE_DIR}/include/mkw_pch.h")
mkw_apply_common_compile_options(mkw_runtime_common)

# Host ISA guard. Everything in MKW_ALL_BUILD_TARGETS below is compiled with
# -march=x86-64-v3; this object library deliberately is not, which
# is the whole point of keeping it out of mkw_runtime_common. It runs a CPUID
# check from a C initializer so an unsupported machine gets a readable error
# instead of an illegal-instruction crash. Excluded from the unity build and the
# precompiled header because both are produced with the owning target's flags.
add_library(mkw_cpu_baseline OBJECT "${MKW_CPU_BASELINE_SOURCE}")
target_compile_features(mkw_cpu_baseline PRIVATE cxx_std_17)
set_target_properties(mkw_cpu_baseline PROPERTIES UNITY_BUILD OFF)
target_compile_options(mkw_cpu_baseline PRIVATE -w)

if(NOT MKW_BASE_COMMON_SHARDS)
    message(FATAL_ERROR "Translator build graph contains no shared base shards")
endif()

add_library(mkw_base_shared STATIC ${MKW_BASE_COMMON_SHARDS})
mkw_configure_translated_target(mkw_base_shared)
target_precompile_headers(mkw_base_shared PRIVATE "${MKW_RUNTIME_SOURCE_DIR}/include/mkw_pch.h")

if(MKW_BASE_PORTABLE_SENSITIVE_SHARDS)
    add_library(mkw_base_sensitive OBJECT ${MKW_BASE_PORTABLE_SENSITIVE_SHARDS})
    mkw_configure_translated_target(mkw_base_sensitive)
    target_precompile_headers(mkw_base_sensitive REUSE_FROM mkw_base_shared)
endif()

if(MKW_HAVE_RETRO_REWIND)
    if(MKW_RETRO_PORTABLE_SENSITIVE_SHARDS)
        add_library(mkw_retro_sensitive OBJECT ${MKW_RETRO_PORTABLE_SENSITIVE_SHARDS})
        mkw_configure_translated_target(mkw_retro_sensitive)
        target_precompile_headers(mkw_retro_sensitive REUSE_FROM mkw_base_shared)
    endif()

    set(MKW_RETRO_TRANSLATED_SOURCES ${MKW_RETRO_MOD_SHARDS} ${MKW_RETRO_EXTRA_SOURCES})
    set(MKW_RETRO_BLOB_OBJECTS)
    foreach(source IN LISTS MKW_RETRO_EXTRA_SOURCES)
        if(source MATCHES "\\.S$")
            enable_language(ASM)
            set_source_files_properties("${source}" PROPERTIES LANGUAGE ASM SKIP_PRECOMPILE_HEADERS ON)
        endif()
    endforeach()
    add_library(mkw_retro_rewind_functions OBJECT ${MKW_RETRO_TRANSLATED_SOURCES})
    mkw_configure_translated_target(mkw_retro_rewind_functions)
    target_precompile_headers(mkw_retro_rewind_functions REUSE_FROM mkw_base_shared)
endif()

function(mkw_configure_product target)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:mkw_runtime_common>)
    # Startup CPU check. Must stay a separate object library so it keeps the
    # plain baseline ISA while everything around it is built for x86-64-v3.
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:mkw_cpu_baseline>)
    target_include_directories(${target} PRIVATE
        "${MKW_RUNTIME_SOURCE_DIR}/include"
        "${MKW_RUNTIME_SOURCE_DIR}/src"
        # Workspace root, so translator output is spelled "generated/<x>.h"
        # instead of a ../ chain whose depth depends on the includer.
        "${MKW_RUNTIME_SOURCE_DIR}/.."
        "${MKW_RUNTIME_SOURCE_DIR}/../aurora-main/include")
    target_compile_definitions(${target} PRIVATE
        SDL_MAIN_HANDLED _DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION TARGET_PC)
    target_compile_features(${target} PRIVATE cxx_std_20)
    mkw_apply_common_compile_options(${target})
    # The dispatch-table and registration shards compile inside the product target itself and
    # include the same fat translated headers; bound them by the same pool.
    mkw_bound_translated_compiles(${target})
    target_link_libraries(${target} PRIVATE
        mkw_base_shared mkw::pugixml mkw::toml11 mkw::cryptopp mkw::mbedtls)

    target_link_libraries(${target} PRIVATE
        aurora::gx aurora::pad aurora::si aurora::vi aurora::mtx aurora::thp)
    # mkw_runtime_common is an OBJECT library (see the longer explanation on the WIN32/libco
    # branch below): its own target_link_libraries(... TracyClient) doesn't propagate to a
    # consumer that only pulls in its .o files via $<TARGET_OBJECTS:>, so the Tracy zone/plot
    # calls compiled into those objects need TracyClient linked here directly too.
    target_link_libraries(${target} PRIVATE TracyClient)
    if(EXISTS "${MKW_AURORA_DIR}/cmake/AuroraCopyRuntimeDLLs.cmake")
        include("${MKW_AURORA_DIR}/cmake/AuroraCopyRuntimeDLLs.cmake")
        aurora_copy_runtime_dlls(${target})
    endif()
    if(TARGET sqlite3)
        get_target_property(MKW_SQLITE_TARGET_TYPE sqlite3 TYPE)
    endif()
    if(TARGET sqlite3 AND
       (MKW_SQLITE_TARGET_TYPE STREQUAL "SHARED_LIBRARY" OR
        MKW_SQLITE_TARGET_TYPE STREQUAL "MODULE_LIBRARY"))
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:sqlite3> $<TARGET_FILE_DIR:${target}>)
    endif()

    if(WIN32)
        target_link_libraries(${target} PRIVATE
            dbghelp user32 winmm ws2_32 iphlpapi secur32 crypt32 windowsapp)

        set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
    else()
        # mkw_runtime_common is an OBJECT library: WiiCompiled/RetroRewind only pull in its .o
        # files via $<TARGET_OBJECTS:>, which does not propagate mkw_runtime_common's own
        # target_link_libraries (object libraries don't carry usage requirements to a consumer
        # that isn't itself linked against as a target). fiber_manager.cpp's co_* calls live in
        # those objects, so the actual executable link needs mkw::libco directly, same as it
        # needs it independently of that first `if(WIN32)` branch above.
        target_link_libraries(${target} PRIVATE mkw::libco)
    endif()
    if(MKW_TARGET_ANDROID)
        # log: __android_log_* (runtime_log's Android sink - not yet wired, P3/P5 follow-up).
        # android: NDK glue (ANativeWindow, asset manager, etc - aurora's SDL3/window path uses
        # this transitively too, linking it directly here is cheap and avoids relying on a
        # transitive dependency from a static lib always propagating it correctly).
        # EGL/GLESv2: the OpenGL ES fallback path (DAWN_ENABLE_OPENGLES, set above in the parent
        # CMakeLists.txt) needs these at link time even when Vulkan is the backend actually
        # selected at runtime, since Dawn's OpenGL ES backend is compiled in either way.
        # vulkan: the NDK's libvulkan.so loader stub - verified present for every API level this
        # NDK ships (checked API 24 through 35 in the r27c sysroot).
        target_link_libraries(${target} PRIVATE log android EGL GLESv2 vulkan)
        if(TARGET adrenotools)
            target_link_libraries(${target} PRIVATE adrenotools)
            # adrenotools loads these from nativeLibraryDir at runtime; build them with the product
            # so they are always there to copy into jniLibs beside it.
            add_dependencies(${target} main_hook file_redirect_hook gsl_alloc_hook)
        endif()
        # Bind calls between functions defined in this library directly instead of through the
        # PLT. Nothing interposes on these symbols (Java finds its JNI entry points by name, which
        # this doesn't affect), and profiling showed ~5% of the game thread in @plt stubs.
        target_link_options(${target} PRIVATE -Wl,-Bsymbolic-functions)
        # Optional hot/cold code layout: a plain list of symbol names, hottest first, as produced
        # from a profile (see runtime/tools/gen_symbol_order.sh). The translated corpus is ~70MB of text
        # laid out in arbitrary order, which thrashes the instruction TLB; ordering it costs
        # nothing at runtime and changes no codegen.
        if(MKW_SYMBOL_ORDER_FILE)
            if(NOT EXISTS "${MKW_SYMBOL_ORDER_FILE}")
                message(FATAL_ERROR "MKW_SYMBOL_ORDER_FILE does not exist: ${MKW_SYMBOL_ORDER_FILE}")
            endif()
            target_link_options(${target} PRIVATE
                "-Wl,--symbol-ordering-file=${MKW_SYMBOL_ORDER_FILE}" -Wl,--no-warn-symbol-ordering)
        endif()
    endif()
    if(WIN32)
        foreach(runtime_dll libc++.dll libunwind.dll)
            execute_process(
                COMMAND "${CMAKE_CXX_COMPILER}" "--print-file-name=${runtime_dll}"
                OUTPUT_VARIABLE runtime_dll_path
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT EXISTS "${runtime_dll_path}")
                get_filename_component(mkw_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
                set(runtime_dll_path "${mkw_compiler_bin}/${runtime_dll}")
            endif()
            if(NOT EXISTS "${runtime_dll_path}")
                message(FATAL_ERROR "llvm-mingw runtime DLL not found: ${runtime_dll}")
            endif()
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${runtime_dll_path}" $<TARGET_FILE_DIR:${target}>)
        endforeach()
    endif()

    set(MKW_WII_BOOTSTRAP_SOURCE_DIR "${MKW_RUNTIME_SOURCE_DIR}/assets/wii")
    if(NOT EXISTS "${MKW_WII_BOOTSTRAP_SOURCE_DIR}/shared2/wc24")
        message(FATAL_ERROR "Missing Wii first-run bootstrap payload: ${MKW_WII_BOOTSTRAP_SOURCE_DIR}")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MKW_WII_BOOTSTRAP_SOURCE_DIR}" "$<TARGET_FILE_DIR:${target}>/wii_bootstrap")

    set(MKW_DSP_COEFFICIENT_ROM "${MKW_RUNTIME_SOURCE_DIR}/assets/dsp/dsp_coef.bin")
    if(NOT EXISTS "${MKW_DSP_COEFFICIENT_ROM}")
        message(FATAL_ERROR "Missing Wii DSP coefficient ROM: ${MKW_DSP_COEFFICIENT_ROM}")
    endif()
    file(SHA256 "${MKW_DSP_COEFFICIENT_ROM}" MKW_DSP_COEFFICIENT_ROM_SHA256)
    if(NOT MKW_DSP_COEFFICIENT_ROM_SHA256 STREQUAL
       "d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179")
        message(FATAL_ERROR "Wii DSP coefficient ROM hash mismatch: ${MKW_DSP_COEFFICIENT_ROM_SHA256}")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MKW_DSP_COEFFICIENT_ROM}" "$<TARGET_FILE_DIR:${target}>/dsp_coef.bin")

    # Aurora imports this portable recipe database into each user's writable
    # pipeline cache. Keep the upstream filename so its default resourcesPath
    # lookup works without application-specific configuration.
    set(MKW_INITIAL_PIPELINE_CACHE
        "${MKW_RUNTIME_SOURCE_DIR}/assets/pipeline/initial_pipeline_cache.db")
    if(NOT EXISTS "${MKW_INITIAL_PIPELINE_CACHE}")
        message(FATAL_ERROR "Missing transferable Aurora pipeline cache: ${MKW_INITIAL_PIPELINE_CACHE}")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MKW_INITIAL_PIPELINE_CACHE}"
        "$<TARGET_FILE_DIR:${target}>/initial_pipeline_cache.db")

    # Non-Windows TLS (runtime/src/hle/net/network_ssl.cpp's mbed TLS path) needs a trusted root
    # CA bundle to verify server certificates against - Windows gets this for free from the OS via
    # Schannel, mbed TLS does not ship one itself. Not SHA256-pinned like the DSP ROM above: unlike
    # a fixed hardware ROM, this bundle is expected to be refreshed periodically as CAs rotate.
    if(NOT MKW_TARGET_ANDROID)
        set(MKW_CA_CERTIFICATE_BUNDLE "${MKW_RUNTIME_SOURCE_DIR}/assets/certs/cacert.pem")
        if(NOT EXISTS "${MKW_CA_CERTIFICATE_BUNDLE}")
            message(FATAL_ERROR "Missing TLS root CA bundle: ${MKW_CA_CERTIFICATE_BUNDLE}")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MKW_CA_CERTIFICATE_BUNDLE}" "$<TARGET_FILE_DIR:${target}>/cacert.pem")
    endif()
endfunction()

# On every desktop platform - and on Switch, which also execs a real process (an .nro via
# hbloader, not a library loaded into a host app) - each product is a standalone executable. Only
# Android differs: there is no process to exec there, so the product becomes a SHARED library
# (libwii.so / libretro_rewind.so) loaded into the host Kotlin app's process via JNI, with
# RuntimeMain (runtime/src/main.cpp) as the entry point the JNI bridge calls instead of a real
# argv-driven main(). Not yet wired up: the actual JNI bridge that calls RuntimeMain is P5 work
# (android/app/src/main/cpp/jni/bridge.cpp,
# not written yet) - this only makes the library itself buildable as a .so.
function(mkw_add_product_target target)
    if(MKW_TARGET_ANDROID)
        add_library(${target} SHARED ${ARGN})
    else()
        add_executable(${target} ${ARGN})
    endif()
endfunction()

mkw_add_product_target(WiiCompiled "${MKW_BASE_PRODUCT_SOURCE}" ${MKW_BASE_REGISTRATION_SOURCES})
mkw_configure_product(WiiCompiled)
target_precompile_headers(WiiCompiled PRIVATE
    "${MKW_RUNTIME_SOURCE_DIR}/include/mkw_pch.h")
if(TARGET mkw_base_sensitive)
    target_sources(WiiCompiled PRIVATE $<TARGET_OBJECTS:mkw_base_sensitive>)
endif()

if(MKW_HAVE_RETRO_REWIND)
    mkw_add_product_target(RetroRewind "${MKW_RETRO_REWIND_PRODUCT_SOURCE}" ${MKW_RETRO_REGISTRATION_SOURCES})
    mkw_configure_product(RetroRewind)
    target_precompile_headers(RetroRewind REUSE_FROM WiiCompiled)
    if(TARGET mkw_retro_sensitive)
        target_sources(RetroRewind PRIVATE $<TARGET_OBJECTS:mkw_retro_sensitive>)
    endif()
    target_sources(RetroRewind PRIVATE $<TARGET_OBJECTS:mkw_retro_rewind_functions>)
    if(MKW_RETRO_BLOB_OBJECTS)
        target_sources(RetroRewind PRIVATE ${MKW_RETRO_BLOB_OBJECTS})
    endif()

    if(MKW_TARGET_ANDROID)
        # Android-only: one combined libGameCombined.so instead of separate
        # libWiiCompiled.so/libRetroRewind.so. See runtime/cmake/build_combined_android_lib.py
        # for the full rationale (live-device profiling showed real cross-.so call overhead
        # from the split) and how the 1,341 same-name-different-body translated functions
        # between the two profiles are resolved. Desktop's WiiCompiled/RetroRewind targets
        # above are completely unaffected - this reuses their already-compiled objects
        # read-only, it does not modify them.
        add_library(mkw_combined_product OBJECT "${MKW_COMBINED_PRODUCT_SOURCE}")
        mkw_configure_object_target(mkw_combined_product)
        target_compile_features(mkw_combined_product PRIVATE cxx_std_20)

        # NDK's android.toolchain.cmake sets CMAKE_OBJCOPY; fall back to deriving it from the
        # compiler path (same bin/ directory) if some other Android toolchain file doesn't.
        if(CMAKE_OBJCOPY)
            set(MKW_ANDROID_OBJCOPY "${CMAKE_OBJCOPY}")
        else()
            get_filename_component(MKW_ANDROID_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
            set(MKW_ANDROID_OBJCOPY "${MKW_ANDROID_TOOLCHAIN_BIN}/llvm-objcopy")
        endif()

        set(MKW_COMBINED_ANDROID_LIB "${CMAKE_CURRENT_BINARY_DIR}/libGameCombined.so")
        add_custom_command(
            OUTPUT "${MKW_COMBINED_ANDROID_LIB}"
            COMMAND python3
                "${MKW_RUNTIME_SOURCE_DIR}/cmake/build_combined_android_lib.py"
                "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_MAKE_PROGRAM}"
                "${MKW_ANDROID_OBJCOPY}" "${MKW_COMBINED_ANDROID_LIB}"
            DEPENDS WiiCompiled RetroRewind mkw_combined_product
                "${MKW_RUNTIME_SOURCE_DIR}/cmake/build_combined_android_lib.py"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Linking combined Android library (WiiCompiled + RetroRewind, one .so)"
            VERBATIM)
        add_custom_target(GameCombined ALL DEPENDS "${MKW_COMBINED_ANDROID_LIB}")
    endif()

    add_custom_target(mkw_release DEPENDS WiiCompiled RetroRewind)
else()
    add_custom_target(mkw_release DEPENDS WiiCompiled)
    message(STATUS "RetroRewind target disabled (run translate-mod and emit-build-shards)")
endif()

set(MKW_ALL_BUILD_TARGETS
    mkw_runtime_common mkw_base_shared mkw_base_sensitive mkw_retro_sensitive
    mkw_retro_rewind_functions WiiCompiled RetroRewind)
foreach(target IN LISTS MKW_ALL_BUILD_TARGETS)
    if(TARGET ${target})
        if(NOT MKW_TARGET_ANDROID AND NOT MKW_TARGET_SWITCH)
            # x86-64-v3 (Haswell-class: AVX2/FMA/BMI2/...) is this project's fixed baseline on
            # Windows/Linux - see host_cpu_baseline.cpp, which turns a machine below that line
            # into a readable error instead of an illegal-instruction crash. There is no arm64
            # equivalent question to ask: NEON/Advanced SIMD is mandatory on every ARMv8-A chip,
            # so Android and Switch get no -march flag at all (the NDK's per-ABI defaults and
            # devkitA64's own aarch64-none-elf defaults already target a reasonable baseline for
            # their respective chips - Switch's Cortex-A57 in particular is far below Haswell-class
            # x86, so this exclusion is not optional the way it might look from the Android case
            # alone).
            target_compile_options(${target} PRIVATE -march=x86-64-v3)
        endif()
    endif()
endforeach()
