// BC texture fallback adapted from this repository's Android renderer.
enum class BCnFallbackMode
{
    None,
    Decode,
    Transcode,
};

struct BCnFallback
{
    BCnFallbackMode mode = BCnFallbackMode::None;
    RenderFormat targetFormat{};
    uint32_t sourceBlockSize = 0;
    uint32_t bytesPerPixel = 0;   // Decode: decoded pixel size.
    uint32_t targetBlockSize = 0; // Transcode: ETC2/EAC bytes per 4x4 block.
    void (*decodeBlock)(const void* compressedBlock, void* decompressedBlock, int destinationPitch) = nullptr;
    void (*encodeBlockRow)(const uint32_t* src, uint64_t* dst, uint32_t blocks, size_t width) = nullptr;
};

// etcpak consumes pixels as b,g,r,a in memory while bcdec emits r,g,b,a: swap R and B
// while decoding into the intermediate buffer.
template<void (*TDecoder)(const void*, void*, int)>
static void DecodeBlockSwapRB(const void* src, void* dst, int pitch)
{
    uint8_t block[4][16];
    TDecoder(src, block, 16);

    for (size_t y = 0; y < 4; y++)
    {
        uint8_t* row = reinterpret_cast<uint8_t*>(dst) + y * pitch;
        for (size_t x = 0; x < 4; x++)
        {
            row[x * 4 + 0] = block[y][x * 4 + 2];
            row[x * 4 + 1] = block[y][x * 4 + 1];
            row[x * 4 + 2] = block[y][x * 4 + 0];
            row[x * 4 + 3] = block[y][x * 4 + 3];
        }
    }
}

static void DecodeBC4BlockForEac(const void* src, void* dst, int pitch)
{
    uint8_t block[4][4];
    bcdec_bc4(src, block, 4);

    for (size_t y = 0; y < 4; y++)
    {
        uint8_t* row = reinterpret_cast<uint8_t*>(dst) + y * pitch;
        for (size_t x = 0; x < 4; x++)
        {
            row[x * 4 + 0] = 0;
            row[x * 4 + 1] = 0;
            row[x * 4 + 2] = block[y][x]; // etcpak's EAC R channel lives in byte 2.
            row[x * 4 + 3] = 0xFF;
        }
    }
}

static void DecodeBC5BlockForEac(const void* src, void* dst, int pitch)
{
    uint8_t block[4][8];
    bcdec_bc5(src, block, 8);

    for (size_t y = 0; y < 4; y++)
    {
        uint8_t* row = reinterpret_cast<uint8_t*>(dst) + y * pitch;
        for (size_t x = 0; x < 4; x++)
        {
            row[x * 4 + 0] = 0;
            row[x * 4 + 1] = block[y][x * 2 + 1];
            row[x * 4 + 2] = block[y][x * 2 + 0];
            row[x * 4 + 3] = 0xFF;
        }
    }
}

// DXT1 stores punch-through alpha in blocks where color0 <= color1 and an index equals 3.
// Scanning the raw blocks decides between ETC2 RGB (opaque, same size as BC1) and ETC2
// RGBA (transparent, double size) without a full decode pass.
static bool BC1HasTransparency(const uint8_t* blocks, size_t blockCount)
{
    for (size_t i = 0; i < blockCount; i++, blocks += 8)
    {
        const uint16_t c0 = blocks[0] | (blocks[1] << 8);
        const uint16_t c1 = blocks[2] | (blocks[3] << 8);
        if (c0 > c1)
            continue;

        const uint32_t indices = blocks[4] | (blocks[5] << 8) | (blocks[6] << 16) | (uint32_t(blocks[7]) << 24);
        for (uint32_t j = 0; j < 16; j++)
        {
            if (((indices >> (j * 2)) & 0x3) == 0x3)
                return true;
        }
    }

    return false;
}

