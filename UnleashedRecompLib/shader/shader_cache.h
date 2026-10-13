#pragma once

struct ShaderCacheEntry
{
    const uint64_t hash;
    const uint32_t dxilOffset;
    const uint32_t dxilSize;
    const uint32_t spirvOffset;
    const uint32_t spirvSize;
#ifdef UNLEASHED_RECOMP_XENOS_METAL
    // Only the iOS XenosRecomp writes these.
    const uint32_t airOffset;
    const uint32_t airSize;
#endif
    const uint32_t specConstantsMask;
#ifdef UNLEASHED_RECOMP_XENOS_METAL
    char filename[256];
#endif
    struct GuestShader* guestShader;
};

extern ShaderCacheEntry g_shaderCacheEntries[];
extern const size_t g_shaderCacheEntryCount;

extern const uint8_t g_compressedDxilCache[];
extern const size_t g_dxilCacheCompressedSize;
extern const size_t g_dxilCacheDecompressedSize;

#ifdef UNLEASHED_RECOMP_XENOS_METAL
extern const uint8_t g_compressedAirCache[];
extern const size_t g_airCacheCompressedSize;
extern const size_t g_airCacheDecompressedSize;
#endif

extern const uint8_t g_compressedSpirvCache[];
extern const size_t g_spirvCacheCompressedSize;
extern const size_t g_spirvCacheDecompressedSize;
