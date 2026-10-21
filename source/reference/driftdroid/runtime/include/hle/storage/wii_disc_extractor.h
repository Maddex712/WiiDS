#pragma once

#include <cstdint>

// Extracts a Wii disc image into the "sys"/"files" DATA folder layout hle/storage/dvd.cpp expects
// as its DVD root. Supports a plain, uncompressed disc image (ISO/GCM) and a single-file WBFS
// container; RVZ/WIA/CISO/GCZ are out of scope (they need their own decompressors).
//
// Format details (partition table, ticket/title-key decryption, per-cluster data decryption, the
// boot.bin/FST layout) are the long-public Wii disc format, the same one every other disc tool
// (Dolphin included) implements independently from the same public documentation - see
// wiibrew.org's "Wii Disc" and "Wii Ticket" pages. The Wii common key used to decrypt the
// per-title key is likewise long-public (leaked in 2008) and is embedded the same way Dolphin's
// own public source does.

#ifdef __cplusplus
extern "C" {
#endif

// `sourceFd` must be a POSIX file descriptor already open for reading and seeking (Android's SAF
// hands back a content:// Uri, not a path - the caller opens it via
// ContentResolver.openFileDescriptor(uri, "r") and passes the raw fd here). This function does
// not take ownership of the fd and does not close it. `destDataFolder` must already exist as a
// writable directory; this function creates "sys" and "files" underneath it.
//
// Returns 0 on success. On failure, returns a negative error code and writes a human-readable
// message into `errorOut` (truncated to fit, always nul-terminated when errorOutCapacity > 0).
int WiiDiscExtractor_Extract(int sourceFd, const char* destDataFolder, char* errorOut,
                              int errorOutCapacity);

// Same, for an image split into consecutive parts (FAT32's 4 GiB limit: "game.wbfs" + "game.wbf1"
// ...), read as one file in the order given.
int WiiDiscExtractor_ExtractParts(const int* partFds, int partCount, const char* destDataFolder,
                                  char* errorOut, int errorOutCapacity);

// Progress, safe to poll from another thread while WiiDiscExtractor_Extract runs on its own.
// Both read as 0 before extraction starts. bytesTotal becomes nonzero once the source container
// and partition have been parsed enough to know the real amount of file data to extract.
uint64_t WiiDiscExtractor_BytesDone();
uint64_t WiiDiscExtractor_BytesTotal();

#ifdef __cplusplus
}
#endif