static bool GetBCnFallback(RenderFormat format, const uint8_t* data, size_t dataSize, uint32_t headerSize, BCnFallback& fallback)
{
    const bool etc2 = g_capabilities.textureCompressionETC2;

    switch (format)
    {
    case RenderFormat::BC4_SNORM:
    case RenderFormat::BC5_SNORM:
        LOG_WARNING("Converting SNORM BC texture as UNORM; values will be biased.");
        break;
    case RenderFormat::BC1_UNORM_SRGB:
    case RenderFormat::BC2_UNORM_SRGB:
    case RenderFormat::BC3_UNORM_SRGB:
    case RenderFormat::BC7_UNORM_SRGB:
        // The transcode targets have proper sRGB variants; only the plain decode loses the
        // automatic sRGB-to-linear conversion (RenderFormat has no R8G8B8A8_UNORM_SRGB).
        if (!etc2)
        {
            static bool s_warnedSrgb;
            if (!s_warnedSrgb)
            {
                s_warnedSrgb = true;
                LOG_WARNING("Decoding sRGB BC texture to UNORM: sampling will return sRGB-encoded values.");
            }
        }
        break;
    default:
        break;
    }

    switch (format)
    {
    case RenderFormat::BC1_TYPELESS:
    case RenderFormat::BC1_UNORM:
    case RenderFormat::BC1_UNORM_SRGB:
    {
        const bool srgb = format == RenderFormat::BC1_UNORM_SRGB;
        fallback.sourceBlockSize = BCDEC_BC1_BLOCK_SIZE;
        if (etc2)
        {
            fallback.mode = BCnFallbackMode::Transcode;
            fallback.decodeBlock = &DecodeBlockSwapRB<&bcdec_bc1>;
            if (BC1HasTransparency(data + headerSize, (dataSize - headerSize) / BCDEC_BC1_BLOCK_SIZE))
            {
                fallback.targetFormat = srgb ? RenderFormat::ETC2_RGBA8_UNORM_SRGB : RenderFormat::ETC2_RGBA8_UNORM;
                fallback.targetBlockSize = 16;
                fallback.encodeBlockRow = [](const uint32_t* src, uint64_t* dst, uint32_t blocks, size_t width) { CompressEtc2Rgba(src, dst, blocks, width, true); };
            }
            else
            {
                fallback.targetFormat = srgb ? RenderFormat::ETC2_RGB8_UNORM_SRGB : RenderFormat::ETC2_RGB8_UNORM;
                fallback.targetBlockSize = 8;
                fallback.encodeBlockRow = [](const uint32_t* src, uint64_t* dst, uint32_t blocks, size_t width) { CompressEtc2Rgb(src, dst, blocks, width, true); };
            }
        }
        else
        {
            fallback.mode = BCnFallbackMode::Decode;
            fallback.targetFormat = RenderFormat::R8G8B8A8_UNORM;
            fallback.bytesPerPixel = 4;
            fallback.decodeBlock = &bcdec_bc1;
        }
        return true;
    }
    case RenderFormat::BC2_TYPELESS:
    case RenderFormat::BC2_UNORM:
    case RenderFormat::BC2_UNORM_SRGB:
    case RenderFormat::BC3_TYPELESS:
    case RenderFormat::BC3_UNORM:
    case RenderFormat::BC3_UNORM_SRGB:
    case RenderFormat::BC7_TYPELESS:
    case RenderFormat::BC7_UNORM:
    case RenderFormat::BC7_UNORM_SRGB:
    {
        const bool srgb = format == RenderFormat::BC2_UNORM_SRGB || format == RenderFormat::BC3_UNORM_SRGB || format == RenderFormat::BC7_UNORM_SRGB;
        const bool bc2 = format == RenderFormat::BC2_TYPELESS || format == RenderFormat::BC2_UNORM || format == RenderFormat::BC2_UNORM_SRGB;
        const bool bc3 = format == RenderFormat::BC3_TYPELESS || format == RenderFormat::BC3_UNORM || format == RenderFormat::BC3_UNORM_SRGB;
        fallback.sourceBlockSize = BCDEC_BC3_BLOCK_SIZE;
        if (etc2)
        {
            fallback.mode = BCnFallbackMode::Transcode;
            fallback.targetFormat = srgb ? RenderFormat::ETC2_RGBA8_UNORM_SRGB : RenderFormat::ETC2_RGBA8_UNORM;
            fallback.targetBlockSize = 16;
            fallback.decodeBlock = bc2 ? &DecodeBlockSwapRB<&bcdec_bc2> : (bc3 ? &DecodeBlockSwapRB<&bcdec_bc3> : &DecodeBlockSwapRB<&bcdec_bc7>);
            fallback.encodeBlockRow = [](const uint32_t* src, uint64_t* dst, uint32_t blocks, size_t width) { CompressEtc2Rgba(src, dst, blocks, width, true); };
        }
        else
        {
            fallback.mode = BCnFallbackMode::Decode;
            fallback.targetFormat = RenderFormat::R8G8B8A8_UNORM;
            fallback.bytesPerPixel = 4;
            fallback.decodeBlock = bc2 ? &bcdec_bc2 : (bc3 ? &bcdec_bc3 : &bcdec_bc7);
        }
        return true;
    }
    case RenderFormat::BC4_TYPELESS:
    case RenderFormat::BC4_UNORM:
    case RenderFormat::BC4_SNORM:
        fallback.sourceBlockSize = BCDEC_BC4_BLOCK_SIZE;
        if (etc2)
        {
            fallback.mode = BCnFallbackMode::Transcode;
            fallback.targetFormat = RenderFormat::EAC_R11_UNORM;
            fallback.targetBlockSize = 8;
            fallback.decodeBlock = &DecodeBC4BlockForEac;
            fallback.encodeBlockRow = &CompressEacR;
        }
        else
        {
            fallback.mode = BCnFallbackMode::Decode;
            fallback.targetFormat = RenderFormat::R8_UNORM;
            fallback.bytesPerPixel = 1;
            fallback.decodeBlock = &bcdec_bc4;
        }
        return true;
    case RenderFormat::BC5_TYPELESS:
    case RenderFormat::BC5_UNORM:
    case RenderFormat::BC5_SNORM:
        fallback.sourceBlockSize = BCDEC_BC5_BLOCK_SIZE;
        if (etc2)
        {
            fallback.mode = BCnFallbackMode::Transcode;
            fallback.targetFormat = RenderFormat::EAC_R11G11_UNORM;
            fallback.targetBlockSize = 16;
            fallback.decodeBlock = &DecodeBC5BlockForEac;
            fallback.encodeBlockRow = &CompressEacRg;
        }
        else
        {
            fallback.mode = BCnFallbackMode::Decode;
            fallback.targetFormat = RenderFormat::R8G8_UNORM;
            fallback.bytesPerPixel = 2;
            fallback.decodeBlock = &bcdec_bc5;
        }
        return true;
    default:
        // BC6H stays unsupported: bcdec only emits 3-channel float output for it and no
        // known game or mod content uses it.
        return false;
    }
}

