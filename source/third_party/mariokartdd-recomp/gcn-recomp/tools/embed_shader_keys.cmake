# cmake -DIN=<key list> -DOUT=<file.cpp> -P embed_shader_keys.cmake
#
# A game's key list (gcn_add_game's SHADER_KEYS) as a source that hands it to the renderer
# from a static initializer, so that every target linking the game carries it with no file
# to install beside it.
file(READ ${IN} _hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
string(REGEX REPLACE "((0x..,){32})" "\\1\n" _bytes "${_bytes}")
file(WRITE ${OUT}
"// Generated from ${IN} by embed_shader_keys.cmake.
#include \"gx/render_gl.h\"

alignas(4) static const unsigned char kShaderKeys[] = {
${_bytes}
};
[[maybe_unused]] static const bool kInstalled = (gx::render_set_shader_keys(kShaderKeys, sizeof(kShaderKeys)), true);
")
