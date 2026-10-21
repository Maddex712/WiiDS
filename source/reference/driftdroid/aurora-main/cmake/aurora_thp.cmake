# Ported from upstream aurora commit 3251f4e2 ("Add aurora::thp implementation"): a native THP
# video/audio decoder, verified bit-exact against Dolphin upstream. Used here to replace the
# guest's own THPVideoDecode/THPAudioDecode, which profiling showed dominate movie playback
# (the Select Cup screen runs at 9 FPS, and the attract demo is ~72% THP decode).
add_library(aurora_thp STATIC
  lib/dolphin/thp/THPAudio.cpp
  lib/dolphin/thp/THPDec.cpp
)
add_library(aurora::thp ALIAS aurora_thp)
set_target_properties(aurora_thp PROPERTIES FOLDER "aurora")

target_include_directories(aurora_thp PUBLIC include)
target_link_libraries(aurora_thp PUBLIC aurora::core)

# Movie decode runs every frame on movie-heavy menus. -O3 lets the compiler vectorise the IDCT,
# which measured 2.2x over the -O2 build with byte-identical output across every THP on the disc.
# fp-contract=off keeps the float arithmetic exactly as written (the decoder places its own fma
# calls where the reference fuses), so the output cannot drift with the compiler's contraction
# choices.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(aurora_thp PRIVATE -O3 -ffp-contract=off)
endif ()