static bool LoadTexture(GuestTexture& texture, const uint8_t* data, size_t dataSize, RenderComponentMapping componentMapping, bool forceCubeMap = false, std::string_view sourceName = {})
{
    ddspp::Descriptor ddsDesc;
    if (DecodeDdsHeader(data, dataSize, ddsDesc))
    {
        DdsUploadLayout validatedLayout;
        const uint32_t maxSize = g_capabilities.maxTextureSize;
        if (ddsDesc.width > maxSize || ddsDesc.height > maxSize || ddsDesc.depth > maxSize ||
            !BuildDdsUploadLayout(ddsDesc, dataSize, PITCH_ALIGNMENT, PLACEMENT_ALIGNMENT, validatedLayout))
        {
            LOGF_WARNING("Invalid or unsupported DDS '{}' ({} bytes).", sourceName, dataSize);
            return false;
        }
        forceCubeMap &= (ddsDesc.type == ddspp::Texture2D) && (ddsDesc.arraySize == 1);
        uint32_t arraySize = ddsDesc.type == ddspp::TextureType::Cubemap ? (ddsDesc.arraySize * 6) : ddsDesc.arraySize;

        // Keep every mip in the first iOS compatibility build.
        const uint32_t mipSkip = 0;
        const uint32_t textureMips = ddsDesc.numMips - mipSkip;

        RenderTextureDesc desc;
        desc.dimension = ConvertTextureDimension(ddsDesc.type);
        desc.width = std::max(1u, ddsDesc.width >> mipSkip);
        desc.height = std::max(1u, ddsDesc.height >> mipSkip);
        desc.depth = std::max(1u, ddsDesc.type == ddspp::Texture3D ? ddsDesc.depth >> mipSkip : ddsDesc.depth);
        desc.mipLevels = textureMips;
        desc.arraySize = arraySize;
        desc.format = ConvertDXGIFormat(ddsDesc.format);
        desc.flags = ddsDesc.type == ddspp::TextureType::Cubemap ? RenderTextureFlag::CUBE : RenderTextureFlag::NONE;

        BCnFallback bcFallback{};
        if (!g_capabilities.textureCompressionBC && ddsDesc.blockWidth > 1)
        {
            if (!GetBCnFallback(desc.format, data, dataSize, ddsDesc.headerSize, bcFallback))
            {
                LOGF_ERROR("Cannot load a BC-compressed texture (RenderFormat {}) on a device without BC texture support.", uint32_t(desc.format));
                return false;
            }

            desc.format = bcFallback.targetFormat;
        }

        if (forceCubeMap)
        {
            desc.arraySize = 6;
            desc.flags = RenderTextureFlag::CUBE;
        }

        texture.textureHolder = g_device->createTexture(desc);
        texture.texture = texture.textureHolder.get();
        texture.layout = RenderTextureLayout::COPY_DEST;

        RenderTextureViewDesc viewDesc;
        viewDesc.format = desc.format;
        viewDesc.dimension = ConvertTextureViewDimension(ddsDesc.type);
        viewDesc.mipLevels = textureMips;
        viewDesc.componentMapping = componentMapping;

        if (forceCubeMap)
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_CUBE;

        texture.textureView = texture.texture->createTextureView(viewDesc);
        texture.descriptorIndex = g_textureDescriptorAllocator.allocate();
        g_textureDescriptorSet->setTexture(texture.descriptorIndex, texture.texture, RenderTextureLayout::SHADER_READ, texture.textureView.get());

        texture.width = desc.width;
        texture.height = desc.height;
        texture.viewDimension = viewDesc.dimension;

        struct Slice
        {
            uint32_t width;
            uint32_t height;
            uint32_t depth;
            size_t srcOffset;
            size_t dstOffset;
            uint32_t srcRowPitch;
            uint32_t dstRowPitch;
            uint32_t rowCount;
            uint32_t dstRowCount;
        };

        std::vector<Slice> slices;
        size_t curSrcOffset = 0;
        size_t curDstOffset = 0;

        for (uint32_t arraySlice = 0; arraySlice < arraySize; arraySlice++)
        {
            for (uint32_t mipSlice = 0; mipSlice < ddsDesc.numMips; mipSlice++)
            {
                Slice slice;

                slice.width = std::max(1u, ddsDesc.width >> mipSlice);
                slice.height = std::max(1u, ddsDesc.height >> mipSlice);
                slice.depth = std::max(1u, ddsDesc.depth >> mipSlice);
                slice.srcOffset = curSrcOffset;
                slice.dstOffset = curDstOffset;
                uint32_t rowPitch = ((slice.width + ddsDesc.blockWidth - 1) / ddsDesc.blockWidth) * ddsDesc.bitsPerPixelOrBlock;
                slice.srcRowPitch = (rowPitch + 7) / 8;
                slice.rowCount = (slice.height + ddsDesc.blockHeight - 1) / ddsDesc.blockHeight;

                if (bcFallback.mode == BCnFallbackMode::Decode)
                {
                    // The upload buffer holds decoded pixels instead of the source BC blocks.
                    slice.dstRowPitch = (slice.width * bcFallback.bytesPerPixel + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
                    slice.dstRowCount = slice.height;
                }
                else if (bcFallback.mode == BCnFallbackMode::Transcode)
                {
                    // The upload buffer holds ETC2/EAC blocks with the same 4x4 geometry.
                    slice.dstRowPitch = (((slice.width + 3) / 4) * bcFallback.targetBlockSize + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
                    slice.dstRowCount = slice.rowCount;
                }
                else
                {
                    slice.dstRowPitch = (slice.srcRowPitch + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
                    slice.dstRowCount = slice.rowCount;
                }

                // Source data always advances over every mip in the file; skipped
                // top mips are simply never uploaded.
                curSrcOffset += size_t(slice.srcRowPitch) * slice.rowCount * slice.depth;
                if (mipSlice < mipSkip)
                    continue;

                curDstOffset += (size_t(slice.dstRowPitch) * slice.dstRowCount * slice.depth + PLACEMENT_ALIGNMENT - 1) & ~(PLACEMENT_ALIGNMENT - 1);
                slices.push_back(slice);
            }
        }

        auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(curDstOffset));
        uint8_t* mappedMemory = reinterpret_cast<uint8_t*>(uploadBuffer->map());

        std::vector<uint8_t> decodeTemp;
        for (auto& slice : slices)
        {
            const uint8_t* srcData = data + ddsDesc.headerSize + slice.srcOffset;
            uint8_t* dstData = mappedMemory + slice.dstOffset;

            if (bcFallback.mode != BCnFallbackMode::None)
            {
                // Decode into a buffer rounded up to whole 4x4 blocks first: edge blocks
                // always write a full 4x4 pixel area, which could otherwise run past this
                // slice's region in the upload buffer. Transcode always decodes to 4-byte
                // pixels since the encoders consume 32-bit pixels.
                const uint32_t blocksX = (slice.width + 3) / 4;
                const uint32_t blocksY = slice.rowCount;
                const uint32_t pixelSize = bcFallback.mode == BCnFallbackMode::Decode ? bcFallback.bytesPerPixel : 4;
                const uint32_t tmpPitch = blocksX * 4 * pixelSize;
                decodeTemp.resize(size_t(tmpPitch) * blocksY * 4);

                for (uint32_t z = 0; z < slice.depth; z++)
                {
                    const uint8_t* srcSlice = srcData + size_t(z) * slice.srcRowPitch * blocksY;

                    for (uint32_t blockY = 0; blockY < blocksY; blockY++)
                    {
                        for (uint32_t blockX = 0; blockX < blocksX; blockX++)
                        {
                            bcFallback.decodeBlock(
                                srcSlice + size_t(blockY) * slice.srcRowPitch + size_t(blockX) * bcFallback.sourceBlockSize,
                                decodeTemp.data() + size_t(blockY) * 4 * tmpPitch + size_t(blockX) * 4 * pixelSize,
                                int(tmpPitch));
                        }
                    }

                    if (bcFallback.mode == BCnFallbackMode::Decode)
                    {
                        uint8_t* dstSlice = dstData + size_t(z) * slice.dstRowPitch * slice.height;
                        for (uint32_t row = 0; row < slice.height; row++)
                            memcpy(dstSlice + size_t(row) * slice.dstRowPitch, decodeTemp.data() + size_t(row) * tmpPitch, size_t(slice.width) * pixelSize);
                    }
                    else
                    {
                        uint8_t* dstSlice = dstData + size_t(z) * slice.dstRowPitch * blocksY;
                        for (uint32_t blockY = 0; blockY < blocksY; blockY++)
                        {
                            bcFallback.encodeBlockRow(
                                reinterpret_cast<const uint32_t*>(decodeTemp.data() + size_t(blockY) * 4 * tmpPitch),
                                reinterpret_cast<uint64_t*>(dstSlice + size_t(blockY) * slice.dstRowPitch),
                                blocksX, size_t(blocksX) * 4);
                        }
                    }
                }
            }
            else if (slice.srcRowPitch == slice.dstRowPitch)
            {
                memcpy(dstData, srcData, size_t(slice.srcRowPitch) * slice.rowCount * slice.depth);
            }
            else
            {
                for (size_t i = 0; i < slice.rowCount * slice.depth; i++)
                {
                    memcpy(dstData, srcData, slice.srcRowPitch);
                    srcData += slice.srcRowPitch;
                    dstData += slice.dstRowPitch;
                }
            }
        }

        uploadBuffer->unmap();

        ExecuteCopyCommandList([&]
            {
                g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.texture, RenderTextureLayout::COPY_DEST));

                auto copyTextureRegion = [&](Slice& slice, uint32_t subresourceIndex)
                    {
                        uint32_t footprintRowWidth;
                        if (bcFallback.mode == BCnFallbackMode::Decode)
                            footprintRowWidth = slice.dstRowPitch / bcFallback.bytesPerPixel;
                        else if (bcFallback.mode == BCnFallbackMode::Transcode)
                            footprintRowWidth = slice.dstRowPitch / bcFallback.targetBlockSize * 4;
                        else
                            footprintRowWidth = (slice.dstRowPitch * 8) / ddsDesc.bitsPerPixelOrBlock * ddsDesc.blockWidth;

                        g_copyCommandList->copyTextureRegion(
                            RenderTextureCopyLocation::Subresource(texture.texture, subresourceIndex % textureMips, subresourceIndex / textureMips),
                            RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), desc.format, slice.width, slice.height, slice.depth, footprintRowWidth, slice.dstOffset));
                    };

                for (size_t i = 0; i < slices.size(); i++)
                    copyTextureRegion(slices[i], i);

                // Duplicate the first face across the remaining 6 faces.
                if (forceCubeMap)
                {
                    for (size_t i = 1; i < 6; i++)
                    {
                        for (size_t j = 0; j < slices.size(); j++)
                            copyTextureRegion(slices[j], (slices.size() * i) + j);
                    }
                }
            });

        return true;
    }
    else
    {
        int width, height;
        void* stbImage = stbi_load_from_memory(data, dataSize, &width, &height, nullptr, 4);

        if (stbImage != nullptr)
        {
            texture.textureHolder = g_device->createTexture(RenderTextureDesc::Texture2D(width, height, 1, RenderFormat::R8G8B8A8_UNORM));
            texture.texture = texture.textureHolder.get();
            texture.viewDimension = RenderTextureViewDimension::TEXTURE_2D;
            texture.layout = RenderTextureLayout::COPY_DEST;

            texture.descriptorIndex = g_textureDescriptorAllocator.allocate();
            g_textureDescriptorSet->setTexture(texture.descriptorIndex, texture.texture, RenderTextureLayout::SHADER_READ);

            uint32_t rowPitch = (width * 4 + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
            uint32_t slicePitch = rowPitch * height;

            auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(slicePitch));
            uint8_t* mappedMemory = reinterpret_cast<uint8_t*>(uploadBuffer->map());

            if (rowPitch == (width * 4))
            {
                memcpy(mappedMemory, stbImage, slicePitch);
            }
            else
            {
                auto data = reinterpret_cast<const uint8_t*>(stbImage);

                for (size_t i = 0; i < height; i++)
                {
                    memcpy(mappedMemory, data, width * 4);
                    data += width * 4;
                    mappedMemory += rowPitch;
                }
            }

            uploadBuffer->unmap();

            stbi_image_free(stbImage);

            ExecuteCopyCommandList([&]
                {
                    g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.texture, RenderTextureLayout::COPY_DEST));

                    g_copyCommandList->copyTextureRegion(
                        RenderTextureCopyLocation::Subresource(texture.texture, 0),
                        RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), RenderFormat::R8G8B8A8_UNORM, width, height, 1, rowPitch / 4, 0));
                });

            return true;
        }
    }

    return false;
}

