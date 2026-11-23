#pragma once
// Sizes of Aurora's host/GPU caches, for memory reports (LM 0250: issue #34, memory growing over
// time). Counts are entries, bytes are the GPU (or host) bytes the entries hold, estimated from
// their sizes and formats. Safe to call from any thread.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AuroraMemStats {
  uint64_t textures, textureBytes;               // live Aurora textures (every TextureRef)
  uint64_t texObjects;                           // texture object cache (GXTexObj -> texture)
  uint64_t texContentEntries, texContentBytes;   // content-hash texture cache (budgeted)
  uint64_t tlutCaches;                           // TLUT object caches
  uint64_t copyTextures;                         // EFB copy destinations with a live copy texture
  uint64_t copyCacheEntries, copyCacheBytes;     // EFB copy textures by (dest, size, format, generation)
  uint64_t bindGroups, samplers;                 // bind group / sampler caches
  uint64_t pipelines, shaderModules;             // compiled render pipelines, shader modules
  uint64_t efbEntries;                           // EFB copies to RAM tracked
  uint64_t efbPoolBuffers, efbPoolBytes;         // EFB copy-to-RAM byte buffers by destination
  uint64_t efbDecoded, efbWritten, efbGuestCopies;
} AuroraMemStats;

void aurora_mem_stats(AuroraMemStats* out);

#ifdef __cplusplus
}
#endif
