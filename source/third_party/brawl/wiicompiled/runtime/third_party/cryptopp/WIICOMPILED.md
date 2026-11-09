# Crypto++ vendoring notes

- Upstream version: Crypto++ 8.9.0
- Upstream tag: `CRYPTOPP_8_9_0`
- Upstream commit: `843d74c7c97f9e19a615b8ff3c0ca06599ca501b`
- Source: <https://github.com/weidai11/cryptopp>

The top-level source distribution is vendored for disconnected builds. CMake
excludes upstream test programs, assembly, and algorithm-specific SIMD units.

WiiCompiled carries three small portability adaptations for its supported
Clang/MSVC-ABI build:

1. `config_os.h` has a project-scoped opt-in around Crypto++ 8.9.0's Clang/MSVC
   hard stop.
2. `config_int.h` can avoid Clang's 128-bit division helpers, which are absent
   from this ABI's runtime library.
3. `cpu.cpp` returns inert feature-probe results when `CRYPTOPP_DISABLE_ASM` is
   set, avoiding references to the deliberately omitted x64 assembly helpers.

This configuration supplies SHA-1, sect233r1 public key derivation, ECDSA
signing, signature verification, and key validation for the runtime's Wii ES
crypto implementation.
