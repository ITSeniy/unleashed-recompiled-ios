#include "video.h"
#define BCDEC_IMPLEMENTATION
#include <bcdec.h>
#include <ProcessRGB.hpp>
#include "dds_header.h"
#include "dds_upload.h"
#include "os/macos/ios_diagnostics.h"
#if defined(UNLEASHED_RECOMP_IOS)
#include <ui/ios_event_loop.h>
#endif

#include <climits>

#include "imgui/imgui_common.h"
#include "imgui/imgui_snapshot.h"
#include "imgui/imgui_font_builder.h"

#include <app.h>
#include <bc_diff.h>
#include <cpu/guest_thread.h>
#include <decompressor.h>
#include <kernel/function.h>
#include <kernel/heap.h>
#include <hid/hid.h>
#include <kernel/memory.h>
#include <kernel/xdbf.h>
#include <res/bc_diff/button_bc_diff.bin.h>
#include <res/font/im_font_atlas.dds.h>
#include <shader/shader_cache.h>
#include <SWA.h>

// Entry point of the guest shaders in the cache. The vendored XenosRecomp still
// emits "main"; the iOS one (Metal support) emits "shaderMain".
#ifdef UNLEASHED_RECOMP_XENOS_METAL
#define GUEST_SHADER_ENTRY "shaderMain"
#else
#define GUEST_SHADER_ENTRY "main"
#endif
#include <ui/achievement_menu.h>
#include <ui/achievement_overlay.h>
#include <ui/button_guide.h>
#include <ui/fader.h>
#include <ui/imgui_utils.h>
#if !defined(__ANDROID__)
#include <ui/installer_wizard.h>
#endif
#include <ui/message_window.h>
#include <ui/options_menu.h>
#include <ui/game_window.h>
#include <ui/black_bar.h>
#include <ui/touch_controls.h>
#include <patches/aspect_ratio_patches.h>
#include <user/config.h>
#include <user/paths.h>
#include <sdl_listener.h>
#include <xxHashMap.h>

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#endif
#include <os/logger.h>
#include <os/app_lifecycle.h>
#include <os/process.h>
#include <os/logger.h>
#if defined(__ANDROID__)
#include <os/android/storage_android.h>
#include <os/android/vulkan_driver_android.h>
#include <android/native_window.h>
#include <dlfcn.h>
#endif
#ifdef UNLEASHED_RECOMP_METAL
#include <plume_metal_diagnostics.h>
#if defined(UNLEASHED_RECOMP_IOS)
#include <plume_metal_upscaling.h>
#endif
#endif

#include <cstdlib>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/mach.h>
#endif

#if defined(ASYNC_PSO_DEBUG) || defined(PSO_CACHING)
#include <magic_enum/magic_enum.hpp>
#endif

#define UNLEASHED_RECOMP
#include "../../tools/XenosRecomp/XenosRecomp/shader_common.h"

#ifdef UNLEASHED_RECOMP_D3D12
#include "shader/hlsl/blend_color_alpha_ps.hlsl.dxil.h"
#include "shader/hlsl/copy_vs.hlsl.dxil.h"
#include "shader/hlsl/copy_color_ps.hlsl.dxil.h"
#include "shader/hlsl/copy_depth_ps.hlsl.dxil.h"
#include "shader/hlsl/csd_filter_ps.hlsl.dxil.h"
#include "shader/hlsl/csd_no_tex_vs.hlsl.dxil.h"
#include "shader/hlsl/csd_vs.hlsl.dxil.h"
#include "shader/hlsl/enhanced_motion_blur_ps.hlsl.dxil.h"
#include "shader/hlsl/gamma_correction_ps.hlsl.dxil.h"
#include "shader/hlsl/gaussian_blur_3x3.hlsl.dxil.h"
#include "shader/hlsl/gaussian_blur_5x5.hlsl.dxil.h"
#include "shader/hlsl/gaussian_blur_7x7.hlsl.dxil.h"
#include "shader/hlsl/gaussian_blur_9x9.hlsl.dxil.h"
#include "shader/hlsl/imgui_ps.hlsl.dxil.h"
#include "shader/hlsl/imgui_vs.hlsl.dxil.h"
#include "shader/hlsl/movie_ps.hlsl.dxil.h"
#include "shader/hlsl/movie_vs.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_color_2x.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_color_4x.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_color_8x.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_depth_2x.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_depth_4x.hlsl.dxil.h"
#include "shader/hlsl/resolve_msaa_depth_8x.hlsl.dxil.h"
#endif

#ifdef UNLEASHED_RECOMP_METAL
#include <gpu/shader/msl/blend_color_alpha_ps.metal.metallib.h>
#include <gpu/shader/msl/copy_vs.metal.metallib.h>
#include <gpu/shader/msl/copy_color_ps.metal.metallib.h>
#include <gpu/shader/msl/copy_depth_ps.metal.metallib.h>
#include <gpu/shader/msl/csd_filter_ps.metal.metallib.h>
#include <gpu/shader/msl/csd_no_tex_vs.metal.metallib.h>
#include <gpu/shader/msl/csd_vs.metal.metallib.h>
#include <gpu/shader/msl/enhanced_motion_blur_ps.metal.metallib.h>
#include <gpu/shader/msl/gamma_correction_ps.metal.metallib.h>
#include <gpu/shader/msl/gaussian_blur_3x3.metal.metallib.h>
#include <gpu/shader/msl/gaussian_blur_5x5.metal.metallib.h>
#include <gpu/shader/msl/gaussian_blur_7x7.metal.metallib.h>
#include <gpu/shader/msl/gaussian_blur_9x9.metal.metallib.h>
#include <gpu/shader/msl/imgui_ps.metal.metallib.h>
#include <gpu/shader/msl/imgui_vs.metal.metallib.h>
#include <gpu/shader/msl/movie_ps.metal.metallib.h>
#include <gpu/shader/msl/movie_vs.metal.metallib.h>
#include <gpu/shader/msl/resolve_msaa_color_2x.metal.metallib.h>
#include <gpu/shader/msl/resolve_msaa_color_4x.metal.metallib.h>
#include <gpu/shader/msl/resolve_msaa_color_8x.metal.metallib.h>
#include <gpu/shader/msl/resolve_msaa_depth_2x.metal.metallib.h>
#include <gpu/shader/msl/resolve_msaa_depth_4x.metal.metallib.h>
#include <gpu/shader/msl/resolve_msaa_depth_8x.metal.metallib.h>
#endif

#include "shader/hlsl/blend_color_alpha_ps.hlsl.spirv.h"
#include "shader/hlsl/copy_vs.hlsl.spirv.h"
#include "shader/hlsl/copy_color_ps.hlsl.spirv.h"
#include "shader/hlsl/copy_depth_ps.hlsl.spirv.h"
#include "shader/hlsl/csd_filter_ps.hlsl.spirv.h"
#include "shader/hlsl/csd_no_tex_vs.hlsl.spirv.h"
#include "shader/hlsl/csd_vs.hlsl.spirv.h"
#include "shader/hlsl/enhanced_motion_blur_ps.hlsl.spirv.h"
#include "shader/hlsl/gamma_correction_ps.hlsl.spirv.h"
#include "shader/hlsl/gaussian_blur_3x3.hlsl.spirv.h"
#include "shader/hlsl/gaussian_blur_5x5.hlsl.spirv.h"
#include "shader/hlsl/gaussian_blur_7x7.hlsl.spirv.h"
#include "shader/hlsl/gaussian_blur_9x9.hlsl.spirv.h"
#include "shader/hlsl/imgui_ps.hlsl.spirv.h"
#include "shader/hlsl/imgui_vs.hlsl.spirv.h"
#include "shader/hlsl/movie_ps.hlsl.spirv.h"
#include "shader/hlsl/movie_vs.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_color_2x.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_color_4x.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_color_8x.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_depth_2x.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_depth_4x.hlsl.spirv.h"
#include "shader/hlsl/resolve_msaa_depth_8x.hlsl.spirv.h"

#ifdef _WIN32
extern "C"
{
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace plume
{
#ifdef UNLEASHED_RECOMP_D3D12
    extern std::unique_ptr<RenderInterface> CreateD3D12Interface();
#endif
#ifdef UNLEASHED_RECOMP_METAL
    extern std::unique_ptr<RenderInterface> CreateMetalInterface();
#endif
#ifdef SDL_VULKAN_ENABLED
    extern std::unique_ptr<RenderInterface> CreateVulkanInterface(RenderWindow sdlWindow);
#else
    extern std::unique_ptr<RenderInterface> CreateVulkanInterface();
#endif

    static std::unique_ptr<RenderInterface> CreateVulkanInterfaceWrapper() {
#ifdef SDL_VULKAN_ENABLED
        return CreateVulkanInterface(GameWindow::s_renderWindow);
#else
        return CreateVulkanInterface();
#endif
    }
}

#pragma pack(push, 1)
struct PipelineState
{
    GuestShader* vertexShader = nullptr;
    GuestShader* pixelShader = nullptr;
    GuestVertexDeclaration* vertexDeclaration = nullptr;
    bool instancing = false;
    bool zEnable = true;
    bool zWriteEnable = true;
    RenderBlend srcBlend = RenderBlend::ONE;
    RenderBlend destBlend = RenderBlend::ZERO;
    RenderCullMode cullMode = RenderCullMode::NONE;
    RenderComparisonFunction zFunc = RenderComparisonFunction::LESS;
    bool alphaBlendEnable = false;
    RenderBlendOperation blendOp = RenderBlendOperation::ADD;
    float slopeScaledDepthBias = 0.0f;
    int32_t depthBias = 0;
    RenderBlend srcBlendAlpha = RenderBlend::ONE;
    RenderBlend destBlendAlpha = RenderBlend::ZERO;
    RenderBlendOperation blendOpAlpha = RenderBlendOperation::ADD;
    uint32_t colorWriteEnable = uint32_t(RenderColorWriteEnable::ALL);
    RenderPrimitiveTopology primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
    uint8_t vertexStrides[16]{};
    RenderFormat renderTargetFormat{};
    RenderFormat depthStencilFormat{};
    RenderSampleCounts sampleCount = RenderSampleCount::COUNT_1;
    bool enableAlphaToCoverage = false;
    uint32_t specConstants = 0;
};
#pragma pack(pop)

struct UploadAllocation
{
    const RenderBuffer* buffer;
    uint64_t offset;
    uint8_t* memory;
    uint64_t deviceAddress;
};

struct SharedConstants
{
    uint32_t texture2DIndices[16]{};
    uint32_t texture3DIndices[16]{};
    uint32_t textureCubeIndices[16]{};
    uint32_t samplerIndices[16]{};
    uint32_t booleans{};
    uint32_t swappedTexcoords{};
    float halfPixelOffsetX{};
    float halfPixelOffsetY{};
    float alphaThreshold{};
};

static_assert(offsetof(SharedConstants, texture2DIndices) == 0);
static_assert(offsetof(SharedConstants, samplerIndices) == 192);
static_assert(offsetof(SharedConstants, booleans) == 256);
static_assert(offsetof(SharedConstants, alphaThreshold) == 272);

// Depth bias values here are only used when the render device has 
// dynamic depth bias capability enabled. Otherwise, they get unused
// and the values get assigned in the pipeline state instead.

static GuestSurface* g_renderTarget;
static GuestSurface* g_depthStencil;
static RenderFramebuffer* g_framebuffer;
static RenderViewport g_viewport(0.0f, 0.0f, 1280.0f, 720.0f);
static PipelineState g_pipelineState;
static int32_t g_depthBias;
static float g_slopeScaledDepthBias;
static uint32_t g_vertexShaderConstants[0x400];
static uint32_t g_pixelShaderConstants[0x380];
static SharedConstants g_sharedConstants;
static GuestTexture* g_textures[16];
static RenderSamplerDesc g_samplerDescs[16];
static bool g_scissorTestEnable = false;
static RenderRect g_scissorRect;
static RenderVertexBufferView g_vertexBufferViews[16];
static RenderInputSlot g_inputSlots[16];
static RenderIndexBufferView g_indexBufferView({}, 0, RenderFormat::R16_UINT);

struct DirtyStates
{
    bool renderTargetAndDepthStencil;
    bool viewport;
    bool pipelineState;
    bool depthBias;
    bool sharedConstants;
    bool scissorRect;
    bool vertexShaderConstants;
    uint8_t vertexStreamFirst;
    uint8_t vertexStreamLast;
    bool indices;
    bool pixelShaderConstants;

    DirtyStates(bool value)
        : renderTargetAndDepthStencil(value)
        , viewport(value)
        , pipelineState(value)
        , depthBias(value)
        , sharedConstants(value)
        , scissorRect(value)
        , vertexShaderConstants(value)
        , vertexStreamFirst(value ? 0 : 255)
        , vertexStreamLast(value ? 15 : 0)
        , indices(value)
        , pixelShaderConstants(value)
    {
    }
};

static DirtyStates g_dirtyStates(true);

template<typename T>
static void SetDirtyValue(bool& dirtyState, T& dest, const T& src)
{
    if (dest != src)
    {
        dest = src;
        dirtyState = true;
    }
}

static constexpr size_t PROFILER_VALUE_COUNT = 256;
static size_t g_profilerValueIndex;

struct Profiler
{
    std::atomic<double> value;
    double values[PROFILER_VALUE_COUNT];
    std::chrono::steady_clock::time_point start;

    void Begin()
    {
        start = std::chrono::steady_clock::now();
    }

    void End()
    {
        value = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    void Set(double v)
    {
        value = v;
    }

    void Reset()
    {
        End();
        Begin();
    }

    double UpdateAndReturnAverage()
    {
        values[g_profilerValueIndex] = value;
        return std::accumulate(values, values + PROFILER_VALUE_COUNT, 0.0) / PROFILER_VALUE_COUNT;
    }
};

static double g_applicationValues[PROFILER_VALUE_COUNT];
static Profiler g_gpuFrameProfiler;
static Profiler g_presentProfiler;
static Profiler g_updateDirectorProfiler;
static Profiler g_renderDirectorProfiler;
static Profiler g_frameFenceProfiler;
static Profiler g_presentWaitProfiler;
static Profiler g_swapChainAcquireProfiler;

// Command stream counters defined in plume_vulkan.cpp (always compiled in);
// zero when the D3D12 backend is active. Snapshotted at end of frame recording.
// The iOS plume pin has no counters, so they are defined here and stay zero.
#if defined(UNLEASHED_RECOMP_IOS)
#define PLUME_COUNTER
#else
#define PLUME_COUNTER extern
#endif
extern "C" {
    PLUME_COUNTER uint32_t g_plumeDrawCalls;
    PLUME_COUNTER uint32_t g_plumeRenderPassBegins;
    PLUME_COUNTER uint32_t g_plumePipelineBinds;
    PLUME_COUNTER uint32_t g_plumeBarrierCalls;
    PLUME_COUNTER uint32_t g_plumeFramebufferSets;
    PLUME_COUNTER std::atomic<bool> g_plumeDeviceLost;
}
#undef PLUME_COUNTER

static uint32_t g_statDrawCalls;
static uint32_t g_statRenderPassBegins;
static uint32_t g_statPipelineBinds;
static uint32_t g_statBarrierCalls;
static uint32_t g_statFramebufferSets;

// On mobile there is no F1 key: the overlay follows Config::ShowProfiler (launcher
// and Video menu), and closing it writes the config back so it stays closed on the
// next launch (issue #46). See DrawProfiler.
static bool g_profilerVisible;
static bool g_profilerWasToggled;
static bool g_profilerConfigShown;

// iOS presents in sync with the display; VSync off only selected a worse timer.
static bool IsVSyncEnabled()
{
#if defined(UNLEASHED_RECOMP_IOS)
    return true;
#else
    return Config::VSync;
#endif
}

#if !defined(UNLEASHED_RECOMP_D3D12) && !defined(UNLEASHED_RECOMP_METAL)
static constexpr Backend g_backend = Backend::VULKAN;
#else
static Backend g_backend;
#endif

static bool g_triangleStripWorkaround = false;

static std::unique_ptr<RenderInterface> g_interface;
static std::unique_ptr<RenderDevice> g_device;

static RenderDeviceCapabilities g_capabilities;

static constexpr size_t NUM_FRAMES = 2;
static constexpr size_t NUM_QUERIES = 2;

static uint32_t g_frame = 0;
static uint32_t g_nextFrame = 1;

static std::unique_ptr<RenderCommandQueue> g_queue;
static std::unique_ptr<RenderCommandList> g_commandLists[NUM_FRAMES];
static std::unique_ptr<RenderCommandFence> g_commandFences[NUM_FRAMES];
static std::unique_ptr<RenderQueryPool> g_queryPools[NUM_FRAMES];
static bool g_commandListStates[NUM_FRAMES];

static Mutex g_copyMutex;
static std::unique_ptr<RenderCommandQueue> g_copyQueue;
static std::unique_ptr<RenderCommandList> g_copyCommandList;
static std::unique_ptr<RenderCommandFence> g_copyCommandFence;

static std::unique_ptr<RenderSwapChain> g_swapChain;
static bool g_swapChainValid;
#if defined(__ANDROID__)
static uint64_t g_androidSwapchainRetryAfterMs;
#endif

#if defined(__ANDROID__)
// The Android WSI surface backing the ANativeWindow on this device only reports RGBA-order
// formats (R8G8B8A8_UNORM/_SRGB) among its supported surface formats, not BGRA - confirmed via
// on-device vkGetPhysicalDeviceSurfaceFormatsKHR query. Using B8G8R8A8_UNORM here (as on
// Windows/D3D-style platforms) leaves VulkanSwapChain's compatible-format search empty, which
// silently leaves the swap chain's format at VK_FORMAT_UNDEFINED and crashes on the first
// vkCreateSwapchainKHR call.
static constexpr RenderFormat BACKBUFFER_FORMAT = RenderFormat::R8G8B8A8_UNORM;
#else
static constexpr RenderFormat BACKBUFFER_FORMAT = RenderFormat::B8G8R8A8_UNORM;
#endif

static std::unique_ptr<RenderCommandSemaphore> g_acquireSemaphores[NUM_FRAMES];
static std::unique_ptr<RenderCommandSemaphore> g_renderSemaphores[NUM_FRAMES];
static uint32_t g_backBufferIndex;
static std::unique_ptr<GuestSurface> g_backBufferHolder;
static GuestSurface* g_backBuffer;

static std::unique_ptr<RenderTexture> g_intermediaryBackBufferTexture;
static uint32_t g_intermediaryBackBufferTextureWidth;
static uint32_t g_intermediaryBackBufferTextureHeight;
static uint32_t g_intermediaryBackBufferTextureDescriptorIndex;

#if defined(UNLEASHED_RECOMP_IOS)
static std::unique_ptr<RenderTexture> g_iosMetalFXOutput;
static uint32_t g_iosMetalFXOutputWidth, g_iosMetalFXOutputHeight, g_iosMetalFXOutputDescriptor;
static bool g_iosMetalFXActive, g_iosMetalFXScaled;
static bool g_iosTemporalActive, g_iosTemporalInputsReady, g_iosPreviousTemporalFrame;
static bool g_iosMotionBlurDraw, g_iosObjectVelocityDraw;
static GuestSurface* g_iosTemporalSceneDepth;
static GuestShader* g_iosCsdVertexShader;
static GuestShader* g_iosCsdNoTexVertexShader;
static uint64_t g_iosTemporalFrameIndex;
static float g_iosJitterX, g_iosJitterY;
static bool IOSTemporalRequested()
{
    static const bool requested = [] {
        const char* value = std::getenv("UNLEASHED_IOS_METALFX_TEMPORAL");
        return value != nullptr && value[0] == '1';
    }();
    return requested;
}
static float IOSHalton(uint32_t index, uint32_t base)
{
    float result = 0.0f, fraction = 1.0f;
    while (index) { fraction /= base; result += fraction * (index % base); index /= base; }
    return result;
}
static bool IOSMetalFXRequested()
{
    // The menu setting applies live; the launch environment overrides it for A/B runs.
    static const int forced = [] {
        const char* value = std::getenv("UNLEASHED_IOS_METALFX_SPATIAL");
        return value == nullptr ? -1 : int(value[0] == '1');
    }();
    return (forced < 0 ? Config::MetalFXQuality != EMetalFXQuality::Off : forced == 1) || IOSTemporalRequested();
}

// UNLEASHED_IOS_GPU_PASS_DIAGNOSTICS=1: name the next render pass in sampled frames.
static void IOSLabelNextPass(const char* label);
#endif

static std::unique_ptr<RenderPipeline> g_gammaCorrectionPipeline;

struct std::unique_ptr<RenderDescriptorSet> g_textureDescriptorSet;
struct std::unique_ptr<RenderDescriptorSet> g_samplerDescriptorSet;

enum
{
    TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D,
    TEXTURE_DESCRIPTOR_NULL_TEXTURE_3D,
    TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE,
    TEXTURE_DESCRIPTOR_NULL_COUNT
};

struct TextureDescriptorAllocator
{
    Mutex mutex;
    uint32_t capacity = TEXTURE_DESCRIPTOR_NULL_COUNT;
    std::vector<uint32_t> freed;

    uint32_t allocate()
    {
        std::lock_guard lock(mutex);

        uint32_t value;
        if (!freed.empty())
        {
            value = freed.back();
            freed.pop_back();
        }
        else
        {
            value = capacity;
            ++capacity;
        }

        return value;
    }

    void free(uint32_t value)
    {
        assert(value != NULL);
        std::lock_guard lock(mutex);
        freed.push_back(value);
    }
};

static std::unique_ptr<RenderTexture> g_blankTextures[TEXTURE_DESCRIPTOR_NULL_COUNT];
static std::unique_ptr<RenderTextureView> g_blankTextureViews[TEXTURE_DESCRIPTOR_NULL_COUNT];

static TextureDescriptorAllocator g_textureDescriptorAllocator;

static void FreeTextureDescriptor(uint32_t descriptorIndex)
{
    // Uninitialized resources and the shared null textures do not own a slot.
    if (descriptorIndex < TEXTURE_DESCRIPTOR_NULL_COUNT)
        return;
    g_textureDescriptorSet->setTexture(descriptorIndex, nullptr, RenderTextureLayout::UNKNOWN);
    g_textureDescriptorAllocator.free(descriptorIndex);
}

static std::unique_ptr<RenderPipelineLayout> g_pipelineLayout;
static xxHashMap<std::unique_ptr<RenderPipeline>> g_pipelines;

#ifdef ASYNC_PSO_DEBUG
static std::atomic<uint32_t> g_pipelinesCreatedInRenderThread;
static std::atomic<uint32_t> g_pipelinesCreatedAsynchronously;
static std::atomic<uint32_t> g_pipelinesDropped;
static std::atomic<uint32_t> g_pipelinesCurrentlyCompiling;
static std::string g_pipelineDebugText;
static Mutex g_debugMutex;
#endif

#ifdef PSO_CACHING
static xxHashMap<PipelineState> g_pipelineStatesToCache;
static Mutex g_pipelineCacheMutex;
#endif

static std::atomic<uint32_t> g_compilingPipelineTaskCount;
static std::atomic<uint32_t> g_pendingPipelineTaskCount;

enum class PipelineTaskType
{
    Null,
    DatabaseData,
    PrecompilePipelines,
    RecompilePipelines
};

struct PipelineTask
{
    PipelineTaskType type{};
    boost::shared_ptr<Hedgehog::Database::CDatabaseData> databaseData;
};

static Mutex g_pipelineTaskMutex;
static std::vector<PipelineTask> g_pipelineTaskQueue;

static void EnqueuePipelineTask(PipelineTaskType type, const boost::shared_ptr<Hedgehog::Database::CDatabaseData>& databaseData)
{
    // Precompiled pipelines deliberately do not increment 
    // this counter to overlap the compilation with intro logos.
    if (type != PipelineTaskType::PrecompilePipelines)
        ++g_compilingPipelineTaskCount;

    {
        std::lock_guard lock(g_pipelineTaskMutex);
        g_pipelineTaskQueue.emplace_back(type, databaseData);
    }

    if ((++g_pendingPipelineTaskCount) == 1)
        g_pendingPipelineTaskCount.notify_one();
}

static const PipelineState g_pipelineStateCache[] =
{
#include "cache/pipeline_state_cache.h"
};

#include "cache/vertex_element_cache.h"

static uint8_t* const g_vertexDeclarationCache[] =
{
#include "cache/vertex_declaration_cache.h"
};

static xxHashMap<std::pair<uint32_t, std::unique_ptr<RenderSampler>>> g_samplerStates;

static Mutex g_vertexDeclarationMutex;
static xxHashMap<GuestVertexDeclaration*> g_vertexDeclarations;

struct UploadBuffer
{
    static constexpr size_t SIZE = 16 * 1024 * 1024;

    std::unique_ptr<RenderBuffer> buffer;
    uint8_t* memory = nullptr;
    uint64_t deviceAddress = 0;
};

struct UploadAllocator
{
    static constexpr uint32_t TRIM_AFTER_LOW_USAGE_FRAMES = 120;

    std::vector<UploadBuffer> buffers;
    uint32_t index = 0;
    uint32_t offset = 0;
    uint32_t lowUsageFrames = 0;

    UploadAllocation allocate(uint32_t size, uint32_t alignment)
    {
        assert(size <= UploadBuffer::SIZE);

        offset = (offset + alignment - 1) & ~(alignment - 1);

        if (offset + size > UploadBuffer::SIZE)
        {
            ++index;
            offset = 0;
        }

        if (buffers.size() <= index)
            buffers.resize(index + 1);

        auto& buffer = buffers[index];
        if (buffer.buffer == nullptr)
        {
#if defined(UNLEASHED_RECOMP_IOS)
            // The iOS plume pin only gives buffers a GPU address on request.
            buffer.buffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(UploadBuffer::SIZE, RenderBufferFlag::CONSTANT | RenderBufferFlag::VERTEX | RenderBufferFlag::INDEX | RenderBufferFlag::DEVICE_ADDRESSABLE));
#else
            buffer.buffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(UploadBuffer::SIZE, RenderBufferFlag::CONSTANT | RenderBufferFlag::VERTEX | RenderBufferFlag::INDEX));
#endif
            buffer.memory = reinterpret_cast<uint8_t*>(buffer.buffer->map());
            buffer.deviceAddress = buffer.buffer->getDeviceAddress();
        }
        
        auto ref = buffer.buffer->at(offset);
        offset += size;

        return { ref.ref, ref.offset, buffer.memory + ref.offset, buffer.deviceAddress + ref.offset };
    }

    template<bool TByteSwap, typename T>
    UploadAllocation allocate(const T* memory, uint32_t size, uint32_t alignment)
    {
        auto result = allocate(size, alignment);

        if constexpr (TByteSwap)
        {
            auto destination = reinterpret_cast<T*>(result.memory);

            for (size_t i = 0; i < size; i += sizeof(T))
            {
                *destination = ByteSwap(*memory);
                ++destination;
                ++memory;
            }
        }
        else
        {
            memcpy(result.memory, memory, size);
        }

        return result;
    }

    void reset()
    {
        // Keep peak buffers during heavy streaming, but drop back to one
        // upload buffer after a short period of low usage (e.g. menus).
        if (index == 0)
        {
            if (lowUsageFrames < TRIM_AFTER_LOW_USAGE_FRAMES)
                ++lowUsageFrames;

            if (lowUsageFrames >= TRIM_AFTER_LOW_USAGE_FRAMES && buffers.size() > 1)
                buffers.resize(1);
        }
        else
        {
            lowUsageFrames = 0;
        }

        index = 0;
        offset = 0;
    }
};

static UploadAllocator g_uploadAllocators[NUM_FRAMES];

struct IntermediaryUploadAllocator
{
    static constexpr size_t SIZE = 16 * 1024 * 1024;
    static constexpr uint32_t TRIM_AFTER_LOW_USAGE_FRAMES = 120;

    std::vector<std::unique_ptr<uint8_t[]>> buffers;
    uint32_t index = 0;
    uint32_t offset = 0;
    uint32_t lowUsageFrames = 0;

    uint8_t* allocate(uint32_t size)
    {
        assert(size <= SIZE);

        if (offset + size > SIZE)
        {
            ++index;
            offset = 0;
        }

        if (buffers.size() <= index)
            buffers.resize(index + 1);

        auto& buffer = buffers[index];
        if (buffer == nullptr)
            buffer = std::make_unique_for_overwrite<uint8_t[]>(SIZE);

        auto result = buffer.get() + offset;
        offset += ((size + 0xF) & ~0xF);

        return result;
    }

    uint8_t* allocate(const void* memory, uint32_t size)
    {
        auto result = allocate(size);
        memcpy(result, memory, size);
        return result;
    }

    void reset()
    {
        if (index == 0)
        {
            if (lowUsageFrames < TRIM_AFTER_LOW_USAGE_FRAMES)
                ++lowUsageFrames;

            if (lowUsageFrames >= TRIM_AFTER_LOW_USAGE_FRAMES && buffers.size() > 1)
                buffers.resize(1);
        }
        else
        {
            lowUsageFrames = 0;
        }

        index = 0;
        offset = 0;
    }
};

static IntermediaryUploadAllocator g_intermediaryUploadAllocator;

static std::vector<GuestResource*> g_tempResources[NUM_FRAMES];
static std::vector<std::unique_ptr<RenderBuffer>> g_tempBuffers[NUM_FRAMES];
static std::vector<std::unique_ptr<RenderPipeline>> g_tempPipelines[NUM_FRAMES];

template<GuestPrimitiveType PrimitiveType>
struct PrimitiveIndexData
{
    std::vector<uint16_t> indexData;
    RenderBufferReference indexBuffer;
    uint32_t currentIndexCount = 0;

    uint32_t prepare(uint32_t guestPrimCount)
    {
        uint32_t primCount;
        uint32_t indexCountPerPrimitive;

        switch (PrimitiveType)
        {
        case D3DPT_TRIANGLEFAN:
            primCount = guestPrimCount - 2;
            indexCountPerPrimitive = 3; 
            break;
        case D3DPT_QUADLIST:
            primCount = guestPrimCount / 4;
            indexCountPerPrimitive = 6;
            break;
        default:
            assert(false && "Unknown primitive type.");
            break;
        }

        uint32_t indexCount = primCount * indexCountPerPrimitive;

        if (indexData.size() < indexCount)
        {
            const size_t oldPrimCount = indexData.size() / indexCountPerPrimitive;
            indexData.resize(indexCount);

            for (size_t i = oldPrimCount; i < primCount; i++)
            {
                switch (PrimitiveType)
                {
                case D3DPT_TRIANGLEFAN:
                {
                    indexData[i * 3 + 0] = 0;
                    indexData[i * 3 + 1] = static_cast<uint16_t>(i + 1);
                    indexData[i * 3 + 2] = static_cast<uint16_t>(i + 2);
                    break;
                }
                case D3DPT_QUADLIST:
                {
                    indexData[i * 6 + 0] = static_cast<uint16_t>(i * 4 + 0);
                    indexData[i * 6 + 1] = static_cast<uint16_t>(i * 4 + 1);
                    indexData[i * 6 + 2] = static_cast<uint16_t>(i * 4 + 2);

                    indexData[i * 6 + 3] = static_cast<uint16_t>(i * 4 + 0);
                    indexData[i * 6 + 4] = static_cast<uint16_t>(i * 4 + 2);
                    indexData[i * 6 + 5] = static_cast<uint16_t>(i * 4 + 3);
                    break;
                }
                default:
                    assert(false && "Unknown primitive type.");
                    break;
                }
            }
        }

        if (indexBuffer == NULL || currentIndexCount < indexCount)
        {
            auto allocation = g_uploadAllocators[g_frame].allocate<false>(indexData.data(), indexCount * 2, 2);
            indexBuffer = allocation.buffer->at(allocation.offset);
            currentIndexCount = indexCount;
        }

        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.buffer, indexBuffer);
        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.size, indexCount * 2);
        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.format, RenderFormat::R16_UINT);

        return indexCount;
    }

    void reset()
    {
        indexBuffer = {};
        currentIndexCount = 0;
    }
};

static PrimitiveIndexData<D3DPT_TRIANGLEFAN> g_triangleFanIndexData;
static PrimitiveIndexData<D3DPT_QUADLIST> g_quadIndexData;

#if defined(UNLEASHED_RECOMP_IOS)
// Descriptor slots replaced by resolve swaps; freed once this frame's GPU work is done.
static std::vector<uint32_t> g_iosRetiredDescriptors[NUM_FRAMES];
#endif

static void DestructTempResources()
{
#if defined(UNLEASHED_RECOMP_IOS)
    for (const uint32_t descriptorIndex : g_iosRetiredDescriptors[g_frame])
        FreeTextureDescriptor(descriptorIndex);
    g_iosRetiredDescriptors[g_frame].clear();
#endif

    for (auto resource : g_tempResources[g_frame])
    {
        switch (resource->type)
        {
        case ResourceType::Texture:
        case ResourceType::VolumeTexture:
        {
            const auto texture = reinterpret_cast<GuestTexture*>(resource);

            if (texture->mappedMemory != nullptr)
                g_userHeap.Free(texture->mappedMemory);

            FreeTextureDescriptor(texture->descriptorIndex);

            if (texture->patchedTexture != nullptr)
            {
                FreeTextureDescriptor(texture->patchedTexture->descriptorIndex);
            }
            
            if (texture->recreatedCubeMapTexture != nullptr)
            {
                FreeTextureDescriptor(texture->recreatedCubeMapTexture->descriptorIndex);
            }

            texture->~GuestTexture();
            break;
        }

        case ResourceType::VertexBuffer:
        case ResourceType::IndexBuffer:
        {
            const auto buffer = reinterpret_cast<GuestBuffer*>(resource);

            if (buffer->mappedMemory != nullptr)
                g_userHeap.Free(buffer->mappedMemory);

            buffer->~GuestBuffer();
            break;
        }

        case ResourceType::RenderTarget:
        case ResourceType::DepthStencil:
        {
            const auto surface = reinterpret_cast<GuestSurface*>(resource);

            FreeTextureDescriptor(surface->descriptorIndex);

            surface->~GuestSurface();
            break;
        }

        case ResourceType::VertexDeclaration:
            reinterpret_cast<GuestVertexDeclaration*>(resource)->~GuestVertexDeclaration();
            break;

        case ResourceType::VertexShader:
        case ResourceType::PixelShader:
        {
            reinterpret_cast<GuestShader*>(resource)->~GuestShader();
            break;
        }
        }

        g_userHeap.Free(resource);
    }

    g_tempResources[g_frame].clear();
    g_tempBuffers[g_frame].clear();
    g_tempPipelines[g_frame].clear();
}

static std::thread::id g_presentThreadId = std::this_thread::get_id();
static std::thread::id g_mainThreadId = std::this_thread::get_id();
static std::atomic<bool> g_readyForCommands;

PPC_FUNC_IMPL(__imp__sub_824ECA00);
PPC_FUNC(sub_824ECA00)
{
    // Guard against thread ownership changes when between command lists.
    g_readyForCommands.wait(false);
    g_presentThreadId = std::this_thread::get_id();
    __imp__sub_824ECA00(ctx, base);
}

static ankerl::unordered_dense::map<RenderTexture*, RenderTextureLayout> g_barrierMap;

static void AddBarrier(GuestBaseTexture* texture, RenderTextureLayout layout)
{
    if (texture != nullptr && texture->layout != layout)
    {
        g_barrierMap[texture->texture] = layout;
        texture->layout = layout;
    }
}

static std::vector<RenderTextureBarrier> g_barriers;

static void FlushBarriers()
{
    if (!g_barrierMap.empty())
    {
        for (auto& [texture, layout] : g_barrierMap)
            g_barriers.emplace_back(texture, layout);

        g_commandLists[g_frame]->barriers(RenderBarrierStage::GRAPHICS | RenderBarrierStage::COPY, g_barriers);

        g_barrierMap.clear();
        g_barriers.clear();
    }
}

static std::unique_ptr<uint8_t[]> g_shaderCache;
static std::unique_ptr<uint8_t[]> g_buttonBcDiff;

static void LoadEmbeddedResources()
{
    switch (g_backend)
    {
    case Backend::VULKAN:
        g_shaderCache = std::make_unique<uint8_t[]>(g_spirvCacheDecompressedSize);
        ZSTD_decompress(g_shaderCache.get(), g_spirvCacheDecompressedSize, g_compressedSpirvCache, g_spirvCacheCompressedSize);
        break;
#if defined(UNLEASHED_RECOMP_D3D12)
    case Backend::D3D12:
        g_shaderCache = std::make_unique<uint8_t[]>(g_dxilCacheDecompressedSize);
        ZSTD_decompress(g_shaderCache.get(), g_dxilCacheDecompressedSize, g_compressedDxilCache, g_dxilCacheCompressedSize);
        break;
#elif defined(UNLEASHED_RECOMP_METAL)
    case Backend::METAL:
        g_shaderCache = std::make_unique<uint8_t[]>(g_airCacheDecompressedSize);
        ZSTD_decompress(g_shaderCache.get(), g_airCacheDecompressedSize, g_compressedAirCache, g_airCacheCompressedSize);
        break;
#endif
    default:
        assert(false);
    }

    g_buttonBcDiff = decompressZstd(g_button_bc_diff, g_button_bc_diff_uncompressed_size);
}

enum class CsdFilterState
{
    Unknown,
    On,
    Off
};

static CsdFilterState g_csdFilterState;

static ankerl::unordered_dense::set<GuestSurface*> g_pendingSurfaceCopies;
static ankerl::unordered_dense::set<GuestSurface*> g_pendingMsaaResolves;

enum class RenderCommandType
{
    SetRenderState,
    DestructResource,
    UnlockTextureRect,
    UnlockBuffer16,
    UnlockBuffer32,
    DrawImGui,
    ExecuteCommandList,
    BeginCommandList,
    StretchRect,
    SetRenderTarget,
    SetDepthStencilSurface,
    ExecutePendingStretchRectCommands,
    Clear,
    SetViewport,
    SetTexture,
    SetScissorRect,
    SetSamplerState,
    SetBooleans,
    SetVertexShaderConstants,
    SetPixelShaderConstants,
    AddPipeline,
    DrawPrimitive,
    DrawIndexedPrimitive,
    DrawPrimitiveUP,
    SetVertexDeclaration,
    SetVertexShader,
    SetStreamSource,
    SetIndices,
    SetPixelShader,
    TrimRuntimeCaches,
};

struct RenderCommand
{
    RenderCommandType type;
    union
    {
        struct
        {
            GuestRenderState type;
            uint32_t value;
        } setRenderState;

        struct 
        {
            GuestResource* resource;
        } destructResource;

        struct
        {
            GuestTexture* texture;
        } unlockTextureRect;

        struct
        {
            GuestBuffer* buffer;
        } unlockBuffer;

        struct 
        {
            GuestDevice* device;
            uint32_t flags;
            GuestTexture* texture;
        } stretchRect;

        struct 
        {
            GuestSurface* renderTarget;
        } setRenderTarget;

        struct 
        {
            GuestSurface* depthStencil;
        } setDepthStencilSurface;

        struct 
        {
            uint32_t flags;
            float color[4];
            float z;
        } clear;

        struct 
        {
            float x;
            float y;
            float width;
            float height;
            float minDepth;
            float maxDepth;
        } setViewport;

        struct 
        {
            uint32_t index;
            GuestTexture* texture;
        } setTexture;

        struct 
        {
            int32_t left;
            int32_t top;
            int32_t right;
            int32_t bottom;
        } setScissorRect;

        struct
        {
            uint32_t index;
            uint32_t data0;
            uint32_t data3;
            uint32_t data5;
        } setSamplerState;

        struct
        {
            uint32_t booleans;
        } setBooleans;

        struct
        {
            uint8_t* memory;
            uint32_t index;
            uint32_t size;
        } setVertexShaderConstants;  
        
        struct
        {
            uint8_t* memory;
            uint32_t index;
            uint32_t size;
        } setPixelShaderConstants;

        struct
        {
            XXH64_hash_t hash;
            RenderPipeline* pipeline;
        } addPipeline;

        struct 
        {
            uint32_t primitiveType; 
            uint32_t startVertex; 
            uint32_t primitiveCount;
        } drawPrimitive;

        struct 
        {
            uint32_t primitiveType;
            int32_t baseVertexIndex; 
            uint32_t startIndex;
            uint32_t primCount;
        } drawIndexedPrimitive;

        struct 
        {
            uint32_t primitiveType;
            uint32_t primitiveCount; 
            uint8_t* vertexStreamZeroData;
            uint32_t vertexStreamZeroSize;
            uint32_t vertexStreamZeroStride;
            CsdFilterState csdFilterState;
        } drawPrimitiveUP;

        struct 
        {
            GuestVertexDeclaration* vertexDeclaration;
        } setVertexDeclaration;

        struct 
        {
            GuestShader* shader;
        } setVertexShader;

        struct 
        {
            uint32_t index;
            GuestBuffer* buffer;
            uint32_t offset;
            uint32_t stride;
        } setStreamSource;

        struct 
        {
            GuestBuffer* buffer;
        } setIndices;

        struct 
        {
            GuestShader* shader;
        } setPixelShader;
    };
};

static moodycamel::BlockingConcurrentQueue<RenderCommand> g_renderQueue;

template<GuestRenderState TType>
static void SetRenderState(GuestDevice* device, uint32_t value)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetRenderState;
    cmd.setRenderState.type = TType;
    cmd.setRenderState.value = value;
    g_renderQueue.enqueue(cmd);
}

static void SetRenderStateUnimplemented(GuestDevice* device, uint32_t value)
{
}

static void SetAlphaTestMode(bool enable)
{
    uint32_t specConstants = 0;
    bool enableAlphaToCoverage = false;

    if (enable)
    {
        enableAlphaToCoverage = Config::TransparencyAntiAliasing && g_renderTarget != nullptr && g_renderTarget->sampleCount != RenderSampleCount::COUNT_1;

        if (enableAlphaToCoverage)
            specConstants = SPEC_CONSTANT_ALPHA_TO_COVERAGE;
        else
            specConstants = SPEC_CONSTANT_ALPHA_TEST;
    }

    specConstants |= (g_pipelineState.specConstants & ~(SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE));

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.enableAlphaToCoverage, enableAlphaToCoverage);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.specConstants, specConstants);
}

static RenderBlend ConvertBlendMode(uint32_t blendMode)
{
    switch (blendMode)
    {
    case D3DBLEND_ZERO:
        return RenderBlend::ZERO;
    case D3DBLEND_ONE:
        return RenderBlend::ONE;
    case D3DBLEND_SRCCOLOR:
        return RenderBlend::SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR:
        return RenderBlend::INV_SRC_COLOR;
    case D3DBLEND_SRCALPHA:
        return RenderBlend::SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA:
        return RenderBlend::INV_SRC_ALPHA;
    case D3DBLEND_DESTCOLOR:
        return RenderBlend::DEST_COLOR;
    case D3DBLEND_INVDESTCOLOR:
        return RenderBlend::INV_DEST_COLOR;
    case D3DBLEND_DESTALPHA:
        return RenderBlend::DEST_ALPHA;
    case D3DBLEND_INVDESTALPHA:
        return RenderBlend::INV_DEST_ALPHA;
    default:
        assert(false && "Invalid blend mode");
        return RenderBlend::ZERO;
    }
}

static RenderBlendOperation ConvertBlendOp(uint32_t blendOp)
{
    switch (blendOp)
    {
    case D3DBLENDOP_ADD:
        return RenderBlendOperation::ADD;
    case D3DBLENDOP_SUBTRACT:
        return RenderBlendOperation::SUBTRACT;
    case D3DBLENDOP_REVSUBTRACT:
        return RenderBlendOperation::REV_SUBTRACT;
    case D3DBLENDOP_MIN:
        return RenderBlendOperation::MIN;
    case D3DBLENDOP_MAX:
        return RenderBlendOperation::MAX;
    default:
        assert(false && "Unknown blend operation");
        return RenderBlendOperation::ADD;
    }
}

static void ProcSetRenderState(const RenderCommand& cmd)
{
    uint32_t value = cmd.setRenderState.value;

    switch (cmd.setRenderState.type)
    {
    case D3DRS_ZENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.zEnable, value != 0);
        g_dirtyStates.renderTargetAndDepthStencil |= g_dirtyStates.pipelineState;
        break;
    }
    case D3DRS_ZWRITEENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.zWriteEnable, value != 0);
        break;
    }
    case D3DRS_ALPHATESTENABLE:
    {
        SetAlphaTestMode(value != 0);
        break;
    }
    case D3DRS_SRCBLEND:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.srcBlend, ConvertBlendMode(value));
        break;
    }
    case D3DRS_DESTBLEND:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.destBlend, ConvertBlendMode(value));
        break;
    }
    case D3DRS_CULLMODE:
    {
        RenderCullMode cullMode;

        switch (value) {
        case D3DCULL_NONE:
        case D3DCULL_NONE_2:
            cullMode = RenderCullMode::NONE;
            break;
        case D3DCULL_CW:
            cullMode = RenderCullMode::FRONT;
            break;
        case D3DCULL_CCW:
            cullMode = RenderCullMode::BACK;
            break;
        default:
            assert(false && "Invalid cull mode");
            cullMode = RenderCullMode::NONE;
            break;
        }

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.cullMode, cullMode);
        break;
    }
    case D3DRS_ZFUNC:
    {
        RenderComparisonFunction comparisonFunc;

        switch (value)
        {
        case D3DCMP_NEVER:
            comparisonFunc = RenderComparisonFunction::NEVER;
            break;
        case D3DCMP_LESS:
            comparisonFunc = RenderComparisonFunction::LESS;
            break;
        case D3DCMP_EQUAL:
            comparisonFunc = RenderComparisonFunction::EQUAL;
            break;
        case D3DCMP_LESSEQUAL:
            comparisonFunc = RenderComparisonFunction::LESS_EQUAL;
            break;
        case D3DCMP_GREATER:
            comparisonFunc = RenderComparisonFunction::GREATER;
            break;
        case D3DCMP_NOTEQUAL:
            comparisonFunc = RenderComparisonFunction::NOT_EQUAL;
            break;
        case D3DCMP_GREATEREQUAL:
            comparisonFunc = RenderComparisonFunction::GREATER_EQUAL;
            break;
        case D3DCMP_ALWAYS:
            comparisonFunc = RenderComparisonFunction::ALWAYS;
            break;
        default:
            assert(false && "Unknown comparison function");
            comparisonFunc = RenderComparisonFunction::NEVER;
            break;
        }

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.zFunc, comparisonFunc);
        break;
    }
    case D3DRS_ALPHAREF:
    {
        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.alphaThreshold, float(value) / 256.0f);
        break;
    }
    case D3DRS_ALPHABLENDENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.alphaBlendEnable, value != 0);
        break;
    }
    case D3DRS_BLENDOP:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.blendOp, ConvertBlendOp(value));
        break;
    }
    case D3DRS_SCISSORTESTENABLE:
    {
        SetDirtyValue(g_dirtyStates.scissorRect, g_scissorTestEnable, value != 0);
        break;
    }
    case D3DRS_SLOPESCALEDEPTHBIAS:
    {
        if (g_capabilities.dynamicDepthBias)
            SetDirtyValue(g_dirtyStates.depthBias, g_slopeScaledDepthBias, *reinterpret_cast<float*>(&value));
        else 
            SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.slopeScaledDepthBias, *reinterpret_cast<float*>(&value));

        break;
    }
    case D3DRS_DEPTHBIAS:
    {
        if (g_capabilities.dynamicDepthBias)
            SetDirtyValue(g_dirtyStates.depthBias, g_depthBias, int32_t(*reinterpret_cast<float*>(&value) * (1 << 24)));
        else
            SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthBias, int32_t(*reinterpret_cast<float*>(&value)* (1 << 24)));

        break;
    }
    case D3DRS_SRCBLENDALPHA:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.srcBlendAlpha, ConvertBlendMode(value));
        break;
    }
    case D3DRS_DESTBLENDALPHA:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.destBlendAlpha, ConvertBlendMode(value));
        break;
    }
    case D3DRS_BLENDOPALPHA:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.blendOpAlpha, ConvertBlendOp(value));
        break;
    }
    case D3DRS_COLORWRITEENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.colorWriteEnable, value);
        g_dirtyStates.renderTargetAndDepthStencil |= g_dirtyStates.pipelineState;
        break;
    }
    }
}

static const std::pair<GuestRenderState, PPCFunc*> g_setRenderStateFunctions[] =
{
    { D3DRS_ZENABLE, HostToGuestFunction<SetRenderState<D3DRS_ZENABLE>> },
    { D3DRS_ZWRITEENABLE, HostToGuestFunction<SetRenderState<D3DRS_ZWRITEENABLE>> },
    { D3DRS_ALPHATESTENABLE, HostToGuestFunction<SetRenderState<D3DRS_ALPHATESTENABLE>> },
    { D3DRS_SRCBLEND, HostToGuestFunction<SetRenderState<D3DRS_SRCBLEND>> },
    { D3DRS_DESTBLEND, HostToGuestFunction<SetRenderState<D3DRS_DESTBLEND>> },
    { D3DRS_CULLMODE, HostToGuestFunction<SetRenderState<D3DRS_CULLMODE>> },
    { D3DRS_ZFUNC, HostToGuestFunction<SetRenderState<D3DRS_ZFUNC>> },
    { D3DRS_ALPHAREF, HostToGuestFunction<SetRenderState<D3DRS_ALPHAREF>> },
    { D3DRS_ALPHABLENDENABLE, HostToGuestFunction<SetRenderState<D3DRS_ALPHABLENDENABLE>> },
    { D3DRS_BLENDOP, HostToGuestFunction<SetRenderState<D3DRS_BLENDOP>> },
    { D3DRS_SCISSORTESTENABLE, HostToGuestFunction<SetRenderState<D3DRS_SCISSORTESTENABLE>> },
    { D3DRS_SLOPESCALEDEPTHBIAS, HostToGuestFunction<SetRenderState<D3DRS_SLOPESCALEDEPTHBIAS>> },
    { D3DRS_DEPTHBIAS, HostToGuestFunction<SetRenderState<D3DRS_DEPTHBIAS>> },
    { D3DRS_SRCBLENDALPHA, HostToGuestFunction<SetRenderState<D3DRS_SRCBLENDALPHA>> },
    { D3DRS_DESTBLENDALPHA, HostToGuestFunction<SetRenderState<D3DRS_DESTBLENDALPHA>> },
    { D3DRS_BLENDOPALPHA, HostToGuestFunction<SetRenderState<D3DRS_BLENDOPALPHA>> },
    { D3DRS_COLORWRITEENABLE, HostToGuestFunction<SetRenderState<D3DRS_COLORWRITEENABLE>> }
};

static std::unique_ptr<RenderShader> g_copyShader;

static std::unique_ptr<RenderShader> g_copyColorShader;
static ankerl::unordered_dense::map<RenderFormat, std::unique_ptr<RenderPipeline>> g_copyColorPipelines;
static std::unique_ptr<RenderPipeline> g_copyDepthPipeline;

static std::unique_ptr<RenderShader> g_resolveMsaaColorShaders[3];
static ankerl::unordered_dense::map<RenderFormat, std::array<std::unique_ptr<RenderPipeline>, 3>> g_resolveMsaaColorPipelines;
static std::unique_ptr<RenderPipeline> g_resolveMsaaDepthPipelines[3];

enum
{
    GAUSSIAN_BLUR_3X3,
    GAUSSIAN_BLUR_5X5,
    GAUSSIAN_BLUR_7X7,
    GAUSSIAN_BLUR_9X9,
    GAUSSIAN_BLUR_COUNT
};

static std::unique_ptr<GuestShader> g_gaussianBlurShaders[GAUSSIAN_BLUR_COUNT];

static std::unique_ptr<GuestShader> g_csdFilterShader;
static GuestShader* g_csdShader;

static std::unique_ptr<GuestShader> g_enhancedMotionBlurShader;

#if defined(UNLEASHED_RECOMP_D3D12)

#define CREATE_SHADER(NAME) \
    g_device->createShader( \
        (g_backend == Backend::VULKAN) ? g_##NAME##_spirv : g_##NAME##_dxil, \
        (g_backend == Backend::VULKAN) ? sizeof(g_##NAME##_spirv) : sizeof(g_##NAME##_dxil), \
        "shaderMain", \
        (g_backend == Backend::VULKAN) ? RenderShaderFormat::SPIRV : RenderShaderFormat::DXIL)

#elif defined(UNLEASHED_RECOMP_METAL)

#define CREATE_SHADER(NAME) \
    g_device->createShader( \
        (g_backend == Backend::VULKAN) ? g_##NAME##_spirv : g_##NAME##_air, \
        (g_backend == Backend::VULKAN) ? sizeof(g_##NAME##_spirv) : sizeof(g_##NAME##_air), \
        "shaderMain", \
        (g_backend == Backend::VULKAN) ? RenderShaderFormat::SPIRV : RenderShaderFormat::METAL)

#else

#define CREATE_SHADER(NAME) \
    g_device->createShader(g_##NAME##_spirv, sizeof(g_##NAME##_spirv), "shaderMain", RenderShaderFormat::SPIRV)

#endif

#ifdef _WIN32
static bool DetectWine()
{
    HMODULE dllHandle = GetModuleHandle("ntdll.dll");
    return dllHandle != nullptr && GetProcAddress(dllHandle, "wine_get_version") != nullptr;
}
#endif

static constexpr size_t TEXTURE_DESCRIPTOR_SIZE = 65536;
static constexpr size_t SAMPLER_DESCRIPTOR_SIZE = 1024;

// Clamped to the device's descriptor set limits after device creation. Desktop GPUs and
// recent Mali report far more than the defaults; PowerVR is the family that can go lower.
static uint32_t g_textureDescriptorSize = TEXTURE_DESCRIPTOR_SIZE;
static uint32_t g_samplerDescriptorSize = SAMPLER_DESCRIPTOR_SIZE;

static std::unique_ptr<GuestTexture> g_imFontTexture;
static std::unique_ptr<RenderPipelineLayout> g_imPipelineLayout;
static std::unique_ptr<RenderPipeline> g_imPipeline;
static std::unique_ptr<RenderPipeline> g_imAdditivePipeline;

template<typename T>
static void ExecuteCopyCommandList(const T& function)
{
    std::lock_guard lock(g_copyMutex);

    g_copyCommandList->begin();
    function();
    g_copyCommandList->end();
    g_copyQueue->executeCommandLists(g_copyCommandList.get(), g_copyCommandFence.get());
    g_copyQueue->waitForCommandFence(g_copyCommandFence.get());
}

static constexpr uint32_t PITCH_ALIGNMENT = 0x100;
static constexpr uint32_t PLACEMENT_ALIGNMENT = 0x200;

struct ImGuiPushConstants
{
    ImVec2 boundsMin{};
    ImVec2 boundsMax{};
    ImU32 gradientTopLeft{};
    ImU32 gradientTopRight{};
    ImU32 gradientBottomRight{};
    ImU32 gradientBottomLeft{};
    uint32_t shaderModifier{};
    uint32_t texture2DDescriptorIndex{};
    ImVec2 displaySize{};
    ImVec2 inverseDisplaySize{};
    ImVec2 origin{ 0.0f, 0.0f };
    ImVec2 scale{ 1.0f, 1.0f };
    ImVec2 proceduralOrigin{ 0.0f, 0.0f };
    float outline{};
};

extern ImFontBuilderIO g_fontBuilderIO;

static void CreateImGuiBackend()
{
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

#ifdef ENABLE_IM_FONT_ATLAS_SNAPSHOT
    IM_DELETE(io.Fonts);
    io.Fonts = ImFontAtlasSnapshot::Load();
#else
    io.Fonts->AddFontDefault();
    ImFontAtlasSnapshot::GenerateGlyphRanges();
#endif

    InitImGuiUtils();
    AchievementMenu::Init();
    AchievementOverlay::Init();
    ButtonGuide::Init();
    TouchControls::Init();
    MessageWindow::Init();
    OptionsMenu::Init();
#if !defined(__ANDROID__)
    InstallerWizard::Init();
#endif

    ImGui_ImplSDL2_InitForOther(GameWindow::s_pWindow);

#ifdef ENABLE_IM_FONT_ATLAS_SNAPSHOT
    g_imFontTexture = LoadTexture(
        decompressZstd(g_im_font_atlas_texture, g_im_font_atlas_texture_uncompressed_size).get(), g_im_font_atlas_texture_uncompressed_size);
#else
    io.Fonts->FontBuilderIO = &g_fontBuilderIO;
    io.Fonts->Build();

    g_imFontTexture = std::make_unique<GuestTexture>(ResourceType::Texture);

    uint8_t* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    RenderTextureDesc textureDesc;
    textureDesc.dimension = RenderTextureDimension::TEXTURE_2D;
    textureDesc.width = width;
    textureDesc.height = height;
    textureDesc.depth = 1;
    textureDesc.mipLevels = 1;
    textureDesc.arraySize = 1;
    textureDesc.format = RenderFormat::R8G8B8A8_UNORM;

    g_imFontTexture->textureHolder = g_device->createTexture(textureDesc);
    g_imFontTexture->texture = g_imFontTexture->textureHolder.get();

    uint32_t rowPitch = (width * 4 + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
    uint32_t slicePitch = (rowPitch * height + PLACEMENT_ALIGNMENT - 1) & ~(PLACEMENT_ALIGNMENT - 1);
    auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(slicePitch));
    uint8_t* mappedMemory = reinterpret_cast<uint8_t*>(uploadBuffer->map());

    if (rowPitch == (width * 4))
    {
        memcpy(mappedMemory, pixels, slicePitch);
    }
    else
    {
        for (size_t i = 0; i < height; i++)
        {
            memcpy(mappedMemory, pixels, width * 4);
            pixels += width * 4;
            mappedMemory += rowPitch;
        }
    }

    uploadBuffer->unmap();

    ExecuteCopyCommandList([&]
        {
            g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(g_imFontTexture->texture, RenderTextureLayout::COPY_DEST));

            g_copyCommandList->copyTextureRegion(
                RenderTextureCopyLocation::Subresource(g_imFontTexture->texture, 0),
                RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), RenderFormat::R8G8B8A8_UNORM, width, height, 1, rowPitch / 4, 0));
        });

    g_imFontTexture->layout = RenderTextureLayout::COPY_DEST;

    RenderTextureViewDesc textureViewDesc;
    textureViewDesc.format = textureDesc.format;
    textureViewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
    textureViewDesc.mipLevels = 1;
    g_imFontTexture->textureView = g_imFontTexture->texture->createTextureView(textureViewDesc);

    g_imFontTexture->descriptorIndex = g_textureDescriptorAllocator.allocate();
    g_textureDescriptorSet->setTexture(g_imFontTexture->descriptorIndex, g_imFontTexture->texture, RenderTextureLayout::SHADER_READ, g_imFontTexture->textureView.get());
#endif

    io.Fonts->SetTexID(g_imFontTexture.get());

    RenderPipelineLayoutBuilder pipelineLayoutBuilder;
    pipelineLayoutBuilder.begin(false, true);

    RenderDescriptorSetBuilder descriptorSetBuilder;
    descriptorSetBuilder.begin();
    descriptorSetBuilder.addTexture(0, g_textureDescriptorSize);
    descriptorSetBuilder.end(true, g_textureDescriptorSize);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);

    descriptorSetBuilder.begin();
    descriptorSetBuilder.addSampler(0, g_samplerDescriptorSize);
    descriptorSetBuilder.end(true, g_samplerDescriptorSize);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);

    pipelineLayoutBuilder.addPushConstant(0, 2, sizeof(ImGuiPushConstants), RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);

    pipelineLayoutBuilder.end();
    g_imPipelineLayout = pipelineLayoutBuilder.create(g_device.get());

    auto vertexShader = CREATE_SHADER(imgui_vs);
    auto pixelShader = CREATE_SHADER(imgui_ps);

    RenderInputElement inputElements[3];
    inputElements[0] = RenderInputElement("POSITION", 0, 0, RenderFormat::R32G32_FLOAT, 0, offsetof(ImDrawVert, pos));
    inputElements[1] = RenderInputElement("TEXCOORD", 0, 1, RenderFormat::R32G32_FLOAT, 0, offsetof(ImDrawVert, uv));
    inputElements[2] = RenderInputElement("COLOR", 0, 2, RenderFormat::R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col));

    RenderInputSlot inputSlot(0, sizeof(ImDrawVert));

    RenderGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.pipelineLayout = g_imPipelineLayout.get();
    pipelineDesc.vertexShader = vertexShader.get();
    pipelineDesc.pixelShader = pixelShader.get();
    pipelineDesc.renderTargetFormat[0] = BACKBUFFER_FORMAT;
    pipelineDesc.renderTargetBlend[0] = RenderBlendDesc::AlphaBlend();
    pipelineDesc.renderTargetCount = 1;
    pipelineDesc.inputElements = inputElements;
    pipelineDesc.inputElementsCount = std::size(inputElements);
    pipelineDesc.inputSlots = &inputSlot;
    pipelineDesc.inputSlotsCount = 1;
    g_imPipeline = g_device->createGraphicsPipeline(pipelineDesc);

    pipelineDesc.renderTargetBlend[0].dstBlend = RenderBlend::ONE;
    g_imAdditivePipeline = g_device->createGraphicsPipeline(pipelineDesc);

#ifndef ENABLE_IM_FONT_ATLAS_SNAPSHOT
    ImFontAtlasSnapshot snapshot;
    snapshot.Snap();

    FILE* file = fopen("im_font_atlas.bin", "wb");
    if (file)
    {
        fwrite(snapshot.data.data(), 1, snapshot.data.size(), file);
        fclose(file);
    }

    ddspp::Header header;
    ddspp::HeaderDXT10 headerDX10;
    ddspp::encode_header(ddspp::R8G8B8A8_UNORM, width, height, 1, ddspp::Texture2D, 1, 1, header, headerDX10);

    file = fopen("im_font_atlas.dds", "wb");
    if (file)
    {
        fwrite(&ddspp::DDS_MAGIC, 4, 1, file);
        fwrite(&header, sizeof(header), 1, file);
        fwrite(&headerDX10, sizeof(headerDX10), 1, file);
        fwrite(pixels, 4, width * height, file);
        fclose(file);
    }
#endif
}

#if defined(__ANDROID__)
// Without a frame-rate vote, high refresh rate phones (e.g. Realme GT 7 Pro at 120 Hz) keep
// the game surface at 60 Hz, so FIFO presentation caps the game at 60 FPS whatever the FPS
// option says. Vote the FPS option on every new ANativeWindow and whenever the option
// changes. ANativeWindow_setFrameRate is API 30+, minSdk is 29, so it is resolved at runtime.
static void UpdateAndroidFrameRateVote(plume::RenderWindow window)
{
    static plume::RenderWindow s_votedWindow = nullptr;
    static int32_t s_votedFps = 0;
    if (window == nullptr || (window == s_votedWindow && Config::FPS == s_votedFps))
        return;

    s_votedWindow = window;
    s_votedFps = Config::FPS;

    using SetFrameRateFn = int32_t (*)(ANativeWindow *, float, int8_t);
    static SetFrameRateFn s_setFrameRate = []() -> SetFrameRateFn
    {
        void *library = dlopen("libnativewindow.so", RTLD_NOW | RTLD_LOCAL);
        return library != nullptr ? reinterpret_cast<SetFrameRateFn>(dlsym(library, "ANativeWindow_setFrameRate")) : nullptr;
    }();

    if (s_setFrameRate == nullptr)
    {
        LOG_WARNING("ANativeWindow_setFrameRate is unavailable (Android 10); the display keeps its default refresh rate.");
        return;
    }

    // FPS_MAX means uncapped: vote high and let the platform pick the fastest mode.
    const float frameRate = (Config::FPS >= FPS_MIN && Config::FPS < FPS_MAX) ? float(Config::FPS) : 240.0f;
    const int32_t result = s_setFrameRate(window, frameRate, 0 /* ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT */);
    LOGF("Android frame-rate vote: {} FPS (result {}).", frameRate, result);
}

// After VK_ERROR_DEVICE_LOST every submit fails, so the image stays frozen while the game
// logic and audio keep running (issue #170). Tell the user to pick another driver instead.
[[noreturn]] static void ExitOnAndroidDeviceLost()
{
    LOGFN_ERROR("Vulkan device lost (configured driver mode {}). Stopping the game.", uint32_t(Config::VulkanDriver.Value));
    const auto message = os::android::LocaliseMessage("vulkan_device_lost",
        "The GPU driver stopped responding (Vulkan device lost). Select another Vulkan driver in the launcher and start the game again.");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), message.c_str(), GameWindow::s_pWindow);
    std::_Exit(1);
}
#endif

static void CheckSwapChain()
{
    static uint64_t s_installerInvalidCounter = 0;
#if defined(UNLEASHED_RECOMP_IOS)
    static uint64_t failedAcquisitions = 0;
    static std::chrono::steady_clock::time_point lastFailureLog;
    static bool loggedFirstDrawable = false;
#endif

    g_swapChain->setVsyncEnabled(IsVSyncEnabled());
    const bool needsResize = g_swapChain->needsResize();

#if defined(__ANDROID__)
    // Android replaces the ANativeWindow across background/foreground. The old VkSurfaceKHR
    // then references a dead window: swapchain recreation and presents "succeed" but never
    // reach the screen (black screen while audio keeps running). Compare every frame so the
    // change is caught even if lifecycle events were missed.
    plume::RenderWindow currentNativeWindow = GameWindow::GetAndroidNativeWindow();
    if (currentNativeWindow != g_swapChain->getWindow())
        g_swapChainValid = false;

    UpdateAndroidFrameRateVote(currentNativeWindow);
    if (!g_swapChainValid)
#else
    if (needsResize)
        g_swapChainValid = false;

    if (!g_swapChainValid && needsResize)
#endif
    {
#if defined(__ANDROID__)
        const uint64_t nowMs = SDL_GetTicks64();
        if (nowMs < g_androidSwapchainRetryAfterMs)
            goto skipResizeAttempt;

        // Backgrounded: no window to present into yet. Keep rendering offscreen and retry.
        if (currentNativeWindow == nullptr)
        {
            g_androidSwapchainRetryAfterMs = nowMs + 100;
            goto skipResizeAttempt;
        }

        if (currentNativeWindow != g_swapChain->getWindow())
        {
            Video::WaitForGPU();
            os::logger::Log("native window changed; recreating Vulkan surface", os::logger::ELogType::Utility, "android");
            GameWindow::s_renderWindow = currentNativeWindow;

            if (!g_swapChain->recreateSurface(currentNativeWindow))
            {
                g_androidSwapchainRetryAfterMs = nowMs + 500;
                os::logger::Log("surface recreate failed; delaying retry", os::logger::ELogType::Warning, "android");
                goto skipResizeAttempt;
            }
        }
#endif

        Video::WaitForGPU();
        g_backBuffer->framebuffers.clear();
        g_swapChainValid = g_swapChain->resize();
        g_needsResize = g_swapChainValid;

#if defined(__ANDROID__)
        if (!g_swapChainValid)
        {
            g_androidSwapchainRetryAfterMs = nowMs + 500;
            os::logger::Log("swapchain resize failed; delaying retry", os::logger::ELogType::Warning, "android");
        }
        else
        {
            os::logger::Log("swapchain (re)created successfully", os::logger::ELogType::Utility, "android");
            AndroidMarkVulkanStartupSuccessful();
        }
#endif
    }
    else if (!g_swapChainValid)
    {
        // Recover from present/acquire failures that don't require a resize.
        g_swapChainValid = true;
    }

#if defined(__ANDROID__)
skipResizeAttempt:
#endif
    if (g_swapChainValid)
    {
#if defined(__ANDROID__)
        // Idempotent after success and retries marker removal if storage had a transient error.
        AndroidMarkVulkanStartupSuccessful();
#endif
        g_swapChainAcquireProfiler.Begin();
        g_swapChainValid = g_swapChain->acquireTexture(g_acquireSemaphores[g_frame].get(), &g_backBufferIndex);
        g_swapChainAcquireProfiler.End();
    }

#if defined(UNLEASHED_RECOMP_IOS)
    if (!g_swapChainValid)
    {
        ++failedAcquisitions;
        const auto now = std::chrono::steady_clock::now();
        if (failedAcquisitions == 1 || now - lastFailureLog >= std::chrono::seconds(5))
        {
            LOGFN_ERROR("iOS drawable unavailable: attempts={}, size={}x{}, empty={}, focused={}, needsResize={}",
                failedAcquisitions, g_swapChain->getWidth(), g_swapChain->getHeight(),
                g_swapChain->isEmpty(), GameWindow::s_isFocused.load(), needsResize);
            lastFailureLog = now;
        }
    }
    else
    {
        if (!loggedFirstDrawable || failedAcquisitions != 0)
            LOGFN("iOS drawable acquired: index={}, size={}x{}, priorFailures={}",
                g_backBufferIndex, g_swapChain->getWidth(), g_swapChain->getHeight(), failedAcquisitions);
        loggedFirstDrawable = true;
        failedAcquisitions = 0;
    }
#endif

#if !defined(__ANDROID__)
    if (InstallerWizard::s_isVisible && !g_swapChainValid)
    {
        s_installerInvalidCounter++;
        if ((s_installerInvalidCounter % 120) == 0)
        {
            os::logger::Log(fmt::format(
                "CheckSwapChain - still invalid during installer (count: {}, needsResize: {}, swapChainSize: {}x{}, isEmpty: {})",
                s_installerInvalidCounter, needsResize, g_swapChain->getWidth(), g_swapChain->getHeight(), g_swapChain->isEmpty()));
        }
    }
#endif

    if (g_needsResize)
        Video::ComputeViewportDimensions();

    g_backBuffer->width = Video::s_viewportWidth;
    g_backBuffer->height = Video::s_viewportHeight;
}

static void BeginCommandList()
{
    g_renderTarget = g_backBuffer;
    g_depthStencil = nullptr;
    g_framebuffer = nullptr;

    g_pipelineState.renderTargetFormat = BACKBUFFER_FORMAT;
    g_pipelineState.depthStencilFormat = RenderFormat::UNKNOWN;

    if (g_swapChainValid)
    {
        uint32_t width = Video::s_viewportWidth;
        uint32_t height = Video::s_viewportHeight;

#if defined(UNLEASHED_RECOMP_IOS)
        g_iosMetalFXActive = false;
        g_iosMetalFXScaled = false;
        g_iosTemporalActive = false;
        g_iosTemporalInputsReady = false;
        g_iosMotionBlurDraw = false;
        g_iosObjectVelocityDraw = false;
        g_iosTemporalSceneDepth = nullptr;
        g_iosJitterX = g_iosJitterY = 0.0f;
        plume::beginMetalTemporalFrame();
        if (IOSMetalFXRequested() && g_backend == Backend::METAL && Config::ResolutionScale > 0.0f && Config::ResolutionScale < 1.0f)
        {
            const uint32_t inputWidth = std::max(1u, uint32_t(round(width * Config::ResolutionScale)));
            const uint32_t inputHeight = std::max(1u, uint32_t(round(height * Config::ResolutionScale)));
            g_iosMetalFXActive = plume::prepareMetalSpatialUpscaling(g_device.get(), inputWidth, inputHeight,
                width, height, BACKBUFFER_FORMAT);
            if (g_iosMetalFXActive)
            {
                if (inputWidth != g_intermediaryBackBufferTextureWidth || inputHeight != g_intermediaryBackBufferTextureHeight ||
                    width != g_iosMetalFXOutputWidth || height != g_iosMetalFXOutputHeight)
                    g_iosPreviousTemporalFrame = false;
                if (IOSTemporalRequested() && Config::MotionBlur != EMotionBlur::Off)
                    g_iosTemporalActive = plume::prepareMetalTemporalUpscaling(g_device.get(), inputWidth, inputHeight,
                        width, height, BACKBUFFER_FORMAT);
                if (g_iosTemporalActive && g_iosPreviousTemporalFrame)
                {
                    const uint32_t sample = uint32_t(g_iosTemporalFrameIndex++ % 16) + 1;
                    g_iosJitterX = IOSHalton(sample, 2) - 0.5f;
                    g_iosJitterY = IOSHalton(sample, 3) - 0.5f;
                }
                if (!g_iosMetalFXOutput || g_iosMetalFXOutputWidth != width || g_iosMetalFXOutputHeight != height)
                {
                    Video::WaitForGPU();
                    g_backBuffer->framebuffers.clear();
                    if (g_iosMetalFXOutputDescriptor == 0)
                        g_iosMetalFXOutputDescriptor = g_textureDescriptorAllocator.allocate();
                    g_iosMetalFXOutput = g_device->createTexture(RenderTextureDesc::Texture2D(width, height, 1,
                        BACKBUFFER_FORMAT, RenderTextureFlag::RENDER_TARGET));
                    g_textureDescriptorSet->setTexture(g_iosMetalFXOutputDescriptor, g_iosMetalFXOutput.get(), RenderTextureLayout::SHADER_READ);
                    g_iosMetalFXOutputWidth = width; g_iosMetalFXOutputHeight = height;
                }
                width = inputWidth; height = inputHeight;
                // Guest UI layout remains in native logical coordinates.
            }
        }
#endif

#if defined(UNLEASHED_RECOMP_IOS)
        if (!g_iosTemporalActive) g_iosPreviousTemporalFrame = false;
#endif

        bool usingIntermediaryTexture = (width != g_swapChain->getWidth()) || (height != g_swapChain->getHeight()) ||
            Config::XboxColorCorrection || (abs(Config::Brightness - 0.5f) > 0.001f);

        if (usingIntermediaryTexture)
        {
            if (g_intermediaryBackBufferTextureWidth != width ||
                g_intermediaryBackBufferTextureHeight != height)
            {
                if (g_intermediaryBackBufferTextureDescriptorIndex == NULL)
                    g_intermediaryBackBufferTextureDescriptorIndex = g_textureDescriptorAllocator.allocate();

                Video::WaitForGPU(); // Fine to wait for GPU, this'll only happen during resize.

                g_intermediaryBackBufferTexture = g_device->createTexture(RenderTextureDesc::Texture2D(width, height, 1, BACKBUFFER_FORMAT, RenderTextureFlag::RENDER_TARGET));
                g_textureDescriptorSet->setTexture(g_intermediaryBackBufferTextureDescriptorIndex, g_intermediaryBackBufferTexture.get(), RenderTextureLayout::SHADER_READ);

                g_intermediaryBackBufferTextureWidth = width;
                g_intermediaryBackBufferTextureHeight = height;

                g_backBuffer->framebuffers.clear();
            }

            g_backBuffer->texture = g_intermediaryBackBufferTexture.get();
        }
        else
        {
            g_backBuffer->texture = g_swapChain->getTexture(g_backBufferIndex);
        }
    }
    else
    {
        g_backBuffer->texture = g_backBuffer->textureHolder.get();
    }

    g_backBuffer->layout = RenderTextureLayout::UNKNOWN;

    for (size_t i = 0; i < 16; i++)
    {
        g_sharedConstants.texture2DIndices[i] = TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D;
        g_sharedConstants.texture3DIndices[i] = TEXTURE_DESCRIPTOR_NULL_TEXTURE_3D;
        g_sharedConstants.textureCubeIndices[i] = TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE;
    }

    memset(g_textures, 0, sizeof(g_textures));

    if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
        g_pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;
    else
        g_pipelineState.specConstants &= ~SPEC_CONSTANT_BICUBIC_GI_FILTER;

    auto& commandList = g_commandLists[g_frame];

    commandList->begin();
    commandList->resetQueryPool(g_queryPools[g_frame].get(), 0, NUM_QUERIES);
    commandList->writeTimestamp(g_queryPools[g_frame].get(), 0);
    commandList->setGraphicsPipelineLayout(g_pipelineLayout.get());
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 0);
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 1);
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 2);
    commandList->setGraphicsDescriptorSet(g_samplerDescriptorSet.get(), 3);

    g_readyForCommands = true;
    g_readyForCommands.notify_one();
}

template<typename T>
static void ApplyLowEndDefault(ConfigDef<T> &configDef, T newDefault, bool &changed)
{
    if (configDef.IsDefaultValue() && !configDef.IsLoadedFromConfig)
    {
        configDef = newDefault;
        changed = true;
    }
    
    configDef.DefaultValue = newDefault;
}

static void ApplyLowEndDefaults()
{
    bool changed = false;

#ifdef __ANDROID__
    // MSAA triggers rendering artifacts on Turnip a7xx gen3 and is never worth its cost here.
    ApplyLowEndDefault(Config::AntiAliasing, EAntiAliasing::None, changed);
#elif defined(UNLEASHED_RECOMP_IOS)
    ApplyLowEndDefault(Config::AntiAliasing, EAntiAliasing::None, changed);
#else
    ApplyLowEndDefault(Config::AntiAliasing, EAntiAliasing::MSAA2x, changed);
#endif
    ApplyLowEndDefault(Config::ShadowResolution, EShadowResolution::Original, changed);
    ApplyLowEndDefault(Config::TransparencyAntiAliasing, false, changed);
    ApplyLowEndDefault(Config::GITextureFiltering, EGITextureFiltering::Bilinear, changed);

    if (changed) 
    {
        Config::Save();
    }
}

#if defined(__APPLE__) && TARGET_OS_IOS
static void ApplyIOSMetalHUDConfig()
{
    setenv("MTL_HUD_ENABLED", Config::MetalHUD ? "1" : "0", 1);
}
#endif

bool Video::CreateHostDevice(const char *sdlVideoDriver, bool graphicsApiRetry)
{
#if defined(__APPLE__) && TARGET_OS_IOS
    // SDL's Metal view can create a device while initializing the window.
    // Configure the HUD before that first Metal device is created.
    ApplyIOSMetalHUDConfig();
#endif
#ifdef UNLEASHED_RECOMP_METAL
    plume::setMetalDiagnosticCallback([](const char *message, bool error) {
        os::logger::Log(message, error ? os::logger::ELogType::Error : os::logger::ELogType::None);
    });
#endif
    for (uint32_t i = 0; i < 16; i++)
        g_inputSlots[i].index = i;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    GameWindow::Init(sdlVideoDriver);
    if (GameWindow::s_pWindow == nullptr)
    {
        LOG_ERROR("Video::CreateHostDevice - GameWindow initialization failed (SDL window is null).");
        return false;
    }

#if defined(UNLEASHED_RECOMP_D3D12)
    g_backend = (DetectWine() || Config::GraphicsAPI == EGraphicsAPI::Vulkan) ? Backend::VULKAN : Backend::D3D12;
#elif defined(UNLEASHED_RECOMP_METAL)
    g_backend = Config::GraphicsAPI == EGraphicsAPI::Vulkan ? Backend::VULKAN : Backend::METAL;
#endif

    // Attempt to create the possible backends using a vector of function pointers. Whichever succeeds first will be the chosen API.
    using RenderInterfaceFunction = std::unique_ptr<RenderInterface>(void);
    std::vector<RenderInterfaceFunction *> interfaceFunctions;

#ifdef UNLEASHED_RECOMP_D3D12
    bool allowVulkanRedirection = true;

    if (graphicsApiRetry)
    {
        // If we are attempting to create again after a reboot due to a crash, swap the order.
        g_backend = (g_backend == Backend::VULKAN) ? Backend::D3D12 : Backend::VULKAN;

        // Don't allow redirection to Vulkan if we are retrying after a crash, 
        // so the user can at least boot the game with D3D12 if Vulkan fails to work.
        allowVulkanRedirection = false;
    }

    interfaceFunctions.push_back((g_backend == Backend::VULKAN) ? CreateVulkanInterfaceWrapper : CreateD3D12Interface);
    interfaceFunctions.push_back((g_backend == Backend::VULKAN) ? CreateD3D12Interface : CreateVulkanInterfaceWrapper);
#elif defined(UNLEASHED_RECOMP_METAL)
    interfaceFunctions.push_back((g_backend == Backend::VULKAN) ? CreateVulkanInterfaceWrapper : CreateMetalInterface);
    interfaceFunctions.push_back((g_backend == Backend::VULKAN) ? CreateMetalInterface : CreateVulkanInterfaceWrapper);
#else
    interfaceFunctions.push_back(CreateVulkanInterfaceWrapper);
#endif

    for (size_t i = 0; i < interfaceFunctions.size(); i++)
    {
        RenderInterfaceFunction* interfaceFunction = interfaceFunctions[i];

#ifdef UNLEASHED_RECOMP_D3D12
        // Wrap the device creation in __try/__except to survive from driver crashes.
        __try
#endif
        {
            g_interface = interfaceFunction();
            if (g_interface == nullptr)
            {
                continue;
            }

            g_device = g_interface->createDevice(Config::GraphicsDevice);
            if (g_device != nullptr)
            {
#ifdef UNLEASHED_RECOMP_METAL
                if (interfaceFunction == CreateMetalInterface && !g_device->getCapabilities().bufferDeviceAddress)
                {
                    LOGF_ERROR("Metal device '{}' cannot run the generated shaders: GPU addresses require Tier 2 argument buffers and iOS 16/macOS 13 or later.",
                        g_device->getDescription().name);
                    g_device.reset();
                    g_interface.reset();
                    continue;
                }
#endif
                const RenderDeviceDescription &deviceDescription = g_device->getDescription();
                
#if defined(UNLEASHED_RECOMP_D3D12)
                if (interfaceFunction == CreateD3D12Interface)
                {
                    if (allowVulkanRedirection)
                    {
                        bool redirectToVulkan = false;

                        if (deviceDescription.vendor == RenderDeviceVendor::AMD)
                        {
                            // AMD Drivers before this version have a known issue where MSAA resolve targets will fail to work correctly.
                            // If no specific graphics API was selected, we silently destroy this one and move to the next option as it'll
                            // just work incorrectly otherwise and result in visual glitches and 3D rendering not working in general.
                            constexpr uint64_t MinimumAMDDriverVersion = 0x1F00005DC2005CULL; // 31.0.24002.92
                            if ((Config::GraphicsAPI == EGraphicsAPI::Auto) && (deviceDescription.driverVersion < MinimumAMDDriverVersion))
                                redirectToVulkan = true;
                        }
                        else if (deviceDescription.vendor == RenderDeviceVendor::INTEL)
                        {
                            // Intel drivers on D3D12 are extremely buggy, introducing various graphical glitches.
                            // We will redirect users to Vulkan until a workaround can be found.
                            if (Config::GraphicsAPI == EGraphicsAPI::Auto)
                                redirectToVulkan = true;
                        }

                        if (redirectToVulkan)
                        {
                            g_device.reset();
                            g_interface.reset();

                            // In case Vulkan fails to initialize, we will try D3D12 again afterwards, 
                            // just to get the game to boot. This only really happens in very old Intel GPU drivers.
                            if (g_backend != Backend::VULKAN)
                            {
                                interfaceFunctions.push_back(CreateD3D12Interface);
                                allowVulkanRedirection = false;
                            }

                            continue;
                        }
                    }
                }

                g_backend = (interfaceFunction == CreateVulkanInterfaceWrapper) ? Backend::VULKAN : Backend::D3D12;
#elif defined(UNLEASHED_RECOMP_METAL)
                g_backend = (interfaceFunction == CreateVulkanInterfaceWrapper) ? Backend::VULKAN : Backend::METAL;
#endif
                // Enable triangle strip workaround if we are on AMD, as there is a bug where
                // restart indices cause triangles to be culled incorrectly. Converting them to degenerate triangles fixes it.
                g_triangleStripWorkaround = (deviceDescription.vendor == RenderDeviceVendor::AMD);

                break;
            }
        }
#ifdef UNLEASHED_RECOMP_D3D12
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (graphicsApiRetry)
            {
                // If we were retrying, and this also failed, then we'll show the user neither of the graphics APIs succeeded.
                return false;
            }
            else
            {
                // If this is the first crash we ran into, reboot and try the other graphics API.
                os::process::StartProcess(os::process::GetExecutablePath(), { "--graphics-api-retry" });
                std::_Exit(0);
            }
        }
#endif
    }

    if (g_device == nullptr)
    {
        return false;
    }

#ifdef UNLEASHED_RECOMP_D3D12
    if (graphicsApiRetry)
    {
        // If we managed to create a device after retrying it in a reboot, remember the one we picked.
        Config::GraphicsAPI = g_backend == Backend::VULKAN ? EGraphicsAPI::Vulkan : EGraphicsAPI::D3D12;
    }
#endif

    g_capabilities = g_device->getCapabilities();
    LOGF("Texture capabilities: BC={}, ETC2/EAC={}, maximum dimension={}", g_capabilities.textureCompressionBC, g_capabilities.textureCompressionETC2, g_capabilities.maxTextureSize);

    {
        const RenderDeviceDescription &createdDeviceDescription = g_device->getDescription();
        LOGF("GPU device created: \"{}\" (vendor 0x{:X}, driver version 0x{:X}, API {}).",
            createdDeviceDescription.name, uint32_t(createdDeviceDescription.vendor),
            createdDeviceDescription.driverVersion,
            g_backend == Backend::VULKAN ? "Vulkan" : g_backend == Backend::METAL ? "Metal" : "D3D12");
    }

#if defined(__ANDROID__)
    // Testing hook: a force_no_bc.txt in the external driver_import folder (reachable over
    // MTP/file managers) or in the internal files dir (reachable via adb run-as, since
    // shell-created files under Android/data are not readable by the app on some devices)
    // exercises the BC fallback paths on hardware that natively supports BC (e.g. Adreno).
    {
        std::error_code forceNoBcEc;
        if (g_capabilities.textureCompressionBC &&
            (std::filesystem::exists(os::android::GetExternalFilesDir() / "driver_import" / "force_no_bc.txt", forceNoBcEc) ||
             std::filesystem::exists(os::android::GetInternalFilesDir() / "force_no_bc.txt", forceNoBcEc)))
        {
            g_capabilities.textureCompressionBC = false;
            LOG_WARNING("force_no_bc.txt present: BC texture support disabled to test the fallback paths.");
        }

        // Opposite override (issue #72): force the native BC path even if BC was not
        // reported, skipping the ETC2/EAC transcode and its per-texture CPU overhead.
        // Only correct on GPUs that actually support BC (e.g. Xclipse) but end up on
        // the fallback path anyway; on Adreno/Turnip, which genuinely lacks BC, this
        // WILL corrupt textures. The launcher's "force native BC" debug option writes
        // the marker; it is gated behind Debug options for that reason.
        std::error_code forceBcEc;
        if (!g_capabilities.textureCompressionBC &&
            (std::filesystem::exists(os::android::GetExternalFilesDir() / "driver_import" / "force_bc.txt", forceBcEc) ||
             std::filesystem::exists(os::android::GetInternalFilesDir() / "force_bc.txt", forceBcEc)))
        {
            g_capabilities.textureCompressionBC = true;
            LOG_WARNING("force_bc.txt present: forcing native BC textures and disabling ETC2 transcode. Textures will corrupt if this GPU lacks BC support.");
        }
    }
#elif defined(UNLEASHED_RECOMP_IOS)
    // Testing hook (the launcher's Force ETC2 textures switch): exercise the ETC2/EAC
    // transcode on Apple GPUs that support BC.
    if (const char* value = std::getenv("UNLEASHED_IOS_FORCE_NO_BC"); value != nullptr && value[0] == '1' &&
        g_capabilities.textureCompressionBC && g_capabilities.textureCompressionETC2)
    {
        g_capabilities.textureCompressionBC = false;
        LOG_WARNING("UNLEASHED_IOS_FORCE_NO_BC=1: BC texture support disabled to test the ETC2 transcode.");
    }
#endif

    if (!g_capabilities.textureCompressionBC)
    {
        if (g_capabilities.textureCompressionETC2)
            LOG_WARNING("BC texture formats are unsupported by this device. Textures will be transcoded to ETC2/EAC on the CPU at load time.");
        else
            LOG_WARNING("BC texture formats are unsupported by this device. Textures will be decompressed on the CPU at load time, increasing memory usage.");
    }

#if !defined(UNLEASHED_RECOMP_IOS)
    // The iOS plume pin does not report descriptor limits.
    if (g_capabilities.maxSampledImageDescriptors != 0)
        g_textureDescriptorSize = uint32_t(std::min<size_t>(TEXTURE_DESCRIPTOR_SIZE, g_capabilities.maxSampledImageDescriptors));

    if (g_capabilities.maxSamplerDescriptors != 0)
        g_samplerDescriptorSize = uint32_t(std::min<size_t>(SAMPLER_DESCRIPTOR_SIZE, g_capabilities.maxSamplerDescriptors));
#endif

    if (g_textureDescriptorSize < TEXTURE_DESCRIPTOR_SIZE || g_samplerDescriptorSize < SAMPLER_DESCRIPTOR_SIZE)
    {
        LOGF_WARNING("Descriptor set sizes clamped to device limits: {} textures, {} samplers.",
            g_textureDescriptorSize, g_samplerDescriptorSize);
    }

    LoadEmbeddedResources();

    constexpr uint64_t LowEndMemoryLimit = 2048ULL * 1024ULL * 1024ULL;
    RenderDeviceDescription deviceDescription = g_device->getDescription();
    bool lowEndType = deviceDescription.type != RenderDeviceType::UNKNOWN && deviceDescription.type != RenderDeviceType::DISCRETE;
    bool lowEndMemory = deviceDescription.dedicatedVideoMemory < LowEndMemoryLimit;
    bool lowEndUMA = deviceDescription.type == RenderDeviceType::UNKNOWN && g_capabilities.uma;
    if (lowEndType || lowEndMemory || lowEndUMA)
    {
        // Switch to low end defaults if a non-discrete GPU was detected or a low amount of VRAM was detected.
        // Checking for UMA on D3D12 seems to be a reliable way to detect integrated GPUs.
        ApplyLowEndDefaults();
    }

    const RenderSampleCounts colourSampleCount = g_device->getSampleCountsSupported(RenderFormat::R16G16B16A16_FLOAT);
    const RenderSampleCounts depthSampleCount  = g_device->getSampleCountsSupported(RenderFormat::D32_FLOAT);
    const RenderSampleCounts commonSampleCount = colourSampleCount & depthSampleCount;

    // Disable specific MSAA levels if they are not supported.
    if ((commonSampleCount & RenderSampleCount::COUNT_2) == 0)
        Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA2x);
    if ((commonSampleCount & RenderSampleCount::COUNT_4) == 0)
        Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA4x);
    if ((commonSampleCount & RenderSampleCount::COUNT_8) == 0)
        Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA8x);

    // Set Anti-Aliasing to nearest supported level.
    Config::AntiAliasing.SnapToNearestAccessibleValue(false);

    g_queue = g_device->createCommandQueue(RenderCommandListType::DIRECT);

    for (auto& commandList : g_commandLists)
        commandList = g_queue->createCommandList();

    for (auto& commandFence : g_commandFences)
        commandFence = g_device->createCommandFence();

    for (auto& queryPool : g_queryPools)
        queryPool = g_device->createQueryPool(NUM_QUERIES);

    g_copyQueue = g_device->createCommandQueue(RenderCommandListType::COPY);
    g_copyCommandList = g_copyQueue->createCommandList();
    g_copyCommandFence = g_device->createCommandFence();

    uint32_t bufferCount = 2;

    switch (Config::TripleBuffering)
    {
    case ETripleBuffering::Auto:
        switch (g_backend) {
        case Backend::VULKAN:
            // Defaulting to 3 is fine if presentWait as supported, as the maximum frame latency allowed is only 1.
            bufferCount = g_device->getCapabilities().presentWait ? 3 : 2;
            break;
        case Backend::D3D12:
            // Defaulting to 3 is fine on D3D12 thanks to flip discard model.
            bufferCount = 3;
            break;
        case Backend::METAL:
#if defined(UNLEASHED_RECOMP_IOS)
            // Two drawables serialize rendering with scanout at any FPS cap; a missed
            // refresh then idles the GPU, which clocks down and keeps missing it.
            bufferCount = 3;
#else
            bufferCount = 2;
#endif
            break;
        }

        break;
    case ETripleBuffering::On:
        bufferCount = 3;
        break;
    case ETripleBuffering::Off:
        bufferCount = 2;
        break;
    }

    g_swapChain = g_queue->createSwapChain(GameWindow::s_renderWindow, bufferCount, BACKBUFFER_FORMAT, Config::MaxFrameLatency);
    g_swapChain->setVsyncEnabled(IsVSyncEnabled());
    g_swapChainValid = !g_swapChain->needsResize();

#if defined(__ANDROID__)
    // A custom loader reaching dlopen/device creation is not sufficient: only a usable WSI
    // swapchain proves that Vulkan startup completed. If initial WSI creation is deferred,
    // CheckSwapChain() clears the marker after its first successful resize instead.
    if (g_backend == Backend::VULKAN && g_swapChainValid)
        AndroidMarkVulkanStartupSuccessful();
#endif

    for (auto& acquireSemaphore : g_acquireSemaphores)
        acquireSemaphore = g_device->createCommandSemaphore();
    
    for (auto& renderSemaphore : g_renderSemaphores)
        renderSemaphore = g_device->createCommandSemaphore();

    RenderPipelineLayoutBuilder pipelineLayoutBuilder;
    pipelineLayoutBuilder.begin(false, true);
    
    RenderDescriptorSetBuilder descriptorSetBuilder;
    descriptorSetBuilder.begin();
    descriptorSetBuilder.addTexture(0, g_textureDescriptorSize);
    descriptorSetBuilder.end(true, g_textureDescriptorSize);
    
    g_textureDescriptorSet = descriptorSetBuilder.create(g_device.get());
    
    for (size_t i = 0; i < TEXTURE_DESCRIPTOR_NULL_COUNT; i++)
    {
        auto& texture = g_blankTextures[i];
        auto& textureView = g_blankTextureViews[i];

        RenderTextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.format = RenderFormat::R8_UNORM;

        RenderTextureViewDesc viewDesc;
        viewDesc.format = desc.format;
        viewDesc.componentMapping = RenderComponentMapping(RenderSwizzle::ZERO, RenderSwizzle::ZERO, RenderSwizzle::ZERO, RenderSwizzle::ZERO);
        viewDesc.mipLevels = 1;

        switch (i)
        {
        case TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D:
            desc.dimension = RenderTextureDimension::TEXTURE_2D;
            desc.arraySize = 1;
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
            break;

        case TEXTURE_DESCRIPTOR_NULL_TEXTURE_3D:
            desc.dimension = RenderTextureDimension::TEXTURE_3D;
            desc.arraySize = 1;
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_3D;
            break;

        case TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE:
            desc.dimension = RenderTextureDimension::TEXTURE_2D;
            desc.arraySize = 6;
            desc.flags = RenderTextureFlag::CUBE;
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_CUBE;
            break;

        default:
            assert(false && "Unknown null descriptor dimension");
            break;
        }

        texture = g_device->createTexture(desc);
        textureView = texture->createTextureView(viewDesc);

        g_textureDescriptorSet->setTexture(i, texture.get(), RenderTextureLayout::SHADER_READ, textureView.get());
    }

    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);
    
    descriptorSetBuilder.begin();
    descriptorSetBuilder.addSampler(0, g_samplerDescriptorSize);
    descriptorSetBuilder.end(true, g_samplerDescriptorSize);
    
    g_samplerDescriptorSet = descriptorSetBuilder.create(g_device.get());
    auto& [descriptorIndex, sampler] = g_samplerStates[XXH3_64bits(&g_samplerDescs[0], sizeof(RenderSamplerDesc))];
    descriptorIndex = 1;
    sampler = g_device->createSampler(g_samplerDescs[0]);
    g_samplerDescriptorSet->setSampler(0, sampler.get());

    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);

    if (g_backend != Backend::D3D12)
    {
        pipelineLayoutBuilder.addPushConstant(0, 4, 24, RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
    }
    else
    {
        pipelineLayoutBuilder.addRootDescriptor(0, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
        pipelineLayoutBuilder.addRootDescriptor(1, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
        pipelineLayoutBuilder.addRootDescriptor(2, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
        pipelineLayoutBuilder.addPushConstant(3, 4, 4, RenderShaderStageFlag::PIXEL); // For copy/resolve shaders.
    }
    pipelineLayoutBuilder.end();
    
    g_pipelineLayout = pipelineLayoutBuilder.create(g_device.get());

    g_copyShader = CREATE_SHADER(copy_vs);
    g_copyColorShader = CREATE_SHADER(copy_color_ps);
    auto copyDepthShader = CREATE_SHADER(copy_depth_ps);

    RenderGraphicsPipelineDesc desc;
    desc.pipelineLayout = g_pipelineLayout.get();
    desc.vertexShader = g_copyShader.get();
    desc.pixelShader = copyDepthShader.get();
    desc.depthFunction = RenderComparisonFunction::ALWAYS;
    desc.depthEnabled = true;
    desc.depthWriteEnabled = true;
    desc.depthTargetFormat = RenderFormat::D32_FLOAT;
    g_copyDepthPipeline = g_device->createGraphicsPipeline(desc);

    g_resolveMsaaColorShaders[0] = CREATE_SHADER(resolve_msaa_color_2x);
    g_resolveMsaaColorShaders[1] = CREATE_SHADER(resolve_msaa_color_4x);
    g_resolveMsaaColorShaders[2] = CREATE_SHADER(resolve_msaa_color_8x);

    for (size_t i = 0; i < std::size(g_resolveMsaaDepthPipelines); i++)
    {
        std::unique_ptr<RenderShader> pixelShader;
        switch (i)
        {
        case 0:
            pixelShader = CREATE_SHADER(resolve_msaa_depth_2x);
            break;
        case 1:
            pixelShader = CREATE_SHADER(resolve_msaa_depth_4x);
            break;
        case 2:
            pixelShader = CREATE_SHADER(resolve_msaa_depth_8x);
            break;
        }

        desc = {};
        desc.pipelineLayout = g_pipelineLayout.get();
        desc.vertexShader = g_copyShader.get();
        desc.pixelShader = pixelShader.get();
        desc.depthFunction = RenderComparisonFunction::ALWAYS;
        desc.depthEnabled = true;
        desc.depthWriteEnabled = true;
        desc.depthTargetFormat = RenderFormat::D32_FLOAT;
        g_resolveMsaaDepthPipelines[i] = g_device->createGraphicsPipeline(desc);
    }

    for (auto& shader : g_gaussianBlurShaders)
        shader = std::make_unique<GuestShader>(ResourceType::PixelShader);

    g_gaussianBlurShaders[GAUSSIAN_BLUR_3X3]->shader = CREATE_SHADER(gaussian_blur_3x3);
    g_gaussianBlurShaders[GAUSSIAN_BLUR_5X5]->shader = CREATE_SHADER(gaussian_blur_5x5);
    g_gaussianBlurShaders[GAUSSIAN_BLUR_7X7]->shader = CREATE_SHADER(gaussian_blur_7x7);
    g_gaussianBlurShaders[GAUSSIAN_BLUR_9X9]->shader = CREATE_SHADER(gaussian_blur_9x9);

    g_csdFilterShader = std::make_unique<GuestShader>(ResourceType::PixelShader);
    g_csdFilterShader->shader = CREATE_SHADER(csd_filter_ps);

    g_enhancedMotionBlurShader = std::make_unique<GuestShader>(ResourceType::PixelShader);
    g_enhancedMotionBlurShader->shader = CREATE_SHADER(enhanced_motion_blur_ps);

    CreateImGuiBackend();

    auto gammaCorrectionShader = CREATE_SHADER(gamma_correction_ps);

    desc = {};
    desc.pipelineLayout = g_pipelineLayout.get();
    desc.vertexShader = g_copyShader.get();
    desc.pixelShader = gammaCorrectionShader.get();
    desc.renderTargetFormat[0] = BACKBUFFER_FORMAT;
    desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
    desc.renderTargetCount = 1;
    g_gammaCorrectionPipeline = g_device->createGraphicsPipeline(desc);

    // NOTE: We initially allocate this on host memory to make the installer work, even if the 4 GB memory allocation fails.
    g_backBufferHolder = std::make_unique<GuestSurface>(ResourceType::RenderTarget);

    g_backBuffer = g_backBufferHolder.get();
    g_backBuffer->width = 1280;
    g_backBuffer->height = 720;
    g_backBuffer->format = BACKBUFFER_FORMAT;
    g_backBuffer->textureHolder = g_device->createTexture(RenderTextureDesc::Texture2D(1, 1, 1, BACKBUFFER_FORMAT, RenderTextureFlag::RENDER_TARGET));

    Video::ComputeViewportDimensions();
    CheckSwapChain();
    BeginCommandList();

    RenderTextureBarrier blankTextureBarriers[TEXTURE_DESCRIPTOR_NULL_COUNT];
    for (size_t i = 0; i < TEXTURE_DESCRIPTOR_NULL_COUNT; i++)
        blankTextureBarriers[i] = RenderTextureBarrier(g_blankTextures[i].get(), RenderTextureLayout::SHADER_READ);

    g_commandLists[g_frame]->barriers(RenderBarrierStage::NONE, blankTextureBarriers, std::size(blankTextureBarriers));

    return true;
}

static uint32_t g_waitForGPUCount = 0;
static std::unordered_map<uint16_t, std::unique_ptr<GuestTexture>> g_xdbfTextureCacheOwned;

void Video::WaitForGPU()
{
    g_waitForGPUCount++;

    // Wait for all queued frames to finish.
    for (size_t i = 0; i < NUM_FRAMES; i++)
    {
        if (g_commandListStates[i])
        {
            g_queue->waitForCommandFence(g_commandFences[i].get());
            g_commandListStates[i] = false;
        }
    }

    // Execute an empty command list and wait for it to end to guarantee that any remaining presentation has finished.
    g_commandLists[0]->begin();
    g_commandLists[0]->end();
    g_queue->executeCommandLists(g_commandLists[0].get(), g_commandFences[0].get());
    g_queue->waitForCommandFence(g_commandFences[0].get());
}

static uint32_t CreateDevice(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, be<uint32_t>* a6)
{
    LOGFN("Guest CreateDevice begin - a1: 0x{:08X}, a2: 0x{:08X}, a3: 0x{:08X}, a4: 0x{:08X}, a5: 0x{:08X}", a1, a2, a3, a4, a5);

    g_xdbfTextureCache.clear();
    g_xdbfTextureCacheOwned.clear();

    for (auto &achievement : g_xdbfWrapper.GetAchievements(XDBF_LANGUAGE_ENGLISH))
    {
        // huh?
        if (!achievement.pImageBuffer || !achievement.ImageBufferSize)
            continue;

        auto texture = LoadTexture((uint8_t *)achievement.pImageBuffer, achievement.ImageBufferSize);
        if (!texture)
            continue;

        auto* texturePtr = texture.get();
        g_xdbfTextureCache.insert_or_assign(achievement.ID, texturePtr);
        g_xdbfTextureCacheOwned.insert_or_assign(achievement.ID, std::move(texture));
    }

    // Move backbuffer to guest memory.
    assert(!g_memory.IsInMemoryRange(g_backBuffer) && g_backBufferHolder != nullptr);
    g_backBuffer = g_userHeap.AllocPhysical<GuestSurface>(std::move(*g_backBufferHolder));

    // Check for stale reference. BeginCommandList() gets called before CreateDevice() which is where the assignment happens.
    if (g_renderTarget == g_backBufferHolder.get()) g_renderTarget = g_backBuffer;
    if (g_depthStencil == g_backBufferHolder.get()) g_depthStencil = g_backBuffer;

    // Free the host backbuffer.
    g_backBufferHolder = nullptr;

    auto device = g_userHeap.AllocPhysical<GuestDevice>();
    memset(device, 0, sizeof(*device));

    // Append render state functions to the end of guest function table.
    uint32_t functionOffset = PPC_CODE_BASE + PPC_CODE_SIZE;
    g_memory.InsertFunction(functionOffset, HostToGuestFunction<SetRenderStateUnimplemented>);

    for (size_t i = 0; i < std::size(device->setRenderStateFunctions); i++)
        device->setRenderStateFunctions[i] = functionOffset;

    for (auto& [state, function] : g_setRenderStateFunctions)
    {
        functionOffset += 4;
        g_memory.InsertFunction(functionOffset, function);
        device->setRenderStateFunctions[state / 4] = functionOffset;
    }

    for (size_t i = 0; i < std::size(device->setSamplerStateFunctions); i++)
        device->setSamplerStateFunctions[i] = *reinterpret_cast<uint32_t*>(g_memory.Translate(0x8330F3DC + i * 0xC));

    device->viewport.width = 1280.0f;
    device->viewport.height = 720.0f;
    device->viewport.maxZ = 1.0f;

    *a6 = g_memory.MapVirtual(device);

    LOGFN("Guest CreateDevice end - device: 0x{:08X}", a6->get());
    return 0;
}

static void DestructResource(GuestResource* resource) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::DestructResource;
    cmd.destructResource.resource = resource;
    g_renderQueue.enqueue(cmd);
}

static void ProcDestructResource(const RenderCommand& cmd)
{
    const auto& args = cmd.destructResource;
    g_tempResources[g_frame].push_back(args.resource);
}

static uint32_t ComputeTexturePitch(GuestTexture* texture)
{
    return (texture->width * RenderFormatSize(texture->format) + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
}

static void LockTextureRect(GuestTexture* texture, uint32_t, GuestLockedRect* lockedRect) 
{
    uint32_t pitch = ComputeTexturePitch(texture);
    uint32_t slicePitch = pitch * texture->height;

    if (texture->mappedMemory == nullptr)
        texture->mappedMemory = g_userHeap.AllocPhysical(slicePitch, 0x10);

    lockedRect->pitch = pitch;
    lockedRect->bits = g_memory.MapVirtual(texture->mappedMemory);
}

static void UnlockTextureRect(GuestTexture* texture) 
{
    if (texture == nullptr || texture->mappedMemory == nullptr)
    {
        return;
    }

    RenderCommand cmd;
    cmd.type = RenderCommandType::UnlockTextureRect;
    cmd.unlockTextureRect.texture = texture;
    g_renderQueue.enqueue(cmd);
}

static void ProcUnlockTextureRect(const RenderCommand& cmd)
{
    const auto& args = cmd.unlockTextureRect;

    AddBarrier(args.texture, RenderTextureLayout::COPY_DEST);
    FlushBarriers();

    uint32_t pitch = ComputeTexturePitch(args.texture);
    uint32_t slicePitch = pitch * args.texture->height;

    auto allocation = g_uploadAllocators[g_frame].allocate(slicePitch, PLACEMENT_ALIGNMENT);
    memcpy(allocation.memory, args.texture->mappedMemory, slicePitch);

    g_commandLists[g_frame]->copyTextureRegion(
        RenderTextureCopyLocation::Subresource(args.texture->texture, 0),
        RenderTextureCopyLocation::PlacedFootprint(allocation.buffer, args.texture->format, args.texture->width, args.texture->height, 1, pitch / RenderFormatSize(args.texture->format), allocation.offset));
}

static void* LockBuffer(GuestBuffer* buffer, uint32_t flags)
{
    buffer->lockedReadOnly = (flags & 0x10) != 0;

    if (buffer->mappedMemory == nullptr)
        buffer->mappedMemory = g_userHeap.AllocPhysical(buffer->dataSize, 0x10);

    return buffer->mappedMemory;
}

static void* LockVertexBuffer(GuestBuffer* buffer, uint32_t, uint32_t, uint32_t flags)
{
    return LockBuffer(buffer, flags);
}

static std::atomic<uint32_t> g_bufferUploadCount = 0;

template<typename T>
static void UnlockBuffer(GuestBuffer* buffer, bool useCopyQueue)
{
    auto copyBuffer = [&](T* dest)
        {
            auto src = reinterpret_cast<const T*>(buffer->mappedMemory);

            for (size_t i = 0; i < buffer->dataSize; i += sizeof(T))
            {
                *dest = ByteSwap(*src);
                ++dest;
                ++src;
            }
        };

    if (useCopyQueue && g_capabilities.gpuUploadHeap)
    {
        copyBuffer(reinterpret_cast<T*>(buffer->buffer->map()));
        buffer->buffer->unmap();
    }
    else
    {
        auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(buffer->dataSize));
        copyBuffer(reinterpret_cast<T*>(uploadBuffer->map()));
        uploadBuffer->unmap();

        if (useCopyQueue)
        {
            ExecuteCopyCommandList([&]
                {
                    g_copyCommandList->copyBufferRegion(buffer->buffer->at(0), uploadBuffer->at(0), buffer->dataSize);
                });
        }
        else
        {
            auto& commandList = g_commandLists[g_frame];

            commandList->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(buffer->buffer.get(), RenderBufferAccess::WRITE));
            commandList->copyBufferRegion(buffer->buffer->at(0), uploadBuffer->at(0), buffer->dataSize);
            commandList->barriers(RenderBarrierStage::GRAPHICS, RenderBufferBarrier(buffer->buffer.get(), RenderBufferAccess::READ));

            g_tempBuffers[g_frame].emplace_back(std::move(uploadBuffer));
        }
    }

    g_bufferUploadCount++;
}

template<typename T>
static void UnlockBuffer(GuestBuffer* buffer)
{
    if (!buffer->lockedReadOnly)
    {
        if (std::this_thread::get_id() == g_presentThreadId)
        {
            RenderCommand cmd;
            cmd.type = (sizeof(T) == 2) ? RenderCommandType::UnlockBuffer16 : RenderCommandType::UnlockBuffer32;
            cmd.unlockBuffer.buffer = buffer;
            g_renderQueue.enqueue(cmd);
        }
        else
        {
            UnlockBuffer<T>(buffer, true);
        }
    }
}

static void ProcUnlockBuffer16(const RenderCommand& cmd)
{
    UnlockBuffer<uint16_t>(cmd.unlockBuffer.buffer, false);
}

static void ProcUnlockBuffer32(const RenderCommand& cmd)
{
    UnlockBuffer<uint32_t>(cmd.unlockBuffer.buffer, false);
}

static void UnlockVertexBuffer(GuestBuffer* buffer)
{
    UnlockBuffer<uint32_t>(buffer);
}

static void GetVertexBufferDesc(GuestBuffer* buffer, GuestBufferDesc* desc) 
{
    desc->size = buffer->dataSize;
}

static void* LockIndexBuffer(GuestBuffer* buffer, uint32_t, uint32_t, uint32_t flags) 
{
    return LockBuffer(buffer, flags);
}

static void UnlockIndexBuffer(GuestBuffer* buffer) 
{
    if (buffer->guestFormat == D3DFMT_INDEX32)
        UnlockBuffer<uint32_t>(buffer);
    else
        UnlockBuffer<uint16_t>(buffer);
}

static void GetIndexBufferDesc(GuestBuffer* buffer, GuestBufferDesc* desc)
{
    desc->format = buffer->guestFormat;
    desc->size = buffer->dataSize;
}

static void GetSurfaceDesc(GuestSurface* surface, GuestSurfaceDesc* desc) 
{
    desc->width = surface->width;
    desc->height = surface->height;
}

static void GetVertexDeclaration(GuestVertexDeclaration* vertexDeclaration, GuestVertexElement* vertexElements, be<uint32_t>* count) 
{
    memcpy(vertexElements, vertexDeclaration->vertexElements.get(), vertexDeclaration->vertexElementCount * sizeof(GuestVertexElement));
    *count = vertexDeclaration->vertexElementCount;
}

static uint32_t HashVertexDeclaration(uint32_t vertexDeclaration) 
{
    // Vertex declarations are cached on host side, so the pointer itself can be used.
    return vertexDeclaration;
}

static const char *DeviceTypeName(RenderDeviceType type)
{
    switch (type) 
    {
    case RenderDeviceType::INTEGRATED:
        return "Integrated";
    case RenderDeviceType::DISCRETE:
        return "Discrete";
    case RenderDeviceType::VIRTUAL:
        return "Virtual";
    case RenderDeviceType::CPU:
        return "CPU";
    default:
        return "Unknown";
    }
}

static void DrawProfiler()
{
#if defined(__ANDROID__) || defined(UNLEASHED_RECOMP_IOS)
    if (Config::ShowProfiler != g_profilerConfigShown)
    {
        g_profilerConfigShown = Config::ShowProfiler;
        g_profilerVisible = g_profilerConfigShown;
    }
#endif

    bool toggleProfiler = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_F1] != 0;

    if (!g_profilerWasToggled && toggleProfiler)
    {
        g_profilerVisible = !g_profilerVisible;

        GameWindow::SetFullscreenCursorVisibility(App::s_isInit ? g_profilerVisible : true);
    }

    g_profilerWasToggled = toggleProfiler;

    if (!g_profilerVisible)
        return;

    ImFont* font = ImFontAtlasSnapshot::GetFont("FOT-SeuratPro-M.otf");
    float defaultScale = font->Scale;
    font->Scale = ImGui::GetDefaultFont()->FontSize / font->FontSize;
    ImGui::PushFont(font);

    if (ImGui::Begin("Profiler", &g_profilerVisible))
    {
        g_applicationValues[g_profilerValueIndex] = App::s_deltaTime * 1000.0;

        const double applicationAvg = std::accumulate(g_applicationValues, g_applicationValues + PROFILER_VALUE_COUNT, 0.0) / PROFILER_VALUE_COUNT;
        double gpuFrameAvg = g_gpuFrameProfiler.UpdateAndReturnAverage();
        double presentAvg = g_presentProfiler.UpdateAndReturnAverage();
        double updateDirectorAvg = g_updateDirectorProfiler.UpdateAndReturnAverage();
        double renderDirectorAvg = g_renderDirectorProfiler.UpdateAndReturnAverage();
        double frameFenceAvg = g_frameFenceProfiler.UpdateAndReturnAverage();
        double presentWaitAvg = g_presentWaitProfiler.UpdateAndReturnAverage();
        double swapChainAcquireAvg = g_swapChainAcquireProfiler.UpdateAndReturnAverage();

        if (ImPlot::BeginPlot("Frame Time"))
        {
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 20.0);
            ImPlot::SetupAxis(ImAxis_Y1, "ms", ImPlotAxisFlags_None);
            ImPlot::PlotLine<double>("Application", g_applicationValues, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("GPU Frame", g_gpuFrameProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Present", g_presentProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Update Director", g_updateDirectorProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Render Director", g_renderDirectorProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Frame Fence", g_frameFenceProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Present Wait", g_presentWaitProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Swap Chain Acquire", g_swapChainAcquireProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::EndPlot();
        }

        g_profilerValueIndex = (g_profilerValueIndex + 1) % PROFILER_VALUE_COUNT;

        ImGui::Text("Current Application: %g ms (%g FPS)", App::s_deltaTime * 1000.0, 1.0 / App::s_deltaTime);
        ImGui::Text("Current GPU Frame: %g ms (%g FPS)", g_gpuFrameProfiler.value.load(), 1000.0 / g_gpuFrameProfiler.value.load());
        ImGui::Text("Current Present: %g ms (%g FPS)", g_presentProfiler.value.load(), 1000.0 / g_presentProfiler.value.load());
        ImGui::Text("Current Update Director: %g ms (%g FPS)", g_updateDirectorProfiler.value.load(), 1000.0 / g_updateDirectorProfiler.value.load());
        ImGui::Text("Current Render Director: %g ms (%g FPS)", g_renderDirectorProfiler.value.load(), 1000.0 / g_renderDirectorProfiler.value.load());
        ImGui::Text("Current Frame Fence: %g ms", g_frameFenceProfiler.value.load());
        ImGui::Text("Current Present Wait: %g ms", g_presentWaitProfiler.value.load());
        ImGui::Text("Current Swap Chain Acquire: %g ms", g_swapChainAcquireProfiler.value.load());

        ImGui::NewLine();

        ImGui::Text("Average Application: %g ms (%g FPS)", applicationAvg, 1000.0 / applicationAvg);
        ImGui::Text("Average GPU Frame: %g ms (%g FPS)", gpuFrameAvg, 1000.0 / gpuFrameAvg);
        ImGui::Text("Average Present: %g ms (%g FPS)", presentAvg, 1000.0 / presentAvg);
        ImGui::Text("Average Update Director: %g ms (%g FPS)", updateDirectorAvg, 1000.0 / updateDirectorAvg);
        ImGui::Text("Average Render Director: %g ms (%g FPS)", renderDirectorAvg, 1000.0 / renderDirectorAvg);
        ImGui::Text("Average Frame Fence: %g ms", frameFenceAvg);
        ImGui::Text("Average Present Wait: %g ms", presentWaitAvg);
        ImGui::Text("Average Swap Chain Acquire: %g ms", swapChainAcquireAvg);

        ImGui::NewLine();

        if (g_userHeap.heap != nullptr && g_userHeap.physicalHeap != nullptr)
        {
            O1HeapDiagnostics diagnostics, physicalDiagnostics;
            {
                std::lock_guard lock(g_userHeap.mutex);
                diagnostics = o1heapGetDiagnostics(g_userHeap.heap);
            }
            {
                std::lock_guard lock(g_userHeap.physicalMutex);
                physicalDiagnostics = o1heapGetDiagnostics(g_userHeap.physicalHeap);
            }

            ImGui::Text("Heap Allocated: %d MB", int32_t(diagnostics.allocated / (1024 * 1024)));
            ImGui::Text("Physical Heap Allocated: %d MB", int32_t(physicalDiagnostics.allocated / (1024 * 1024)));
        }

        ImGui::Text("GPU Waits: %d", int32_t(g_waitForGPUCount));
        ImGui::Text("Buffer Uploads: %d", int32_t(g_bufferUploadCount));
        ImGui::NewLine();

        ImGui::Text("Draw Calls: %u", g_statDrawCalls);
        ImGui::Text("Render Pass Begins: %u", g_statRenderPassBegins);
        ImGui::Text("Pipeline Binds: %u", g_statPipelineBinds);
        ImGui::Text("Barrier Calls: %u", g_statBarrierCalls);
        ImGui::Text("Framebuffer Sets: %u", g_statFramebufferSets);
        ImGui::NewLine();

        ImGui::Text("Present Wait: %s", g_capabilities.presentWait ? "Supported" : "Unsupported");
        ImGui::Text("Triangle Fan: %s", g_capabilities.triangleFan ? "Supported" : "Unsupported");
        ImGui::Text("BC Textures: %s", g_capabilities.textureCompressionBC
            ? "Supported" : (g_capabilities.textureCompressionETC2 ? "ETC2 Transcode" : "CPU Decode"));
        ImGui::Text("Dynamic Depth Bias: %s", g_capabilities.dynamicDepthBias ? "Supported" : "Unsupported");
        ImGui::Text("Hardware Resolve Modes: %s", g_capabilities.resolveModes ? "Supported" : "Unsupported");
        ImGui::Text("Triangle Strip Workaround: %s", g_triangleStripWorkaround ? "Enabled" : "Disabled");
        ImGui::NewLine();

#ifndef NDEBUG
        std::string backend;

        switch (g_backend) {
        case Backend::VULKAN:
            backend = "Vulkan";
            break;
        case Backend::D3D12:
            backend = "D3D12";
            break;
        case Backend::METAL:
            backend = "Metal";
            break;
        default:
            assert(false && "Unknown graphics backend");
        }

        ImGui::Text("API: %s", backend.c_str());
        ImGui::Text("Device: %s", g_device->getDescription().name.c_str());
        ImGui::Text("Device Type: %s", DeviceTypeName(g_device->getDescription().type));
        ImGui::Text("VRAM: %.2f MiB", (double)(g_device->getDescription().dedicatedVideoMemory) / (1024.0 * 1024.0));
        ImGui::Text("UMA: %s", g_capabilities.uma ? "Supported" : "Unsupported");
        ImGui::Text("GPU Upload Heap: %s", g_capabilities.gpuUploadHeap ? "Supported" : "Unsupported");

        ImGui::NewLine();
#endif

        const char* sdlVideoDriver = SDL_GetCurrentVideoDriver();
        if (sdlVideoDriver != nullptr)
            ImGui::Text("SDL Video Driver: %s", sdlVideoDriver);

        ImGui::NewLine();
        ImGui::Checkbox("Show FPS", &Config::ShowFPS.Value);
        ImGui::NewLine();

        if (ImGui::TreeNode("Device Names"))
        {
            ImGui::Indent();

            uint32_t deviceIndex = 0;
            for (const std::string &deviceName : g_interface->getDeviceNames())
            {
                ImGui::Text("Option #%d: %s", deviceIndex++, deviceName.c_str());
            }

            ImGui::Unindent();
            ImGui::TreePop();
        }
    }
    ImGui::End();

#if defined(__ANDROID__) || defined(UNLEASHED_RECOMP_IOS)
    // Closing the overlay with its X button turns the option off and persists it,
    // so the window does not come back on every launch (issue #46).
    if (!g_profilerVisible && Config::ShowProfiler)
    {
        Config::ShowProfiler.Value = false;
        g_profilerConfigShown = false;
        Config::Save();
    }
#endif

    ImGui::PopFont();
    font->Scale = defaultScale;
}

static void DrawFPS()
{
    if (!Config::ShowFPS)
        return;

    double time = ImGui::GetTime();
    static double updateTime = time;
    static double fps = 0;
    static double totalDeltaTime = 0.0;
    static uint32_t totalDeltaCount = 0;

    totalDeltaTime += g_presentProfiler.value.load();
    totalDeltaCount++;

    if (time - updateTime >= 1.0f)
    {
        fps = 1000.0 / std::max(totalDeltaTime / double(totalDeltaCount), 1.0);
        updateTime = time;
        totalDeltaTime = 0.0;
        totalDeltaCount = 0;
    }

    auto drawList = ImGui::GetBackgroundDrawList();

    auto fmt = fmt::format("FPS: {:.2f}", fps);
    auto font = ImFontAtlasSnapshot::GetFont("FOT-SeuratPro-M.otf");
    auto fontSize = Scale(10);
    auto textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0, fmt.c_str());

    ImVec2 min = { Scale(40), Scale(30) };
    ImVec2 max = { min.x + std::max(Scale(75), textSize.x + Scale(10)), min.y + Scale(15) };
    ImVec2 textPos = { min.x + Scale(2), CENTRE_TEXT_VERT(min, max, textSize) + Scale(0.2f) };

    drawList->AddRectFilled(min, max, IM_COL32(0, 0, 0, 200));
    drawList->AddText(font, fontSize, textPos, IM_COL32_WHITE, fmt.c_str());
}

#if defined(__APPLE__) && TARGET_OS_IOS
static uint64_t GetIOSFreeMemoryBytes()
{
    mach_port_t hostPort = mach_host_self();
    vm_size_t pageSize = 0;
    if (host_page_size(hostPort, &pageSize) != KERN_SUCCESS)
        return 0;

    vm_statistics64_data_t vmStats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(hostPort, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vmStats), &count) != KERN_SUCCESS)
        return 0;

    return (uint64_t(vmStats.free_count) + uint64_t(vmStats.inactive_count)) * uint64_t(pageSize);
}

static void DrawIOSLowMemoryWarning()
{
    constexpr uint64_t warningThresholdBytes = 500ull * 1024ull * 1024ull;

    static double nextSampleTime = 0.0;
    static uint64_t freeMemoryBytes = 0;

    const double currentTime = ImGui::GetTime();
    if (currentTime >= nextSampleTime)
    {
        freeMemoryBytes = GetIOSFreeMemoryBytes();
        nextSampleTime = currentTime + 0.5;
    }

    if (freeMemoryBytes == 0 || freeMemoryBytes >= warningThresholdBytes)
        return;

    const double freeMemoryMiB = double(freeMemoryBytes) / (1024.0 * 1024.0);
    const std::string warningText = fmt::format("Low memory warning: {:.0f} MB free", freeMemoryMiB);

    auto* drawList = ImGui::GetBackgroundDrawList();
    ImFont* font = ImFontAtlasSnapshot::GetFont("FOT-SeuratPro-M.otf");
    const float fontSize = Scale(11);
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, warningText.c_str());
    const ImVec2 textPos = {
        std::max(Scale(8), (float(Video::s_viewportWidth) - textSize.x) * 0.5f),
        Scale(8)
    };

    const ImVec2 padding = { Scale(8), Scale(4) };
    const ImVec2 bgMin = { textPos.x - padding.x, textPos.y - padding.y };
    const ImVec2 bgMax = { textPos.x + textSize.x + padding.x, textPos.y + textSize.y + padding.y };

    drawList->AddRectFilled(bgMin, bgMax, IM_COL32(0, 0, 0, 185), Scale(4));
    drawList->AddText(font, fontSize, textPos, IM_COL32(255, 165, 0, 255), warningText.c_str());
}
#endif

static void DrawImGui()
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    if (std::this_thread::get_id() == g_mainThreadId)
        ImGui_ImplSDL2_NewFrame();
    else
        dispatch_sync_f(dispatch_get_main_queue(), nullptr, [](void*) { ImGui_ImplSDL2_NewFrame(); });
#else
    ImGui_ImplSDL2_NewFrame();
#endif

    auto& io = ImGui::GetIO();
    io.DisplaySize = { float(Video::s_viewportWidth), float(Video::s_viewportHeight) };

    // ImGui doesn't know that we center the screen for specific aspect ratio
    // settings, which causes mouse events to not work correctly. To fix this, 
    // we can adjust the mouse events before ImGui processes them.
    uint32_t width = g_swapChain->getWidth();
    uint32_t height = g_swapChain->getHeight();
    float mousePosScaleX = float(width) / float(GameWindow::s_width);
    float mousePosScaleY = float(height) / float(GameWindow::s_height);
    float mousePosOffsetX = (width - Video::s_viewportWidth) / 2.0f;
    float mousePosOffsetY = (height - Video::s_viewportHeight) / 2.0f;
    for (int i = 0; i < io.Ctx->InputEventsQueue.Size; i++)
    {
        auto& e = io.Ctx->InputEventsQueue[i];
        if (e.Type == ImGuiInputEventType_MousePos)
        {
            if (e.MousePos.PosX != -FLT_MAX)
            {
                e.MousePos.PosX *= mousePosScaleX;
                e.MousePos.PosX -= mousePosOffsetX;
            }

            if (e.MousePos.PosY != -FLT_MAX)
            {
                e.MousePos.PosY *= mousePosScaleY;
                e.MousePos.PosY -= mousePosOffsetY;
            }
        }
    }

    ImGui::NewFrame();

    ResetImGuiCallbacks();

#ifdef ASYNC_PSO_DEBUG
    if (ImGui::Begin("Async PSO Stats"))
    {
        ImGui::Text("Pipelines Created In Render Thread: %d", g_pipelinesCreatedInRenderThread.load());
        ImGui::Text("Pipelines Created Asynchronously: %d", g_pipelinesCreatedAsynchronously.load());
        ImGui::Text("Pipelines Dropped: %d", g_pipelinesDropped.load());
        ImGui::Text("Pipelines Currently Compiling: %d", g_pipelinesCurrentlyCompiling.load());
        ImGui::Text("Compiling Pipeline Task Count: %d", g_compilingPipelineTaskCount.load());
        ImGui::Text("Pending Pipeline Task Count: %d", g_pendingPipelineTaskCount.load());

        std::lock_guard lock(g_debugMutex);
        ImGui::TextUnformatted(g_pipelineDebugText.c_str());
    }
    ImGui::End();
#endif

    AchievementMenu::Draw();
    OptionsMenu::Draw();
    AchievementOverlay::Draw();
#if !defined(__ANDROID__)
    InstallerWizard::Draw();
#endif
    MessageWindow::Draw();
    ButtonGuide::Draw();
#if defined(__ANDROID__) || defined(UNLEASHED_RECOMP_IOS)
    TouchControls::Draw();
#endif
    Fader::Draw();
    BlackBar::Draw();

    assert(ImGui::GetBackgroundDrawList()->_ClipRectStack.Size == 1 && "Some clip rects were not removed from the stack!");

    DrawFPS();
#if defined(__APPLE__) && TARGET_OS_IOS
    DrawIOSLowMemoryWarning();
#endif
    DrawProfiler();
    ImGui::Render();

    auto drawData = ImGui::GetDrawData();

    if (drawData->CmdListsCount != 0)
    {
        RenderCommand cmd;
        cmd.type = RenderCommandType::DrawImGui;
        g_renderQueue.enqueue(cmd);
    }
}

static void SetFramebuffer(GuestSurface *renderTarget, GuestSurface *depthStencil, bool settingForClear);

#if defined(UNLEASHED_RECOMP_IOS)
static void ProcUpscaleIOSBackBuffer()
{
    if (!g_swapChainValid || !g_iosMetalFXActive || g_iosMetalFXScaled)
        return;
    auto &list = g_commandLists[g_frame];
    RenderTextureBarrier barriers[] = {
        RenderTextureBarrier(g_intermediaryBackBufferTexture.get(), RenderTextureLayout::SHADER_READ),
        RenderTextureBarrier(g_iosMetalFXOutput.get(), RenderTextureLayout::GENERAL)
    };
    list->barriers(RenderBarrierStage::GRAPHICS | RenderBarrierStage::COMPUTE, barriers, std::size(barriers));
    const bool temporal = g_iosTemporalActive && plume::encodeMetalTemporalUpscaling(list.get(),
        g_intermediaryBackBufferTexture.get(), g_iosMetalFXOutput.get(), g_iosJitterX, g_iosJitterY,
        !g_iosPreviousTemporalFrame);
    if (!temporal)
        plume::encodeMetalSpatialUpscaling(list.get(), g_intermediaryBackBufferTexture.get(), g_iosMetalFXOutput.get());
    static int previousMode = -1;
    if (IOSTemporalRequested() && previousMode != int(temporal))
    {
        os::logger::Log(temporal ? "MetalFX mode: Temporal; real scene depth and decoded game motion, 16-sample jitter"
            : "MetalFX mode: Spatial fallback; Temporal unsupported or no matching scene inputs");
        previousMode = int(temporal);
    }
    g_iosPreviousTemporalFrame = temporal;
    list->barriers(RenderBarrierStage::GRAPHICS | RenderBarrierStage::COMPUTE,
        RenderTextureBarrier(g_iosMetalFXOutput.get(), RenderTextureLayout::SHADER_READ));
    g_backBuffer->texture = g_iosMetalFXOutput.get();
    g_backBuffer->layout = RenderTextureLayout::SHADER_READ;
    g_backBuffer->width = Video::s_viewportWidth;
    g_backBuffer->height = Video::s_viewportHeight;
    g_iosMetalFXScaled = true;
    g_dirtyStates.renderTargetAndDepthStencil = true;
    g_dirtyStates.viewport = true;
    g_dirtyStates.scissorRect = true;
}
#endif

static void ProcDrawImGui(const RenderCommand& cmd)
{
#if defined(UNLEASHED_RECOMP_IOS)
    ProcUpscaleIOSBackBuffer(); // Draw the host menus at native resolution afterward.
#endif
    // Make sure the backbuffer is the current target.
    AddBarrier(g_backBuffer, RenderTextureLayout::COLOR_WRITE);
    FlushBarriers();
    SetFramebuffer(g_backBuffer, nullptr, false);

    auto& commandList = g_commandLists[g_frame];
    auto pipeline = g_imPipeline.get();

    commandList->setGraphicsPipelineLayout(g_imPipelineLayout.get());
    commandList->setPipeline(pipeline);
#if defined(UNLEASHED_RECOMP_IOS)
    IOSLabelNextPass("host|ImGui");
#endif
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 0);
    commandList->setGraphicsDescriptorSet(g_samplerDescriptorSet.get(), 1);

    auto& drawData = *ImGui::GetDrawData();
    commandList->setViewports(RenderViewport(drawData.DisplayPos.x, drawData.DisplayPos.y, drawData.DisplaySize.x, drawData.DisplaySize.y));

    ImGuiPushConstants pushConstants{};
    pushConstants.displaySize = drawData.DisplaySize;
    pushConstants.inverseDisplaySize = { 1.0f / drawData.DisplaySize.x, 1.0f / drawData.DisplaySize.y };
    commandList->setGraphicsPushConstants(0, &pushConstants);

    size_t pushConstantRangeMin = ~0;
    size_t pushConstantRangeMax = 0;

    auto setPushConstants = [&](void* destination, const void* source, size_t size)
        {
            bool dirty = memcmp(destination, source, size) != 0;

            memcpy(destination, source, size);

            if (dirty)
            {
                size_t offset = reinterpret_cast<size_t>(destination) - reinterpret_cast<size_t>(&pushConstants);
                pushConstantRangeMin = std::min(pushConstantRangeMin, offset);
                pushConstantRangeMax = std::max(pushConstantRangeMax, offset + size);
            }
        };

    ImRect clipRect{};

    for (int i = 0; i < drawData.CmdListsCount; i++)
    {
        auto& drawList = drawData.CmdLists[i];

        auto vertexBufferAllocation = g_uploadAllocators[g_frame].allocate<false>(drawList->VtxBuffer.Data, drawList->VtxBuffer.Size * sizeof(ImDrawVert), alignof(ImDrawVert));
        auto indexBufferAllocation = g_uploadAllocators[g_frame].allocate<false>(drawList->IdxBuffer.Data, drawList->IdxBuffer.Size * sizeof(uint16_t), alignof(uint16_t));

        const RenderVertexBufferView vertexBufferView(vertexBufferAllocation.buffer->at(vertexBufferAllocation.offset), drawList->VtxBuffer.Size * sizeof(ImDrawVert));
        const RenderInputSlot inputSlot(0, sizeof(ImDrawVert));
        commandList->setVertexBuffers(0, &vertexBufferView, 1, &inputSlot);

        const RenderIndexBufferView indexBufferView(indexBufferAllocation.buffer->at(indexBufferAllocation.offset), drawList->IdxBuffer.Size * sizeof(uint16_t), RenderFormat::R16_UINT);
        commandList->setIndexBuffer(&indexBufferView);

        for (int j = 0; j < drawList->CmdBuffer.Size; j++)
        {
            auto& drawCmd = drawList->CmdBuffer[j];
            if (drawCmd.UserCallback != nullptr)
            {
                auto callbackData = reinterpret_cast<const ImGuiCallbackData*>(drawCmd.UserCallbackData);

                switch (static_cast<ImGuiCallback>(reinterpret_cast<size_t>(drawCmd.UserCallback)))
                {
                case ImGuiCallback::SetGradient:
                    setPushConstants(&pushConstants.boundsMin, &callbackData->setGradient, sizeof(callbackData->setGradient));
                    break;       
                case ImGuiCallback::SetShaderModifier:
                    setPushConstants(&pushConstants.shaderModifier, &callbackData->setShaderModifier, sizeof(callbackData->setShaderModifier));
                    break;
                case ImGuiCallback::SetOrigin:
                    setPushConstants(&pushConstants.origin, &callbackData->setOrigin, sizeof(callbackData->setOrigin));
                    break;
                case ImGuiCallback::SetScale:
                    setPushConstants(&pushConstants.scale, &callbackData->setScale, sizeof(callbackData->setScale));
                    break;       
                case ImGuiCallback::SetMarqueeFade:
                    setPushConstants(&pushConstants.boundsMin, &callbackData->setMarqueeFade, sizeof(callbackData->setMarqueeFade));
                    break;
                case ImGuiCallback::SetOutline:
                    setPushConstants(&pushConstants.outline, &callbackData->setOutline, sizeof(callbackData->setOutline));
                    break;
                case ImGuiCallback::SetProceduralOrigin:
                    setPushConstants(&pushConstants.proceduralOrigin, &callbackData->setProceduralOrigin, sizeof(callbackData->setProceduralOrigin));
                    break;
                case ImGuiCallback::SetAdditive:
                {
                    auto pipelineToSet = callbackData->setAdditive.enabled ? g_imAdditivePipeline.get() : g_imPipeline.get();
                    if (pipeline != pipelineToSet)
                    {
                        commandList->setPipeline(pipelineToSet);
                        pipeline = pipelineToSet;
                    }
                    break;
                }
                default:
                    assert(false && "Unknown ImGui callback type.");
                    break;
                }
            }
            else
            {
                if (drawCmd.ClipRect.z <= drawCmd.ClipRect.x || drawCmd.ClipRect.w <= drawCmd.ClipRect.y)
                    continue;

                auto texture = reinterpret_cast<GuestTexture*>(drawCmd.TextureId);
                uint32_t descriptorIndex = TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D;
                if (texture != nullptr)
                {
                    if (texture->layout != RenderTextureLayout::SHADER_READ)
                    {
                        commandList->barriers(RenderBarrierStage::GRAPHICS | RenderBarrierStage::COPY,
                            RenderTextureBarrier(texture->texture, RenderTextureLayout::SHADER_READ));

                        texture->layout = RenderTextureLayout::SHADER_READ;
                    }

                    descriptorIndex = texture->descriptorIndex;

                    if (texture == g_imFontTexture.get())
                        descriptorIndex |= 0x80000000;

                    setPushConstants(&pushConstants.texture2DDescriptorIndex, &descriptorIndex, sizeof(descriptorIndex));
                }

                if (pushConstantRangeMin < pushConstantRangeMax)
                {
                    commandList->setGraphicsPushConstants(0, reinterpret_cast<const uint8_t*>(&pushConstants) + pushConstantRangeMin, pushConstantRangeMin, pushConstantRangeMax - pushConstantRangeMin);
                    pushConstantRangeMin = ~0;
                    pushConstantRangeMax = 0;
                }

                if (memcmp(&clipRect, &drawCmd.ClipRect, sizeof(clipRect)) != 0)
                {
                    commandList->setScissors(RenderRect(int32_t(drawCmd.ClipRect.x), int32_t(drawCmd.ClipRect.y), int32_t(drawCmd.ClipRect.z), int32_t(drawCmd.ClipRect.w)));
                    clipRect = drawCmd.ClipRect;
                }

                commandList->drawIndexedInstanced(drawCmd.ElemCount, 1, drawCmd.IdxOffset, drawCmd.VtxOffset, 0);
            }
        }
    }
}

// We have to check for this to properly handle the following situation:
// 1. Wait on swap chain.
// 2. Create loading thread.
// 3. Loading thread also waits on swap chain.
// 4. Loading thread presents and quits.
// 5. After the loading thread quits, application also presents.
static bool g_pendingWaitOnSwapChain = true;

void Video::WaitOnSwapChain()
{
    if (g_pendingWaitOnSwapChain)
    {
        if (g_swapChainValid)
        {
            g_presentWaitProfiler.Begin();
            g_swapChain->wait();
            g_presentWaitProfiler.End();
        }

        g_pendingWaitOnSwapChain = false;
    }
}

void Video::OnAndroidResume()
{
    // Called from app tick unpause (after nativeResume / surface resume).
    // Android may have invalidated the ANativeWindow-backed surface during background.
    // Invalidate so CheckSwapChain will recreate on next present.
    // This fixes black screen on resume while audio continues independently.
    g_swapChainValid = false;
    g_pendingWaitOnSwapChain = true;
#if defined(__ANDROID__)
    g_androidSwapchainRetryAfterMs = 0;
#endif
    // Note: actual recreate happens in CheckSwapChain() via g_swapChain->resize()
}

static bool g_shouldPrecompilePipelines;
static std::atomic<bool> g_executedCommandList;

#if defined(UNLEASHED_RECOMP_IOS)
static bool IOSFrameDiagnosticsEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("UNLEASHED_IOS_FRAME_DIAGNOSTICS");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

static bool IOSPassDiagnosticsEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("UNLEASHED_IOS_GPU_PASS_DIAGNOSTICS");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

static void IOSLabelNextPass(const char* label)
{
    if (IOSPassDiagnosticsEnabled())
        plume::setMetalPassDiagnosticLabel(g_commandLists[g_frame].get(), label);
}

// UNLEASHED_IOS_RESOLVE_DIAGNOSTICS=1: classify resolve copies by what forced them
// and whether the destination texture is bound before it is resolved again.
enum : uint8_t { IOSResolveNone, IOSResolveClear, IOSResolveDraw, IOSResolvePresent, IOSResolveTriggerCount };
static const char* const g_iosResolveTriggerNames[] = { "none", "clear", "draw", "present" };
static uint8_t g_iosResolveTrigger = IOSResolveNone;
static struct { uint32_t copies, read, unread; uint64_t pixels; } g_iosResolveStats[IOSResolveTriggerCount][2];

static bool IOSResolveDiagnosticsEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("UNLEASHED_IOS_RESOLVE_DIAGNOSTICS");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

static bool IOSResolveIsLarge(const GuestTexture* texture)
{
    return uint64_t(texture->width) * texture->height >= 1000000;
}

static void IOSResolveCopied(GuestTexture* texture)
{
    if (!IOSResolveDiagnosticsEnabled())
        return;
    auto& stats = g_iosResolveStats[g_iosResolveTrigger][IOSResolveIsLarge(texture)];
    ++stats.copies;
    stats.pixels += uint64_t(texture->width) * texture->height;
    texture->iosResolveTrigger = g_iosResolveTrigger;
}

// read=true when the texture is bound for sampling, false when a new resolve replaces it.
static void IOSResolveConsumed(GuestTexture* texture, bool read)
{
    if (!IOSResolveDiagnosticsEnabled() || texture == nullptr || texture->iosResolveTrigger == IOSResolveNone)
        return;
    auto& stats = g_iosResolveStats[texture->iosResolveTrigger][IOSResolveIsLarge(texture)];
    ++(read ? stats.read : stats.unread);
    static uint32_t reports;
    if (!read && IOSResolveIsLarge(texture) && reports++ < 12)
        LOGFN("Resolve copy replaced unread: trigger={}, texture={:X}, size={}x{}, format={}",
            g_iosResolveTriggerNames[texture->iosResolveTrigger], uintptr_t(texture), texture->width, texture->height, int(texture->format));
    texture->iosResolveTrigger = IOSResolveNone;
}

// Textures stay bound across draws, so also count a read when a draw samples a bound slot.
static void IOSResolveCheckBoundTextures();

// Video → Skip Unused Copies (UNLEASHED_IOS_SKIP_UNREAD_RESOLVES=0/1): the end-of-frame flush copies
// every pending resolve, but some textures are always resolved again before anything
// samples them. After a streak of such frames their flush copy is skipped. If a skipped
// texture is ever sampled, it is logged and always copied from then on.
static constexpr uint8_t IOS_UNREAD_RESOLVE_STREAK = 30;
enum : uint8_t { IOSFrameEndNone, IOSFrameEndCopied, IOSFrameEndSkipped };
static uint32_t g_iosSkippedResolves;
static uint64_t g_iosSkippedResolvePixels;

// A resolve target's kind is its format plus whether it covers the full render
// resolution, which identifies it across stages and resolution scales (small targets
// of the same format, e.g. bloom, are sampled every frame). A kind whose resolve was
// ever sampled after a frame-end copy or skip is never skipped again; the set
// persists in the user directory.
static std::bitset<size_t(RenderFormat::MAX) * 2> g_iosSampledResolveFormats;
static std::once_flag g_iosSampledResolveFormatsLoaded;

static size_t IOSResolveKind(const GuestTexture* texture)
{
    const auto renderWidth = uint32_t(round(Video::s_viewportWidth * Config::ResolutionScale));
    const auto renderHeight = uint32_t(round(Video::s_viewportHeight * Config::ResolutionScale));
    const bool fullResolution = std::abs(int32_t(texture->width) - int32_t(renderWidth)) <= 1 &&
        std::abs(int32_t(texture->height) - int32_t(renderHeight)) <= 1;
    return size_t(texture->format) * 2 + fullResolution;
}

static std::filesystem::path IOSSampledResolveFormatsPath()
{
    return GetUserPath() / "ios_sampled_resolve_kinds.txt";
}

static void IOSLoadSampledResolveFormats()
{
    std::call_once(g_iosSampledResolveFormatsLoaded, [] {
        std::ifstream file(IOSSampledResolveFormatsPath());
        uint32_t format;
        while (file >> format)
        {
            if (format < g_iosSampledResolveFormats.size())
                g_iosSampledResolveFormats.set(format);
        }
    });
}

static void IOSRememberSampledResolveFormat(const GuestTexture* texture)
{
    const size_t kind = IOSResolveKind(texture);
    if (g_iosSampledResolveFormats.test(kind))
        return;
    g_iosSampledResolveFormats.set(kind);
    LOGFN("Resolve kind sampled after a frame-end resolve: format {}, {} ({}x{}); never skipping it.",
        int(texture->format), (kind & 1) ? "full render resolution" : "other size", texture->width, texture->height);
    std::ofstream file(IOSSampledResolveFormatsPath());
    for (size_t i = 0; i < g_iosSampledResolveFormats.size(); i++)
    {
        if (g_iosSampledResolveFormats.test(i))
            file << i << '\n';
    }
}

static bool IOSSkipUnreadResolvesEnabled()
{
    // Graphics setting, applied live; the launch environment overrides it for A/B runs.
    static const int forced = [] {
        const char* value = std::getenv("UNLEASHED_IOS_SKIP_UNREAD_RESOLVES");
        return value == nullptr ? -1 : int(value[0] == '1');
    }();
    return forced < 0 ? bool(Config::SkipUnusedCopies) : forced == 1;
}

static void IOSFrameEndResolveOutcome(GuestTexture* texture, bool read, int slot = -1)
{
    if (!IOSSkipUnreadResolvesEnabled() || texture == nullptr || texture->iosFrameEndResolve == IOSFrameEndNone)
        return;
    if (texture->iosFrameEndResolve == IOSFrameEndCopied)
    {
        texture->iosUnreadResolveStreak = read ? 0 : uint8_t(std::min(255, texture->iosUnreadResolveStreak + 1));
        // Learn the kind before any skip happens when a copied texture is sampled.
        if (read)
            IOSRememberSampledResolveFormat(texture);
    }
    else if (read)
    {
        texture->iosAlwaysResolve = true;
        texture->iosUnreadResolveStreak = 0;
        IOSRememberSampledResolveFormat(texture);
        // At SetTexture time the pipeline may still be the previous draw's.
        LOGFN("Skipped resolve was sampled; always copying this kind from now on (format {}, texture {:X}, {}x{}). Slot {} ({}), pipeline: {}|{}",
            int(texture->format), uintptr_t(texture), texture->width, texture->height, slot, slot < 0 ? "SetTexture" : "bound at draw",
            g_pipelineState.vertexShader ? g_pipelineState.vertexShader->name : "-",
            g_pipelineState.pixelShader ? g_pipelineState.pixelShader->name : "-");
    }
    texture->iosFrameEndResolve = IOSFrameEndNone;
}

// The draw that is about to be flushed (diagnostics for draw-triggered resolve copies).
static struct { const char* kind; uint32_t primitiveType, count; const uint8_t* vertices; uint32_t stride; } g_iosCurrentDraw{ "?", 0, 0, nullptr, 0 };

// Video → Reuse Render Targets (UNLEASHED_IOS_SWAP_RESOLVES=0/1): when a draw is about to overwrite a
// surface whose resolve into a texture is still pending, the port normally copies
// the surface into the texture first. If that draw provably rewrites every pixel of
// the surface without reading it (opaque full-viewport quad, no blending, depth
// test or alpha test), the texture can take the surface's memory instead and the
// surface continue in the texture's old memory: no copy.
static uint32_t g_iosSwappedResolves;
static uint64_t g_iosSwappedResolvePixels;

static bool IOSSwapResolvesEnabled()
{
    // Graphics setting, applied live; the launch environment overrides it for A/B runs.
    static const int forced = [] {
        const char* value = std::getenv("UNLEASHED_IOS_SWAP_RESOLVES");
        return value == nullptr ? -1 : int(value[0] == '1');
    }();
    return forced < 0 ? bool(Config::ReuseRenderTargets) : forced == 1;
}

static float IOSLoadGuestFloat(const uint8_t* data)
{
    uint32_t value;
    std::memcpy(&value, data, sizeof(value));
    return std::bit_cast<float>(ByteSwap(value));
}

// A 4-vertex strip whose positions are exactly the corners of the viewport, either in
// clip space or in pixels. Position is assumed to be the first element of the vertex.
static bool IOSDrawCoversSurface(const GuestSurface* surface)
{
    if (std::strcmp(g_iosCurrentDraw.kind, "DrawUP") != 0 || g_iosCurrentDraw.primitiveType != D3DPT_TRIANGLESTRIP ||
        g_iosCurrentDraw.count != 4 || g_iosCurrentDraw.vertices == nullptr || g_iosCurrentDraw.stride < 8)
        return false;
    float x[4], y[4];
    for (uint32_t i = 0; i < 4; i++)
    {
        x[i] = IOSLoadGuestFloat(g_iosCurrentDraw.vertices + i * g_iosCurrentDraw.stride);
        y[i] = IOSLoadGuestFloat(g_iosCurrentDraw.vertices + i * g_iosCurrentDraw.stride + 4);
    }
    const float minX = *std::min_element(x, x + 4), maxX = *std::max_element(x, x + 4);
    const float minY = *std::min_element(y, y + 4), maxY = *std::max_element(y, y + 4);
    // Guest quads carry the D3D9 half-pixel offset (shifted by up to one pixel, e.g.
    // x in [-1.001, 0.999]; [-2, 0] for a 1x1 target), which the translated vertex
    // shaders compensate. Require the full 2.0 clip-space span within a pixel of [-1, 1].
    const float pixelX = 2.0f / float(surface->width), pixelY = 2.0f / float(surface->height);
    const bool clipSpace = maxX - minX >= 2.0f - 1e-4f && maxY - minY >= 2.0f - 1e-4f &&
        std::abs(minX + 1.0f) <= pixelX * 1.01f && std::abs(maxY - 1.0f) <= pixelY * 1.01f &&
        std::abs(maxX - 1.0f) <= pixelX * 1.01f && std::abs(minY + 1.0f) <= pixelY * 1.01f;
    const bool pixelSpace = minX <= 0.5f && maxX >= float(surface->width) - 0.5f && minY <= 0.5f && maxY >= float(surface->height) - 0.5f;
    if (!clipSpace && !pixelSpace)
        return false;
    uint32_t corners = 0;
    for (uint32_t i = 0; i < 4; i++)
    {
        if ((x[i] != minX && x[i] != maxX) || (y[i] != minY && y[i] != maxY))
            return false;
        corners |= 1u << ((x[i] == maxX) * 2 + (y[i] == maxY));
    }
    return corners == 0xF;
}

static bool IOSCanSwapResolve(const GuestSurface* surface, const GuestTexture* texture)
{
    return surface->sampleCount == RenderSampleCount::COUNT_1 && surface->format != RenderFormat::D32_FLOAT &&
        texture->type == ResourceType::Texture && texture->viewDimension == RenderTextureViewDimension::TEXTURE_2D &&
        texture->depth == 1 && texture->iosMipLevels == 1 && texture->iosRenderTargetUsage && texture->iosPlainView &&
        texture->patchedTexture == nullptr && texture->recreatedCubeMapTexture == nullptr &&
        texture->width == surface->width && texture->height == surface->height && texture->format == surface->format &&
        texture->texture != surface->texture &&
        !g_pipelineState.alphaBlendEnable && g_pipelineState.colorWriteEnable == uint32_t(RenderColorWriteEnable::ALL) &&
        (!g_pipelineState.zEnable || g_pipelineState.zFunc == RenderComparisonFunction::ALWAYS) &&
        (g_pipelineState.specConstants & (SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE)) == 0 &&
        g_viewport.x == 0.0f && g_viewport.y == 0.0f &&
        g_viewport.width >= float(surface->width) && g_viewport.height >= float(surface->height) &&
        (!g_scissorTestEnable || (g_scissorRect.left <= 0 && g_scissorRect.top <= 0 &&
            g_scissorRect.right >= int32_t(surface->width) && g_scissorRect.bottom >= int32_t(surface->height))) &&
        IOSDrawCoversSurface(surface);
}

static void SetTextureInRenderThread(uint32_t index, GuestTexture* texture);
static void AddBarrier(GuestBaseTexture* texture, RenderTextureLayout layout);

static void IOSTrySwapResolve(GuestSurface* surface)
{
    if (!IOSSwapResolvesEnabled() || surface == nullptr || surface->destinationTextures.size() != 1)
        return;
    GuestTexture* texture = *surface->destinationTextures.begin();
    if (!IOSCanSwapResolve(surface, texture))
        return;

    // The texture keeps the surface's rendered memory; the surface renders on into the
    // texture's previous memory, which the draw overwrites completely.
    std::swap(surface->textureHolder, texture->textureHolder);
    std::swap(surface->texture, texture->texture);
    std::swap(surface->textureView, texture->textureView);
    std::swap(surface->layout, texture->layout);
    // Bindless descriptors are read when the GPU executes, so earlier draws of this
    // frame must keep seeing the old slots. Give both objects new slots instead and
    // free the old ones after this frame's GPU work completes.
    g_iosRetiredDescriptors[g_frame].push_back(surface->descriptorIndex);
    g_iosRetiredDescriptors[g_frame].push_back(texture->descriptorIndex);
    surface->descriptorIndex = g_textureDescriptorAllocator.allocate();
    texture->descriptorIndex = g_textureDescriptorAllocator.allocate();
    g_textureDescriptorSet->setTexture(surface->descriptorIndex, surface->texture, RenderTextureLayout::SHADER_READ, surface->textureView.get());
    g_textureDescriptorSet->setTexture(texture->descriptorIndex, texture->texture, RenderTextureLayout::SHADER_READ, texture->textureView.get());
    texture->framebuffer.reset(); // Its copy target was the old texture.

    surface->destinationTextures.erase(texture);
    texture->sourceSurface = nullptr;
    IOSResolveCopied(texture); // Same outcome as a copy for the diagnostics.

    // Same as after a copy: slots that sampled the surface point back at the texture.
    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        if (g_textures[i] == texture)
            SetTextureInRenderThread(i, texture);
    }

    AddBarrier(texture, RenderTextureLayout::SHADER_READ);
    g_dirtyStates.renderTargetAndDepthStencil = true;
    ++g_iosSwappedResolves;
    g_iosSwappedResolvePixels += uint64_t(surface->width) * surface->height;
}

static void IOSResolveCheckBoundTextures()
{
    const bool diagnostics = IOSResolveDiagnosticsEnabled();
    if (!diagnostics && !IOSSkipUnreadResolvesEnabled())
        return;
    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        if (diagnostics)
            IOSResolveConsumed(g_textures[i], true);
        IOSFrameEndResolveOutcome(g_textures[i], true, int(i));
    }
}

// For each copy-on-write resolve of a color surface, describe whether the draw that
// forces it would overwrite the whole surface without reading its old contents.
static ankerl::unordered_dense::map<std::string, uint32_t> g_iosDrawResolveKinds;

static void IOSDescribeDrawResolve(const GuestSurface* surface)
{
    if (!IOSResolveDiagnosticsEnabled() || surface == nullptr || surface->destinationTextures.empty() || surface->format == RenderFormat::D32_FLOAT)
        return;
    const GuestTexture* texture = *surface->destinationTextures.begin();
    const bool viewportFull = g_viewport.x == 0.0f && g_viewport.y == 0.0f &&
        g_viewport.width >= float(surface->width) && g_viewport.height >= float(surface->height);
    const bool alphaTest = (g_pipelineState.specConstants & (SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE)) != 0;
    const std::string key = fmt::format(
        "{}x{} f{} dests={} destRT={} destMips={} sameShape={} | {}|{} | blend={} write={:X} z={}/{} alphaTest={} viewportFull={} scissor={} | {} type={} count={}",
        surface->width, surface->height, int(surface->format), surface->destinationTextures.size(), texture->iosRenderTargetUsage,
        texture->iosMipLevels, texture->width == surface->width && texture->height == surface->height && texture->format == surface->format,
        g_pipelineState.vertexShader ? g_pipelineState.vertexShader->name : "-", g_pipelineState.pixelShader ? g_pipelineState.pixelShader->name : "-",
        g_pipelineState.alphaBlendEnable, g_pipelineState.colorWriteEnable, g_pipelineState.zEnable, int(g_pipelineState.zFunc),
        alphaTest, viewportFull, g_scissorTestEnable, g_iosCurrentDraw.kind, g_iosCurrentDraw.primitiveType, g_iosCurrentDraw.count);
    ++g_iosDrawResolveKinds[key];
    static uint32_t vertexReports;
    if (g_iosCurrentDraw.vertices != nullptr && g_iosCurrentDraw.count <= 4 && vertexReports++ < 24)
    {
        std::string vertices;
        for (uint32_t i = 0; i < g_iosCurrentDraw.count; i++)
        {
            const uint8_t* vertex = g_iosCurrentDraw.vertices + i * g_iosCurrentDraw.stride;
            vertices += fmt::format(" ({:.3f},{:.3f},{:.3f},{:.3f})", IOSLoadGuestFloat(vertex), IOSLoadGuestFloat(vertex + 4),
                g_iosCurrentDraw.stride >= 12 ? IOSLoadGuestFloat(vertex + 8) : 0.0f, g_iosCurrentDraw.stride >= 16 ? IOSLoadGuestFloat(vertex + 12) : 0.0f);
        }
        LOGFN("Draw-triggered resolve vertices: {}x{} {}|{} stride={} covers={} canSwap={} viewport=({},{} {}x{}):{}",
            surface->width, surface->height, g_pipelineState.vertexShader ? g_pipelineState.vertexShader->name : "-",
            g_pipelineState.pixelShader ? g_pipelineState.pixelShader->name : "-", g_iosCurrentDraw.stride,
            IOSDrawCoversSurface(surface), IOSCanSwapResolve(surface, texture), g_viewport.x, g_viewport.y, g_viewport.width, g_viewport.height, vertices);
    }
}

static void IOSReportResolveStats()
{
    static uint32_t frames;
    if (!IOSResolveDiagnosticsEnabled() || ++frames % 120 != 0)
        return;
    for (uint8_t trigger = 1; trigger < IOSResolveTriggerCount; trigger++)
    {
        for (int large = 1; large >= 0; large--)
        {
            auto& stats = g_iosResolveStats[trigger][large];
            LOGFN("Resolve copies: trigger={}, {}: copies/frame={:.2f}, Mpx/frame={:.2f}, readBeforeReplaced={}, replacedUnread={}",
                g_iosResolveTriggerNames[trigger], large ? "large" : "small", stats.copies / 120.0, stats.pixels / 120.0 / 1e6, stats.read, stats.unread);
            stats = {};
        }
    }
    for (const auto& [key, count] : g_iosDrawResolveKinds)
        LOGFN("Draw-triggered resolve: {:.2f}/frame: {}", count / 120.0, key);
    g_iosDrawResolveKinds.clear();
}

// Only two aggregate records per 120 frames; no per-frame disk I/O.
#include <os/signpost.h>
#include <bitset>

struct IOSFrameTimingSample
{
    using Clock = std::chrono::steady_clock;
    bool enabled = IOSFrameDiagnosticsEnabled();
    Clock::time_point begin{}, last{};
    double phases[4]{};

    IOSFrameTimingSample()
    {
        if (enabled) begin = last = Clock::now();
    }

    void Mark(size_t phase)
    {
        if (!enabled) return;
        const auto now = Clock::now();
        phases[phase] = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
    }

    ~IOSFrameTimingSample()
    {
        if (!enabled) return;
        static thread_local Clock::time_point previous{};
        static thread_local uint32_t count = 0, late = 0;
        static thread_local double intervalSum = 0.0, intervalMax = 0.0;
        static thread_local double peak[4]{};
        const double interval = std::chrono::duration<double, std::milli>(begin - previous).count();
        previous = begin;
        if (interval > 1000.0 || interval <= 0.0) return;
        if (interval > 50.0)
        {
            // Point of interest for Instruments, so a hitch can be found exactly in a trace.
            static os_log_t pointsOfInterest = os_log_create("io.github.unleashedrecomp", OS_LOG_CATEGORY_POINTS_OF_INTEREST);
            os_signpost_event_emit(pointsOfInterest, OS_SIGNPOST_ID_EXCLUSIVE, "LongFrame", "%.1f ms", interval);
            LOGFN("iOS long frame: {:.1f} ms (present phases: render-thread wait {:.1f}, fence {:.1f}, acquire {:.1f}, limiter {:.1f} ms)",
                interval, phases[0], phases[1], phases[2], phases[3]);
        }
        ++count;
        intervalSum += interval;
        intervalMax = std::max(intervalMax, interval);
        late += interval > 1.25 * (1000.0 / std::max(1, int(Config::FPS)));
        for (size_t i = 0; i < 4; ++i) peak[i] = std::max(peak[i], phases[i]);
        if (count < 120) return;
        LOGFN("iOS CPU frame timing: n={}, mean={:.3f} ms, max={:.3f} ms, late={}, submitMax={:.3f} ms, fenceMax={:.3f} ms, acquireMax={:.3f} ms, limiterMax={:.3f} ms",
            count, intervalSum / count, intervalMax, late, peak[0], peak[1], peak[2], peak[3]);
        count = late = 0; intervalSum = intervalMax = 0.0;
        for (double& value : peak) value = 0.0;
    }
};
#endif

void Video::Present() 
{
    // Feeds the Android hang-watchdog (no-op elsewhere): if these stop, the log.txt
    // timestamp of the last ping marks when the app froze.
    os::logger::Heartbeat();

#if defined(UNLEASHED_RECOMP_IOS)
    app_lifecycle::GetExecutionGate().WaitUntilActive();
    // Keep presentation at the configured cadence rather than the next 60 Hz
    // refresh. The independent software timer alone can alternate 16/50 ms.
    static const bool usePresentDuration = [] {
        const char* value = std::getenv("UNLEASHED_IOS_PRESENT_DURATION");
        return value == nullptr || value[0] != '0';
    }();
    // iOS never tears, so VSync off only selects the timer. Below 60 FPS that
    // timer drifts against refreshes too; keep it for higher caps only.
    const bool useMetalPacing = g_backend == Backend::METAL && usePresentDuration && (IsVSyncEnabled() || Config::FPS < 60) &&
        Config::FPS >= FPS_MIN && Config::FPS < FPS_MAX;
    plume::setMetalPresentMinimumDuration(useMetalPacing ? 1.0 / Config::FPS : 0.0);
    IOSFrameTimingSample frameTiming;
#endif
    static std::atomic<uint32_t> s_presentLogCount = 0;
    const uint32_t presentLogIndex = s_presentLogCount.fetch_add(1);
    const bool logPresent = presentLogIndex < 4;

    if (logPresent)
        LOGFN("Video::Present begin - index: {}, frame: {}, swapChainValid: {}", presentLogIndex, g_frame, g_swapChainValid);

    g_readyForCommands = false;

    RenderCommand cmd;
    cmd.type = RenderCommandType::ExecutePendingStretchRectCommands;
    g_renderQueue.enqueue(cmd);

    DrawImGui();

    cmd.type = RenderCommandType::ExecuteCommandList;
    g_renderQueue.enqueue(cmd);

    // All the shaders are available at this point. We can precompile embedded PSOs then.
    if (g_shouldPrecompilePipelines)
    {
        EnqueuePipelineTask(PipelineTaskType::PrecompilePipelines, {});
        g_shouldPrecompilePipelines = false;
    }

    if (logPresent)
        LOGFN("Video::Present waiting for command list - index: {}", presentLogIndex);

    g_executedCommandList.wait(false);
    g_executedCommandList = false;

#if defined(UNLEASHED_RECOMP_IOS)
    frameTiming.Mark(0);
#endif

    if (logPresent)
        LOGFN("Video::Present command list completed - index: {}", presentLogIndex);

    if (g_swapChainValid)
    {
        if (g_pendingWaitOnSwapChain)
        {
            g_presentWaitProfiler.Begin();
            g_swapChain->wait(); // Never gonna happen outside loading threads as explained above.
            g_presentWaitProfiler.End();
        }

        RenderCommandSemaphore* signalSemaphores[] = { g_renderSemaphores[g_frame].get() };
        if (logPresent)
            LOGFN("Video::Present presenting swapchain - index: {}, backBufferIndex: {}", presentLogIndex, g_backBufferIndex);

        g_swapChainValid = g_swapChain->present(g_backBufferIndex, signalSemaphores, std::size(signalSemaphores));
    }

    g_pendingWaitOnSwapChain = true;

    g_frame = g_nextFrame;
    g_nextFrame = (g_frame + 1) % NUM_FRAMES;

    if (g_commandListStates[g_frame])
    {
        g_frameFenceProfiler.Begin();
        g_queue->waitForCommandFence(g_commandFences[g_frame].get());
        g_frameFenceProfiler.End();
        g_commandListStates[g_frame] = false;

#if defined(__ANDROID__)
        if (g_plumeDeviceLost)
            ExitOnAndroidDeviceLost();
#endif

        // Update the GPU profiler with the results from the timestamps of the frame.
        g_queryPools[g_frame]->queryResults();
        const uint64_t *frameTimestamps = g_queryPools[g_frame]->getResults();
        g_gpuFrameProfiler.Set(double(frameTimestamps[1] - frameTimestamps[0]) / 1000000.0);
    }

    g_dirtyStates = DirtyStates(true);
#if defined(UNLEASHED_RECOMP_IOS)
    frameTiming.Mark(1);
#endif
    g_uploadAllocators[g_frame].reset();
    g_intermediaryUploadAllocator.reset();
    g_triangleFanIndexData.reset();
    g_quadIndexData.reset();

    CheckSwapChain();

#if defined(UNLEASHED_RECOMP_IOS)
    frameTiming.Mark(2);
#endif

    cmd.type = RenderCommandType::BeginCommandList;
    g_renderQueue.enqueue(cmd);

    bool useSoftwareLimiter = Config::FPS >= FPS_MIN && Config::FPS < FPS_MAX;
#if defined(UNLEASHED_RECOMP_IOS)
    // CAMetalLayer's drawable pool throttles the producer to the actual display
    // cadence. A second unsynchronized timer periodically misses a refresh.
    useSoftwareLimiter = useSoftwareLimiter && !useMetalPacing;
#endif
    if (useSoftwareLimiter)
    {
        using namespace std::chrono_literals;

        static std::chrono::steady_clock::time_point s_next;

        auto now = std::chrono::steady_clock::now();

        if (now < s_next)
        {
            std::this_thread::sleep_for(std::chrono::floor<std::chrono::milliseconds>(s_next - now - 2ms));

            while ((now = std::chrono::steady_clock::now()) < s_next)
                std::this_thread::yield();
        }
        else
        {
            s_next = now;
        }

        std::chrono::nanoseconds interval = 1000000000ns / Config::FPS;
#if defined(UNLEASHED_RECOMP_IOS)
        // iPhone panels run slightly off nominal (60.09 Hz measured on iPhone 15).
        // An exact 1/FPS timer slowly falls behind and skips one refresh every
        // few seconds; match the cap to whole real refreshes when it is close.
        if (const double refresh = GetIOSDisplayRefreshInterval(); refresh > 0.0)
        {
            const double target = 1.0 / Config::FPS;
            const double refreshes = std::max(1.0, std::round(target / refresh));
            if (std::abs(refreshes * refresh - target) <= target * 0.02)
                interval = std::chrono::nanoseconds(int64_t(refreshes * refresh * 1e9));
        }
#endif
        s_next += interval;
    }

    g_presentProfiler.Reset();

#if defined(UNLEASHED_RECOMP_IOS)
    frameTiming.Mark(3);
#endif

    if (logPresent)
        LOGFN("Video::Present end - index: {}, frame: {}, swapChainValid: {}", presentLogIndex, g_frame, g_swapChainValid);
}

void Video::StartPipelinePrecompilation()
{
    g_shouldPrecompilePipelines = true;
}

static void SetRootDescriptor(const UploadAllocation& allocation, size_t index)
{
    auto& commandList = g_commandLists[g_frame];

    if (g_backend != Backend::D3D12)
        commandList->setGraphicsPushConstants(0, &allocation.deviceAddress, 8 * index, 8);
    else
        commandList->setGraphicsRootDescriptor(allocation.buffer->at(allocation.offset), index);
}

static void ProcExecuteCommandList(const RenderCommand& cmd)
{
#if defined(UNLEASHED_RECOMP_IOS)
    ProcUpscaleIOSBackBuffer(); // Also covers frames with no host UI draw commands.
#endif
    if (g_swapChainValid)
    {
        auto swapChainTexture = g_swapChain->getTexture(g_backBufferIndex);
        if (g_backBuffer->texture == g_intermediaryBackBufferTexture.get()
#if defined(UNLEASHED_RECOMP_IOS)
            || (g_iosMetalFXScaled && g_backBuffer->texture == g_iosMetalFXOutput.get())
#endif
        )
        {
            struct
            {
                float gammaR;
                float gammaG;
                float gammaB;
                uint32_t textureDescriptorIndex;

                int32_t viewportOffsetX;
                int32_t viewportOffsetY;
                int32_t viewportWidth;
                int32_t viewportHeight;
            } constants;

            if (Config::XboxColorCorrection)
            {
                constants.gammaR = 1.2f;
                constants.gammaG = 1.17f;
                constants.gammaB = 0.98f;
            }
            else
            {
                constants.gammaR = 1.0f;
                constants.gammaG = 1.0f;
                constants.gammaB = 1.0f;
            }

            float offset = (Config::Brightness - 0.5f) * 1.2f;

            constants.gammaR = 1.0f / std::clamp(constants.gammaR + offset, 0.1f, 4.0f);
            constants.gammaG = 1.0f / std::clamp(constants.gammaG + offset, 0.1f, 4.0f);
            constants.gammaB = 1.0f / std::clamp(constants.gammaB + offset, 0.1f, 4.0f);
            constants.textureDescriptorIndex = g_intermediaryBackBufferTextureDescriptorIndex;
#if defined(UNLEASHED_RECOMP_IOS)
            if (g_iosMetalFXScaled) constants.textureDescriptorIndex = g_iosMetalFXOutputDescriptor;
#endif

            constants.viewportOffsetX = (int32_t(g_swapChain->getWidth()) - int32_t(Video::s_viewportWidth)) / 2;
            constants.viewportOffsetY = (int32_t(g_swapChain->getHeight()) - int32_t(Video::s_viewportHeight)) / 2;
            constants.viewportWidth = Video::s_viewportWidth;
            constants.viewportHeight = Video::s_viewportHeight;

            auto &framebuffer = g_backBuffer->framebuffers[swapChainTexture];
            if (!framebuffer)
            {
                RenderFramebufferDesc desc;
                desc.colorAttachments = const_cast<const RenderTexture **>(&swapChainTexture);
                desc.colorAttachmentsCount = 1;
                framebuffer = g_device->createFramebuffer(desc);
            }

            RenderTextureBarrier srcBarriers[] =
            {
                RenderTextureBarrier(g_backBuffer->texture, RenderTextureLayout::SHADER_READ),
                RenderTextureBarrier(swapChainTexture, RenderTextureLayout::COLOR_WRITE)
            };

            auto &commandList = g_commandLists[g_frame];
            commandList->barriers(RenderBarrierStage::GRAPHICS, srcBarriers, std::size(srcBarriers));
            commandList->setGraphicsPipelineLayout(g_pipelineLayout.get());
            commandList->setPipeline(g_gammaCorrectionPipeline.get());
#if defined(UNLEASHED_RECOMP_IOS)
            IOSLabelNextPass("host|GammaCopy");
#endif
            commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 0);
            SetRootDescriptor(g_uploadAllocators[g_frame].allocate<false>(&constants, sizeof(constants), 0x100), 2);
            commandList->setFramebuffer(framebuffer.get());
            commandList->setViewports(RenderViewport(0.0f, 0.0f, g_swapChain->getWidth(), g_swapChain->getHeight()));
            commandList->setScissors(RenderRect(0, 0, g_swapChain->getWidth(), g_swapChain->getHeight()));
            commandList->drawInstanced(6, 1, 0, 0);
            commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::PRESENT));
        }
        else
        {
            AddBarrier(g_backBuffer, RenderTextureLayout::PRESENT);
            FlushBarriers();
        }
    }

    auto &commandList = g_commandLists[g_frame];
    commandList->writeTimestamp(g_queryPools[g_frame].get(), 1);
    commandList->end();

    g_statDrawCalls = g_plumeDrawCalls;
    g_statRenderPassBegins = g_plumeRenderPassBegins;
    g_statPipelineBinds = g_plumePipelineBinds;
    g_statBarrierCalls = g_plumeBarrierCalls;
    g_statFramebufferSets = g_plumeFramebufferSets;
    g_plumeDrawCalls = 0;
    g_plumeRenderPassBegins = 0;
    g_plumePipelineBinds = 0;
    g_plumeBarrierCalls = 0;
    g_plumeFramebufferSets = 0;

    if (g_swapChainValid)
    {
        const RenderCommandList *commandLists[] = { commandList.get() };
        RenderCommandSemaphore *waitSemaphores[] = { g_acquireSemaphores[g_frame].get() };
        RenderCommandSemaphore *signalSemaphores[] = { g_renderSemaphores[g_frame].get() };

        g_queue->executeCommandLists(
            commandLists, std::size(commandLists),
            waitSemaphores, std::size(waitSemaphores),
            signalSemaphores, std::size(signalSemaphores),
            g_commandFences[g_frame].get());
    }
    else
    {
        g_queue->executeCommandLists(commandList.get(), g_commandFences[g_frame].get());
    }

    g_commandListStates[g_frame] = true;

    g_executedCommandList = true;
    g_executedCommandList.notify_one();
}

static void ProcBeginCommandList(const RenderCommand& cmd)
{
    DestructTempResources();
    BeginCommandList();
}

static void ProcTrimRuntimeCaches(const RenderCommand&)
{
    const size_t pipelinesBefore = g_pipelines.size();

    // The current frame may already reference these pipelines. Metal command
    // buffers use unretained references, and Vulkan also requires pipeline
    // lifetime to cover submitted commands. Retire through the same frame
    // fence mechanism as textures and buffers instead of freeing them here.
    auto& retiredPipelines = g_tempPipelines[g_frame];
    retiredPipelines.reserve(retiredPipelines.size() + pipelinesBefore);
    for (auto& [hash, pipeline] : g_pipelines)
        retiredPipelines.emplace_back(std::move(pipeline));

    g_pipelines.clear();
    g_dirtyStates.pipelineState = true;

#ifdef PSO_CACHING
    {
        std::lock_guard lock(g_pipelineCacheMutex);
        g_pipelineStatesToCache.clear();
    }
#endif

    for (size_t i = 0; i < g_shaderCacheEntryCount; i++)
    {
        auto* shader = g_shaderCacheEntries[i].guestShader;
        if (shader == nullptr)
            continue;

        std::lock_guard lock(shader->mutex);
        shader->linkedShaders.clear();
#ifdef UNLEASHED_RECOMP_D3D12
        shader->shaderBlobs.clear();
#endif
    }

    os::logger::Log(fmt::format("TrimRuntimeCaches - pipelines: {} -> 0", pipelinesBefore));
}

void Video::QueueTrimRuntimeCaches()
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::TrimRuntimeCaches;
    g_renderQueue.enqueue(cmd);
}

static GuestSurface* GetBackBuffer() 
{
    g_backBuffer->AddRef();
    return g_backBuffer;
}

void Video::ComputeViewportDimensions()
{
    uint32_t width = g_swapChain->getWidth();
    uint32_t height = g_swapChain->getHeight();
    float aspectRatio = float(width) / float(height);

    switch (Config::AspectRatio)
    {
    case EAspectRatio::Wide:
    {
        if (aspectRatio > WIDE_ASPECT_RATIO)
        {
            s_viewportWidth = height * 16 / 9;
            s_viewportHeight = height;
        }
        else
        {
            s_viewportWidth = width;
            s_viewportHeight = width * 9 / 16;
        }

        break;
    }

    case EAspectRatio::Narrow:
    case EAspectRatio::OriginalNarrow:
    {
        if (aspectRatio > NARROW_ASPECT_RATIO)
        {
            s_viewportWidth = height * 4 / 3;
            s_viewportHeight = height;
        }
        else
        {
            s_viewportWidth = width;
            s_viewportHeight = width * 3 / 4;
        }

        break;
    }

    default:
        s_viewportWidth = width;
        s_viewportHeight = height;
        break;
    }

    AspectRatioPatches::ComputeOffsets();
}

static RenderFormat ConvertFormat(uint32_t format)
{
    switch (format)
    {
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_A16B16G16R16F_2:
        return RenderFormat::R16G16B16A16_FLOAT;
    case D3DFMT_A8B8G8R8:
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        return RenderFormat::R8G8B8A8_UNORM;
    case D3DFMT_D24FS8:
    case D3DFMT_D24S8:
        return RenderFormat::D32_FLOAT;
    case D3DFMT_G16R16F:
    case D3DFMT_G16R16F_2:
        return RenderFormat::R16G16_FLOAT;
    case D3DFMT_INDEX16:
        return RenderFormat::R16_UINT;
    case D3DFMT_INDEX32:
        return RenderFormat::R32_UINT;
    case D3DFMT_L8:
    case D3DFMT_L8_2:
        return RenderFormat::R8_UNORM;
    default:
        assert(false && "Unknown format");
        return RenderFormat::R16G16B16A16_FLOAT;
    }
}

static GuestTexture* CreateTexture(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels, uint32_t usage, uint32_t format, uint32_t pool, uint32_t type) 
{
    const auto texture = g_userHeap.AllocPhysical<GuestTexture>(type == 17 ? ResourceType::VolumeTexture : ResourceType::Texture);

    RenderTextureDesc desc;
    desc.dimension = texture->type == ResourceType::VolumeTexture ? RenderTextureDimension::TEXTURE_3D : RenderTextureDimension::TEXTURE_2D;
    desc.width = width;
    desc.height = height;
    desc.depth = depth;
    desc.mipLevels = levels;
    desc.arraySize = 1;
    desc.format = ConvertFormat(format);

    if (desc.format == RenderFormat::D32_FLOAT)
        desc.flags = RenderTextureFlag::DEPTH_TARGET;
    else if (usage != 0)
        desc.flags = RenderTextureFlag::RENDER_TARGET;
    else
        desc.flags = RenderTextureFlag::NONE;

    texture->textureHolder = g_device->createTexture(desc);
    texture->texture = texture->textureHolder.get();
#if defined(UNLEASHED_RECOMP_IOS)
    texture->iosRenderTargetUsage = (desc.flags & RenderTextureFlag::RENDER_TARGET) != 0;
    texture->iosMipLevels = levels;
#endif

    RenderTextureViewDesc viewDesc;
    viewDesc.format = desc.format;
    viewDesc.dimension = texture->type == ResourceType::VolumeTexture ? RenderTextureViewDimension::TEXTURE_3D : RenderTextureViewDimension::TEXTURE_2D;
    viewDesc.mipLevels = levels;

    switch (format)
    {
    case D3DFMT_D24FS8:
    case D3DFMT_D24S8:
    case D3DFMT_L8:
    case D3DFMT_L8_2:
        viewDesc.componentMapping = RenderComponentMapping(RenderSwizzle::R, RenderSwizzle::R, RenderSwizzle::R, RenderSwizzle::ONE);
        break;

    case D3DFMT_X8R8G8B8:
        viewDesc.componentMapping = RenderComponentMapping(RenderSwizzle::G, RenderSwizzle::B, RenderSwizzle::A, RenderSwizzle::ONE);
        break;
    }
#if defined(UNLEASHED_RECOMP_IOS)
    texture->iosPlainView = format != D3DFMT_D24FS8 && format != D3DFMT_D24S8 && format != D3DFMT_L8 &&
        format != D3DFMT_L8_2 && format != D3DFMT_X8R8G8B8;
#endif

    texture->textureView = texture->texture->createTextureView(viewDesc);

    texture->width = width;
    texture->height = height;
    texture->depth = depth;
    texture->format = desc.format;
    texture->viewDimension = viewDesc.dimension;
    texture->descriptorIndex = g_textureDescriptorAllocator.allocate();

    g_textureDescriptorSet->setTexture(texture->descriptorIndex, texture->texture, RenderTextureLayout::SHADER_READ, texture->textureView.get());
   
#ifdef _DEBUG 
    texture->texture->setName(fmt::format("Texture {:X}", g_memory.MapVirtual(texture)));
#endif

    return texture;
}

static RenderHeapType GetBufferHeapType()
{
    return g_capabilities.gpuUploadHeap ? RenderHeapType::GPU_UPLOAD : RenderHeapType::DEFAULT;
}

static GuestBuffer* CreateVertexBuffer(uint32_t length) 
{
    auto buffer = g_userHeap.AllocPhysical<GuestBuffer>(ResourceType::VertexBuffer);
    buffer->buffer = g_device->createBuffer(RenderBufferDesc::VertexBuffer(length, GetBufferHeapType(), RenderBufferFlag::INDEX));
    buffer->dataSize = length;
#ifdef _DEBUG 
    buffer->buffer->setName(fmt::format("Vertex Buffer {:X}", g_memory.MapVirtual(buffer)));
#endif
    return buffer;
}

static GuestBuffer* CreateIndexBuffer(uint32_t length, uint32_t, uint32_t format)
{
    auto buffer = g_userHeap.AllocPhysical<GuestBuffer>(ResourceType::IndexBuffer);
    buffer->buffer = g_device->createBuffer(RenderBufferDesc::IndexBuffer(length, GetBufferHeapType()));
    buffer->dataSize = length;
    buffer->format = ConvertFormat(format);
    buffer->guestFormat = format;
#ifdef _DEBUG 
    buffer->buffer->setName(fmt::format("Index Buffer {:X}", g_memory.MapVirtual(buffer)));
#endif
    return buffer;
}

static GuestSurface* CreateSurface(uint32_t width, uint32_t height, uint32_t format, uint32_t multiSample) 
{
    RenderTextureDesc desc;
    desc.dimension = RenderTextureDimension::TEXTURE_2D;
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arraySize = 1;
    desc.multisampling.sampleCount = multiSample != 0 && Config::AntiAliasing != EAntiAliasing::None ? int32_t(Config::AntiAliasing.Value) : RenderSampleCount::COUNT_1;
    desc.format = ConvertFormat(format);
    desc.flags = desc.format == RenderFormat::D32_FLOAT ? RenderTextureFlag::DEPTH_TARGET : RenderTextureFlag::RENDER_TARGET;

    auto surface = g_userHeap.AllocPhysical<GuestSurface>(desc.format == RenderFormat::D32_FLOAT ? 
        ResourceType::DepthStencil : ResourceType::RenderTarget);

    surface->textureHolder = g_device->createTexture(desc);
    surface->texture = surface->textureHolder.get();
    surface->width = width;
    surface->height = height;
    surface->format = desc.format;
    surface->guestFormat = format;
    surface->sampleCount = desc.multisampling.sampleCount;

    RenderTextureViewDesc viewDesc;
    viewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
    viewDesc.format = desc.format;
    viewDesc.mipLevels = 1;
    surface->textureView = surface->textureHolder->createTextureView(viewDesc);
    surface->descriptorIndex = g_textureDescriptorAllocator.allocate();
    g_textureDescriptorSet->setTexture(surface->descriptorIndex, surface->textureHolder.get(), RenderTextureLayout::SHADER_READ, surface->textureView.get());

#ifdef _DEBUG 
    surface->texture->setName(fmt::format("{} {:X}", desc.flags & RenderTextureFlag::RENDER_TARGET ? "Render Target" : "Depth Stencil", g_memory.MapVirtual(surface)));
#endif

    return surface;
}

static void FlushViewport()
{
    auto& commandList = g_commandLists[g_frame];

#if defined(UNLEASHED_RECOMP_IOS)
    // Scale only rasterization into the smaller final target. Guest surface
    // dimensions and UI projection constants still describe the native viewport.
    const bool scaledBackBuffer = g_iosMetalFXActive && !g_iosMetalFXScaled && g_renderTarget == g_backBuffer;
    const float scaleX = scaledBackBuffer ? float(g_intermediaryBackBufferTextureWidth) / Video::s_viewportWidth : 1.0f;
    const float scaleY = scaledBackBuffer ? float(g_intermediaryBackBufferTextureHeight) / Video::s_viewportHeight : 1.0f;
#endif

    if (g_dirtyStates.viewport)
    {
        auto viewport = g_viewport;
#if defined(UNLEASHED_RECOMP_IOS)
        viewport.x *= scaleX; viewport.y *= scaleY;
        viewport.width *= scaleX; viewport.height *= scaleY;
        // Shift scene geometry/depth together; post-process full-screen quads and
        // native UI remain unjittered. Shadows have their own, unmatched target.
        if (g_iosTemporalActive && !g_iosMetalFXScaled && g_renderTarget && g_depthStencil &&
            g_renderTarget != g_backBuffer && g_pipelineState.zEnable &&
            g_renderTarget->width == g_intermediaryBackBufferTextureWidth &&
            g_renderTarget->height == g_intermediaryBackBufferTextureHeight &&
            g_depthStencil->width == g_intermediaryBackBufferTextureWidth &&
            g_depthStencil->height == g_intermediaryBackBufferTextureHeight)
        {
            viewport.x -= g_iosJitterX; viewport.y -= g_iosJitterY;
        }
#endif

        if (viewport.minDepth > viewport.maxDepth)
            std::swap(viewport.minDepth, viewport.maxDepth);

        commandList->setViewports(viewport);

        g_dirtyStates.viewport = false;
    }

    if (g_dirtyStates.scissorRect)
    {
        auto scissorRect = g_scissorTestEnable ? g_scissorRect : RenderRect(
            g_viewport.x,
            g_viewport.y,
            g_viewport.x + g_viewport.width,
            g_viewport.y + g_viewport.height);

#if defined(UNLEASHED_RECOMP_IOS)
        scissorRect.left = int32_t(std::floor(scissorRect.left * scaleX));
        scissorRect.top = int32_t(std::floor(scissorRect.top * scaleY));
        scissorRect.right = int32_t(std::ceil(scissorRect.right * scaleX));
        scissorRect.bottom = int32_t(std::ceil(scissorRect.bottom * scaleY));
#endif
        commandList->setScissors(scissorRect);

        g_dirtyStates.scissorRect = false;
    }
}

static void StretchRect(GuestDevice* device, uint32_t flags, uint32_t, GuestTexture* texture)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::StretchRect;
    cmd.stretchRect.flags = flags;
    cmd.stretchRect.texture = texture;
    g_renderQueue.enqueue(cmd);
}

static void SetTextureInRenderThread(uint32_t index, GuestTexture* texture);
static void SetSurface(uint32_t index, GuestSurface* surface);

static void ProcStretchRect(const RenderCommand& cmd)
{
    const auto& args = cmd.stretchRect;

    const bool isDepthStencil = (args.flags & 0x4) != 0;
    const auto surface = isDepthStencil ? g_depthStencil : g_renderTarget;

#if defined(UNLEASHED_RECOMP_IOS)
    IOSResolveConsumed(args.texture, false);
    IOSFrameEndResolveOutcome(args.texture, false);
#endif

    // Erase previous pending command so it doesn't cause the texture to be overriden.
    if (args.texture->sourceSurface != nullptr)
        args.texture->sourceSurface->destinationTextures.erase(args.texture);

    args.texture->sourceSurface = surface;
    surface->destinationTextures.emplace(args.texture);

    // If the texture is assigned to any slots, set it again. This'll also push the barrier.
    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        if (g_textures[i] == args.texture)
        {
            // Set the original texture for MSAA textures as they always get resolved.
            if (surface->sampleCount != RenderSampleCount::COUNT_1)
            {
                SetTextureInRenderThread(i, args.texture);
                g_pendingMsaaResolves.emplace(surface);
            }
            else
            {
                SetSurface(i, surface);
            }
        }
    }

    // Remember to clear later.
    g_pendingSurfaceCopies.emplace(surface);
}

static void SetDefaultViewport(GuestDevice* device, GuestSurface* surface)
{
    if (surface != nullptr)
    {
        RenderCommand cmd;
        cmd.type = RenderCommandType::SetViewport;
        cmd.setViewport.x = 0.0f;
        cmd.setViewport.y = 0.0f;
        cmd.setViewport.width = float(surface->width);
        cmd.setViewport.height = float(surface->height);
        cmd.setViewport.minDepth = 0.0f;
        cmd.setViewport.maxDepth = 1.0f;
        g_renderQueue.enqueue(cmd);

        device->viewport.x = 0.0f;
        device->viewport.y = 0.0f;
        device->viewport.width = float(surface->width);
        device->viewport.height = float(surface->height);
        device->viewport.minZ = 0.0f;
        device->viewport.maxZ = 1.0f;
    }
}

static void SetRenderTarget(GuestDevice* device, uint32_t index, GuestSurface* renderTarget) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetRenderTarget;
    cmd.setRenderTarget.renderTarget = renderTarget;
    g_renderQueue.enqueue(cmd);

    SetDefaultViewport(device, renderTarget);
}

static void ProcSetRenderTarget(const RenderCommand& cmd)
{
    const auto& args = cmd.setRenderTarget;
#if defined(UNLEASHED_RECOMP_IOS)
    if (g_iosMetalFXActive && g_renderTarget != args.renderTarget)
    {
        g_dirtyStates.viewport = true;
        g_dirtyStates.scissorRect = true;
    }
#endif

    SetDirtyValue(g_dirtyStates.renderTargetAndDepthStencil, g_renderTarget, args.renderTarget);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.renderTargetFormat, args.renderTarget != nullptr ? args.renderTarget->format : RenderFormat::UNKNOWN);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.sampleCount, args.renderTarget != nullptr ? args.renderTarget->sampleCount : RenderSampleCount::COUNT_1);

    // When alpha to coverage is enabled, update the alpha test mode as it's dependent on sample count.
    SetAlphaTestMode((g_pipelineState.specConstants & (SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE)) != 0);
}

static void SetDepthStencilSurface(GuestDevice* device, GuestSurface* depthStencil) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetDepthStencilSurface;
    cmd.setDepthStencilSurface.depthStencil = depthStencil;
    g_renderQueue.enqueue(cmd);

    SetDefaultViewport(device, depthStencil);
}

static void ProcSetDepthStencilSurface(const RenderCommand& cmd)
{
    const auto& args = cmd.setDepthStencilSurface;

    SetDirtyValue(g_dirtyStates.renderTargetAndDepthStencil, g_depthStencil, args.depthStencil);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthStencilFormat, args.depthStencil != nullptr ? args.depthStencil->format : RenderFormat::UNKNOWN);
}

static bool PopulateBarriersForStretchRect(GuestSurface* renderTarget, GuestSurface* depthStencil)
{
    bool addedAny = false;

    for (const auto surface : { renderTarget, depthStencil })
    {
        if (surface != nullptr && !surface->destinationTextures.empty())
        {
            const bool multiSampling = surface->sampleCount != RenderSampleCount::COUNT_1;

            RenderTextureLayout srcLayout;
            RenderTextureLayout dstLayout;
            bool shaderResolve = true;

            if (multiSampling)
            {
                if (surface->format != RenderFormat::D32_FLOAT || g_capabilities.resolveModes)
                {
                    srcLayout = RenderTextureLayout::RESOLVE_SOURCE;
                    dstLayout = RenderTextureLayout::RESOLVE_DEST;
                    shaderResolve = false;
                }
            }
            
            if (shaderResolve)
            {
                srcLayout = RenderTextureLayout::SHADER_READ;
                dstLayout = (surface->format == RenderFormat::D32_FLOAT ? RenderTextureLayout::DEPTH_WRITE : RenderTextureLayout::COLOR_WRITE);
            }

            AddBarrier(surface, srcLayout);

            for (const auto texture : surface->destinationTextures)
                AddBarrier(texture, dstLayout);

            addedAny = true;
        }
    }

    return addedAny;
}

static void ExecutePendingStretchRectCommands(GuestSurface* renderTarget, GuestSurface* depthStencil)
{
    auto& commandList = g_commandLists[g_frame];

    for (const auto surface : { renderTarget, depthStencil })
    {
        if (surface != nullptr && !surface->destinationTextures.empty())
        {
            const bool multiSampling = surface->sampleCount != RenderSampleCount::COUNT_1;

            for (const auto texture : surface->destinationTextures)
            {
                bool shaderResolve = true;
#if defined(UNLEASHED_RECOMP_IOS)
                IOSResolveCopied(texture);
                if (IOSSkipUnreadResolvesEnabled() && g_iosResolveTrigger == IOSResolvePresent)
                    texture->iosFrameEndResolve = IOSFrameEndCopied;
#endif

                if (multiSampling)
                {
                    if (surface->format != RenderFormat::D32_FLOAT || g_capabilities.resolveModes)
                    {
                        if (surface->format == RenderFormat::D32_FLOAT)
                            commandList->resolveTextureRegion(texture->texture, 0, 0, surface->texture, nullptr, RenderResolveMode::MIN);
                        else
                            commandList->resolveTexture(texture->texture, surface->texture);

                        shaderResolve = false;
                    }
                }

                if (shaderResolve)
                {
                    RenderPipeline* pipeline = nullptr;

                    if (multiSampling)
                    {
                        uint32_t pipelineIndex = 0;

                        switch (surface->sampleCount)
                        {
                        case RenderSampleCount::COUNT_2:
                            pipelineIndex = 0;
                            break;
                        case RenderSampleCount::COUNT_4:
                            pipelineIndex = 1;
                            break;
                        case RenderSampleCount::COUNT_8:
                            pipelineIndex = 2;
                            break;
                        default:
                            assert(false && "Unsupported MSAA sample count");
                            break;
                        }

                        if (texture->format == RenderFormat::D32_FLOAT)
                        {
                            pipeline = g_resolveMsaaDepthPipelines[pipelineIndex].get();
                        }
                        else
                        {
                            auto& resolveMsaaColorPipeline = g_resolveMsaaColorPipelines[surface->format][pipelineIndex];
                            if (resolveMsaaColorPipeline == nullptr)
                            {
                                RenderGraphicsPipelineDesc desc;
                                desc.pipelineLayout = g_pipelineLayout.get();
                                desc.vertexShader = g_copyShader.get();
                                desc.pixelShader = g_resolveMsaaColorShaders[pipelineIndex].get();
                                desc.renderTargetFormat[0] = texture->format;
                                desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
                                desc.renderTargetCount = 1;
                                resolveMsaaColorPipeline = g_device->createGraphicsPipeline(desc);
                            }

                            pipeline = resolveMsaaColorPipeline.get();
                        }
                    }
                    else
                    {
                        if (texture->format == RenderFormat::D32_FLOAT)
                        {
                            pipeline = g_copyDepthPipeline.get();
                        }
                        else
                        {
                            auto& copyColorPipeline = g_copyColorPipelines[surface->format];
                            if (copyColorPipeline == nullptr)
                            {
                                RenderGraphicsPipelineDesc desc;
                                desc.pipelineLayout = g_pipelineLayout.get();
                                desc.vertexShader = g_copyShader.get();
                                desc.pixelShader = g_copyColorShader.get();
                                desc.renderTargetFormat[0] = texture->format;
                                desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
                                desc.renderTargetCount = 1;
                                copyColorPipeline = g_device->createGraphicsPipeline(desc);
                            }

                            pipeline = copyColorPipeline.get();
                        }
                    }

                    if (texture->framebuffer == nullptr)
                    {
                        if (texture->format == RenderFormat::D32_FLOAT)
                        {
                            RenderFramebufferDesc desc;
                            desc.depthAttachment = texture->texture;
                            texture->framebuffer = g_device->createFramebuffer(desc);
                        }
                        else
                        {
                            RenderFramebufferDesc desc;
                            desc.colorAttachments = const_cast<const RenderTexture**>(&texture->texture);
                            desc.colorAttachmentsCount = 1;
                            texture->framebuffer = g_device->createFramebuffer(desc);
                        }
                    }

                    if (g_framebuffer != texture->framebuffer.get())
                    {
                        commandList->setFramebuffer(texture->framebuffer.get());
                        g_framebuffer = texture->framebuffer.get();
                    }

                    commandList->setPipeline(pipeline);
#if defined(UNLEASHED_RECOMP_IOS)
                    if (IOSPassDiagnosticsEnabled())
                    {
                        const std::string label = fmt::format("host|{}@{}", pipeline == g_copyDepthPipeline.get() ? "ResolveCopyDepth" : "ResolveCopyColor",
                            g_iosResolveTriggerNames[g_iosResolveTrigger]);
                        IOSLabelNextPass(label.c_str());
                    }
#endif
                    commandList->setViewports(RenderViewport(0.0f, 0.0f, float(texture->width), float(texture->height), 0.0f, 1.0f));
                    commandList->setScissors(RenderRect(0, 0, texture->width, texture->height));
                    commandList->setGraphicsPushConstants(0, &surface->descriptorIndex, 0, sizeof(uint32_t));
                    commandList->drawInstanced(6, 1, 0, 0);

                    g_dirtyStates.renderTargetAndDepthStencil = true;
                    g_dirtyStates.viewport = true;
                    g_dirtyStates.pipelineState = true;
                    g_dirtyStates.scissorRect = true;

                    if (g_backend != Backend::D3D12)
                    {
                        g_dirtyStates.vertexShaderConstants = true; // The push constant call invalidates vertex shader constants.
                        g_dirtyStates.depthBias = true; // Static depth bias in copy pipeline invalidates dynamic depth bias.
                    }
                }

                texture->sourceSurface = nullptr;

                // Check if any texture slots had this texture assigned, and make it point back at the original texture.
                for (uint32_t i = 0; i < std::size(g_textures); i++)
                {
                    if (g_textures[i] == texture)
                        SetTextureInRenderThread(i, texture);
                }
            }

            surface->destinationTextures.clear();
        }
    }
}

static void ProcExecutePendingStretchRectCommands(const RenderCommand& cmd)
{
#if defined(UNLEASHED_RECOMP_IOS)
    if (IOSSkipUnreadResolvesEnabled())
    {
        IOSLoadSampledResolveFormats();
        std::vector<GuestTexture*> skipped;
        for (const auto surface : g_pendingSurfaceCopies)
        {
            if (surface->format == RenderFormat::D32_FLOAT)
                continue;
            skipped.clear();
            for (const auto texture : surface->destinationTextures)
            {
                if (!texture->iosAlwaysResolve && texture->iosUnreadResolveStreak >= IOS_UNREAD_RESOLVE_STREAK &&
                    !g_iosSampledResolveFormats.test(IOSResolveKind(texture)))
                    skipped.push_back(texture);
            }
            for (const auto texture : skipped)
            {
                surface->destinationTextures.erase(texture);
                texture->sourceSurface = nullptr;
                texture->iosFrameEndResolve = IOSFrameEndSkipped;
                ++g_iosSkippedResolves;
                g_iosSkippedResolvePixels += uint64_t(texture->width) * texture->height;
                // Same as after a copy: slots that sampled the surface point back at the texture.
                for (uint32_t i = 0; i < std::size(g_textures); i++)
                {
                    if (g_textures[i] == texture)
                        SetTextureInRenderThread(i, texture);
                }
            }
        }
        static uint32_t frames;
        if (++frames % 120 == 0)
        {
            LOGFN("Skipped unread end-of-frame resolves: {:.2f}/frame, {:.2f} Mpx/frame",
                g_iosSkippedResolves / 120.0, g_iosSkippedResolvePixels / 120.0 / 1e6);
            g_iosSkippedResolves = 0;
            g_iosSkippedResolvePixels = 0;
        }
    }
#endif

    bool foundAny = false;

    for (const auto surface : g_pendingSurfaceCopies)
    {
        // Depth stencil textures in this game are guaranteed to be transient.
        if (surface->format != RenderFormat::D32_FLOAT)
            foundAny |= PopulateBarriersForStretchRect(surface, nullptr);
    }

    if (foundAny)
    {
        FlushBarriers();
#if defined(UNLEASHED_RECOMP_IOS)
        g_iosResolveTrigger = IOSResolvePresent;
#endif

        for (const auto surface : g_pendingSurfaceCopies)
        {
            if (surface->format != RenderFormat::D32_FLOAT)
                ExecutePendingStretchRectCommands(surface, nullptr);

            for (const auto texture : surface->destinationTextures)
                texture->sourceSurface = nullptr;

            surface->destinationTextures.clear();
        }
    }

    g_pendingSurfaceCopies.clear();
    g_pendingMsaaResolves.clear();
#if defined(UNLEASHED_RECOMP_IOS)
    IOSReportResolveStats();
    if (IOSSwapResolvesEnabled())
    {
        static uint32_t frames;
        if (++frames % 120 == 0)
        {
            LOGFN("Swapped resolves instead of copying: {:.2f}/frame, {:.2f} Mpx/frame",
                g_iosSwappedResolves / 120.0, g_iosSwappedResolvePixels / 120.0 / 1e6);
            g_iosSwappedResolves = 0;
            g_iosSwappedResolvePixels = 0;
        }
    }
#endif
}

static void SetFramebuffer(GuestSurface* renderTarget, GuestSurface* depthStencil, bool settingForClear)
{
    if (settingForClear || g_dirtyStates.renderTargetAndDepthStencil)
    {
        GuestSurface* framebufferContainer = nullptr;
        RenderTexture* framebufferKey = nullptr;

        if (renderTarget != nullptr && depthStencil != nullptr)
        {
            framebufferContainer = depthStencil; // Backbuffer texture changes per frame so we can't use the depth stencil as the key.
            framebufferKey = renderTarget->texture;
        }
        else if (renderTarget != nullptr && depthStencil == nullptr)
        {
            framebufferContainer = renderTarget;
            framebufferKey = renderTarget->texture; // Backbuffer texture changes per frame so we can't assume nullptr for it.
        }
        else if (renderTarget == nullptr && depthStencil != nullptr)
        {
            framebufferContainer = depthStencil;
            framebufferKey = nullptr;
        }

        auto& commandList = g_commandLists[g_frame];

        if (framebufferContainer != nullptr)
        {
            auto& framebuffer = framebufferContainer->framebuffers[framebufferKey];

            if (framebuffer == nullptr)
            {
                RenderFramebufferDesc desc;

                if (renderTarget != nullptr)
                {
                    desc.colorAttachments = const_cast<const RenderTexture**>(&renderTarget->texture);
                    desc.colorAttachmentsCount = 1;
                }

                if (depthStencil != nullptr)
                    desc.depthAttachment = depthStencil->texture;

                framebuffer = g_device->createFramebuffer(desc);
            }

            if (g_framebuffer != framebuffer.get())
            {
                commandList->setFramebuffer(framebuffer.get());
                g_framebuffer = framebuffer.get();
            }
        }
        else if (g_framebuffer != nullptr)
        {
            commandList->setFramebuffer(nullptr);
            g_framebuffer = nullptr;
        }

        if (g_framebuffer != nullptr)
        {
            SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.halfPixelOffsetX, 1.0f / float(g_framebuffer->getWidth()));
            SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.halfPixelOffsetY, -1.0f / float(g_framebuffer->getHeight()));
        }

        g_dirtyStates.renderTargetAndDepthStencil = settingForClear;
    }
}

static void Clear(GuestDevice* device, uint32_t flags, uint32_t, be<float>* color, double z) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::Clear;
    cmd.clear.flags = flags;
    cmd.clear.color[0] = color[0];
    cmd.clear.color[1] = color[1];
    cmd.clear.color[2] = color[2];
    cmd.clear.color[3] = color[3];
    cmd.clear.z = float(z);
    g_renderQueue.enqueue(cmd);
}

static void ProcClear(const RenderCommand& cmd)
{
    const auto& args = cmd.clear;

    if (PopulateBarriersForStretchRect(g_renderTarget, g_depthStencil))
    {
        FlushBarriers();
#if defined(UNLEASHED_RECOMP_IOS)
        g_iosResolveTrigger = IOSResolveClear;
#endif
        ExecutePendingStretchRectCommands(g_renderTarget, g_depthStencil);
    }

    AddBarrier(g_renderTarget, RenderTextureLayout::COLOR_WRITE);
    AddBarrier(g_depthStencil, RenderTextureLayout::DEPTH_WRITE);
    FlushBarriers();

    bool canClearInOnePass = (g_renderTarget == nullptr) || (g_depthStencil == nullptr) ||
        (g_renderTarget->width == g_depthStencil->width && g_renderTarget->height == g_depthStencil->height);

    if (canClearInOnePass)
        SetFramebuffer(g_renderTarget, g_depthStencil, true);

    auto& commandList = g_commandLists[g_frame];

    if (g_renderTarget != nullptr && (args.flags & D3DCLEAR_TARGET) != 0)
    {
        if (!canClearInOnePass)
            SetFramebuffer(g_renderTarget, nullptr, true);

        commandList->clearColor(0, RenderColor(args.color[0], args.color[1], args.color[2], args.color[3]));
    }

    if (g_depthStencil != nullptr && (args.flags & D3DCLEAR_ZBUFFER) != 0)
    {
        if (!canClearInOnePass)
            SetFramebuffer(nullptr, g_depthStencil, true);

        commandList->clearDepth(true, args.z);
    }
}

static void SetViewport(GuestDevice* device, GuestViewport* viewport)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetViewport;
    cmd.setViewport.x = viewport->x;
    cmd.setViewport.y = viewport->y;
    cmd.setViewport.width = viewport->width;
    cmd.setViewport.height = viewport->height;
    cmd.setViewport.minDepth = viewport->minZ;
    cmd.setViewport.maxDepth = viewport->maxZ;
    g_renderQueue.enqueue(cmd);

    device->viewport.x = float(viewport->x);
    device->viewport.y = float(viewport->y);
    device->viewport.width = float(viewport->width);
    device->viewport.height = float(viewport->height);
    device->viewport.minZ = viewport->minZ;
    device->viewport.maxZ = viewport->maxZ;
}

static void ProcSetViewport(const RenderCommand& cmd)
{
    const auto& args = cmd.setViewport;

    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.x, args.x);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.y, args.y);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.width, args.width);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.height, args.height);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.minDepth, args.minDepth);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.maxDepth, args.maxDepth);
    
    uint32_t specConstants = g_pipelineState.specConstants;
    if (args.minDepth > args.maxDepth)
        specConstants |= SPEC_CONSTANT_REVERSE_Z;
    else 
        specConstants &= ~SPEC_CONSTANT_REVERSE_Z;

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.specConstants, specConstants);

    g_dirtyStates.scissorRect |= g_dirtyStates.viewport;
}

static void SetTexture(GuestDevice* device, uint32_t index, GuestTexture* texture) 
{
    auto isPlayStation = Config::ControllerIcons == EControllerIcons::PlayStation;

    if (Config::ControllerIcons == EControllerIcons::Auto)
        isPlayStation = hid::g_inputDeviceController == hid::EInputDevice::PlayStation;

    if (isPlayStation && texture != nullptr && texture->patchedTexture != nullptr)
        texture = texture->patchedTexture.get();

    RenderCommand cmd;
    cmd.type = RenderCommandType::SetTexture;
    cmd.setTexture.index = index;
    cmd.setTexture.texture = texture;
    g_renderQueue.enqueue(cmd);
}

static void SetTextureInRenderThread(uint32_t index, GuestTexture* texture)
{
    AddBarrier(texture, RenderTextureLayout::SHADER_READ);

    auto viewDimension = texture != nullptr ? texture->viewDimension : RenderTextureViewDimension::UNKNOWN;

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DIndices[index],
        viewDimension == RenderTextureViewDimension::TEXTURE_2D ? texture->descriptorIndex : TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D);

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture3DIndices[index], texture != nullptr &&
        viewDimension == RenderTextureViewDimension::TEXTURE_3D ? texture->descriptorIndex : TEXTURE_DESCRIPTOR_NULL_TEXTURE_3D);

    // Check if there's a cubemap texture we recreated and assign it if it's valid. The shader will pick whichever is correct.
    if (viewDimension == RenderTextureViewDimension::TEXTURE_2D && texture->recreatedCubeMapTexture != nullptr)
    {
        texture = texture->recreatedCubeMapTexture.get();
        AddBarrier(texture, RenderTextureLayout::SHADER_READ);
        viewDimension = texture->viewDimension;
    }

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.textureCubeIndices[index], texture != nullptr &&
        viewDimension == RenderTextureViewDimension::TEXTURE_CUBE ? texture->descriptorIndex : TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE);
}

static void SetSurface(uint32_t index, GuestSurface* surface)
{
    AddBarrier(surface, RenderTextureLayout::SHADER_READ);

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DIndices[index], surface->descriptorIndex);
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture3DIndices[index], uint32_t(TEXTURE_DESCRIPTOR_NULL_TEXTURE_3D));
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.textureCubeIndices[index], uint32_t(TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE));
}

static void ProcSetTexture(const RenderCommand& cmd)
{
    const auto& args = cmd.setTexture;

#if defined(UNLEASHED_RECOMP_IOS)
    IOSResolveConsumed(args.texture, true);
    IOSFrameEndResolveOutcome(args.texture, true);
#endif

    // If a pending copy operation is detected, set the source surface. The indices will be fixed later if flushing is necessary.
    bool shouldSetTexture = true;
    if (args.texture != nullptr && args.texture->sourceSurface != nullptr)
    {
        // MSAA surfaces need to be resolved and cannot be used directly.
        if (args.texture->sourceSurface->sampleCount != RenderSampleCount::COUNT_1)
        {
            g_pendingMsaaResolves.emplace(args.texture->sourceSurface);
        }
        else
        {
            SetSurface(args.index, args.texture->sourceSurface);
            shouldSetTexture = false;
        }
    }
    
    if (shouldSetTexture)
        SetTextureInRenderThread(args.index, args.texture);
    
    g_textures[args.index] = args.texture;
}

static void SetScissorRect(GuestDevice* device, GuestRect* rect)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetScissorRect;
    cmd.setScissorRect.top = rect->top;
    cmd.setScissorRect.left = rect->left;
    cmd.setScissorRect.bottom = rect->bottom;
    cmd.setScissorRect.right = rect->right;
    g_renderQueue.enqueue(cmd);
}

static void ProcSetScissorRect(const RenderCommand& cmd)
{
    const auto& args = cmd.setScissorRect;

    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.top, args.top);
    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.left, args.left);
    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.bottom, args.bottom);
    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.right, args.right);
}

static RenderShader* GetOrLinkShader(GuestShader* guestShader, uint32_t specConstants)
{
    if (g_backend != Backend::D3D12 ||
        guestShader->shaderCacheEntry == nullptr || 
        guestShader->shaderCacheEntry->specConstantsMask == 0)
    {
        std::lock_guard lock(guestShader->mutex);

        if (guestShader->shader == nullptr)
        {
            assert(guestShader->shaderCacheEntry != nullptr);

            switch (g_backend) {
            case Backend::VULKAN:
            {
                auto compressedSpirvData = g_shaderCache.get() + guestShader->shaderCacheEntry->spirvOffset;

                std::vector<uint8_t> decoded(smolv::GetDecodedBufferSize(compressedSpirvData, guestShader->shaderCacheEntry->spirvSize));
                bool result = smolv::Decode(compressedSpirvData, guestShader->shaderCacheEntry->spirvSize, decoded.data(), decoded.size());
                assert(result);

                guestShader->shader = g_device->createShader(decoded.data(), decoded.size(), GUEST_SHADER_ENTRY, RenderShaderFormat::SPIRV);
                break;
            }
            case Backend::D3D12:
            {
                guestShader->shader = g_device->createShader(g_shaderCache.get() + guestShader->shaderCacheEntry->dxilOffset,
                    guestShader->shaderCacheEntry->dxilSize, GUEST_SHADER_ENTRY, RenderShaderFormat::DXIL);
                break;
            }
#ifdef UNLEASHED_RECOMP_METAL
            case Backend::METAL:
            {
                guestShader->shader = g_device->createShader(g_shaderCache.get() + guestShader->shaderCacheEntry->airOffset,
                    guestShader->shaderCacheEntry->airSize, GUEST_SHADER_ENTRY, RenderShaderFormat::METAL);
                break;
            }
#endif
            }

#if defined(_DEBUG) && defined(UNLEASHED_RECOMP_XENOS_METAL)
            guestShader->shader->setName(fmt::format("{}:{:x}", guestShader->shaderCacheEntry->filename, guestShader->shaderCacheEntry->hash));
#endif
        }

        return guestShader->shader.get();
    }

    specConstants &= guestShader->shaderCacheEntry->specConstantsMask;

    RenderShader* shader;
    {
        std::lock_guard lock(guestShader->mutex);
        shader = guestShader->linkedShaders[specConstants].get();
    }

#ifdef UNLEASHED_RECOMP_D3D12
    if (shader == nullptr)
    {
        static Mutex g_compiledSpecConstantLibraryBlobMutex;
        static ankerl::unordered_dense::map<uint32_t, ComPtr<IDxcBlob>> g_compiledSpecConstantLibraryBlobs;

        thread_local ComPtr<IDxcCompiler3> s_dxcCompiler;
        thread_local ComPtr<IDxcLinker> s_dxcLinker;
        thread_local ComPtr<IDxcUtils> s_dxcUtils;

        wchar_t specConstantsLibName[0x100];
        swprintf_s(specConstantsLibName, L"SpecConstants_%d", specConstants);

        ComPtr<IDxcBlob> specConstantLibraryBlob;
        {
            std::lock_guard lock(g_compiledSpecConstantLibraryBlobMutex);
            specConstantLibraryBlob = g_compiledSpecConstantLibraryBlobs[specConstants];
        }

        if (specConstantLibraryBlob == nullptr)
        {
            if (s_dxcCompiler == nullptr)
            {
                HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(s_dxcCompiler.GetAddressOf()));
                assert(SUCCEEDED(hr) && s_dxcCompiler != nullptr);
            }

            char libraryHlsl[0x100];
            sprintf_s(libraryHlsl, "export uint g_SpecConstants() { return %d; }", specConstants);

            DxcBuffer buffer{};
            buffer.Ptr = libraryHlsl;
            buffer.Size = strlen(libraryHlsl);

            const wchar_t* args[1];
            args[0] = L"-T lib_6_3";

            ComPtr<IDxcResult> result;
            HRESULT hr = s_dxcCompiler->Compile(&buffer, args, std::size(args), nullptr, IID_PPV_ARGS(result.GetAddressOf()));
            assert(SUCCEEDED(hr) && result != nullptr);

            hr = result->GetResult(specConstantLibraryBlob.GetAddressOf());
            assert(SUCCEEDED(hr) && specConstantLibraryBlob != nullptr);

            std::lock_guard lock(g_compiledSpecConstantLibraryBlobMutex);
            g_compiledSpecConstantLibraryBlobs.emplace(specConstants, specConstantLibraryBlob);
        }

        if (s_dxcLinker == nullptr)
        {
            HRESULT hr = DxcCreateInstance(CLSID_DxcLinker, IID_PPV_ARGS(s_dxcLinker.GetAddressOf()));
            assert(SUCCEEDED(hr) && s_dxcLinker != nullptr);
        }

        s_dxcLinker->RegisterLibrary(specConstantsLibName, specConstantLibraryBlob.Get());

        wchar_t shaderLibName[0x100];
        swprintf_s(shaderLibName, L"Shader_%d", guestShader->shaderCacheEntry->dxilOffset);

        ComPtr<IDxcBlobEncoding> shaderLibraryBlob;
        {
            std::lock_guard lock(guestShader->mutex);
            shaderLibraryBlob = guestShader->libraryBlob;
        }

        if (shaderLibraryBlob == nullptr)
        {
            if (s_dxcUtils == nullptr)
            {
                HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(s_dxcUtils.GetAddressOf()));
                assert(SUCCEEDED(hr) && s_dxcUtils != nullptr);
            }

            HRESULT hr = s_dxcUtils->CreateBlobFromPinned(
                g_shaderCache.get() + guestShader->shaderCacheEntry->dxilOffset,
                guestShader->shaderCacheEntry->dxilSize,
                DXC_CP_ACP,
                shaderLibraryBlob.GetAddressOf());

            assert(SUCCEEDED(hr) && shaderLibraryBlob != nullptr);

            std::lock_guard lock(guestShader->mutex);
            guestShader->libraryBlob = shaderLibraryBlob;
        }

        s_dxcLinker->RegisterLibrary(shaderLibName, shaderLibraryBlob.Get());

        const wchar_t* libraryNames[] = { specConstantsLibName, shaderLibName };

        ComPtr<IDxcOperationResult> result;
        HRESULT hr = s_dxcLinker->Link(L"" GUEST_SHADER_ENTRY, guestShader->type == ResourceType::VertexShader ? L"vs_6_0" : L"ps_6_0",
            libraryNames, std::size(libraryNames), nullptr, 0, result.GetAddressOf());

        assert(SUCCEEDED(hr) && result != nullptr);

        ComPtr<IDxcBlob> blob;
        hr = result->GetResult(blob.GetAddressOf());
        assert(SUCCEEDED(hr) && blob != nullptr);

        {
            std::lock_guard lock(guestShader->mutex);

            auto& linkedShader = guestShader->linkedShaders[specConstants];
            if (linkedShader == nullptr)
            {
                linkedShader = g_device->createShader(blob->GetBufferPointer(), blob->GetBufferSize(), GUEST_SHADER_ENTRY, RenderShaderFormat::DXIL);
                guestShader->shaderBlobs.push_back(std::move(blob));
            }

            shader = linkedShader.get();

#if defined(_DEBUG) && defined(UNLEASHED_RECOMP_XENOS_METAL)
            shader->setName(fmt::format("{}:{:x}", guestShader->shaderCacheEntry->filename, guestShader->shaderCacheEntry->hash));
#endif
        }        
    }
#endif

    return shader;
}

static void SanitizePipelineState(PipelineState& pipelineState)
{
    if (!pipelineState.zEnable)
    {
        pipelineState.zWriteEnable = false;
        pipelineState.zFunc = RenderComparisonFunction::LESS;
        pipelineState.slopeScaledDepthBias = 0.0f;
        pipelineState.depthBias = 0;
        pipelineState.depthStencilFormat = RenderFormat::UNKNOWN;
    }

    if (pipelineState.slopeScaledDepthBias == 0.0f)
        pipelineState.slopeScaledDepthBias = 0.0f; // Remove sign.

    if (!pipelineState.colorWriteEnable)
    {
        pipelineState.alphaBlendEnable = false;
        pipelineState.renderTargetFormat = RenderFormat::UNKNOWN;
    }

    if (!pipelineState.alphaBlendEnable)
    {
        pipelineState.srcBlend = RenderBlend::ONE;
        pipelineState.destBlend = RenderBlend::ZERO;
        pipelineState.blendOp = RenderBlendOperation::ADD;
        pipelineState.srcBlendAlpha = RenderBlend::ONE;
        pipelineState.destBlendAlpha = RenderBlend::ZERO;
        pipelineState.blendOpAlpha = RenderBlendOperation::ADD;
    }

    for (size_t i = 0; i < 16; i++)
    {
        if (!pipelineState.vertexDeclaration->vertexStreams[i])
            pipelineState.vertexStrides[i] = 0;
    }

    uint32_t specConstantsMask = 0;
    if (pipelineState.vertexShader->shaderCacheEntry != nullptr)
        specConstantsMask |= pipelineState.vertexShader->shaderCacheEntry->specConstantsMask;

    if (pipelineState.pixelShader != nullptr && pipelineState.pixelShader->shaderCacheEntry != nullptr)
        specConstantsMask |= pipelineState.pixelShader->shaderCacheEntry->specConstantsMask;

    pipelineState.specConstants &= specConstantsMask;
}

static std::unique_ptr<RenderPipeline> CreateGraphicsPipeline(const PipelineState& pipelineState)
{
#if defined(UNLEASHED_RECOMP_IOS)
    const auto pipelineStart = IOSFrameDiagnosticsEnabled() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
#ifdef ASYNC_PSO_DEBUG
    ++g_pipelinesCurrentlyCompiling;
#endif

    RenderGraphicsPipelineDesc desc;
    desc.pipelineLayout = g_pipelineLayout.get();
    desc.vertexShader = GetOrLinkShader(pipelineState.vertexShader, pipelineState.specConstants);
    desc.pixelShader = pipelineState.pixelShader != nullptr ? GetOrLinkShader(pipelineState.pixelShader, pipelineState.specConstants) : nullptr;
    desc.depthFunction = pipelineState.zFunc;
    desc.depthEnabled = pipelineState.zEnable;
    desc.depthWriteEnabled = pipelineState.zWriteEnable;
    desc.depthBias = pipelineState.depthBias;
    desc.slopeScaledDepthBias = pipelineState.slopeScaledDepthBias;
    desc.dynamicDepthBiasEnabled = g_capabilities.dynamicDepthBias;
    desc.depthClipEnabled = true;
    desc.primitiveTopology = pipelineState.primitiveTopology;
    desc.cullMode = pipelineState.cullMode;
    desc.renderTargetFormat[0] = pipelineState.renderTargetFormat;
    desc.renderTargetBlend[0].blendEnabled = pipelineState.alphaBlendEnable;
    desc.renderTargetBlend[0].srcBlend = pipelineState.srcBlend;
    desc.renderTargetBlend[0].dstBlend = pipelineState.destBlend;
    desc.renderTargetBlend[0].blendOp = pipelineState.blendOp;
    desc.renderTargetBlend[0].srcBlendAlpha = pipelineState.srcBlendAlpha;
    desc.renderTargetBlend[0].dstBlendAlpha = pipelineState.destBlendAlpha;
    desc.renderTargetBlend[0].blendOpAlpha = pipelineState.blendOpAlpha;
    desc.renderTargetBlend[0].renderTargetWriteMask = pipelineState.colorWriteEnable;
    desc.renderTargetCount = pipelineState.renderTargetFormat != RenderFormat::UNKNOWN ? 1 : 0;
    desc.depthTargetFormat = pipelineState.depthStencilFormat;
    desc.multisampling.sampleCount = pipelineState.sampleCount;
    desc.alphaToCoverageEnabled = pipelineState.enableAlphaToCoverage;
    desc.inputElements = pipelineState.vertexDeclaration->inputElements.get();
    desc.inputElementsCount = pipelineState.vertexDeclaration->inputElementCount;
    
    RenderSpecConstant specConstant{};
    specConstant.value = pipelineState.specConstants;
    
    if (pipelineState.specConstants != 0)
    {
        desc.specConstants = &specConstant;
        desc.specConstantsCount = 1;
    }
    
    RenderInputSlot inputSlots[16]{};
    uint32_t inputSlotIndices[16]{};
    uint32_t inputSlotCount = 0;
    
    for (size_t i = 0; i < pipelineState.vertexDeclaration->inputElementCount; i++)
    {
        auto& inputElement = pipelineState.vertexDeclaration->inputElements[i];
        auto& inputSlotIndex = inputSlotIndices[inputElement.slotIndex];
    
        if (inputSlotIndex == NULL)
            inputSlotIndex = ++inputSlotCount;
    
        auto& inputSlot = inputSlots[inputSlotIndex - 1];
        inputSlot.index = inputElement.slotIndex;
        inputSlot.stride = pipelineState.vertexStrides[inputElement.slotIndex];
    
        if (pipelineState.instancing && inputElement.slotIndex != 0 && inputElement.slotIndex != 15)
            inputSlot.classification = RenderInputSlotClassification::PER_INSTANCE_DATA;
        else
            inputSlot.classification = RenderInputSlotClassification::PER_VERTEX_DATA;
    }
    
    desc.inputSlots = inputSlots;
    desc.inputSlotsCount = inputSlotCount;
    
    auto pipeline = g_device->createGraphicsPipeline(desc);

#if defined(UNLEASHED_RECOMP_IOS)
    if (IOSFrameDiagnosticsEnabled())
    {
        const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pipelineStart).count();
        static std::atomic<uint32_t> reported{0};
        if (elapsed > 10.0 && reported.fetch_add(1) < 128)
            LOGFN("iOS pipeline compile: {:.3f} ms, loading={}, state={:X}", elapsed,
                bool(*SWA::SGlobals::ms_IsLoading), XXH3_64bits(&pipelineState, sizeof(pipelineState)));
    }
#endif

#ifdef ASYNC_PSO_DEBUG
    --g_pipelinesCurrentlyCompiling;
#endif

    return pipeline;
}

static RenderPipeline* CreateGraphicsPipelineInRenderThread(PipelineState pipelineState)
{
    SanitizePipelineState(pipelineState);

    XXH64_hash_t hash = XXH3_64bits(&pipelineState, sizeof(pipelineState));
    auto& pipeline = g_pipelines[hash];
    if (pipeline == nullptr)
    {
        pipeline = CreateGraphicsPipeline(pipelineState);

#ifdef ASYNC_PSO_DEBUG
        bool loading = *SWA::SGlobals::ms_IsLoading;

        if (loading)
            ++g_pipelinesCreatedAsynchronously;
        else
            ++g_pipelinesCreatedInRenderThread;

        pipeline->setName(fmt::format("{} {} {} {:X}", loading ? "ASYNC" : "",
            pipelineState.vertexShader->name, pipelineState.pixelShader != nullptr ? pipelineState.pixelShader->name : "<none>", hash));
        
        if (!loading)
        {
            std::lock_guard lock(g_debugMutex);
            g_pipelineDebugText = fmt::format(
                "PipelineState {:X}:\n"
                "  vertexShader: {}\n"
                "  pixelShader: {}\n"
                "  vertexDeclaration: {:X}\n"
                "  instancing: {}\n"
                "  zEnable: {}\n"
                "  zWriteEnable: {}\n"
                "  srcBlend: {}\n"
                "  destBlend: {}\n"
                "  cullMode: {}\n"
                "  zFunc: {}\n"
                "  alphaBlendEnable: {}\n"
                "  blendOp: {}\n"
                "  slopeScaledDepthBias: {}\n"
                "  depthBias: {}\n"
                "  srcBlendAlpha: {}\n"
                "  destBlendAlpha: {}\n"
                "  blendOpAlpha: {}\n"
                "  colorWriteEnable: {:X}\n"
                "  primitiveTopology: {}\n"
                "  vertexStrides[0]: {}\n"
                "  vertexStrides[1]: {}\n"
                "  vertexStrides[2]: {}\n"
                "  vertexStrides[3]: {}\n"
                "  renderTargetFormat: {}\n"
                "  depthStencilFormat: {}\n"
                "  sampleCount: {}\n"
                "  enableAlphaToCoverage: {}\n"
                "  specConstants: {:X}\n",
                hash,
                pipelineState.vertexShader->name,
                pipelineState.pixelShader != nullptr ? pipelineState.pixelShader->name : "<none>",
                reinterpret_cast<size_t>(pipelineState.vertexDeclaration),
                pipelineState.instancing,
                pipelineState.zEnable,
                pipelineState.zWriteEnable,
                magic_enum::enum_name(pipelineState.srcBlend),
                magic_enum::enum_name(pipelineState.destBlend),
                magic_enum::enum_name(pipelineState.cullMode),
                magic_enum::enum_name(pipelineState.zFunc),
                pipelineState.alphaBlendEnable,
                magic_enum::enum_name(pipelineState.blendOp),
                pipelineState.slopeScaledDepthBias,
                pipelineState.depthBias,
                magic_enum::enum_name(pipelineState.srcBlendAlpha),
                magic_enum::enum_name(pipelineState.destBlendAlpha),
                magic_enum::enum_name(pipelineState.blendOpAlpha),
                pipelineState.colorWriteEnable,
                magic_enum::enum_name(pipelineState.primitiveTopology),
                pipelineState.vertexStrides[0],
                pipelineState.vertexStrides[1],
                pipelineState.vertexStrides[2],
                pipelineState.vertexStrides[3],
                magic_enum::enum_name(pipelineState.renderTargetFormat),
                magic_enum::enum_name(pipelineState.depthStencilFormat),
                pipelineState.sampleCount,
                pipelineState.enableAlphaToCoverage,
                pipelineState.specConstants)
                + g_pipelineDebugText;
        }
#endif

#ifdef PSO_CACHING
        std::lock_guard lock(g_pipelineCacheMutex);
        g_pipelineStatesToCache.emplace(hash, pipelineState);
#endif
    }
    
    return pipeline.get();
}

static RenderTextureAddressMode ConvertTextureAddressMode(size_t value)
{
    switch (value)
    {
    case D3DTADDRESS_WRAP:
        return RenderTextureAddressMode::WRAP;
    case D3DTADDRESS_MIRROR:
        return RenderTextureAddressMode::MIRROR;
    case D3DTADDRESS_CLAMP:
        return RenderTextureAddressMode::CLAMP;
    case D3DTADDRESS_MIRRORONCE:
        return RenderTextureAddressMode::MIRROR_ONCE;
    case D3DTADDRESS_BORDER:
        return RenderTextureAddressMode::BORDER;
    default:
        assert(false && "Unknown texture address mode");
        return RenderTextureAddressMode::UNKNOWN;
    }
}

static RenderFilter ConvertTextureFilter(uint32_t value)
{
    switch (value)
    {
    case D3DTEXF_POINT:
    case D3DTEXF_NONE:
        return RenderFilter::NEAREST;
    case D3DTEXF_LINEAR:
        return RenderFilter::LINEAR;
    default:
        assert(false && "Unknown texture filter");
        return RenderFilter::UNKNOWN;
    }
}

static RenderBorderColor ConvertBorderColor(uint32_t value)
{
    switch (value)
    {
    case 0:
        return RenderBorderColor::TRANSPARENT_BLACK;
    case 1:
        return RenderBorderColor::OPAQUE_WHITE;
    default:
        assert(false && "Unknown border color");
        return RenderBorderColor::UNKNOWN;
    }
}

struct LocalRenderCommandQueue
{
    RenderCommand commands[20];
    uint32_t count = 0;

    RenderCommand& enqueue()
    {
        assert(count < std::size(commands));
        return commands[count++];
    }

    void submit()
    {
        g_renderQueue.enqueue_bulk(commands, count);
    }
};

static void FlushRenderStateForMainThread(GuestDevice* device, LocalRenderCommandQueue& queue)
{
    constexpr size_t BOOL_MASK = 0x100000000000000ull;
    if ((device->dirtyFlags[4].get() & BOOL_MASK) != 0)
    {
        auto& cmd = queue.enqueue();
        cmd.type = RenderCommandType::SetBooleans;
        cmd.setBooleans.booleans = (device->vertexShaderBoolConstants[0].get() & 0xFF) | ((device->pixelShaderBoolConstants[0].get() & 0xFF) << 16);

        device->dirtyFlags[4] = device->dirtyFlags[4].get() & ~BOOL_MASK;
    }

    for (uint32_t i = 0; i < 16; i++)
    {
        const size_t mask = 0x8000000000000000ull >> (i + 32);
        if (device->dirtyFlags[3].get() & mask)
        {
            auto& cmd = queue.enqueue();
            cmd.type = RenderCommandType::SetSamplerState;
            cmd.setSamplerState.index = i;
            cmd.setSamplerState.data0 = device->samplerStates[i].data[0];
            cmd.setSamplerState.data3 = device->samplerStates[i].data[3];
            cmd.setSamplerState.data5 = device->samplerStates[i].data[5];

            device->dirtyFlags[3] = device->dirtyFlags[3].get() & ~mask;
        }
    }

    uint64_t dirtyFlags = device->dirtyFlags[0].get();
    if (dirtyFlags != 0)
    {
        int startRegister = std::countl_zero(dirtyFlags);
        int endRegister = 64 - std::countr_zero(dirtyFlags);

        uint32_t index = startRegister * 16;
        uint32_t size = (endRegister - startRegister) * 64;

        auto& cmd = queue.enqueue();
        cmd.type = RenderCommandType::SetVertexShaderConstants;
        cmd.setVertexShaderConstants.memory = g_intermediaryUploadAllocator.allocate(&device->vertexShaderFloatConstants[index], size);
        cmd.setVertexShaderConstants.index = index;
        cmd.setVertexShaderConstants.size = size;

        device->dirtyFlags[0] = 0;
    }

    dirtyFlags = device->dirtyFlags[1].get();
    if (dirtyFlags != 0)
    {
        int startRegister = std::countl_zero(dirtyFlags);
        int endRegister = std::min(56, 64 - std::countr_zero(dirtyFlags));

        uint32_t index = startRegister * 16;
        uint32_t size = (endRegister - startRegister) * 64;

        auto& cmd = queue.enqueue();
        cmd.type = RenderCommandType::SetPixelShaderConstants;
        cmd.setPixelShaderConstants.memory = g_intermediaryUploadAllocator.allocate(&device->pixelShaderFloatConstants[index], size);
        cmd.setPixelShaderConstants.index = index;
        cmd.setPixelShaderConstants.size = size;

        device->dirtyFlags[1] = 0;
    }
}

static void ProcSetBooleans(const RenderCommand& cmd)
{
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.booleans, cmd.setBooleans.booleans);
}

static void ProcSetSamplerState(const RenderCommand& cmd)
{
    const auto& args = cmd.setSamplerState;

    const auto addressU = ConvertTextureAddressMode((args.data0 >> 10) & 0x7);
    const auto addressV = ConvertTextureAddressMode((args.data0 >> 13) & 0x7);
    const auto addressW = ConvertTextureAddressMode((args.data0 >> 16) & 0x7);
    auto magFilter = ConvertTextureFilter((args.data3 >> 19) & 0x3);
    auto minFilter = ConvertTextureFilter((args.data3 >> 21) & 0x3);
    auto mipFilter = ConvertTextureFilter((args.data3 >> 23) & 0x3);
    const auto borderColor = ConvertBorderColor(args.data5 & 0x3);

    bool anisotropyEnabled = Config::AnisotropicFiltering > 0 && mipFilter == RenderFilter::LINEAR;
    if (anisotropyEnabled)
    {
        magFilter = RenderFilter::LINEAR;
        minFilter = RenderFilter::LINEAR;
    }

    auto& samplerDesc = g_samplerDescs[args.index];

    bool dirty = false;

    SetDirtyValue(dirty, samplerDesc.addressU, addressU);
    SetDirtyValue(dirty, samplerDesc.addressV, addressV);
    SetDirtyValue(dirty, samplerDesc.addressW, addressW);
    SetDirtyValue(dirty, samplerDesc.minFilter, minFilter);
    SetDirtyValue(dirty, samplerDesc.magFilter, magFilter);
    SetDirtyValue(dirty, samplerDesc.mipmapMode, RenderMipmapMode(mipFilter));
    SetDirtyValue(dirty, samplerDesc.maxAnisotropy, anisotropyEnabled ? Config::AnisotropicFiltering : 16u);
    SetDirtyValue(dirty, samplerDesc.anisotropyEnabled, anisotropyEnabled);
    SetDirtyValue(dirty, samplerDesc.borderColor, borderColor);

    if (dirty)
    {
        auto& [descriptorIndex, sampler] = g_samplerStates[XXH3_64bits(&samplerDesc, sizeof(RenderSamplerDesc))];
        if (descriptorIndex == NULL)
        {
            descriptorIndex = g_samplerStates.size();
            sampler = g_device->createSampler(samplerDesc);

            g_samplerDescriptorSet->setSampler(descriptorIndex - 1, sampler.get());
        }

        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.samplerIndices[args.index], descriptorIndex - 1);
    }
}

static void ProcSetVertexShaderConstants(const RenderCommand& cmd)
{
    auto& args = cmd.setVertexShaderConstants;
    assert((args.index * sizeof(uint32_t) + args.size) <= sizeof(g_vertexShaderConstants));

    memcpy(&g_vertexShaderConstants[args.index], args.memory, args.size);
    g_dirtyStates.vertexShaderConstants = true;
}

static void ProcSetPixelShaderConstants(const RenderCommand& cmd)
{
    auto& args = cmd.setPixelShaderConstants;
    assert((args.index * sizeof(uint32_t) + args.size) <= sizeof(g_pixelShaderConstants));

    memcpy(&g_pixelShaderConstants[args.index], args.memory, args.size);
    g_dirtyStates.pixelShaderConstants = true;
}

static void ProcAddPipeline(const RenderCommand& cmd)
{
    auto& args = cmd.addPipeline;
    auto& pipeline = g_pipelines[args.hash];

    if (pipeline == nullptr)
    {
        pipeline = std::unique_ptr<RenderPipeline>(args.pipeline);
#ifdef ASYNC_PSO_DEBUG
        ++g_pipelinesCreatedAsynchronously;
#endif
    }
    else
    {
#ifdef ASYNC_PSO_DEBUG
        ++g_pipelinesDropped;
#endif
        delete args.pipeline;
    }
}

static constexpr int32_t COMMON_DEPTH_BIAS_VALUE = int32_t((1 << 24) * 0.002f);
static constexpr float COMMON_SLOPE_SCALED_DEPTH_BIAS_VALUE = 1.0f;

static void FlushRenderStateForRenderThread()
{
#if defined(UNLEASHED_RECOMP_IOS)
    IOSResolveCheckBoundTextures();
    // Spatial retains its validated final-image path. Native CSD interception
    // splits the final pass and regresses frame time on A16; reserve it for
    // Temporal, where UI must be excluded from reconstruction history.
    if (g_iosTemporalActive && !g_iosMetalFXScaled && g_renderTarget == g_backBuffer && g_pipelineState.vertexShader &&
        (g_pipelineState.vertexShader == g_iosCsdVertexShader || g_pipelineState.vertexShader == g_iosCsdNoTexVertexShader))
        ProcUpscaleIOSBackBuffer(); // CSD HUD/text follows the upscale at native size.
#endif
    auto renderTarget = g_pipelineState.colorWriteEnable ? g_renderTarget : nullptr;
    auto depthStencil = g_pipelineState.zEnable ? g_depthStencil : nullptr;

#if defined(UNLEASHED_RECOMP_IOS)
    if (g_iosTemporalActive && renderTarget && depthStencil && renderTarget != g_backBuffer &&
        renderTarget->width == g_intermediaryBackBufferTextureWidth && renderTarget->height == g_intermediaryBackBufferTextureHeight &&
        depthStencil->width == g_intermediaryBackBufferTextureWidth && depthStencil->height == g_intermediaryBackBufferTextureHeight &&
        depthStencil->sampleCount == RenderSampleCount::COUNT_1)
        g_iosTemporalSceneDepth = depthStencil;
#endif

#if defined(UNLEASHED_RECOMP_IOS)
    IOSTrySwapResolve(renderTarget);
#endif
    bool foundAny = PopulateBarriersForStretchRect(renderTarget, depthStencil);

    for (const auto surface : g_pendingMsaaResolves)
    {
        bool isDepthStencil = (surface->format == RenderFormat::D32_FLOAT);
        foundAny |= PopulateBarriersForStretchRect(isDepthStencil ? nullptr : surface, isDepthStencil ? surface : nullptr);
    }

    if (foundAny)
    {
        FlushBarriers();
#if defined(UNLEASHED_RECOMP_IOS)
        g_iosResolveTrigger = IOSResolveDraw;
        IOSDescribeDrawResolve(renderTarget);
#endif
        ExecutePendingStretchRectCommands(renderTarget, depthStencil);

        for (const auto surface : g_pendingMsaaResolves)
        {
            bool isDepthStencil = (surface->format == RenderFormat::D32_FLOAT);
            ExecutePendingStretchRectCommands(isDepthStencil ? nullptr : surface, isDepthStencil ? surface : nullptr);
        }
    }

    if (!g_pendingMsaaResolves.empty())
        g_pendingMsaaResolves.clear();

    AddBarrier(renderTarget, RenderTextureLayout::COLOR_WRITE);
    AddBarrier(depthStencil, RenderTextureLayout::DEPTH_WRITE);

    FlushBarriers();

#if defined(UNLEASHED_RECOMP_IOS)
    if (g_iosTemporalActive && g_iosMotionBlurDraw && !g_iosTemporalInputsReady && g_textures[1] && g_iosTemporalSceneDepth)
    {
        auto boundTexture = [](GuestTexture* texture) -> const RenderTexture* {
            if (texture->sourceSurface && texture->sourceSurface->sampleCount == RenderSampleCount::COUNT_1)
                return texture->sourceSurface->texture;
            return texture->texture;
        };
        // Motion blur uses half-resolution depth; Temporal needs the original,
        // jittered scene depth. The packed half-resolution UV velocity is decoded
        // and sampled at the Temporal input pixel centers by the Metal kernel.
        AddBarrier(g_iosTemporalSceneDepth, RenderTextureLayout::SHADER_READ);
        FlushBarriers();
        g_iosTemporalInputsReady = plume::captureMetalTemporalInputs(g_commandLists[g_frame].get(),
            boundTexture(g_textures[1]), g_iosTemporalSceneDepth->texture);
    }
#endif
    SetFramebuffer(renderTarget, depthStencil, false);
    FlushViewport();

    auto& commandList = g_commandLists[g_frame];

    // D3D12 resets depth bias values to the pipeline values, even if they are dynamic.
    // We can reduce unnecessary calls by making common depth bias values part of the pipeline.
    if (g_capabilities.dynamicDepthBias && g_backend == Backend::D3D12)
    {
        bool useDepthBias = (g_depthBias != 0) || (g_slopeScaledDepthBias != 0.0f);

        int32_t depthBias = useDepthBias ? COMMON_DEPTH_BIAS_VALUE : 0;
        float slopeScaledDepthBias = useDepthBias ? COMMON_SLOPE_SCALED_DEPTH_BIAS_VALUE : 0.0f;

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthBias, depthBias);
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.slopeScaledDepthBias, slopeScaledDepthBias);
    }

    if (g_dirtyStates.pipelineState)
    {
        commandList->setPipeline(CreateGraphicsPipelineInRenderThread(g_pipelineState));
#if defined(UNLEASHED_RECOMP_IOS)
        if (IOSPassDiagnosticsEnabled())
        {
            const std::string label = fmt::format("{}|{}", g_pipelineState.vertexShader ? g_pipelineState.vertexShader->name : "-",
                g_pipelineState.pixelShader ? g_pipelineState.pixelShader->name : "-");
            IOSLabelNextPass(label.c_str());
        }
#endif

        // D3D12 resets the depth bias values. Check if they need to be set again.
        if (g_capabilities.dynamicDepthBias && g_backend == Backend::D3D12)
            g_dirtyStates.depthBias = (g_depthBias != g_pipelineState.depthBias) || (g_slopeScaledDepthBias != g_pipelineState.slopeScaledDepthBias);
    }

    if (g_dirtyStates.depthBias && g_capabilities.dynamicDepthBias)
        commandList->setDepthBias(g_depthBias, 0.0f, g_slopeScaledDepthBias);

    if (g_dirtyStates.vertexShaderConstants)
    {
        auto vertexShaderConstants = g_uploadAllocators[g_frame].allocate<true>(g_vertexShaderConstants, sizeof(g_vertexShaderConstants), 0x100);
        SetRootDescriptor(vertexShaderConstants, 0);
    }

    if (g_dirtyStates.pixelShaderConstants)
    {
        const uint32_t* constants = g_pixelShaderConstants;
#if defined(UNLEASHED_RECOMP_IOS)
        uint32_t temporalConstants[std::size(g_pixelShaderConstants)];
        if (g_iosTemporalActive && g_iosObjectVelocityDraw)
        {
            std::memcpy(temporalConstants, constants, sizeof(temporalConstants));
            static bool reported = false;
            if (!reported)
            {
                uint32_t limitBits = __builtin_bswap32(constants[85*4+1]);
                uint32_t cutoffBits = __builtin_bswap32(constants[85*4+2]);
                float limit, cutoff;
                std::memcpy(&limit, &limitBits, sizeof(limit)); std::memcpy(&cutoff, &cutoffBits, sizeof(cutoff));
                char report[160]{};
                std::snprintf(report, sizeof(report), "Temporal object velocity: original limit=%.6f, cutoff=%.6f; retaining full packed UV motion", limit, cutoff);
                os::logger::Log(report);
                reported = true;
            }
            // The motion-blur shader otherwise clips/culls motion needed for
            // Temporal reprojection. Override this draw's upload only; leave the
            // guest constants untouched and restore their buffer on shader changes.
            temporalConstants[85*4+1] = 0x0000803F; // Big-endian float 1.0: packed signed UV range.
            temporalConstants[85*4+2] = 0;
            constants = temporalConstants;
        }
#endif
        auto pixelShaderConstants = g_uploadAllocators[g_frame].allocate<true>(constants, sizeof(g_pixelShaderConstants), 0x100);
        SetRootDescriptor(pixelShaderConstants, 1);
    }

    if (g_dirtyStates.sharedConstants)
    {
        auto sharedConstants = g_uploadAllocators[g_frame].allocate<false>(&g_sharedConstants, sizeof(g_sharedConstants), 0x100);
        SetRootDescriptor(sharedConstants, 2);
    }

    if (g_dirtyStates.vertexStreamFirst <= g_dirtyStates.vertexStreamLast)
    {
        commandList->setVertexBuffers(
            g_dirtyStates.vertexStreamFirst,
            g_vertexBufferViews + g_dirtyStates.vertexStreamFirst,
            g_dirtyStates.vertexStreamLast - g_dirtyStates.vertexStreamFirst + 1,
            g_inputSlots + g_dirtyStates.vertexStreamFirst);
    }

    if (g_dirtyStates.indices && (g_backend == Backend::D3D12 || g_indexBufferView.buffer.ref != nullptr))
        commandList->setIndexBuffer(&g_indexBufferView);

    g_dirtyStates = DirtyStates(false);
}

static RenderPrimitiveTopology ConvertPrimitiveType(uint32_t primitiveType)
{
    switch (primitiveType)
    {
    case D3DPT_POINTLIST:
        return RenderPrimitiveTopology::POINT_LIST;
    case D3DPT_LINELIST:
        return RenderPrimitiveTopology::LINE_LIST;
    case D3DPT_LINESTRIP:
        return RenderPrimitiveTopology::LINE_STRIP;
    case D3DPT_TRIANGLELIST:
    case D3DPT_QUADLIST:
        return RenderPrimitiveTopology::TRIANGLE_LIST;
    case D3DPT_TRIANGLESTRIP:
        return RenderPrimitiveTopology::TRIANGLE_STRIP;
    case D3DPT_TRIANGLEFAN:
        return g_capabilities.triangleFan ? RenderPrimitiveTopology::TRIANGLE_FAN : RenderPrimitiveTopology::TRIANGLE_LIST;
    default:
        assert(false && "Unknown primitive type");
        return RenderPrimitiveTopology::UNKNOWN;
    }
}

static void SetPrimitiveType(uint32_t primitiveType)
{
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.primitiveTopology, ConvertPrimitiveType(primitiveType));
}

static uint32_t CheckInstancing()
{
    uint32_t indexCount = 0;

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.instancing, g_pipelineState.vertexDeclaration->indexVertexStream != 0);
    if (g_pipelineState.instancing)
    {
        // Index buffer is passed as a vertex stream
        indexCount = g_vertexBufferViews[g_pipelineState.vertexDeclaration->indexVertexStream].size / 4;
    }

    return indexCount;
}

static void UnsetInstancingStream()
{
    bool dirty = false;
    uint32_t index = g_pipelineState.vertexDeclaration->indexVertexStream;

    SetDirtyValue(dirty, g_vertexBufferViews[index].buffer, RenderBufferReference{});
    SetDirtyValue(dirty, g_vertexBufferViews[index].size, 0u);
    SetDirtyValue(dirty, g_inputSlots[index].stride, 0u);

    if (dirty)
    {
        g_dirtyStates.vertexStreamFirst = std::min<uint8_t>(g_dirtyStates.vertexStreamFirst, index);
        g_dirtyStates.vertexStreamLast = std::max<uint8_t>(g_dirtyStates.vertexStreamLast, index);
    }
}

static void DrawPrimitive(GuestDevice* device, uint32_t primitiveType, uint32_t startVertex, uint32_t primitiveCount) 
{
    LocalRenderCommandQueue queue;
    FlushRenderStateForMainThread(device, queue);

    auto& cmd = queue.enqueue();
    cmd.type = RenderCommandType::DrawPrimitive;
    cmd.drawPrimitive.primitiveType = primitiveType;
    cmd.drawPrimitive.startVertex = startVertex;
    cmd.drawPrimitive.primitiveCount = primitiveCount;

    queue.submit();
}

static void ProcDrawPrimitive(const RenderCommand& cmd)
{
    const auto& args = cmd.drawPrimitive;
#if defined(UNLEASHED_RECOMP_IOS)
    g_iosCurrentDraw = { "Draw", args.primitiveType, args.primitiveCount, nullptr, 0 };
#endif

    SetPrimitiveType(args.primitiveType);

    uint32_t indexCount = CheckInstancing();
    if (indexCount > 0)
    {
        auto& vertexBufferView = g_vertexBufferViews[g_pipelineState.vertexDeclaration->indexVertexStream];

        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.buffer, vertexBufferView.buffer);
        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.size, vertexBufferView.size);
        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.format, RenderFormat::R32_UINT);

        UnsetInstancingStream();
    }

    FlushRenderStateForRenderThread();

    auto& commandList = g_commandLists[g_frame];

    if (indexCount > 0)
        commandList->drawIndexedInstanced(indexCount, args.primitiveCount / indexCount, 0, 0, 0);
    else
        commandList->drawInstanced(args.primitiveCount, 1, args.startVertex, 0);
}

static void DrawIndexedPrimitive(GuestDevice* device, uint32_t primitiveType, int32_t baseVertexIndex, uint32_t startIndex, uint32_t primCount)
{
    LocalRenderCommandQueue queue;
    FlushRenderStateForMainThread(device, queue);

    auto& cmd = queue.enqueue();
    cmd.type = RenderCommandType::DrawIndexedPrimitive;
    cmd.drawIndexedPrimitive.primitiveType = primitiveType;
    cmd.drawIndexedPrimitive.baseVertexIndex = baseVertexIndex;
    cmd.drawIndexedPrimitive.startIndex = startIndex;
    cmd.drawIndexedPrimitive.primCount = primCount;

    queue.submit();
}

static void ProcDrawIndexedPrimitive(const RenderCommand& cmd)
{
    const auto& args = cmd.drawIndexedPrimitive;
#if defined(UNLEASHED_RECOMP_IOS)
    g_iosCurrentDraw = { "DrawIndexed", args.primitiveType, args.primCount, nullptr, 0 };
#endif

    uint32_t indexCount = CheckInstancing();
    if (indexCount > 0)
        UnsetInstancingStream();

    SetPrimitiveType(args.primitiveType);
    FlushRenderStateForRenderThread();

    g_commandLists[g_frame]->drawIndexedInstanced(args.primCount, 1, args.startIndex, args.baseVertexIndex, 0);
}

static void DrawPrimitiveUP(GuestDevice* device, uint32_t primitiveType, uint32_t primitiveCount, void* vertexStreamZeroData, uint32_t vertexStreamZeroStride)
{
    LocalRenderCommandQueue queue;
    FlushRenderStateForMainThread(device, queue);

    auto& cmd = queue.enqueue();
    cmd.type = RenderCommandType::DrawPrimitiveUP;
    cmd.drawPrimitiveUP.primitiveType = primitiveType;
    cmd.drawPrimitiveUP.primitiveCount = primitiveCount;
    cmd.drawPrimitiveUP.vertexStreamZeroData = g_intermediaryUploadAllocator.allocate(vertexStreamZeroData, primitiveCount * vertexStreamZeroStride);
    cmd.drawPrimitiveUP.vertexStreamZeroSize = primitiveCount * vertexStreamZeroStride;
    cmd.drawPrimitiveUP.vertexStreamZeroStride = vertexStreamZeroStride;
    cmd.drawPrimitiveUP.csdFilterState = g_csdFilterState;
    
    queue.submit();
}

static void ProcDrawPrimitiveUP(const RenderCommand& cmd)
{
    const auto& args = cmd.drawPrimitiveUP;
#if defined(UNLEASHED_RECOMP_IOS)
    g_iosCurrentDraw = { "DrawUP", args.primitiveType, args.primitiveCount, args.vertexStreamZeroData, args.vertexStreamZeroStride };
#endif

    uint32_t indexCount = CheckInstancing();
    if (indexCount > 0)
        UnsetInstancingStream();

    SetPrimitiveType(args.primitiveType);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexStrides[0], uint8_t(args.vertexStreamZeroStride));

    auto allocation = g_uploadAllocators[g_frame].allocate<true>(reinterpret_cast<const uint32_t*>(args.vertexStreamZeroData), args.vertexStreamZeroSize, 0x4);

    auto& vertexBufferView = g_vertexBufferViews[0];
    vertexBufferView.size = args.primitiveCount * args.vertexStreamZeroStride;
    vertexBufferView.buffer = allocation.buffer->at(allocation.offset);
    g_inputSlots[0].stride = args.vertexStreamZeroStride;
    g_dirtyStates.vertexStreamFirst = 0;

    indexCount = 0;

    if (args.primitiveType == D3DPT_QUADLIST)
        indexCount = g_quadIndexData.prepare(args.primitiveCount);
    else if (!g_capabilities.triangleFan && args.primitiveType == D3DPT_TRIANGLEFAN)
        indexCount = g_triangleFanIndexData.prepare(args.primitiveCount);

    if (args.csdFilterState != CsdFilterState::Unknown &&
        (g_pipelineState.pixelShader == g_csdShader || g_pipelineState.pixelShader == g_csdFilterShader.get()))
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.pixelShader,
            args.csdFilterState == CsdFilterState::On ? g_csdFilterShader.get() : g_csdShader);
    }

    FlushRenderStateForRenderThread();

    if (indexCount != 0)
        g_commandLists[g_frame]->drawIndexedInstanced(indexCount, 1, 0, 0, 0);
    else
        g_commandLists[g_frame]->drawInstanced(args.primitiveCount, 1, 0, 0);
}

static const char* ConvertDeclUsage(uint32_t usage)
{
    switch (usage)
    {
    case D3DDECLUSAGE_POSITION:
        return "POSITION";
    case D3DDECLUSAGE_BLENDWEIGHT:
        return "BLENDWEIGHT";
    case D3DDECLUSAGE_BLENDINDICES:
        return "BLENDINDICES";
    case D3DDECLUSAGE_NORMAL:
        return "NORMAL";
    case D3DDECLUSAGE_PSIZE:
        return "PSIZE";
    case D3DDECLUSAGE_TEXCOORD:
        return "TEXCOORD";
    case D3DDECLUSAGE_TANGENT:
        return "TANGENT";
    case D3DDECLUSAGE_BINORMAL:
        return "BINORMAL";
    case D3DDECLUSAGE_TESSFACTOR:
        return "TESSFACTOR";
    case D3DDECLUSAGE_POSITIONT:
        return "POSITIONT";
    case D3DDECLUSAGE_COLOR:
        return "COLOR";
    case D3DDECLUSAGE_FOG:
        return "FOG";
    case D3DDECLUSAGE_DEPTH:
        return "DEPTH";
    case D3DDECLUSAGE_SAMPLE:
        return "SAMPLE";
    default:
        assert(false && "Unknown usage");
        return "UNKNOWN";
    }
}

static RenderFormat ConvertDeclType(uint32_t type)
{
    switch (type)
    {
    case D3DDECLTYPE_FLOAT1:
        return RenderFormat::R32_FLOAT;
    case D3DDECLTYPE_FLOAT2:
        return RenderFormat::R32G32_FLOAT;
    case D3DDECLTYPE_FLOAT3:
        return RenderFormat::R32G32B32_FLOAT;
    case D3DDECLTYPE_FLOAT4:
        return RenderFormat::R32G32B32A32_FLOAT;
    case D3DDECLTYPE_D3DCOLOR:
        return RenderFormat::B8G8R8A8_UNORM;
    case D3DDECLTYPE_UBYTE4:
    case D3DDECLTYPE_UBYTE4_2:
        return RenderFormat::R8G8B8A8_UINT;
    case D3DDECLTYPE_SHORT2:
        return RenderFormat::R16G16_SINT;
    case D3DDECLTYPE_SHORT4:
        return RenderFormat::R16G16B16A16_SINT;
    case D3DDECLTYPE_UBYTE4N:
    case D3DDECLTYPE_UBYTE4N_2:
        return RenderFormat::R8G8B8A8_UNORM;
    case D3DDECLTYPE_SHORT2N:
        return RenderFormat::R16G16_SNORM;
    case D3DDECLTYPE_SHORT4N:
        return RenderFormat::R16G16B16A16_SNORM;
    case D3DDECLTYPE_USHORT2N:
        return RenderFormat::R16G16_UNORM;
    case D3DDECLTYPE_USHORT4N:
        return RenderFormat::R16G16B16A16_UNORM;
    case D3DDECLTYPE_UINT1:
        return RenderFormat::R32_UINT;
    case D3DDECLTYPE_DEC3N_2:
    case D3DDECLTYPE_DEC3N_3:
        return RenderFormat::R32_UINT;
    case D3DDECLTYPE_FLOAT16_2:
        return RenderFormat::R16G16_FLOAT;
    case D3DDECLTYPE_FLOAT16_4:
        return RenderFormat::R16G16B16A16_FLOAT;
    default:
        assert(false && "Unknown type");
        return RenderFormat::UNKNOWN;
    }
}

static GuestVertexDeclaration* CreateVertexDeclarationWithoutAddRef(GuestVertexElement* vertexElements) 
{
    size_t vertexElementCount = 0;
    auto vertexElement = vertexElements;

    while (vertexElement->stream != 0xFF && vertexElement->type != D3DDECLTYPE_UNUSED)
    {
        vertexElement->padding = 0;
        ++vertexElement;
        ++vertexElementCount;
    }

    vertexElement->padding = 0; // Clear the padding in D3DDECL_END() 

    std::lock_guard lock(g_vertexDeclarationMutex);

    XXH64_hash_t hash = XXH3_64bits(vertexElements, vertexElementCount * sizeof(GuestVertexElement));
    auto& vertexDeclaration = g_vertexDeclarations[hash];

    if (vertexDeclaration == nullptr)
    {
        vertexDeclaration = g_userHeap.AllocPhysical<GuestVertexDeclaration>(ResourceType::VertexDeclaration);
        vertexDeclaration->hash = hash;

        static std::vector<RenderInputElement> inputElements;
        inputElements.clear();

        struct Location
        {
            uint32_t usage;
            uint32_t usageIndex;
            uint32_t location;
        };

        constexpr Location locations[] =
        {
            { D3DDECLUSAGE_POSITION, 0, 0 },
            { D3DDECLUSAGE_NORMAL, 0, 1 },
            { D3DDECLUSAGE_TANGENT, 0, 2 },
            { D3DDECLUSAGE_BINORMAL, 0, 3 },
            { D3DDECLUSAGE_TEXCOORD, 0, 4 },
            { D3DDECLUSAGE_TEXCOORD, 1, 5 },
            { D3DDECLUSAGE_TEXCOORD, 2, 6 },
            { D3DDECLUSAGE_TEXCOORD, 3, 7 },
            { D3DDECLUSAGE_COLOR, 0, 8 },
            { D3DDECLUSAGE_BLENDINDICES, 0, 9 },
            { D3DDECLUSAGE_BLENDWEIGHT, 0, 10 },
            { D3DDECLUSAGE_COLOR, 1, 11 },
            { D3DDECLUSAGE_TEXCOORD, 4, 12 },
            { D3DDECLUSAGE_TEXCOORD, 5, 13 },
            { D3DDECLUSAGE_TEXCOORD, 6, 14 },
            { D3DDECLUSAGE_TEXCOORD, 7, 15 },
            { D3DDECLUSAGE_POSITION, 1, 15 }
        };

        vertexElement = vertexElements;
        while (vertexElement->stream != 0xFF && vertexElement->type != D3DDECLTYPE_UNUSED)
        {
            if (vertexElement->usage == D3DDECLUSAGE_POSITION && vertexElement->usageIndex == 2)
            {
                ++vertexElement;
                continue;
            }

            auto& inputElement = inputElements.emplace_back();
            
            inputElement.semanticName = ConvertDeclUsage(vertexElement->usage);
            inputElement.semanticIndex = vertexElement->usageIndex;
            inputElement.location = ~0;

            for (auto& location : locations)
            {
                if (location.usage == vertexElement->usage && location.usageIndex == vertexElement->usageIndex)
                {
                    inputElement.location = location.location;
                    break;
                }
            }

            assert(inputElement.location != ~0);

            inputElement.format = ConvertDeclType(vertexElement->type);
            inputElement.slotIndex = vertexElement->stream;
            inputElement.alignedByteOffset = vertexElement->offset;

            switch (vertexElement->usage)
            {
            case D3DDECLUSAGE_POSITION:
                if (vertexElement->usageIndex == 1)
                    vertexDeclaration->indexVertexStream = vertexElement->stream;
                break;

            case D3DDECLUSAGE_NORMAL:
            case D3DDECLUSAGE_TANGENT:
            case D3DDECLUSAGE_BINORMAL:
                if (vertexElement->type == D3DDECLTYPE_FLOAT3)
                    inputElement.format = RenderFormat::R32G32B32_UINT;
                else
                    vertexDeclaration->hasR11G11B10Normal = true;
                break;

            case D3DDECLUSAGE_TEXCOORD:
                switch (vertexElement->type)
                {
                case D3DDECLTYPE_SHORT2:
                case D3DDECLTYPE_SHORT4:
                case D3DDECLTYPE_SHORT2N:
                case D3DDECLTYPE_SHORT4N:
                case D3DDECLTYPE_USHORT2N:
                case D3DDECLTYPE_USHORT4N:
                case D3DDECLTYPE_FLOAT16_2:
                case D3DDECLTYPE_FLOAT16_4:
                    vertexDeclaration->swappedTexcoords |= 1 << vertexElement->usageIndex;
                    break;
                }

                break;
            }

            vertexDeclaration->vertexStreams[vertexElement->stream] = true;

            ++vertexElement;
        }

        auto addInputElement = [&](uint32_t usage, uint32_t usageIndex)
            {
                uint32_t location = ~0;

                for (auto& alsoLocation : locations)
                {
                    if (alsoLocation.usage == usage && alsoLocation.usageIndex == usageIndex)
                    {
                        location = alsoLocation.location;
                        break;
                    }
                }

                assert(location != ~0);

                for (auto& inputElement : inputElements)
                {
                    if (inputElement.location == location)
                        return;
                }

                auto format = RenderFormat::R32_FLOAT;
                switch (usage)
                {
                case D3DDECLUSAGE_NORMAL:
                case D3DDECLUSAGE_TANGENT:
                case D3DDECLUSAGE_BINORMAL:
                case D3DDECLUSAGE_BLENDINDICES:
                    format = RenderFormat::R32_UINT;
                    break;
                }

                inputElements.emplace_back(ConvertDeclUsage(usage), usageIndex, location, format, 15, 0);
            };

        addInputElement(D3DDECLUSAGE_POSITION, 0);
        addInputElement(D3DDECLUSAGE_NORMAL, 0);
        addInputElement(D3DDECLUSAGE_TANGENT, 0);
        addInputElement(D3DDECLUSAGE_BINORMAL, 0);
        addInputElement(D3DDECLUSAGE_TEXCOORD, 0);
        addInputElement(D3DDECLUSAGE_TEXCOORD, 1);
        addInputElement(D3DDECLUSAGE_TEXCOORD, 2);
        addInputElement(D3DDECLUSAGE_TEXCOORD, 3);
        addInputElement(D3DDECLUSAGE_COLOR, 0);
        addInputElement(D3DDECLUSAGE_BLENDWEIGHT, 0);
        addInputElement(D3DDECLUSAGE_BLENDINDICES, 0);

        vertexDeclaration->inputElements = std::make_unique<RenderInputElement[]>(inputElements.size());
        std::copy(inputElements.begin(), inputElements.end(), vertexDeclaration->inputElements.get());

        vertexDeclaration->vertexElements = std::make_unique<GuestVertexElement[]>(vertexElementCount + 1);
        std::copy(vertexElements, vertexElements + vertexElementCount + 1, vertexDeclaration->vertexElements.get());

        vertexDeclaration->inputElementCount = uint32_t(inputElements.size());
        vertexDeclaration->vertexElementCount = vertexElementCount + 1;
    }

    vertexDeclaration->AddRef();
    return vertexDeclaration;
}

static GuestVertexDeclaration* CreateVertexDeclaration(GuestVertexElement* vertexElements)
{
    auto vertexDeclaration = CreateVertexDeclarationWithoutAddRef(vertexElements);
    vertexDeclaration->AddRef();
    return vertexDeclaration;
}

static void SetVertexDeclaration(GuestDevice* device, GuestVertexDeclaration* vertexDeclaration) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetVertexDeclaration;
    cmd.setVertexDeclaration.vertexDeclaration = vertexDeclaration;
    g_renderQueue.enqueue(cmd);

    device->vertexDeclaration = g_memory.MapVirtual(vertexDeclaration);
}

static void ProcSetVertexDeclaration(const RenderCommand& cmd)
{
    auto& args = cmd.setVertexDeclaration;

    if (args.vertexDeclaration != nullptr)
    {
        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.swappedTexcoords, args.vertexDeclaration->swappedTexcoords);

        uint32_t specConstants = g_pipelineState.specConstants;
        if (args.vertexDeclaration->hasR11G11B10Normal)
            specConstants |= SPEC_CONSTANT_R11G11B10_NORMAL;
        else
            specConstants &= ~SPEC_CONSTANT_R11G11B10_NORMAL;

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.specConstants, specConstants);
    }
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexDeclaration, args.vertexDeclaration);
}

static ShaderCacheEntry* FindShaderCacheEntry(XXH64_hash_t hash)
{
    auto end = g_shaderCacheEntries + g_shaderCacheEntryCount;
    auto findResult = std::lower_bound(g_shaderCacheEntries, end, hash, [](ShaderCacheEntry& lhs, XXH64_hash_t rhs)
        {
            return lhs.hash < rhs;
        });

    return findResult != end && findResult->hash == hash ? findResult : nullptr;
}

static GuestShader* CreateShader(const be<uint32_t>* function, ResourceType resourceType)
{
    XXH64_hash_t hash = XXH3_64bits(function, function[1] + function[2]);

    auto findResult = FindShaderCacheEntry(hash);
    GuestShader* shader = nullptr;

    if (findResult != nullptr)
    {
        if (findResult->guestShader == nullptr)
        {
            shader = g_userHeap.AllocPhysical<GuestShader>(resourceType);

            if (hash == 0x85ED723035ECF535)
                shader->shader = CREATE_SHADER(blend_color_alpha_ps);
            else if (hash == 0xB1086A4947A797DE)
                shader->shader = CREATE_SHADER(csd_no_tex_vs);
            else if (hash == 0xB4CAFC034A37C8A8)
                shader->shader = CREATE_SHADER(csd_vs);
            else
                shader->shaderCacheEntry = findResult;

            findResult->guestShader = shader;
        }
        else
        {
            shader = findResult->guestShader;
        }
    }

    if (shader == nullptr)
        shader = g_userHeap.AllocPhysical<GuestShader>(resourceType);
    else
        shader->AddRef();

#if defined(UNLEASHED_RECOMP_IOS)
    if (hash == 0xB1086A4947A797DE) g_iosCsdNoTexVertexShader = shader;
    if (hash == 0xB4CAFC034A37C8A8) g_iosCsdVertexShader = shader;
#endif
    if (hash == 0x31173204A896098A)
        g_csdShader = shader;

    return shader;
}

static GuestShader* CreateVertexShader(const be<uint32_t>* function) 
{
    return CreateShader(function, ResourceType::VertexShader);
}

static void SetVertexShader(GuestDevice* device, GuestShader* shader)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetVertexShader;
    cmd.setVertexShader.shader = shader;
    g_renderQueue.enqueue(cmd);
}

static void ProcSetVertexShader(const RenderCommand& cmd)
{
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexShader, cmd.setVertexShader.shader);
}

static void SetStreamSource(GuestDevice* device, uint32_t index, GuestBuffer* buffer, uint32_t offset, uint32_t stride) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetStreamSource;
    cmd.setStreamSource.index = index;
    cmd.setStreamSource.buffer = buffer;
    cmd.setStreamSource.offset = offset;
    cmd.setStreamSource.stride = stride;
    g_renderQueue.enqueue(cmd);
}

static void ProcSetStreamSource(const RenderCommand& cmd)
{
    const auto& args = cmd.setStreamSource;

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexStrides[args.index], uint8_t(args.buffer != nullptr ? args.stride : 0));

    bool dirty = false;

    SetDirtyValue(dirty, g_vertexBufferViews[args.index].buffer, args.buffer != nullptr ? args.buffer->buffer->at(args.offset) : RenderBufferReference{});
    SetDirtyValue(dirty, g_vertexBufferViews[args.index].size, args.buffer != nullptr ? (args.buffer->dataSize - args.offset) : 0u);
    SetDirtyValue(dirty, g_inputSlots[args.index].stride, args.buffer != nullptr ? args.stride : 0u);

    if (dirty)
    {
        g_dirtyStates.vertexStreamFirst = std::min<uint8_t>(g_dirtyStates.vertexStreamFirst, args.index);
        g_dirtyStates.vertexStreamLast = std::max<uint8_t>(g_dirtyStates.vertexStreamLast, args.index);
    }
}

static void SetIndices(GuestDevice* device, GuestBuffer* buffer) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetIndices;
    cmd.setIndices.buffer = buffer;
    g_renderQueue.enqueue(cmd);
}

static void ProcSetIndices(const RenderCommand& cmd)
{
    const auto& args = cmd.setIndices;

    SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.buffer, args.buffer != nullptr ? args.buffer->buffer->at(0) : RenderBufferReference{});
    SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.format, args.buffer != nullptr ? args.buffer->format : RenderFormat::R16_UINT);
    SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.size, args.buffer != nullptr ? args.buffer->dataSize : 0u);
}

static GuestShader* CreatePixelShader(const be<uint32_t>* function)
{
    return CreateShader(function, ResourceType::PixelShader);
}

static void SetPixelShader(GuestDevice* device, GuestShader* shader)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetPixelShader;
    cmd.setPixelShader.shader = shader;
    g_renderQueue.enqueue(cmd);
}

static void ProcSetPixelShader(const RenderCommand& cmd)
{
    GuestShader* shader = cmd.setPixelShader.shader;
#if defined(UNLEASHED_RECOMP_IOS)
    g_iosMotionBlurDraw = shader && shader->shaderCacheEntry && shader->shaderCacheEntry->hash == 0x6B9732B4CD7E7740;
    const bool objectVelocity = shader && shader->shaderCacheEntry && shader->shaderCacheEntry->hash == 0xBBDB735BEACC8F41;
    if (g_iosTemporalActive && objectVelocity != g_iosObjectVelocityDraw)
        g_dirtyStates.pixelShaderConstants = true;
    g_iosObjectVelocityDraw = objectVelocity;
#endif
    if (shader != nullptr && 
        shader->shaderCacheEntry != nullptr)
    {
        if (shader->shaderCacheEntry->hash == 0x4294510C775F4EE8)
        {
            size_t shaderIndex = GAUSSIAN_BLUR_3X3;

            switch (Config::DepthOfFieldQuality)
            {
            case EDepthOfFieldQuality::Low:
                shaderIndex = GAUSSIAN_BLUR_3X3;
                break;

            case EDepthOfFieldQuality::Medium:
                shaderIndex = GAUSSIAN_BLUR_5X5;
                break;

            case EDepthOfFieldQuality::High:
                shaderIndex = GAUSSIAN_BLUR_7X7;
                break;

            case EDepthOfFieldQuality::Ultra:
                shaderIndex = GAUSSIAN_BLUR_9X9;
                break;

            default:
            {
                if (g_aspectRatio >= WIDE_ASPECT_RATIO)
                {
                    size_t height = round(Video::s_viewportHeight * Config::ResolutionScale);

                    if (height > 1440)
                        shaderIndex = GAUSSIAN_BLUR_9X9;
                    else if (height > 1080)
                        shaderIndex = GAUSSIAN_BLUR_7X7;
                    else if (height > 720)
                        shaderIndex = GAUSSIAN_BLUR_5X5;
                    else
                        shaderIndex = GAUSSIAN_BLUR_3X3;
                }
                else
                {
                    // Narrow aspect ratios should check for width to account for VERT+.
                    size_t width = round(Video::s_viewportWidth * Config::ResolutionScale);

                    if (width > 2560)
                        shaderIndex = GAUSSIAN_BLUR_9X9;
                    else if (width > 1920)
                        shaderIndex = GAUSSIAN_BLUR_7X7;
                    else if (width > 1280)
                        shaderIndex = GAUSSIAN_BLUR_5X5;
                    else
                        shaderIndex = GAUSSIAN_BLUR_3X3;
                }

                break;
            }
            }

            shader = g_gaussianBlurShaders[shaderIndex].get();
        }
        else if (shader->shaderCacheEntry->hash == 0x6B9732B4CD7E7740 && Config::MotionBlur == EMotionBlur::Enhanced)
        {
            shader = g_enhancedMotionBlurShader.get();
        }
    }

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.pixelShader, shader);
}

static std::thread g_renderThread([]
    {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        GuestThread::SetThreadName(GetCurrentThreadId(), "Render Thread");
#endif

        RenderCommand commands[32];

        while (true)
        {
            size_t count = g_renderQueue.wait_dequeue_bulk(commands, std::size(commands));

#if defined(UNLEASHED_RECOMP_IOS)
            app_lifecycle::GetExecutionGate().WaitUntilActive();
#endif

            for (size_t i = 0; i < count; i++)
            {
                auto& cmd = commands[i];
#if defined(UNLEASHED_RECOMP_IOS)
                // Frame diagnostics: report single render commands that stall the render thread.
                const auto commandStart = IOSFrameDiagnosticsEnabled() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
                switch (cmd.type)
                {
                case RenderCommandType::SetRenderState:                    ProcSetRenderState(cmd); break;
                case RenderCommandType::DestructResource:                  ProcDestructResource(cmd); break;
                case RenderCommandType::UnlockTextureRect:                 ProcUnlockTextureRect(cmd); break;
                case RenderCommandType::UnlockBuffer16:                    ProcUnlockBuffer16(cmd); break;
                case RenderCommandType::UnlockBuffer32:                    ProcUnlockBuffer32(cmd); break;
                case RenderCommandType::DrawImGui:                         ProcDrawImGui(cmd); break;
                case RenderCommandType::ExecuteCommandList:                ProcExecuteCommandList(cmd); break;
                case RenderCommandType::BeginCommandList:                  ProcBeginCommandList(cmd); break;
                case RenderCommandType::StretchRect:                       ProcStretchRect(cmd); break;
                case RenderCommandType::SetRenderTarget:                   ProcSetRenderTarget(cmd); break;
                case RenderCommandType::SetDepthStencilSurface:            ProcSetDepthStencilSurface(cmd); break;
                case RenderCommandType::ExecutePendingStretchRectCommands: ProcExecutePendingStretchRectCommands(cmd); break;
                case RenderCommandType::Clear:                             ProcClear(cmd); break;
                case RenderCommandType::SetViewport:                       ProcSetViewport(cmd); break;
                case RenderCommandType::SetTexture:                        ProcSetTexture(cmd); break;
                case RenderCommandType::SetScissorRect:                    ProcSetScissorRect(cmd); break;
                case RenderCommandType::SetSamplerState:                   ProcSetSamplerState(cmd); break;
                case RenderCommandType::SetBooleans:                       ProcSetBooleans(cmd); break;
                case RenderCommandType::SetVertexShaderConstants:          ProcSetVertexShaderConstants(cmd); break;
                case RenderCommandType::SetPixelShaderConstants:           ProcSetPixelShaderConstants(cmd); break;
                case RenderCommandType::AddPipeline:                       ProcAddPipeline(cmd); break;
                case RenderCommandType::DrawPrimitive:                     ProcDrawPrimitive(cmd); break;
                case RenderCommandType::DrawIndexedPrimitive:              ProcDrawIndexedPrimitive(cmd); break;
                case RenderCommandType::DrawPrimitiveUP:                   ProcDrawPrimitiveUP(cmd); break;
                case RenderCommandType::SetVertexDeclaration:              ProcSetVertexDeclaration(cmd); break;
                case RenderCommandType::SetVertexShader:                   ProcSetVertexShader(cmd); break;
                case RenderCommandType::SetStreamSource:                   ProcSetStreamSource(cmd); break;
                case RenderCommandType::SetIndices:                        ProcSetIndices(cmd); break;
                case RenderCommandType::SetPixelShader:                    ProcSetPixelShader(cmd); break;
                case RenderCommandType::TrimRuntimeCaches:                 ProcTrimRuntimeCaches(cmd); break;
                default:                                                   assert(false && "Unrecognized render command type."); break;
                }
#if defined(UNLEASHED_RECOMP_IOS)
                if (IOSFrameDiagnosticsEnabled())
                {
                    static const char* const commandNames[] = { "SetRenderState", "DestructResource", "UnlockTextureRect", "UnlockBuffer16", "UnlockBuffer32", "DrawImGui", "ExecuteCommandList", "BeginCommandList", "StretchRect", "SetRenderTarget", "SetDepthStencilSurface", "ExecutePendingStretchRectCommands", "Clear", "SetViewport", "SetTexture", "SetScissorRect", "SetSamplerState", "SetBooleans", "SetVertexShaderConstants", "SetPixelShaderConstants", "AddPipeline", "DrawPrimitive", "DrawIndexedPrimitive", "DrawPrimitiveUP", "SetVertexDeclaration", "SetVertexShader", "SetStreamSource", "SetIndices", "SetPixelShader", "TrimRuntimeCaches" };
                    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - commandStart).count();
                    if (elapsed > 15.0)
                    {
                        const size_t type = size_t(cmd.type);
                        LOGFN("iOS slow render command: {} {:.1f} ms, pipeline {}|{}", type < std::size(commandNames) ? commandNames[type] : "?", elapsed,
                            g_pipelineState.vertexShader ? g_pipelineState.vertexShader->name : "-",
                            g_pipelineState.pixelShader ? g_pipelineState.pixelShader->name : "-");
                    }
                }
#endif
            }
        }
    });

static void D3DXFillTexture(GuestTexture* texture, uint32_t function, void* data)
{
    if (texture->width == 1 && texture->height == 1 && texture->format == RenderFormat::R8_UNORM && function == 0x82BA2150)
    {
        auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(PLACEMENT_ALIGNMENT));

        uint8_t* mappedData = reinterpret_cast<uint8_t*>(uploadBuffer->map());
        *mappedData = 0xFF;
        uploadBuffer->unmap();

        ExecuteCopyCommandList([&]
            {
                g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture->texture, RenderTextureLayout::COPY_DEST));

                g_copyCommandList->copyTextureRegion(
                    RenderTextureCopyLocation::Subresource(texture->texture, 0),
                    RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), texture->format, 1, 1, 1, PLACEMENT_ALIGNMENT, 0));
            });

        texture->layout = RenderTextureLayout::COPY_DEST;
    }
}

static void D3DXFillVolumeTexture(GuestTexture* texture, uint32_t function, void* data)
{
    uint32_t rowPitch0 = (texture->width * 4 + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
    uint32_t slicePitch0 = (rowPitch0 * texture->height * texture->depth + PLACEMENT_ALIGNMENT - 1) & ~(PLACEMENT_ALIGNMENT - 1);

    uint32_t rowPitch1 = ((texture->width / 2) * 4 + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
    uint32_t slicePitch1 = (rowPitch1 * (texture->height / 2) * (texture->depth / 2) + PLACEMENT_ALIGNMENT - 1) & ~(PLACEMENT_ALIGNMENT - 1);

    auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(slicePitch0 + slicePitch1));
    uint8_t* mappedData = reinterpret_cast<uint8_t*>(uploadBuffer->map());

    thread_local std::vector<float> mipData;
    mipData.resize((texture->width / 2) * (texture->height / 2) * (texture->depth / 2) * 4);
    memset(mipData.data(), 0, mipData.size() * sizeof(float));

    for (size_t z = 0; z < texture->depth; z++)
    {
        for (size_t y = 0; y < texture->height; y++)
        {
            for (size_t x = 0; x < texture->width; x++)
            {
                auto dest = mappedData + z * rowPitch0 * texture->height + y * rowPitch0 + x * sizeof(uint32_t);
                size_t index = z * texture->width * texture->height + y * texture->width + x;
                size_t mipIndex = ((z / 2) * (texture->width / 2) * (texture->height / 2) + (y / 2) * (texture->width / 2) + x / 2) * 4;

                if (function == 0x82BC7820)
                {
                    auto src = reinterpret_cast<be<float>*>(data) + index * 4;

                    float r = static_cast<uint8_t>(src[0] * 255.0f);
                    float g = static_cast<uint8_t>(src[1] * 255.0f);
                    float b = static_cast<uint8_t>(src[2] * 255.0f);
                    float a = static_cast<uint8_t>(src[3] * 255.0f);

                    dest[0] = r;
                    dest[1] = g;
                    dest[2] = b;
                    dest[3] = a;

                    mipData[mipIndex + 0] += r;
                    mipData[mipIndex + 1] += g;
                    mipData[mipIndex + 2] += b;
                    mipData[mipIndex + 3] += a;
                }
                else if (function == 0x82BC78A8)
                {
                    auto src = reinterpret_cast<uint8_t*>(data) + index * 4;

                    dest[0] = src[3];
                    dest[1] = src[2];
                    dest[2] = src[1];
                    dest[3] = src[0];

                    mipData[mipIndex + 0] += src[3];
                    mipData[mipIndex + 1] += src[2];
                    mipData[mipIndex + 2] += src[1];
                    mipData[mipIndex + 3] += src[0];
                }
            }
        }
    }

    for (size_t z = 0; z < texture->depth / 2; z++)
    {
        for (size_t y = 0; y < texture->height / 2; y++)
        {
            for (size_t x = 0; x < texture->width / 2; x++)
            {
                auto dest = mappedData + slicePitch0 + z * rowPitch1 * (texture->height / 2) + y * rowPitch1 + x * sizeof(uint32_t);
                size_t index = (z * (texture->width / 2) * (texture->height / 2) + y * (texture->width / 2) + x) * 4;

                dest[0] = static_cast<uint8_t>(mipData[index + 0] / 8.0f);
                dest[1] = static_cast<uint8_t>(mipData[index + 1] / 8.0f);
                dest[2] = static_cast<uint8_t>(mipData[index + 2] / 8.0f);
                dest[3] = static_cast<uint8_t>(mipData[index + 3] / 8.0f);
            }
        }
    }

    uploadBuffer->unmap();

    ExecuteCopyCommandList([&]
        {
            g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture->texture, RenderTextureLayout::COPY_DEST));

            g_copyCommandList->copyTextureRegion(
                RenderTextureCopyLocation::Subresource(texture->texture, 0),
                RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), texture->format, texture->width, texture->height, texture->depth, rowPitch0 / RenderFormatSize(texture->format), 0));

            g_copyCommandList->copyTextureRegion(
                RenderTextureCopyLocation::Subresource(texture->texture, 1),
                RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), texture->format, texture->width / 2, texture->height / 2, texture->depth / 2, rowPitch1 / RenderFormatSize(texture->format), slicePitch0));
        });

    texture->layout = RenderTextureLayout::COPY_DEST;
}

struct GuestPictureData
{
    be<uint32_t> vtable;
    uint8_t flags;
    be<uint32_t> name;
    be<uint32_t> texture;
    be<uint32_t> type;
};

static RenderTextureDimension ConvertTextureDimension(ddspp::TextureType type)
{
    switch (type) 
    {
    case ddspp::Texture1D:
        return RenderTextureDimension::TEXTURE_1D;
    case ddspp::Texture2D:
    case ddspp::Cubemap:
        return RenderTextureDimension::TEXTURE_2D;
    case ddspp::Texture3D:
        return RenderTextureDimension::TEXTURE_3D;
    default:
        assert(false && "Unknown texture type from DDS.");
        return RenderTextureDimension::UNKNOWN;
    }
}

static RenderTextureViewDimension ConvertTextureViewDimension(ddspp::TextureType type)
{
    switch (type)
    {
    case ddspp::Texture1D:
        return RenderTextureViewDimension::TEXTURE_1D;
    case ddspp::Texture2D:
        return RenderTextureViewDimension::TEXTURE_2D;
    case ddspp::Texture3D:
        return RenderTextureViewDimension::TEXTURE_3D;
    case ddspp::Cubemap:
        return RenderTextureViewDimension::TEXTURE_CUBE;
    default:
        assert(false && "Unknown texture type from DDS.");
        return RenderTextureViewDimension::UNKNOWN;
    }
}

static RenderFormat ConvertDXGIFormat(ddspp::DXGIFormat format) 
{
    switch (format)
    {
    case ddspp::R32G32B32A32_TYPELESS:
        return RenderFormat::R32G32B32A32_TYPELESS;
    case ddspp::R32G32B32A32_FLOAT:
        return RenderFormat::R32G32B32A32_FLOAT;
    case ddspp::R32G32B32A32_UINT:
        return RenderFormat::R32G32B32A32_UINT;
    case ddspp::R32G32B32A32_SINT:
        return RenderFormat::R32G32B32A32_SINT;
    case ddspp::R32G32B32_TYPELESS:
        return RenderFormat::R32G32B32_TYPELESS;
    case ddspp::R32G32B32_FLOAT:
        return RenderFormat::R32G32B32_FLOAT;
    case ddspp::R32G32B32_UINT:
        return RenderFormat::R32G32B32_UINT;
    case ddspp::R32G32B32_SINT:
        return RenderFormat::R32G32B32_SINT;
    case ddspp::R16G16B16A16_TYPELESS:
        return RenderFormat::R16G16B16A16_TYPELESS;
    case ddspp::R16G16B16A16_FLOAT:
        return RenderFormat::R16G16B16A16_FLOAT;
    case ddspp::R16G16B16A16_UNORM:
        return RenderFormat::R16G16B16A16_UNORM;
    case ddspp::R16G16B16A16_UINT:
        return RenderFormat::R16G16B16A16_UINT;
    case ddspp::R16G16B16A16_SNORM:
        return RenderFormat::R16G16B16A16_SNORM;
    case ddspp::R16G16B16A16_SINT:
        return RenderFormat::R16G16B16A16_SINT;
    case ddspp::R32G32_TYPELESS:
        return RenderFormat::R32G32_TYPELESS;
    case ddspp::R32G32_FLOAT:
        return RenderFormat::R32G32_FLOAT;
    case ddspp::R32G32_UINT:
        return RenderFormat::R32G32_UINT;
    case ddspp::R32G32_SINT:
        return RenderFormat::R32G32_SINT;
    case ddspp::R8G8B8A8_TYPELESS:
        return RenderFormat::R8G8B8A8_TYPELESS;
    case ddspp::R8G8B8A8_UNORM:
        return RenderFormat::R8G8B8A8_UNORM;
    case ddspp::R8G8B8A8_UINT:
        return RenderFormat::R8G8B8A8_UINT;
    case ddspp::R8G8B8A8_SNORM:
        return RenderFormat::R8G8B8A8_SNORM;
    case ddspp::R8G8B8A8_SINT:
        return RenderFormat::R8G8B8A8_SINT;
    case ddspp::B8G8R8A8_UNORM:
        return RenderFormat::B8G8R8A8_UNORM;
    case ddspp::B8G8R8X8_UNORM:
        return RenderFormat::B8G8R8A8_UNORM;   
    case ddspp::R16G16_TYPELESS:
        return RenderFormat::R16G16_TYPELESS;
    case ddspp::R16G16_FLOAT:
        return RenderFormat::R16G16_FLOAT;
    case ddspp::R16G16_UNORM:
        return RenderFormat::R16G16_UNORM;
    case ddspp::R16G16_UINT:
        return RenderFormat::R16G16_UINT;
    case ddspp::R16G16_SNORM:
        return RenderFormat::R16G16_SNORM;
    case ddspp::R16G16_SINT:
        return RenderFormat::R16G16_SINT;
    case ddspp::R32_TYPELESS:
        return RenderFormat::R32_TYPELESS;
    case ddspp::D32_FLOAT:
        return RenderFormat::D32_FLOAT;
    case ddspp::R32_FLOAT:
        return RenderFormat::R32_FLOAT;
    case ddspp::R32_UINT:
        return RenderFormat::R32_UINT;
    case ddspp::R32_SINT:
        return RenderFormat::R32_SINT;
    case ddspp::R8G8_TYPELESS:
        return RenderFormat::R8G8_TYPELESS;
    case ddspp::R8G8_UNORM:
        return RenderFormat::R8G8_UNORM;
    case ddspp::R8G8_UINT:
        return RenderFormat::R8G8_UINT;
    case ddspp::R8G8_SNORM:
        return RenderFormat::R8G8_SNORM;
    case ddspp::R8G8_SINT:
        return RenderFormat::R8G8_SINT;
    case ddspp::R16_TYPELESS:
        return RenderFormat::R16_TYPELESS;
    case ddspp::R16_FLOAT:
        return RenderFormat::R16_FLOAT;
    case ddspp::D16_UNORM:
        return RenderFormat::D16_UNORM;
    case ddspp::R16_UNORM:
        return RenderFormat::R16_UNORM;
    case ddspp::R16_UINT:
        return RenderFormat::R16_UINT;
    case ddspp::R16_SNORM:
        return RenderFormat::R16_SNORM;
    case ddspp::R16_SINT:
        return RenderFormat::R16_SINT;
    case ddspp::R8_TYPELESS:
        return RenderFormat::R8_TYPELESS;
    case ddspp::R8_UNORM:
        return RenderFormat::R8_UNORM;
    case ddspp::R8_UINT:
        return RenderFormat::R8_UINT;
    case ddspp::R8_SNORM:
        return RenderFormat::R8_SNORM;
    case ddspp::R8_SINT:
        return RenderFormat::R8_SINT;
    case ddspp::BC1_TYPELESS:
        return RenderFormat::BC1_TYPELESS;
    case ddspp::BC1_UNORM:
        return RenderFormat::BC1_UNORM;
    case ddspp::BC1_UNORM_SRGB:
        return RenderFormat::BC1_UNORM_SRGB;
    case ddspp::BC2_TYPELESS:
        return RenderFormat::BC2_TYPELESS;
    case ddspp::BC2_UNORM:
        return RenderFormat::BC2_UNORM;
    case ddspp::BC2_UNORM_SRGB:
        return RenderFormat::BC2_UNORM_SRGB;
    case ddspp::BC3_TYPELESS:
        return RenderFormat::BC3_TYPELESS;
    case ddspp::BC3_UNORM:
        return RenderFormat::BC3_UNORM;
    case ddspp::BC3_UNORM_SRGB:
        return RenderFormat::BC3_UNORM_SRGB;
    case ddspp::BC4_TYPELESS:
        return RenderFormat::BC4_TYPELESS;
    case ddspp::BC4_UNORM:
        return RenderFormat::BC4_UNORM;
    case ddspp::BC4_SNORM:
        return RenderFormat::BC4_SNORM;
    case ddspp::BC5_TYPELESS:
        return RenderFormat::BC5_TYPELESS;
    case ddspp::BC5_UNORM:
        return RenderFormat::BC5_UNORM;
    case ddspp::BC5_SNORM:
        return RenderFormat::BC5_SNORM;
    case ddspp::BC6H_TYPELESS:
        return RenderFormat::BC6H_TYPELESS;
    case ddspp::BC6H_UF16:
        return RenderFormat::BC6H_UF16;
    case ddspp::BC6H_SF16:
        return RenderFormat::BC6H_SF16;
    case ddspp::BC7_TYPELESS:
        return RenderFormat::BC7_TYPELESS;
    case ddspp::BC7_UNORM:
        return RenderFormat::BC7_UNORM;
    case ddspp::BC7_UNORM_SRGB:
        return RenderFormat::BC7_UNORM_SRGB;
    default:
        os::logger::Log(fmt::format("Unsupported DDS DXGI format: {}", int(format)));
        assert(false && "Unsupported format from DDS.");
        return RenderFormat::UNKNOWN;
    }
}

// CPU fallbacks for devices without BC sampled-image support (stock Mali/PowerVR drivers).
// Preferred path: transcode to ETC2/EAC with etcpak - the memory footprint matches BC
// (except transparent BC1, which doubles). When ETC2 is unavailable too, decode to plain
// RGBA8/R8/RG8 at 4-8x the BC footprint.
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
        // Reject malformed or oversized DDS data before it reaches the upload math.
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

        // Texture Quality: serve the texture from further down its own mip chain
        // (the DDS already contains the smaller levels, so this only skips data).
        // Mipless textures (UI, fonts) and anything that would drop below 32px
        // stay untouched, so HUD art and small detail maps keep their pixels.
        uint32_t mipSkip = 0;
        if (Config::TextureQuality != ETextureQuality::Full && ddsDesc.numMips > 1)
        {
            mipSkip = Config::TextureQuality == ETextureQuality::Quarter ? 2u : 1u;
            while (mipSkip > 0 &&
                (mipSkip >= ddsDesc.numMips ||
                 std::max(ddsDesc.width >> mipSkip, ddsDesc.height >> mipSkip) < 32))
            {
                mipSkip--;
            }
        }
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

std::unique_ptr<GuestTexture> LoadTexture(const uint8_t* data, size_t dataSize, RenderComponentMapping componentMapping)
{
    GuestTexture texture(ResourceType::Texture);

    if (LoadTexture(texture, data, dataSize, componentMapping, false, {}))
        return std::make_unique<GuestTexture>(std::move(texture));

    return nullptr;
}

static void DiffPatchTexture(GuestTexture& texture, uint8_t* data, uint32_t dataSize, XXH64_hash_t hash)
{
    auto header = reinterpret_cast<BlockCompressionDiffPatchHeader*>(g_buttonBcDiff.get());
    auto entries = reinterpret_cast<BlockCompressionDiffPatchEntry*>(g_buttonBcDiff.get() + header->entriesOffset);
    auto end = entries + header->entryCount;
    
    auto findResult = std::lower_bound(entries, end, hash, [](BlockCompressionDiffPatchEntry& lhs, XXH64_hash_t rhs)
        {
            return lhs.hash < rhs;
        });

    if (findResult != end && findResult->hash == hash)
    {
        auto patch = reinterpret_cast<BlockCompressionDiffPatch*>(g_buttonBcDiff.get() + findResult->patchesOffset);
        for (size_t i = 0; i < findResult->patchCount; i++)
        {
            assert(patch->destinationOffset + patch->patchBytesSize <= dataSize);
            memcpy(data + patch->destinationOffset, g_buttonBcDiff.get() + patch->patchBytesOffset, patch->patchBytesSize);
            ++patch;
        }

        GuestTexture patchedTexture(ResourceType::Texture);
        if (LoadTexture(patchedTexture, data, dataSize, {}))
            texture.patchedTexture = std::make_unique<GuestTexture>(std::move(patchedTexture));
    }
}

static void MakePictureData(GuestPictureData* pictureData, uint8_t* data, uint32_t dataSize)
{
    if ((pictureData->flags & 0x1) == 0 && data != nullptr)
    {
        const char* textureName = reinterpret_cast<char*>(g_memory.Translate(pictureData->name + 2));
        std::string_view textureNameView = textureName != nullptr ? std::string_view(textureName) : std::string_view{};

        GuestTexture texture(ResourceType::Texture);

        if (LoadTexture(texture, data, dataSize, {}, false, textureNameView))
        {
#ifdef _DEBUG
            texture.texture->setName(textureName);
#endif
            XXH64_hash_t hash = XXH3_64bits(data, dataSize);

            // The whale in Cool Edge has a 2D texture assigned as a cubemap which makes it not display in recomp.
            // The hardware duplicates the first face to the remaining 6 faces, so to simulate that we'll recreate the asset.
            bool forceCubeMap = (dataSize == 0xAB38) && (hash == 0x160E9E250FDE88A9);
            if (forceCubeMap)
            {
                GuestTexture recreatedCubeMapTexture(ResourceType::Texture);
                if (LoadTexture(recreatedCubeMapTexture, data, dataSize, {}, true, textureNameView))
                    texture.recreatedCubeMapTexture = std::make_unique<GuestTexture>(std::move(recreatedCubeMapTexture));
            }

            DiffPatchTexture(texture, data, dataSize, hash);

            pictureData->texture = g_memory.MapVirtual(g_userHeap.AllocPhysical<GuestTexture>(std::move(texture)));
            pictureData->type = 0;
        }
    }
}

void IndexBufferLengthMidAsmHook(PPCRegister& r3)
{
    r3.u64 *= 2;
}

void SetShadowResolutionMidAsmHook(PPCRegister& r11)
{
    auto res = (int32_t)Config::ShadowResolution.Value;

    if (res > 0)
        r11.u64 = res;
}

static void SetResolution(be<uint32_t>* device)
{
    Video::ComputeViewportDimensions();

    uint32_t width = uint32_t(round(Video::s_viewportWidth * Config::ResolutionScale));
    uint32_t height = uint32_t(round(Video::s_viewportHeight * Config::ResolutionScale));
    device[46] = width == 0 ? 880 : width;
    device[47] = height == 0 ? 720 : height;
}

// The game does some weird stuff to render targets if they are above 
// 1024x1024 resolution, setting this bool at address 20 seems to avoid all that.
PPC_FUNC(sub_82E9F048)
{
    PPC_STORE_U8(ctx.r4.u32 + 20, 1);
    PPC_STORE_U32(ctx.r4.u32 + 44, PPC_LOAD_U32(ctx.r4.u32 + 8)); // Width
    PPC_STORE_U32(ctx.r4.u32 + 48, PPC_LOAD_U32(ctx.r4.u32 + 12)); // Height
}

static GuestShader* g_movieVertexShader;
static GuestShader* g_moviePixelShader;
static GuestVertexDeclaration* g_movieVertexDeclaration;

static void ScreenShaderInit(be<uint32_t>* a1, uint32_t a2, uint32_t a3, GuestVertexElement* vertexElements)
{
    if (g_moviePixelShader == nullptr)
    {
        g_moviePixelShader = g_userHeap.AllocPhysical<GuestShader>(ResourceType::PixelShader);
        g_moviePixelShader->shader = CREATE_SHADER(movie_ps);
    }

    if (g_movieVertexShader == nullptr)
    {
        g_movieVertexShader = g_userHeap.AllocPhysical<GuestShader>(ResourceType::VertexShader);
        g_movieVertexShader->shader = CREATE_SHADER(movie_vs);
    }

    if (g_movieVertexDeclaration == nullptr)
        g_movieVertexDeclaration = CreateVertexDeclarationWithoutAddRef(vertexElements);

    g_moviePixelShader->AddRef();
    g_movieVertexShader->AddRef();
    g_movieVertexDeclaration->AddRef();

    a1[2] = g_memory.MapVirtual(g_moviePixelShader);
    a1[3] = g_memory.MapVirtual(g_movieVertexShader);
    a1[4] = g_memory.MapVirtual(g_movieVertexDeclaration);
}

void MovieRendererMidAsmHook(PPCRegister& r3)
{
    // Runs once per rendered WMV frame: the touch overlay uses it to collapse
    // into a SKIP button for the whole playback (attract movie included).
    TouchControls::NotifyMovieVisible();

    auto device = reinterpret_cast<GuestDevice*>(g_memory.Translate(r3.u32));

    // Force linear filtering & clamp addressing
    for (size_t i = 0; i < 3; i++)
    {
        device->samplerStates[i].data[0] = (device->samplerStates[i].data[0].get() & ~0x7fc00) | 0x24800;
        device->samplerStates[i].data[3] = (device->samplerStates[i].data[3].get() & ~0x1f80000) | 0x1280000;
    }

    device->dirtyFlags[3] = device->dirtyFlags[3].get() | 0xe0000000ull;
}

static PPCRegister g_r4;
static PPCRegister g_r5;

// CRenderDirectorFxPipeline::Initialize
PPC_FUNC_IMPL(__imp__sub_8258C8A0);
PPC_FUNC(sub_8258C8A0)
{
    g_r4 = ctx.r4;
    g_r5 = ctx.r5;
    __imp__sub_8258C8A0(ctx, base);
}

// CRenderDirectorFxPipeline::Update
PPC_FUNC_IMPL(__imp__sub_8258CAE0);
PPC_FUNC(sub_8258CAE0)
{
    if (g_needsResize)
    {
        // Backup job values. These get modified by cutscenes, 
        // and resizing will cause the values to be forgotten.
        auto traverseFxJobs = [&]<typename TCallback>(const TCallback& callback)
        {
            uint32_t scheduler = PPC_LOAD_U32(ctx.r3.u32 + 0xE0);
            if (scheduler != NULL)
            {
                uint32_t member = PPC_LOAD_U32(scheduler + 0x8);
                if (member != NULL)
                {
                    for (uint32_t it = PPC_LOAD_U32(member + 0x24); it != PPC_LOAD_U32(member + 0x28); it += 8)
                    {
                        uint32_t job = PPC_LOAD_U32(it);
                        if (job != NULL)
                            callback(job);
                    }
                }
            }
        };

        union JobValues
        {
            struct
            {
                uint8_t field50[0x18];
                uint8_t field88;
            } fade;

            struct
            {
                uint8_t camera[0x120];
                uint8_t field44;
                uint8_t fieldA0;
            } shadowMap;
        };

        std::map<uint32_t, JobValues> jobValuesMap;
        traverseFxJobs([&](uint32_t job)
            {
                uint32_t vfTable = PPC_LOAD_U32(job);

                if (vfTable == 0x820CA6F8) // SWA::CFxFade
                {
                    // NOTE: Intentionally not storing shared pointers here. 
                    // Game sends messages that assign these every frame already.
                    JobValues jobValues{};

                    memcpy(jobValues.fade.field50, base + job + 0x50, sizeof(jobValues.fade.field50));
                    jobValues.fade.field88 = PPC_LOAD_U8(job + 0x88);

                    jobValuesMap.emplace(PPC_LOAD_U32(job + 0x48), jobValues);
                }
                else if (vfTable == 0x820CAC5C) // SWA::CFxShadowMap
                {
                    for (uint32_t it = PPC_LOAD_U32(job + 0x88); it != PPC_LOAD_U32(job + 0x8C); it += 8)
                    {
                        uint32_t camera = PPC_LOAD_U32(it);
                        if (camera != NULL && PPC_LOAD_U32(camera) == 0x820BF83C) // SWA::CShadowMapCameraLiSPSM
                        {
                            JobValues jobValues{};

                            memcpy(jobValues.shadowMap.camera, base + camera, sizeof(jobValues.shadowMap.camera));
                            jobValues.shadowMap.field44 = PPC_LOAD_U8(job + 0x44);
                            jobValues.shadowMap.fieldA0 = PPC_LOAD_U8(job + 0xA0);

                            jobValuesMap.emplace(vfTable, jobValues);
                            break;
                        }
                    }
                }
            });

        auto r3 = ctx.r3;
        ctx.r4 = g_r4;
        ctx.r5 = g_r5;
        __imp__sub_8258C8A0(ctx, base);
        ctx.r3 = r3;

        // Restore job values.
        traverseFxJobs([&](uint32_t job)
            {
                uint32_t vfTable = PPC_LOAD_U32(job);

                if (vfTable == 0x820CA6F8) // SWA::CFxFade
                {
                    auto findResult = jobValuesMap.find(PPC_LOAD_U32(job + 0x48));
                    if (findResult != jobValuesMap.end()) // May NOT actually be found.
                    {
                        memcpy(base + job + 0x50, findResult->second.fade.field50, sizeof(findResult->second.fade.field50));
                        PPC_STORE_U8(job + 0x88, findResult->second.fade.field88);
                    }
                }
                else if (vfTable == 0x820CAC5C) // SWA::CFxShadowMap
                {
                    auto findResult = jobValuesMap.find(vfTable);
                    if (findResult != jobValuesMap.end()) // Would be weird if this one wasn't found.
                    {
                        for (uint32_t it = PPC_LOAD_U32(job + 0x88); it != PPC_LOAD_U32(job + 0x8C); it += 8)
                        {
                            uint32_t camera = PPC_LOAD_U32(it);
                            if (camera != NULL && PPC_LOAD_U32(camera) == 0x820BF83C) // SWA::CShadowMapCameraLiSPSM
                            {
                                memcpy(base + camera, findResult->second.shadowMap.camera, sizeof(findResult->second.shadowMap.camera));
                                PPC_STORE_U32(job + 0x80, camera);
                                PPC_STORE_U8(job + 0x44, findResult->second.shadowMap.field44);
                                PPC_STORE_U8(job + 0xA0, findResult->second.shadowMap.fieldA0);
                                break;
                            }
                        }
                    }
                }
            });

        g_needsResize = false;
    }

    __imp__sub_8258CAE0(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_824EB5B0);
PPC_FUNC(sub_824EB5B0)
{
    g_updateDirectorProfiler.Begin();
    __imp__sub_824EB5B0(ctx, base);
    g_updateDirectorProfiler.End();
}

PPC_FUNC_IMPL(__imp__sub_824EB290);
PPC_FUNC(sub_824EB290)
{
    g_renderDirectorProfiler.Begin();
    __imp__sub_824EB290(ctx, base);
    g_renderDirectorProfiler.End();
}

// World map disables VERT+, so scaling by width does not work for it.
static uint32_t g_forceCheckHeightForPostProcessFix;

// SWA::CWorldMapCamera::CWorldMapCamera
PPC_FUNC_IMPL(__imp__sub_824860E0);
PPC_FUNC(sub_824860E0)
{
    ++g_forceCheckHeightForPostProcessFix;
    __imp__sub_824860E0(ctx, base);
}

// SWA::CCameraController::~CCameraController
PPC_FUNC_IMPL(__imp__sub_824831D0);
PPC_FUNC(sub_824831D0)
{
    if (PPC_LOAD_U32(ctx.r3.u32) == 0x8202BF1C) // SWA::CWorldMapCamera
        --g_forceCheckHeightForPostProcessFix;

    __imp__sub_824831D0(ctx, base);
}

void PostProcessResolutionFix(PPCRegister& r4, PPCRegister& f1, PPCRegister& f2)
{
    auto device = reinterpret_cast<be<uint32_t>*>(g_memory.Translate(r4.u32));

    uint32_t width = device[46].get();
    uint32_t height = device[47].get();
    double aspectRatio = double(width) / double(height);

    double factor;
    if ((aspectRatio >= WIDE_ASPECT_RATIO) || (g_forceCheckHeightForPostProcessFix != 0))
        factor = 720.0 / double(height);
    else
        factor = 1280.0 / double(width);

    f1.f64 *= factor;
    f2.f64 *= factor;
}

void LightShaftAspectRatioFix(PPCRegister& f28, PPCRegister& f0)
{
    f28.f64 = f0.f64;
}

static const be<uint16_t> g_particleTestIndexBuffer[] =
{
    0, 1, 2,
    0, 2, 3,
    0, 3, 4,
    0, 4, 5
};

bool ParticleTestIndexBufferMidAsmHook(PPCRegister& r30)
{
    if (!g_capabilities.triangleFan)
    {
        auto buffer = CreateIndexBuffer(sizeof(g_particleTestIndexBuffer), 0, D3DFMT_INDEX16);
        void* memory = LockIndexBuffer(buffer, 0, 0, 0);
        memcpy(memory, g_particleTestIndexBuffer, sizeof(g_particleTestIndexBuffer));
        UnlockIndexBuffer(buffer);

        r30.u32 = g_memory.MapVirtual(buffer);
        return true;
    }
    return false;
}

void ParticleTestDrawIndexedPrimitiveMidAsmHook(PPCRegister& r7)
{
    if (!g_capabilities.triangleFan)
        r7.u64 = std::size(g_particleTestIndexBuffer);
}

void MotionBlurPrevInvViewProjectionMidAsmHook(PPCRegister& r10)
{
    auto mtxProjection = reinterpret_cast<be<float>*>(g_memory.Translate(r10.u32));

    // Reverse Z. Have to be done on CPU side because the matrix multiplications
    // add up and it loses precision by the time it's sent to GPU.
    mtxProjection[10] = -(mtxProjection[10] + 1.0f);
    mtxProjection[14] = -mtxProjection[14];
}

// Normally, we could delay setting IsMadeOne, but the game relies on that flag
// being present to handle load priority. To work around that, we can prevent
// IsMadeAll from being set until the compilation is finished. Time for a custom flag!
enum
{
    eDatabaseDataFlags_CompilingPipelines = 0x80
};

// This is passed to pipeline compilation threads to keep the loading screen busy until 
// all of them are finished. A shared pointer makes sure the destructor is called only once.
struct PipelineTaskToken
{
    PipelineTaskType type{};
    boost::shared_ptr<Hedgehog::Database::CDatabaseData> databaseData;

    PipelineTaskToken() : databaseData()
    {
    }

    PipelineTaskToken(const PipelineTaskToken&) = delete;

    PipelineTaskToken(PipelineTaskToken&& other)
        : type(std::exchange(other.type, PipelineTaskType::Null))
        , databaseData(std::exchange(other.databaseData, nullptr))
    {
    }

    ~PipelineTaskToken()
    {
        if (type != PipelineTaskType::Null)
        {
            if (databaseData.get() != nullptr)
                databaseData->m_Flags &= ~eDatabaseDataFlags_CompilingPipelines;

            if ((--g_compilingPipelineTaskCount) == 0)
                g_compilingPipelineTaskCount.notify_one();
        }
    }
};

struct PipelineStateQueueItem
{
    XXH64_hash_t pipelineHash;
    PipelineState pipelineState;
    std::shared_ptr<PipelineTaskToken> token;
#ifdef ASYNC_PSO_DEBUG
    std::string pipelineName;
#endif
};

static moodycamel::BlockingConcurrentQueue<PipelineStateQueueItem> g_pipelineStateQueue;

static void CompilePipeline(XXH64_hash_t pipelineHash, const PipelineState& pipelineState
#ifdef ASYNC_PSO_DEBUG
    , const std::string& pipelineName
#endif
)
{
    auto pipeline = CreateGraphicsPipeline(pipelineState);
#ifdef ASYNC_PSO_DEBUG
    pipeline->setName(pipelineName);
#endif

    // Will get dropped in render thread if a different thread already managed to compile this.
    RenderCommand cmd;
    cmd.type = RenderCommandType::AddPipeline;
    cmd.addPipeline.hash = pipelineHash;
    cmd.addPipeline.pipeline = pipeline.release();
    g_renderQueue.enqueue(cmd);
}

static void PipelineCompilerThread()
{
#ifdef _WIN32
    int threadPriority = THREAD_PRIORITY_LOWEST;
    SetThreadPriority(GetCurrentThread(), threadPriority);
    GuestThread::SetThreadName(GetCurrentThreadId(), "Pipeline Compiler Thread");
#endif

    std::unique_ptr<GuestThreadContext> ctx;

    while (true)
    {
        PipelineStateQueueItem queueItem;
        g_pipelineStateQueue.wait_dequeue(queueItem);

#if defined(UNLEASHED_RECOMP_IOS)
        app_lifecycle::GetExecutionGate().WaitUntilActive();
#endif

        if (ctx == nullptr)
            ctx = std::make_unique<GuestThreadContext>(0);

#ifdef _WIN32
        int newThreadPriority = threadPriority;

        bool loading = *SWA::SGlobals::ms_IsLoading;
        if (loading)
            newThreadPriority = THREAD_PRIORITY_HIGHEST;
        else
            newThreadPriority = THREAD_PRIORITY_LOWEST;

        if (newThreadPriority != threadPriority)
        {
            SetThreadPriority(GetCurrentThread(), newThreadPriority);
            threadPriority = newThreadPriority;
        }
#endif

        CompilePipeline(queueItem.pipelineHash, queueItem.pipelineState
#ifdef ASYNC_PSO_DEBUG
            , queueItem.pipelineName.c_str()
#endif
        );

        std::this_thread::yield();
    }
}

static std::vector<std::unique_ptr<std::thread>> g_pipelineCompilerThreads = []()
    {
        size_t threadCount = std::max(2u, (std::thread::hardware_concurrency() * 2) / 3);

        std::vector<std::unique_ptr<std::thread>> threads(threadCount);
        for (auto& thread : threads)
            thread = std::make_unique<std::thread>(PipelineCompilerThread);

        return threads;
    }();

static constexpr uint32_t MODEL_DATA_VFTABLE = 0x82073A44;
static constexpr uint32_t TERRAIN_MODEL_DATA_VFTABLE = 0x8211D25C;
static constexpr uint32_t PARTICLE_MATERIAL_VFTABLE = 0x8211F198;

// Allocate the shared pointer only when new compilations are happening.
// If nothing was compiled, the local "token" variable will get destructed with RAII instead.
struct PipelineTaskTokenPair
{
    PipelineTaskToken token;
    std::shared_ptr<PipelineTaskToken> sharedToken;
};

// Having this separate, because I don't want to lock a mutex in the render thread before
// every single draw. Might be worth profiling to see if it actually has an impact and merge them.
static xxHashMap<PipelineState> g_asyncPipelineStates;

static void EnqueueGraphicsPipelineCompilation(
    const PipelineState& pipelineState, 
    PipelineTaskTokenPair& tokenPair, 
    const char* name,
    bool isPrecompiledPipeline = false)
{
    XXH64_hash_t hash = XXH3_64bits(&pipelineState, sizeof(pipelineState));
    bool shouldCompile = g_asyncPipelineStates.emplace(hash, pipelineState).second;

    if (shouldCompile)
    {
        bool loading = *SWA::SGlobals::ms_IsLoading;
        if (!loading && isPrecompiledPipeline)
        {
            // We can just compile here during the logos.
            CompilePipeline(hash, pipelineState
#ifdef ASYNC_PSO_DEBUG
                , fmt::format("CACHE {} {:X}", name, hash)
#endif
            );
        }
        else
        {
            if (tokenPair.sharedToken == nullptr && tokenPair.token.type != PipelineTaskType::Null)
                tokenPair.sharedToken = std::make_shared<PipelineTaskToken>(std::move(tokenPair.token));

            PipelineStateQueueItem queueItem;
            queueItem.pipelineHash = hash;
            queueItem.pipelineState = pipelineState;
            queueItem.token = tokenPair.sharedToken;
#ifdef ASYNC_PSO_DEBUG
            queueItem.pipelineName = fmt::format("ASYNC {} {:X}", name, hash);
#endif
            g_pipelineStateQueue.enqueue(queueItem);
        }
    }

#ifdef PSO_CACHING_CLEANUP
    if (shouldCompile && isPrecompiledPipeline)
    {
        std::lock_guard lock(g_pipelineCacheMutex);
        g_pipelineStatesToCache.emplace(hash, pipelineState);
    }
#endif

#ifdef PSO_CACHING
    if (!isPrecompiledPipeline)
    {
        std::lock_guard lock(g_pipelineCacheMutex);
        g_pipelineStatesToCache.erase(hash);
    }
#endif
}

struct CompilationArgs
{
    PipelineTaskTokenPair tokenPair;
    bool noGI{};
    bool hasMoreThanOneBone{};
    bool velocityMapQuickStep{};
    bool objectIcon{};
    bool instancing{};
};

enum class MeshLayer
{
    Opaque,
    Transparent,
    PunchThrough,
    Special
};

struct Mesh
{
    uint32_t vertexSize{};
    uint32_t morphTargetVertexSize{};
    GuestVertexDeclaration* vertexDeclaration{};
    Hedgehog::Mirage::CMaterialData* material{};
    MeshLayer layer{};
    bool morphModel{};
};

static void CompileMeshPipeline(const Mesh& mesh, CompilationArgs& args)
{
    if (mesh.material == nullptr || mesh.material->m_spShaderListData.get() == nullptr)
        return;

    auto& shaderList = mesh.material->m_spShaderListData;

    bool isFur = !mesh.morphModel && !args.instancing &&
        strstr(shaderList->m_TypeAndName.c_str(), "Fur") != nullptr;

    bool isSky = !mesh.morphModel && !args.instancing &&
        strstr(shaderList->m_TypeAndName.c_str(), "Sky") != nullptr;

    bool isSonicMouth = !mesh.morphModel && !args.instancing &&
        strcmp(mesh.material->m_TypeAndName.c_str() + 2, "sonic_gm_mouth_duble") == 0 &&
        strcmp(shaderList->m_TypeAndName.c_str() + 3, "SonicSkin_dspf[b]") == 0;

    bool compiledOutsideMainFramebuffer = !args.instancing && !isFur && !isSky;

    bool constTexCoord;
    if (args.instancing)
    {
        constTexCoord = false;
    }
    else
    {
        constTexCoord = true;
        if (mesh.material->m_spTexsetData.get() != nullptr)
        {
            for (size_t i = 1; i < mesh.material->m_spTexsetData->m_TextureList.size(); i++)
            {
                if (mesh.material->m_spTexsetData->m_TextureList[i]->m_TexcoordIndex !=
                    mesh.material->m_spTexsetData->m_TextureList[0]->m_TexcoordIndex)
                {
                    constTexCoord = false;
                    break;
                }
            }
        }
    }

    // Shadow pipeline.
    if (compiledOutsideMainFramebuffer && (mesh.layer == MeshLayer::Opaque || mesh.layer == MeshLayer::PunchThrough))
    {
        PipelineState pipelineState{};

        if (mesh.layer == MeshLayer::PunchThrough)
        {
            pipelineState.vertexShader = FindShaderCacheEntry(0xDD4FA7BB53876300)->guestShader;
            pipelineState.pixelShader = FindShaderCacheEntry(0xE2ECA594590DDE8B)->guestShader;
        }
        else
        {
            pipelineState.vertexShader = FindShaderCacheEntry(0x8E4BB23465BD909E)->guestShader;
        }

        pipelineState.vertexDeclaration = mesh.vertexDeclaration;
        pipelineState.cullMode = mesh.material->m_DoubleSided ? RenderCullMode::NONE : RenderCullMode::BACK;
        pipelineState.zFunc = RenderComparisonFunction::LESS_EQUAL;
        
        if (g_capabilities.dynamicDepthBias)
        {
            // Put common depth bias values for reducing unnecessary calls.
            if (g_backend == Backend::D3D12)
            {
                pipelineState.depthBias = COMMON_DEPTH_BIAS_VALUE;
                pipelineState.slopeScaledDepthBias = COMMON_SLOPE_SCALED_DEPTH_BIAS_VALUE;
            }
        }
        else 
        {
            pipelineState.depthBias = (1 << 24) * (*reinterpret_cast<be<float>*>(g_memory.Translate(0x83302760)));
            pipelineState.slopeScaledDepthBias = *reinterpret_cast<be<float>*>(g_memory.Translate(0x83302764));
        }

        pipelineState.colorWriteEnable = 0;
        pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
        pipelineState.vertexStrides[0] = mesh.vertexSize;
        pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT;

        if (mesh.layer == MeshLayer::PunchThrough)
            pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;

        const char* name = (mesh.layer == MeshLayer::PunchThrough ? "MakeShadowMapTransparent" : "MakeShadowMap");
        SanitizePipelineState(pipelineState);
        EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, name);

        // Morph models have 4 targets where unused targets default to the first vertex stream.
        if (mesh.morphModel)
        {
            for (size_t i = 0; i < 5; i++)
            {
                for (size_t j = 0; j < 4; j++)
                    pipelineState.vertexStrides[j + 1] = i > j ? mesh.morphTargetVertexSize : mesh.vertexSize;

                SanitizePipelineState(pipelineState);
                EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, name);
            }
        }
    }

    // Motion blur pipeline. We could normally do the player here only, but apparently Werehog enemies also have object blur.
    // TODO: Do punch through meshes get rendered?
    if (!mesh.morphModel && compiledOutsideMainFramebuffer && args.hasMoreThanOneBone && mesh.layer == MeshLayer::Opaque)
    {
        PipelineState pipelineState{};
        pipelineState.vertexShader = FindShaderCacheEntry(0x4620B236DC38100C)->guestShader;
        pipelineState.pixelShader = FindShaderCacheEntry(0xBBDB735BEACC8F41)->guestShader;
        pipelineState.vertexDeclaration = mesh.vertexDeclaration;
        pipelineState.cullMode = RenderCullMode::NONE;
        pipelineState.zFunc = RenderComparisonFunction::GREATER_EQUAL;
        pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
        pipelineState.vertexStrides[0] = mesh.vertexSize;
        pipelineState.renderTargetFormat = RenderFormat::R8G8B8A8_UNORM;
        pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT;
        pipelineState.specConstants = SPEC_CONSTANT_REVERSE_Z;

        SanitizePipelineState(pipelineState);
        EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, "FxVelocityMap");

        if (args.velocityMapQuickStep)
        {
            pipelineState.vertexShader = FindShaderCacheEntry(0x99DC3F27E402700D)->guestShader;
            SanitizePipelineState(pipelineState);
            EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, "FxVelocityMapQuickStep");
        }
    }

    uint32_t defaultStr = args.instancing ? 0x820C8734 : 0x8202DDBC; // "instancing" for instancing, "default" for regular
    guest_stack_var<Hedgehog::Base::CStringSymbol> defaultSymbol(reinterpret_cast<const char*>(g_memory.Translate(defaultStr)));
    auto defaultFindResult = shaderList->m_PixelShaderPermutations.find(*defaultSymbol);
    if (defaultFindResult == shaderList->m_PixelShaderPermutations.end())
        return;

    uint32_t pixelShaderSubPermutationsToCompile = 0;
    if (constTexCoord) pixelShaderSubPermutationsToCompile |= 0x1;
    if (args.noGI) pixelShaderSubPermutationsToCompile |= 0x2;

    if ((defaultFindResult->second.m_SubPermutations.get() & (1 << pixelShaderSubPermutationsToCompile)) == 0) pixelShaderSubPermutationsToCompile &= ~0x1;
    if ((defaultFindResult->second.m_SubPermutations.get() & (1 << pixelShaderSubPermutationsToCompile)) == 0) pixelShaderSubPermutationsToCompile &= ~0x2;

    uint32_t noneStr = mesh.morphModel ? 0x820D72F0 : 0x8200D938; // "p" for morph, "none" for regular
    guest_stack_var<Hedgehog::Base::CStringSymbol> noneSymbol(reinterpret_cast<const char*>(g_memory.Translate(noneStr)));
    auto noneFindResult = defaultFindResult->second.m_VertexShaderPermutations.find(*noneSymbol);
    if (noneFindResult == defaultFindResult->second.m_VertexShaderPermutations.end())
        return;

    uint32_t vertexShaderSubPermutationsToCompile = 0;
    if (constTexCoord) vertexShaderSubPermutationsToCompile |= 0x1;

    if ((noneFindResult->second->m_SubPermutations.get() & (1 << vertexShaderSubPermutationsToCompile)) == 0)
        vertexShaderSubPermutationsToCompile &= ~0x1;

    auto vertexDeclaration = mesh.vertexDeclaration;
    bool instancing = args.instancing || isFur;

    if (instancing)
    {
        GuestVertexElement vertexElements[64];
        memcpy(vertexElements, mesh.vertexDeclaration->vertexElements.get(), (mesh.vertexDeclaration->vertexElementCount - 1) * sizeof(GuestVertexElement));

        if (args.instancing)
        {
            vertexElements[mesh.vertexDeclaration->vertexElementCount - 1] = { 1, 0, 0x2A23B9, 0, 5, 4 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount] = { 1, 12, 0x2C2159, 0, 5, 5 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount + 1] = { 1, 16, 0x2C2159, 0, 5, 6 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount + 2] = { 1, 20, 0x182886, 0, 10, 1 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount + 3] = { 2, 0, 0x2C82A1, 0, 0, 1 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount + 4] = D3DDECL_END();
        }
        else if (isFur)
        {
            vertexElements[mesh.vertexDeclaration->vertexElementCount - 1] = { 1, 0, 0x2C82A1, 0, 0, 1 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount] = { 2, 0, 0x2C83A4, 0, 0, 2 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount + 1] = D3DDECL_END();
        }

        vertexDeclaration = CreateVertexDeclarationWithoutAddRef(vertexElements);
    }

    for (auto& [pixelShaderSubPermutations, pixelShader] : defaultFindResult->second.m_PixelShaders)
    {
        if (pixelShader.get() == nullptr || (pixelShaderSubPermutations & 0x3) != pixelShaderSubPermutationsToCompile)
            continue;

        for (auto& [vertexShaderSubPermutations, vertexShader] : noneFindResult->second->m_VertexShaders)
        {
            if (vertexShader.get() == nullptr || (vertexShaderSubPermutations & 0x1) != vertexShaderSubPermutationsToCompile)
                continue;

            PipelineState pipelineState{};
            pipelineState.vertexShader = reinterpret_cast<GuestShader*>(vertexShader->m_spCode->m_pD3DVertexShader.get());
            pipelineState.pixelShader = reinterpret_cast<GuestShader*>(pixelShader->m_spCode->m_pD3DPixelShader.get());
            pipelineState.vertexDeclaration = vertexDeclaration;
            pipelineState.instancing = instancing;
            pipelineState.zWriteEnable = !isSky && mesh.layer != MeshLayer::Transparent;
            pipelineState.srcBlend = RenderBlend::SRC_ALPHA;
            pipelineState.destBlend = mesh.material->m_Additive ? RenderBlend::ONE : RenderBlend::INV_SRC_ALPHA;
            pipelineState.cullMode = mesh.material->m_DoubleSided ? RenderCullMode::NONE : RenderCullMode::BACK;
            pipelineState.zFunc = RenderComparisonFunction::GREATER_EQUAL; // Reverse Z
            pipelineState.alphaBlendEnable = mesh.layer == MeshLayer::Transparent || mesh.layer == MeshLayer::Special;
            pipelineState.srcBlendAlpha = RenderBlend::SRC_ALPHA;
            pipelineState.destBlendAlpha = RenderBlend::INV_SRC_ALPHA;
            pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
            pipelineState.vertexStrides[0] = mesh.vertexSize;

            if (args.instancing)
            {
                pipelineState.vertexStrides[1] = 24;
                pipelineState.vertexStrides[2] = 4;
            }
            else if (isFur)
            {
                pipelineState.vertexStrides[1] = 4;
                pipelineState.vertexStrides[2] = 4;
            }

            pipelineState.renderTargetFormat = RenderFormat::R16G16B16A16_FLOAT;
            pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT;
            pipelineState.sampleCount = Config::AntiAliasing != EAntiAliasing::None ? int32_t(Config::AntiAliasing.Value) : 1;

            if (pipelineState.vertexDeclaration->hasR11G11B10Normal)
                pipelineState.specConstants |= SPEC_CONSTANT_R11G11B10_NORMAL;

            if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
                pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;

            if (mesh.layer == MeshLayer::PunchThrough)
            {
                if (Config::AntiAliasing != EAntiAliasing::None && Config::TransparencyAntiAliasing)
                {
                    pipelineState.enableAlphaToCoverage = true;
                    pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                }
                else
                {
                    pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
                }
            }

            if (!isSky)
                pipelineState.specConstants |= SPEC_CONSTANT_REVERSE_Z;

            auto createGraphicsPipeline = [&](PipelineState& pipelineStateToCreate)
                {
                    SanitizePipelineState(pipelineStateToCreate);
                    EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, args.tokenPair, shaderList->m_TypeAndName.c_str() + 3);

                    // Morph models have 4 targets where unused targets default to the first vertex stream.
                    if (mesh.morphModel)
                    {
                        for (size_t i = 0; i < 5; i++)
                        {
                            for (size_t j = 0; j < 4; j++)
                                pipelineStateToCreate.vertexStrides[j + 1] = i > j ? mesh.morphTargetVertexSize : mesh.vertexSize;

                            SanitizePipelineState(pipelineStateToCreate);
                            EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, args.tokenPair, shaderList->m_TypeAndName.c_str() + 3);
                        }
                    }
                };

            createGraphicsPipeline(pipelineState);

            // We cannot rely on this being accurate during loading as SceneEffect.prm.xml gets loaded a bit later.
            bool planarReflectionEnabled = *reinterpret_cast<bool*>(g_memory.Translate(0x832FA0D8));
            bool loading = *SWA::SGlobals::ms_IsLoading;
            bool compileNoMsaaPipeline = pipelineState.sampleCount != 1 && (loading || planarReflectionEnabled);

            auto noMsaaPipeline = pipelineState;
            noMsaaPipeline.sampleCount = 1;
            noMsaaPipeline.enableAlphaToCoverage = false;

            if ((noMsaaPipeline.specConstants & SPEC_CONSTANT_ALPHA_TO_COVERAGE) != 0)
            {
                noMsaaPipeline.specConstants &= ~SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                noMsaaPipeline.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
            }

            if (compileNoMsaaPipeline)
            {
                // Planar reflections don't use MSAA.
                createGraphicsPipeline(noMsaaPipeline);
            }

            if (args.objectIcon) 
            {
                // Object icons get rendered to a SDR buffer without MSAA.
                auto iconPipelineState = noMsaaPipeline;
                iconPipelineState.renderTargetFormat = RenderFormat::R8G8B8A8_UNORM;
                createGraphicsPipeline(iconPipelineState);
            }

            if (isSonicMouth)
            {
                // Sonic's mouth switches between "SonicSkin_dspf[b]" or "SonicSkinNodeInvX_dspf[b]" depending on the view angle.
                auto mouthPipelineState = pipelineState;
                mouthPipelineState.vertexShader = FindShaderCacheEntry(0x689AA3140AB9EBAA)->guestShader;
                createGraphicsPipeline(mouthPipelineState);

                if (compileNoMsaaPipeline)
                {
                    auto noMsaaMouthPipelineState = noMsaaPipeline;
                    noMsaaMouthPipelineState.vertexShader = mouthPipelineState.vertexShader;
                    createGraphicsPipeline(noMsaaMouthPipelineState);
                }
            }
        }
    }
}

static void CompileMeshPipeline(Hedgehog::Mirage::CMeshData* mesh, MeshLayer layer, CompilationArgs& args)
{
    CompileMeshPipeline(Mesh
        {
            mesh->m_VertexSize,
            0,
            reinterpret_cast<GuestVertexDeclaration*>(mesh->m_VertexDeclarationPtr.m_pD3DVertexDeclaration.get()),
            mesh->m_spMaterial.get(),
            layer,
            false
        }, args);
}

static void CompileMeshPipeline(Hedgehog::Mirage::CMorphModelData* morphModel, Hedgehog::Mirage::CMeshIndexData* mesh, MeshLayer layer, CompilationArgs& args)
{
    CompileMeshPipeline(Mesh
        {
            morphModel->m_VertexSize,
            morphModel->m_MorphTargetVertexSize,
            reinterpret_cast<GuestVertexDeclaration*>(morphModel->m_VertexDeclarationPtr.m_pD3DVertexDeclaration.get()),
            mesh->m_spMaterial.get(),
            layer,
            true
        }, args);
}

template<typename T>
static void CompileMeshPipelines(const T& modelData, CompilationArgs& args)
{
    for (auto& meshGroup : modelData.m_NodeGroupModels)
    {
        for (auto& mesh : meshGroup->m_OpaqueMeshes)
        {
            CompileMeshPipeline(mesh.get(), MeshLayer::Opaque, args);

            if (args.noGI) // For models that can be shown transparent (eg. medals)
                CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);
        }

        for (auto& mesh : meshGroup->m_TransparentMeshes)
            CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);

        for (auto& mesh : meshGroup->m_PunchThroughMeshes)
            CompileMeshPipeline(mesh.get(), MeshLayer::PunchThrough, args);

        for (auto& specialMeshGroup : meshGroup->m_SpecialMeshGroups)
        {
            for (auto& mesh : specialMeshGroup)
                CompileMeshPipeline(mesh.get(), MeshLayer::Special, args); // TODO: Are there layer types other than water in this game??
        }
    }

    for (auto& mesh : modelData.m_OpaqueMeshes)
    {
        CompileMeshPipeline(mesh.get(), MeshLayer::Opaque, args);

        if (args.noGI)
            CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);
    }

    for (auto& mesh : modelData.m_TransparentMeshes)
        CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);

    for (auto& mesh : modelData.m_PunchThroughMeshes)
        CompileMeshPipeline(mesh.get(), MeshLayer::PunchThrough, args);

    if constexpr (std::is_same_v<T, Hedgehog::Mirage::CModelData>)
    {
        for (auto& morphModel : modelData.m_MorphModels)
        {
            for (auto& mesh : morphModel->m_OpaqueMeshList)
                CompileMeshPipeline(morphModel.get(), mesh.get(), MeshLayer::Opaque, args);

            for (auto& mesh : morphModel->m_TransparentMeshList)
                CompileMeshPipeline(morphModel.get(), mesh.get(), MeshLayer::Transparent, args);

            for (auto& mesh : morphModel->m_PunchThroughMeshList)
                CompileMeshPipeline(morphModel.get(), mesh.get(), MeshLayer::PunchThrough, args);
        }
    }
}

static void CompileParticleMaterialPipeline(const Hedgehog::Sparkle::CParticleMaterial& material, PipelineTaskTokenPair& tokenPair)
{
    auto& shaderList = material.m_spShaderListData;
    if (shaderList.get() == nullptr)
        return;

    guest_stack_var<Hedgehog::Base::CStringSymbol> defaultSymbol(reinterpret_cast<const char*>(g_memory.Translate(0x8202DDBC)));
    auto defaultFindResult = shaderList->m_PixelShaderPermutations.find(*defaultSymbol);
    if (defaultFindResult == shaderList->m_PixelShaderPermutations.end())
        return;

    guest_stack_var<Hedgehog::Base::CStringSymbol> noneSymbol(reinterpret_cast<const char*>(g_memory.Translate(0x8200D938)));
    auto noneFindResult = defaultFindResult->second.m_VertexShaderPermutations.find(*noneSymbol);
    if (noneFindResult == defaultFindResult->second.m_VertexShaderPermutations.end())
        return;

    // All the particle models in the game come with the unoptimized format, so we can assume it.
    uint8_t unoptimizedVertexElements[144] = 
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x0C, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x18, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x06, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x24, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x07, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x30, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x38, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x40, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x48, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x03, 0x00,
        0x00, 0x00, 0x00, 0x50, 0x00, 0x1A, 0x23, 0xA6, 0x00, 0x0A, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x60, 0x00, 0x1A, 0x23, 0x86, 0x00, 0x02, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x64, 0x00, 0x1A, 0x20, 0x86, 0x00, 0x01, 0x00, 0x00,
        0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
    };

    auto unoptimizedVertexDeclaration = CreateVertexDeclarationWithoutAddRef(reinterpret_cast<GuestVertexElement*>(unoptimizedVertexElements));
    auto sparkleVertexDeclaration = CreateVertexDeclarationWithoutAddRef(reinterpret_cast<GuestVertexElement*>(g_memory.Translate(0x8211F540)));

    bool isMeshShader = strstr(shaderList->m_TypeAndName.c_str(), "Mesh") != nullptr;

    PipelineState pipelineState{};
    pipelineState.vertexShader = reinterpret_cast<GuestShader*>(noneFindResult->second->m_VertexShaders.begin()->second->m_spCode->m_pD3DVertexShader.get());
    pipelineState.pixelShader = reinterpret_cast<GuestShader*>(defaultFindResult->second.m_PixelShaders.begin()->second->m_spCode->m_pD3DPixelShader.get());
    pipelineState.vertexDeclaration = isMeshShader ? unoptimizedVertexDeclaration : sparkleVertexDeclaration;
    pipelineState.zWriteEnable = false;
    pipelineState.zFunc = RenderComparisonFunction::GREATER_EQUAL;
    pipelineState.alphaBlendEnable = true;
    pipelineState.srcBlendAlpha = RenderBlend::SRC_ALPHA;
    pipelineState.destBlendAlpha = RenderBlend::INV_SRC_ALPHA;
    pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
    pipelineState.vertexStrides[0] = isMeshShader ? 104 : 28;
    pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT;
    pipelineState.specConstants = SPEC_CONSTANT_REVERSE_Z;

    if (pipelineState.vertexDeclaration->hasR11G11B10Normal)
        pipelineState.specConstants |= SPEC_CONSTANT_R11G11B10_NORMAL;

    switch (material.m_BlendMode.get())
    {
    case Hedgehog::Sparkle::CParticleMaterial::eBlendMode_Zero:
        pipelineState.srcBlend = RenderBlend::ZERO;
        pipelineState.destBlend = RenderBlend::ZERO;
        break;
    case Hedgehog::Sparkle::CParticleMaterial::eBlendMode_Typical:
        pipelineState.srcBlend = RenderBlend::SRC_ALPHA;
        pipelineState.destBlend = RenderBlend::INV_SRC_ALPHA;
        break;
    case Hedgehog::Sparkle::CParticleMaterial::eBlendMode_Add:
        pipelineState.srcBlend = RenderBlend::SRC_ALPHA;
        pipelineState.destBlend = RenderBlend::ONE;
        break;
    default:
        pipelineState.srcBlend = RenderBlend::ONE;
        pipelineState.destBlend = RenderBlend::ONE;
        break;
    }

    auto createGraphicsPipeline = [&](PipelineState& pipelineStateToCreate)
        {
            SanitizePipelineState(pipelineStateToCreate);
            EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, tokenPair, shaderList->m_TypeAndName.c_str() + 3);
        };

    // Mesh particles can use both cull modes. Quad particles are only NONE.
    RenderCullMode cullModes[] = { RenderCullMode::NONE, RenderCullMode::BACK };
    uint32_t cullModeCount = isMeshShader ? std::size(cullModes) : 1;
    RenderFormat renderTargetFormats[] = { RenderFormat::R16G16B16A16_FLOAT, RenderFormat::R8G8B8A8_UNORM };

    for (size_t i = 0; i < cullModeCount; i++)
    {
        pipelineState.cullMode = cullModes[i];

        for (auto renderTargetFormat : renderTargetFormats)
        {
            pipelineState.renderTargetFormat = renderTargetFormat;

            if (renderTargetFormat == RenderFormat::R16G16B16A16_FLOAT)
                pipelineState.sampleCount = Config::AntiAliasing != EAntiAliasing::None ? int32_t(Config::AntiAliasing.Value) : 1;
            else
                pipelineState.sampleCount = 1;

            createGraphicsPipeline(pipelineState);

            // Always compile no MSAA variant for particles, as the planar
            // reflection variable isn't reliable at this time of compilation.
            bool compileNoMsaaPipeline = pipelineState.sampleCount != 1;

            auto noMsaaPipelineState = pipelineState;
            noMsaaPipelineState.sampleCount = 1;

            if (compileNoMsaaPipeline)
                createGraphicsPipeline(noMsaaPipelineState);

            if (!isMeshShader)
            {
                // Previous compilation was for locus particles. This one will be for quads.
                auto quadPipelineState = pipelineState;
                quadPipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
                createGraphicsPipeline(quadPipelineState);

                if (compileNoMsaaPipeline)
                {
                    auto noMsaaQuadPipelineState = noMsaaPipelineState;
                    noMsaaQuadPipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
                    createGraphicsPipeline(noMsaaQuadPipelineState);
                }
            }
        }
    }
}

static std::vector<uint16_t*> g_newIndicesToFree;

static void FreePendingConvertedIndices()
{
    for (auto newIndicesToFree : g_newIndicesToFree)
        g_userHeap.Free(newIndicesToFree);

    g_newIndicesToFree.clear();
}

// SWA::CGameModeStage::ExitLoading
PPC_FUNC_IMPL(__imp__sub_825369A0);
PPC_FUNC(sub_825369A0)
{
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
    assert(std::this_thread::get_id() == g_mainThreadId);
#endif

    // Wait for pipeline compilations to finish.
#if defined(__APPLE__) && TARGET_OS_IOS
    const auto waitStart = std::chrono::steady_clock::now();
    auto nextWaitLog = waitStart + std::chrono::seconds(5);
    const uint32_t initialPendingCount = g_compilingPipelineTaskCount.load();
    if (initialPendingCount != 0)
    {
        LOGFN("Pipeline loading wait started - remainingTasks: {}", initialPendingCount);
        os::logger::LogRuntimeDiagnostics("Pipeline loading wait started");
    }
#endif
    uint32_t value;
    while ((value = g_compilingPipelineTaskCount.load()) != 0)
    {
    #if !defined(__APPLE__) || !TARGET_OS_IPHONE
        // Pump SDL events to prevent the OS
        // from thinking the process is unresponsive.
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    #endif

#if defined(__APPLE__) && TARGET_OS_IOS
        // atomic::wait has no timeout and the task counter only notifies when
        // it reaches zero. Keep returning to UIKit while Metal compiles a
        // large stage; Xcode's debugger can mask the normal iOS watchdog.
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextWaitLog)
        {
            LOGFN("Pipeline loading wait - elapsedMs: {}, remainingTasks: {}",
                std::chrono::duration_cast<std::chrono::milliseconds>(now - waitStart).count(), value);
            os::logger::LogRuntimeDiagnostics("Pipeline loading wait pending");
            nextWaitLog = now + std::chrono::seconds(5);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#else
        g_compilingPipelineTaskCount.wait(value);
#endif
    }

#if defined(__APPLE__) && TARGET_OS_IOS
    if (initialPendingCount != 0)
    {
        LOGFN("Pipeline loading wait completed - elapsedMs: {}, initialTasks: {}",
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - waitStart).count(), initialPendingCount);
        os::logger::LogRuntimeDiagnostics("Pipeline loading wait completed");
    }
#endif

    __imp__sub_825369A0(ctx, base);

    // Safety net: converted strip indices can survive until stage transitions
    // if their usual reset path does not run in this flow.
    FreePendingConvertedIndices();
}

// CModelData::CheckMadeAll
PPC_FUNC_IMPL(__imp__sub_82E2EFB0);
PPC_FUNC(sub_82E2EFB0)
{   
    if (reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32)->m_Flags & eDatabaseDataFlags_CompilingPipelines)
    {
        ctx.r3.u64 = 0;
    }
    else
    {
        __imp__sub_82E2EFB0(ctx, base);
    }
}

// CTerrainModelData::CheckMadeAll
PPC_FUNC_IMPL(__imp__sub_82E243D8);
PPC_FUNC(sub_82E243D8)
{   
    if (reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32)->m_Flags & eDatabaseDataFlags_CompilingPipelines)
    {
        ctx.r3.u64 = 0;
    }
    else
    {
        __imp__sub_82E243D8(ctx, base);
    }
}

// CParticleMaterial::CheckMadeAll
PPC_FUNC_IMPL(__imp__sub_82E87598);
PPC_FUNC(sub_82E87598)
{   
    if (reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32)->m_Flags & eDatabaseDataFlags_CompilingPipelines)
    {
        ctx.r3.u64 = 0;
    }
    else
    {
        __imp__sub_82E87598(ctx, base);
    }
}

void GetDatabaseDataMidAsmHook(PPCRegister& r1, PPCRegister& r4)
{
    auto& databaseData = *reinterpret_cast<boost::shared_ptr<Hedgehog::Database::CDatabaseData>*>(
        g_memory.Translate(r1.u32 + 0x58));

    if (!databaseData->IsMadeOne() && r4.u32 != NULL)
    {
        if (databaseData->m_pVftable.ptr == MODEL_DATA_VFTABLE)
        {
            // Ignore particle models, the materials they point at don't actually
            // get used and give the threads unnecessary work.
            bool isParticleModel = *reinterpret_cast<be<uint32_t>*>(g_memory.Translate(r4.u32 + 4)) != 5 &&
                strncmp(databaseData->m_TypeAndName.c_str() + 2, "eff_", 4) == 0;

            if (isParticleModel)
                return;

            // Adabat water is broken in original game, which they tried to fix by partially including the files in the update,
            // which then finally fixed for real in the DLC. This confuses the async PSO compiler and causes a hang if the DLC is missing.
            // We'll just ignore it.
            bool isAdabatWater = strcmp(databaseData->m_TypeAndName.c_str() + 2, "evl_sea_obj_st_waterCircle") == 0;
            if (isAdabatWater)
                return;
        }

        databaseData->m_Flags |= eDatabaseDataFlags_CompilingPipelines;
        EnqueuePipelineTask(PipelineTaskType::DatabaseData, databaseData);
    }
}

static bool CheckMadeAll(Hedgehog::Mirage::CMeshData* meshData)
{
    if (!meshData->IsMadeOne())
        return false;

    if (meshData->m_spMaterial.get() != nullptr)
    {
        if (!meshData->m_spMaterial->IsMadeOne())
            return false;

        if (meshData->m_spMaterial->m_spTexsetData.get() != nullptr)
        {
            if (!meshData->m_spMaterial->m_spTexsetData->IsMadeOne())
                return false;

            for (auto& texture : meshData->m_spMaterial->m_spTexsetData->m_TextureList)
            {
                if (!texture->IsMadeOne())
                    return false;
            }
        }
    }

    return true;
}

template<typename T>
static bool CheckMadeAll(const T& modelData)
{
    if (!modelData.IsMadeOne())
        return false;

    for (auto& meshGroup : modelData.m_NodeGroupModels)
    {
        for (auto& mesh : meshGroup->m_OpaqueMeshes)
        {
            if (!CheckMadeAll(mesh.get()))
                return false;
        }     

        for (auto& mesh : meshGroup->m_TransparentMeshes)
        {
            if (!CheckMadeAll(mesh.get()))
                return false;
        }    

        for (auto& mesh : meshGroup->m_PunchThroughMeshes)
        {
            if (!CheckMadeAll(mesh.get()))
                return false;
        }

        for (auto& specialMeshGroup : meshGroup->m_SpecialMeshGroups)
        {
            for (auto& mesh : specialMeshGroup)
            {
                if (!CheckMadeAll(mesh.get()))
                    return false;
            }
        }
    }

    for (auto& mesh : modelData.m_OpaqueMeshes)
    {
        if (!CheckMadeAll(mesh.get()))
            return false;
    }

    for (auto& mesh : modelData.m_TransparentMeshes)
    {
        if (!CheckMadeAll(mesh.get()))
            return false;
    }

    for (auto& mesh : modelData.m_PunchThroughMeshes)
    {
        if (!CheckMadeAll(mesh.get()))
            return false;
    }

    return true;
}

static void PipelineTaskConsumerThread()
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
    GuestThread::SetThreadName(GetCurrentThreadId(), "Pipeline Task Consumer Thread");
#endif

    std::vector<PipelineTask> localPipelineTaskQueue;
    std::unique_ptr<GuestThreadContext> ctx;

    while (true)
    {
        // Wait for tasks to arrive.
        uint32_t pendingPipelineTaskCount;
        while ((pendingPipelineTaskCount = g_pendingPipelineTaskCount.load()) == 0)
            g_pendingPipelineTaskCount.wait(pendingPipelineTaskCount);

#if defined(UNLEASHED_RECOMP_IOS)
        app_lifecycle::GetExecutionGate().WaitUntilActive();
#endif

        if (ctx == nullptr)
            ctx = std::make_unique<GuestThreadContext>(0);

        {
            std::lock_guard lock(g_pipelineTaskMutex);
            localPipelineTaskQueue.insert(localPipelineTaskQueue.end(), g_pipelineTaskQueue.begin(), g_pipelineTaskQueue.end());
            g_pipelineTaskQueue.clear();
        }

        bool allHandled = true;

        for (auto& [type, databaseData] : localPipelineTaskQueue)
        {
            switch (type)
            {
            case PipelineTaskType::DatabaseData:
            {
                bool ready = false;

                if (databaseData->m_pVftable.ptr == MODEL_DATA_VFTABLE)
                    ready = CheckMadeAll(*reinterpret_cast<Hedgehog::Mirage::CModelData*>(databaseData.get()));
                else
                    ready = databaseData->IsMadeOne();

                if (ready || databaseData.unique())
                {
                    if (databaseData->m_pVftable.ptr == TERRAIN_MODEL_DATA_VFTABLE)
                    {
                        CompilationArgs args{};
                        args.tokenPair.token.type = type;
                        args.tokenPair.token.databaseData = databaseData;
                        args.instancing = strncmp(databaseData->m_TypeAndName.c_str() + 3, "ins", 3) == 0;
                        CompileMeshPipelines(*reinterpret_cast<Hedgehog::Mirage::CTerrainModelData*>(databaseData.get()), args);
                    }
                    else if (databaseData->m_pVftable.ptr == PARTICLE_MATERIAL_VFTABLE)
                    {
                        PipelineTaskTokenPair tokenPair;
                        tokenPair.token.type = type;
                        tokenPair.token.databaseData = databaseData;
                        CompileParticleMaterialPipeline(*reinterpret_cast<Hedgehog::Sparkle::CParticleMaterial*>(databaseData.get()), tokenPair);
                    }
                    else
                    {
                        assert(databaseData->m_pVftable.ptr == MODEL_DATA_VFTABLE);

                        auto modelData = reinterpret_cast<Hedgehog::Mirage::CModelData*>(databaseData.get());

                        CompilationArgs args{};
                        args.tokenPair.token.type = type;
                        args.tokenPair.token.databaseData = databaseData;
                        args.noGI = true;
                        args.hasMoreThanOneBone = modelData->m_NodeNum > 1;
                        args.velocityMapQuickStep = strcmp(databaseData->m_TypeAndName.c_str() + 2, "SonicRoot") == 0;

                        // Check for the on screen items, eg. rings going to HUD.
                        auto items = reinterpret_cast<xpointer<const char>*>(g_memory.Translate(0x832A8DD0));
                        for (size_t i = 0; i < 50; i++)
                        {
                            if (strcmp(databaseData->m_TypeAndName.c_str() + 2, (*items).get()) == 0)
                            {
                                args.objectIcon = true;
                                break;
                            }
                            items += 7;
                        }

                        CompileMeshPipelines(*modelData, args);
                    }

                    type = PipelineTaskType::Null;
                    databaseData = nullptr;

                    --g_pendingPipelineTaskCount;
                }
                else
                {
                    allHandled = false;
                }

                break;
            }

            case PipelineTaskType::PrecompilePipelines:
            {
                // Deliberately leaving the type null to account for the enqueue
                // call not incrementing the compiling pipeline task counter.
                PipelineTaskTokenPair tokenPair;

                for (auto vertexElements : g_vertexDeclarationCache)
                    CreateVertexDeclarationWithoutAddRef(reinterpret_cast<GuestVertexElement*>(vertexElements));

                for (auto pipelineState : g_pipelineStateCache)
                {
                    // The hashes were reinterpret casted to pointers in the cache.
                    pipelineState.vertexShader = FindShaderCacheEntry(reinterpret_cast<XXH64_hash_t>(pipelineState.vertexShader))->guestShader;

                    if (pipelineState.pixelShader != nullptr)
                        pipelineState.pixelShader = FindShaderCacheEntry(reinterpret_cast<XXH64_hash_t>(pipelineState.pixelShader))->guestShader;

                    {
                        std::lock_guard lock(g_vertexDeclarationMutex);
                        pipelineState.vertexDeclaration = g_vertexDeclarations[reinterpret_cast<XXH64_hash_t>(pipelineState.vertexDeclaration)];
                    }

                    if (!g_capabilities.triangleFan && pipelineState.primitiveTopology == RenderPrimitiveTopology::TRIANGLE_FAN)
                        pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;

                    // Zero out depth bias for Vulkan/Metal, we only store common values for D3D12.
                    if (g_capabilities.dynamicDepthBias && g_backend != Backend::D3D12)
                    {
                        pipelineState.depthBias = 0;
                        pipelineState.slopeScaledDepthBias = 0.0f;
                    }

                    if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
                        pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;

                    auto createGraphicsPipeline = [&](PipelineState& pipelineStateToCreate, const char* name)
                        {
                            SanitizePipelineState(pipelineStateToCreate);
                            EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, tokenPair, name, true);
                        };

                    // Compile both MSAA and non MSAA variants to work with reflection maps. The render formats are an assumption but it should hold true.
                    if (Config::AntiAliasing != EAntiAliasing::None &&
                        pipelineState.renderTargetFormat == RenderFormat::R16G16B16A16_FLOAT && 
                        pipelineState.depthStencilFormat == RenderFormat::D32_FLOAT)
                    {
                        auto msaaPipelineState = pipelineState;
                        msaaPipelineState.sampleCount = int32_t(Config::AntiAliasing.Value);

                        if (Config::TransparencyAntiAliasing && (msaaPipelineState.specConstants & SPEC_CONSTANT_ALPHA_TEST) != 0)
                        {
                            msaaPipelineState.enableAlphaToCoverage = true;
                            msaaPipelineState.specConstants &= ~SPEC_CONSTANT_ALPHA_TEST;
                            msaaPipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                        }

                        createGraphicsPipeline(msaaPipelineState, "Precompiled Pipeline MSAA");
                    }

                    if (pipelineState.pixelShader != nullptr &&
                        pipelineState.pixelShader->shaderCacheEntry != nullptr)
                    {
                        XXH64_hash_t hash = pipelineState.pixelShader->shaderCacheEntry->hash;

                        // Compile the custom gaussian blur shaders that we pass to the game.
                        if (hash == 0x4294510C775F4EE8)
                        {
                            for (auto& shader : g_gaussianBlurShaders)
                            {
                                auto newPipelineState = pipelineState;
                                newPipelineState.pixelShader = shader.get();
                                createGraphicsPipeline(newPipelineState, "Precompiled Gaussian Blur Pipeline");
                            }
                        }
                        // Compile enhanced motion blur shader.
                        else if (hash == 0x6B9732B4CD7E7740)
                        {
                            auto newPipelineState = pipelineState;
                            newPipelineState.pixelShader = g_enhancedMotionBlurShader.get();
                            createGraphicsPipeline(newPipelineState, "Precompiled Enhanced Motion Blur Pipeline");
                        }
                    }
                
                    createGraphicsPipeline(pipelineState, "Precompiled Pipeline");

                    // Compile the CSD filter shader that we pass to the game when point filtering is used.
                    if (pipelineState.pixelShader == g_csdShader)
                    {
                        pipelineState.pixelShader = g_csdFilterShader.get();
                        createGraphicsPipeline(pipelineState, "Precompiled CSD Filter Pipeline");
                    }
                }

                type = PipelineTaskType::Null;
                --g_pendingPipelineTaskCount;

                break;
            }

            case PipelineTaskType::RecompilePipelines:
            {
                PipelineTaskTokenPair tokenPair;
                tokenPair.token.type = type;

                auto asyncPipelines = g_asyncPipelineStates.values();

                for (auto& [hash, pipelineState] : asyncPipelines)
                {
                    bool alphaTest = (pipelineState.specConstants & (SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE)) != 0;
                    bool msaa = pipelineState.sampleCount != 1 || (pipelineState.renderTargetFormat == RenderFormat::R16G16B16A16_FLOAT && pipelineState.depthStencilFormat == RenderFormat::D32_FLOAT);

                    pipelineState.sampleCount = 1;
                    pipelineState.enableAlphaToCoverage = false;
                    pipelineState.specConstants &= ~(SPEC_CONSTANT_BICUBIC_GI_FILTER | SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE);

                    if (msaa && Config::AntiAliasing != EAntiAliasing::None)
                    {
                        pipelineState.sampleCount = int32_t(Config::AntiAliasing.Value);

                        if (alphaTest)
                        {
                            if (Config::TransparencyAntiAliasing)
                            {
                                pipelineState.enableAlphaToCoverage = true;
                                pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                            }
                            else
                            {
                                pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
                            }
                        }
                    }
                    else if (alphaTest)
                    {
                        pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
                    }

                    if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
                        pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;

                    SanitizePipelineState(pipelineState);
                    EnqueueGraphicsPipelineCompilation(pipelineState, tokenPair, "Recompiled Pipeline State");
                }

                type = PipelineTaskType::Null;
                --g_pendingPipelineTaskCount;

                break;
            }
            }
        }

        if (allHandled)
            localPipelineTaskQueue.clear();

        std::this_thread::yield();
    }
}

static std::thread g_pipelineTaskConsumerThread(PipelineTaskConsumerThread);

#if defined(ASYNC_PSO_DEBUG) || defined(UNLEASHED_RECOMP_IOS)

PPC_FUNC_IMPL(__imp__sub_82E33330);
PPC_FUNC(sub_82E33330)
{
    auto vertexShaderCode = reinterpret_cast<Hedgehog::Mirage::CVertexShaderCodeData*>(g_memory.Translate(ctx.r4.u32));
    __imp__sub_82E33330(ctx, base);
    reinterpret_cast<GuestShader*>(vertexShaderCode->m_pD3DVertexShader.get())->name = vertexShaderCode->m_TypeAndName.c_str() + 3;
}

PPC_FUNC_IMPL(__imp__sub_82E328D8);
PPC_FUNC(sub_82E328D8)
{
    auto pixelShaderCode = reinterpret_cast<Hedgehog::Mirage::CPixelShaderCodeData*>(g_memory.Translate(ctx.r4.u32));
    __imp__sub_82E328D8(ctx, base);
    reinterpret_cast<GuestShader*>(pixelShaderCode->m_pD3DPixelShader.get())->name = pixelShaderCode->m_TypeAndName.c_str() + 2;
}

#endif

#ifdef PSO_CACHING
class SDLEventListenerForPSOCaching : public SDLEventListener
{
public:
    bool OnSDLEvent(SDL_Event* event) override 
    {
        if (event->type != SDL_QUIT)
            return false;

        std::lock_guard lock(g_pipelineCacheMutex);
        if (g_pipelineStatesToCache.empty())
            return false;

        FILE* f = fopen("send_this_file_to_skyth.txt", "ab");
        if (f != nullptr)
        {
            ankerl::unordered_dense::set<GuestVertexDeclaration*> vertexDeclarations;
            xxHashMap<PipelineState> pipelineStatesToCache;

            for (auto& [hash, pipelineState] : g_pipelineStatesToCache)
            {
                if (pipelineState.vertexShader->shaderCacheEntry == nullptr ||
                    (pipelineState.pixelShader != nullptr && pipelineState.pixelShader->shaderCacheEntry == nullptr))
                {
                    continue;
                }

                vertexDeclarations.emplace(pipelineState.vertexDeclaration);

                // Mask out the config options.
                pipelineState.sampleCount = 1;
                pipelineState.enableAlphaToCoverage = false;

                pipelineState.specConstants &= ~SPEC_CONSTANT_BICUBIC_GI_FILTER;
                if ((pipelineState.specConstants & SPEC_CONSTANT_ALPHA_TO_COVERAGE) != 0)
                {
                    pipelineState.specConstants &= ~SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                    pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
                }

                pipelineStatesToCache.emplace(XXH3_64bits(&pipelineState, sizeof(pipelineState)), pipelineState);
            }

            for (auto vertexDeclaration : vertexDeclarations)
            {
                fmt::print(f, "static uint8_t g_vertexElements_{:016X}[] = {{", vertexDeclaration->hash);

                auto bytes = reinterpret_cast<uint8_t*>(vertexDeclaration->vertexElements.get());
                for (size_t i = 0; i < vertexDeclaration->vertexElementCount * sizeof(GuestVertexElement); i++)
                    fmt::print(f, "0x{:X},", bytes[i]);

                fmt::println(f, "}};");
            }

            for (auto& [pipelineHash, pipelineState] : pipelineStatesToCache)
            {
                fmt::println(f, "{{ "
                    "reinterpret_cast<GuestShader*>(0x{:X}),"
                    "reinterpret_cast<GuestShader*>(0x{:X}),"
                    "reinterpret_cast<GuestVertexDeclaration*>(0x{:X}),"
                    "{},"
                    "{},"
                    "{},"
                    "RenderBlend::{},"
                    "RenderBlend::{},"
                    "RenderCullMode::{},"
                    "RenderComparisonFunction::{},"
                    "{},"
                    "RenderBlendOperation::{},"
                    "{},"
                    "{},"
                    "RenderBlend::{},"
                    "RenderBlend::{},"
                    "RenderBlendOperation::{},"
                    "0x{:X},"
                    "RenderPrimitiveTopology::{},"
                    "{{ {},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{} }},"
                    "RenderFormat::{},"
                    "RenderFormat::{},"
                    "{},"
                    "{},"
                    "0x{:X} }},",
                    pipelineState.vertexShader->shaderCacheEntry->hash,
                    pipelineState.pixelShader != nullptr ? pipelineState.pixelShader->shaderCacheEntry->hash : 0,
                    pipelineState.vertexDeclaration->hash,
                    pipelineState.instancing,
                    pipelineState.zEnable,
                    pipelineState.zWriteEnable,
                    magic_enum::enum_name(pipelineState.srcBlend),
                    magic_enum::enum_name(pipelineState.destBlend),
                    magic_enum::enum_name(pipelineState.cullMode),
                    magic_enum::enum_name(pipelineState.zFunc),
                    pipelineState.alphaBlendEnable,
                    magic_enum::enum_name(pipelineState.blendOp),
                    pipelineState.slopeScaledDepthBias,
                    pipelineState.depthBias,
                    magic_enum::enum_name(pipelineState.srcBlendAlpha),
                    magic_enum::enum_name(pipelineState.destBlendAlpha),
                    magic_enum::enum_name(pipelineState.blendOpAlpha),
                    pipelineState.colorWriteEnable,
                    magic_enum::enum_name(pipelineState.primitiveTopology),
                    pipelineState.vertexStrides[0],
                    pipelineState.vertexStrides[1],
                    pipelineState.vertexStrides[2],
                    pipelineState.vertexStrides[3],
                    pipelineState.vertexStrides[4],
                    pipelineState.vertexStrides[5],
                    pipelineState.vertexStrides[6],
                    pipelineState.vertexStrides[7],
                    pipelineState.vertexStrides[8],
                    pipelineState.vertexStrides[9],
                    pipelineState.vertexStrides[10],
                    pipelineState.vertexStrides[11],
                    pipelineState.vertexStrides[12],
                    pipelineState.vertexStrides[13],
                    pipelineState.vertexStrides[14],
                    pipelineState.vertexStrides[15],
                    magic_enum::enum_name(pipelineState.renderTargetFormat),
                    magic_enum::enum_name(pipelineState.depthStencilFormat),
                    pipelineState.sampleCount,
                    pipelineState.enableAlphaToCoverage,
                    pipelineState.specConstants);
            }

            fclose(f);
        }

        return false;
    }
};
SDLEventListenerForPSOCaching g_sdlEventListenerForPSOCaching;
#endif

void VideoConfigValueChangedCallback(IConfigDef* config)
{
    // Config options that require internal resolution resize
    g_needsResize |=
        config == &Config::AspectRatio ||
        config == &Config::ResolutionScale ||
#if defined(UNLEASHED_RECOMP_IOS)
        config == &Config::MetalFXQuality ||
#endif
        config == &Config::AntiAliasing ||
        config == &Config::ShadowResolution;

    if (g_needsResize)
        Video::ComputeViewportDimensions();
        
    // Config options that require pipeline recompilation
    bool shouldRecompile =
        config == &Config::AntiAliasing ||
        config == &Config::TransparencyAntiAliasing ||
        config == &Config::GITextureFiltering;

    if (shouldRecompile)
        EnqueuePipelineTask(PipelineTaskType::RecompilePipelines, {});
}

// SWA::CCsdTexListMirage::SetFilter
PPC_FUNC_IMPL(__imp__sub_825E4300);
PPC_FUNC(sub_825E4300)
{
    g_csdFilterState = ctx.r5.u32 == 0 ? CsdFilterState::On : CsdFilterState::Off;
    ctx.r5.u32 = 1;
    __imp__sub_825E4300(ctx, base);
}

// SWA::CCsdPlatformMirage::EndScene
PPC_FUNC_IMPL(__imp__sub_825E2F78);
PPC_FUNC(sub_825E2F78)
{
    g_csdFilterState = CsdFilterState::Unknown;
    __imp__sub_825E2F78(ctx, base);
}

// Game shares surfaces with identical descriptions. We don't want to share shadow maps,
// so we can set its format to a depth format that still resolves to the same type in recomp,
// but manages to keep the surfaces actually separated in guest code.
void FxShadowMapInitMidAsmHook(PPCRegister& r11)
{
    uint8_t* base = g_memory.base;

    uint32_t surface = PPC_LOAD_U32(PPC_LOAD_U32(PPC_LOAD_U32(r11.u32 + 0x24) + 0x4));
    PPC_STORE_U32(surface + 0x20, D3DFMT_D24FS8);
}

// Re-render objects in the terrain shadow map instead of copying the texture.
static bool g_jumpOverStretchRect;

void FxShadowMapNoTerrainMidAsmHook(PPCRegister& r4, PPCRegister& r30)
{
    // Set the no terrain shadow map as the render target.
    uint8_t* base = g_memory.base;
    r4.u64 = PPC_LOAD_U32(r30.u32 + 0x58);
}

bool FxShadowMapMidAsmHook(PPCRegister& r4, PPCRegister& r5, PPCRegister& r6, PPCRegister& r30)
{
    if (g_jumpOverStretchRect)
    {
        // Reset for the next time shadow maps get rendered.
        g_jumpOverStretchRect = false;

        // Jump over the stretch rect call.
        return false;
    }
    else
    {
        // Mark to jump over the stretch call the next time.
        g_jumpOverStretchRect = true;

        // Jump to the beginning. Set registers accordingly to set the terrain shadow map as the render target.
        uint8_t* base = g_memory.base;
        r6.u64 = 0;
        r5.u64 = 0;
        r4.u64 = PPC_LOAD_U32(r30.u32 + 0x50);

        return true;
    }
}

// There is a bug on AMD where restart indices cause incorrect culling and prevent some triangles from being rendered.
// This seems to happen on both Windows AMD drivers and Mesa. Converting restart indices to degenerate triangles fixes it.
static void ConvertToDegenerateTriangles(uint16_t* indices, uint32_t indexCount, uint16_t*& newIndices, uint32_t& newIndexCount)
{
    newIndices = reinterpret_cast<uint16_t*>(g_userHeap.Alloc(indexCount * sizeof(uint16_t) * 3));
    newIndexCount = 0;

    bool stripStart = true;
    uint32_t stripSize = 0;
    uint16_t lastIndex = 0;

    for (uint32_t i = 0; i < indexCount; i++)
    {
        uint16_t index = indices[i];
        if (index == 0xFFFF)
        {
            if ((stripSize % 2) != 0)
                newIndices[newIndexCount++] = lastIndex;

            stripStart = true;
            stripSize = 0;
        }
        else 
        {
            if (stripStart && newIndexCount != 0)
            {
                newIndices[newIndexCount++] = lastIndex;
                newIndices[newIndexCount++] = index;
            }

            newIndices[newIndexCount++] = index;
            stripStart = false;
            ++stripSize;
            lastIndex = index;
        }
    }
}

struct MeshResource
{
    SWA_INSERT_PADDING(0x4);
    be<uint32_t> indexCount;
    be<uint32_t> indices;
};

// Hedgehog::Mirage::CMeshData::Make
PPC_FUNC_IMPL(__imp__sub_82E44AF8);
PPC_FUNC(sub_82E44AF8)
{
    uint16_t* newIndicesToFree = nullptr;

    auto databaseData = reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32);
    if (g_triangleStripWorkaround && !databaseData->IsMadeOne())
    {
        auto meshResource = reinterpret_cast<MeshResource*>(base + ctx.r4.u32);

        if (meshResource->indexCount != 0)
        {
            uint16_t* newIndices;
            uint32_t newIndexCount;

            ConvertToDegenerateTriangles(
                reinterpret_cast<uint16_t*>(base + meshResource->indices),
                meshResource->indexCount,
                newIndices,
                newIndexCount);

            meshResource->indexCount = newIndexCount;
            meshResource->indices = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(newIndices) - base);

            if (PPC_LOAD_U32(0x83396E98) != NULL)
            {
                // If index buffers are getting merged, new indices need to survive until the merge happens.
                g_newIndicesToFree.push_back(newIndices);
            }
            else 
            {
                // Otherwise, we can free it immediately.
                newIndicesToFree = newIndices;
            }
        }
    }

    __imp__sub_82E44AF8(ctx, base);

    if (newIndicesToFree != nullptr)
        g_userHeap.Free(newIndicesToFree);
}

// Hedgehog::Mirage::CShareVertexBuffer::Reset
PPC_FUNC_IMPL(__imp__sub_82E250D0);
PPC_FUNC(sub_82E250D0)
{
    __imp__sub_82E250D0(ctx, base);
    FreePendingConvertedIndices();
}

struct LightAndIndexBufferResourceV1
{
    SWA_INSERT_PADDING(0x4);
    be<uint32_t> indexCount;
    be<uint32_t> indices;
};

// Hedgehog::Mirage::CLightAndIndexBufferData::MakeV1
PPC_FUNC_IMPL(__imp__sub_82E3AFC8);
PPC_FUNC(sub_82E3AFC8)
{
    uint16_t* newIndices = nullptr;

    auto databaseData = reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32);
    if (g_triangleStripWorkaround && !databaseData->IsMadeOne())
    {
        auto lightAndIndexBufferResource = reinterpret_cast<LightAndIndexBufferResourceV1*>(base + ctx.r4.u32);

        if (lightAndIndexBufferResource->indexCount != 0)
        {
            uint32_t newIndexCount;

            ConvertToDegenerateTriangles(
                reinterpret_cast<uint16_t*>(base + lightAndIndexBufferResource->indices),
                lightAndIndexBufferResource->indexCount,
                newIndices,
                newIndexCount);

            lightAndIndexBufferResource->indexCount = newIndexCount;
            lightAndIndexBufferResource->indices = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(newIndices) - base);
        }
    }

    __imp__sub_82E3AFC8(ctx, base);

    if (newIndices != nullptr)
        g_userHeap.Free(newIndices);
}

struct LightAndIndexBufferResourceV5
{
    SWA_INSERT_PADDING(0x8);
    be<uint32_t> indexCount;
    be<uint32_t> indices;
};

// Hedgehog::Mirage::CLightAndIndexBufferData::MakeV5
PPC_FUNC_IMPL(__imp__sub_82E3B1C0);
PPC_FUNC(sub_82E3B1C0)
{
    uint16_t* newIndices = nullptr;

    auto databaseData = reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32);
    if (g_triangleStripWorkaround && !databaseData->IsMadeOne())
    {
        auto lightAndIndexBufferResource = reinterpret_cast<LightAndIndexBufferResourceV5*>(base + ctx.r4.u32);

        if (lightAndIndexBufferResource->indexCount != 0)
        {
            uint32_t newIndexCount;

            ConvertToDegenerateTriangles(
                reinterpret_cast<uint16_t*>(base + lightAndIndexBufferResource->indices),
                lightAndIndexBufferResource->indexCount,
                newIndices,
                newIndexCount);

            lightAndIndexBufferResource->indexCount = newIndexCount;
            lightAndIndexBufferResource->indices = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(newIndices) - base);
        }
    }

    __imp__sub_82E3B1C0(ctx, base);

    if (newIndices != nullptr)
        g_userHeap.Free(newIndices);
}

GUEST_FUNCTION_HOOK(sub_82BD99B0, CreateDevice);

GUEST_FUNCTION_HOOK(sub_82BE6230, DestructResource);

GUEST_FUNCTION_HOOK(sub_82BE9300, LockTextureRect);
GUEST_FUNCTION_HOOK(sub_82BE7780, UnlockTextureRect);

GUEST_FUNCTION_HOOK(sub_82BE6B98, LockVertexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE6BE8, UnlockVertexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE61D0, GetVertexBufferDesc);

GUEST_FUNCTION_HOOK(sub_82BE6CA8, LockIndexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE6CF0, UnlockIndexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE6200, GetIndexBufferDesc);

GUEST_FUNCTION_HOOK(sub_82BE96F0, GetSurfaceDesc);

GUEST_FUNCTION_HOOK(sub_82BE04B0, GetVertexDeclaration);
GUEST_FUNCTION_HOOK(sub_82BE0530, HashVertexDeclaration);

GUEST_FUNCTION_HOOK(sub_82BDA8C0, Video::Present);
GUEST_FUNCTION_HOOK(sub_82BDD330, GetBackBuffer);

GUEST_FUNCTION_HOOK(sub_82BE9498, CreateTexture);
GUEST_FUNCTION_HOOK(sub_82BE6AD0, CreateVertexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE6BF8, CreateIndexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE95B8, CreateSurface);

GUEST_FUNCTION_HOOK(sub_82BF6400, StretchRect);

GUEST_FUNCTION_HOOK(sub_82BDD9F0, SetRenderTarget);
GUEST_FUNCTION_HOOK(sub_82BDDD38, SetDepthStencilSurface);

GUEST_FUNCTION_HOOK(sub_82BFE4C8, Clear);

GUEST_FUNCTION_HOOK(sub_82BDD8C0, SetViewport);

GUEST_FUNCTION_HOOK(sub_82BE9818, SetTexture);
GUEST_FUNCTION_HOOK(sub_82BDCFB0, SetScissorRect);

GUEST_FUNCTION_HOOK(sub_82BE5900, DrawPrimitive);
GUEST_FUNCTION_HOOK(sub_82BE5CF0, DrawIndexedPrimitive);
GUEST_FUNCTION_HOOK(sub_82BE52F8, DrawPrimitiveUP);

GUEST_FUNCTION_HOOK(sub_82BE0428, CreateVertexDeclaration);
GUEST_FUNCTION_HOOK(sub_82BE02E0, SetVertexDeclaration);

GUEST_FUNCTION_HOOK(sub_82BE1A80, CreateVertexShader);
GUEST_FUNCTION_HOOK(sub_82BE0110, SetVertexShader);

GUEST_FUNCTION_HOOK(sub_82BDD0F8, SetStreamSource);
GUEST_FUNCTION_HOOK(sub_82BDD218, SetIndices);

GUEST_FUNCTION_HOOK(sub_82BE1990, CreatePixelShader);
GUEST_FUNCTION_HOOK(sub_82BDFE58, SetPixelShader);

GUEST_FUNCTION_HOOK(sub_82C003B8, D3DXFillTexture);
GUEST_FUNCTION_HOOK(sub_82C00910, D3DXFillVolumeTexture);

GUEST_FUNCTION_HOOK(sub_82E43FC8, MakePictureData);

GUEST_FUNCTION_HOOK(sub_82E9EE38, SetResolution);

GUEST_FUNCTION_HOOK(sub_82AE2BF8, ScreenShaderInit);

// This is a buggy function that recreates framebuffers
// if the inverse capture ratio is not 2.0, but the parameter
// is completely unused and not stored, so it ends up
// recreating framebuffers every single frame instead.
GUEST_FUNCTION_STUB(sub_82BAAD38);

GUEST_FUNCTION_STUB(sub_822C15D8);
GUEST_FUNCTION_STUB(sub_822C1810);
GUEST_FUNCTION_STUB(sub_82BD97A8);
GUEST_FUNCTION_STUB(sub_82BD97E8);
GUEST_FUNCTION_STUB(sub_82BDD370); // SetGammaRamp
GUEST_FUNCTION_STUB(sub_82BE05B8);
GUEST_FUNCTION_STUB(sub_82BE9C98);
GUEST_FUNCTION_STUB(sub_82BEA308);
GUEST_FUNCTION_STUB(sub_82CD5D68);
GUEST_FUNCTION_STUB(sub_82BE9B28);
GUEST_FUNCTION_STUB(sub_82BEA018);
GUEST_FUNCTION_STUB(sub_82BEA7C0);
GUEST_FUNCTION_STUB(sub_82BFFF88); // D3DXFilterTexture
GUEST_FUNCTION_STUB(sub_82BD96D0);
