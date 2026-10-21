# wiids_disc: third-party notices

wiids_disc (GameCube/Wii disc image reading and extraction for WiiDS) is part of WiiDS, GPL-3.0.

- **Dolphin Emulator** (https://dolphin-emu.org), Copyright Dolphin Emulator Project.
  The WIA/RVZ reader follows Dolphin's format documentation (docs/WiaAndRvz.md) and its reader
  (Source/Core/DiscIO/WIABlob.cpp, WIACompression.cpp): GPL-2.0-or-later. The padding generator
  follows DiscIO/LaggedFibonacciGenerator.cpp: CC0-1.0. The NSMB runtimes answer the disc's BCA
  request the way Dolphin's Core/HW/DVD/DVDInterface.cpp does: GPL-2.0-or-later.
- **XZ Utils / liblzma** (Lasse Collin and contributors), Dolphin's vendored copy: public domain
  (see third_party/liblzma/COPYING).
- **bzip2 / libbzip2** 1.0.8 (Julian Seward): BSD-style licence, third_party/bzip2/LICENSE.
- **Zstandard** (Meta Platforms, Inc. and affiliates), decompression only: BSD licence,
  third_party/zstd/LICENSE.
- **zlib**: provided by the Android platform (zlib licence).
