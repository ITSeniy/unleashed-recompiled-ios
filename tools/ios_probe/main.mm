#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#include <simd/simd.h>
#include <sys/mman.h>
#include <os/proc.h>
#include <cerrno>
#include <cstring>
#include <cmath>
#include <array>
#include <algorithm>
#define BCDEC_IMPLEMENTATION
#include <bcdec.h>
#include <ProcessRGB.hpp>

// Uses the same bcdec / etcpak sources and channel conversions as video.cpp.
// This proves the codec/Metal path only; it does not run the game's renderer.
static NSString *RunProbe()
{
    NSMutableString *report = [NSMutableString stringWithFormat:@"Unleashed Metal Probe\niOS %@\n\n", UIDevice.currentDevice.systemVersion];
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return [report stringByAppendingString:@"FAIL: Metal device unavailable\n"];
    NSString *bcSupport = @"API unavailable before iOS 16.4";
    if (@available(iOS 16.4, *)) bcSupport = device.supportsBCTextureCompression ? @"yes" : @"no";
    [report appendFormat:@"GPU: %@\nApple8: %@\nNative BC: %@\nArgument buffers tier: %lu\nProcess budget: %.0f MiB\n\n",
        device.name, [device supportsFamily:MTLGPUFamilyApple8] ? @"yes" : @"no",
        bcSupport,
        device.argumentBuffersSupport == MTLArgumentBuffersTier2 ? 2ul : 1ul, os_proc_available_memory() / 1048576.0];

    // Reserve, then release the guest window without touching 4 GiB of pages.
    constexpr size_t guestSize = size_t(1) << 32;
    void *guest = mmap(nullptr, guestSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (guest == MAP_FAILED) [report appendFormat:@"Guest 4 GiB virtual reservation: FAIL (%s)\n\n", strerror(errno)];
    else {
        [report appendString:@"Guest 4 GiB virtual reservation: PASS\n\n"];
        munmap(guest, guestSize);
    }

    NSError *error = nil;
    NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void sample_block(texture2d<float, access::sample> tex [[texture(0)]], "
        "device float4 *out [[buffer(0)]], uint2 xy [[thread_position_in_grid]]) { "
        "constexpr sampler s(coord::normalized, filter::nearest); "
        "out[xy.y * 4 + xy.x] = tex.sample(s, (float2(xy) + 0.5f) / 4.0f); }";
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    id<MTLComputePipelineState> pipeline = library ? [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"sample_block"] error:&error] : nil;
    if (!pipeline) return [report stringByAppendingFormat:@"FAIL: shader compilation: %@\n", error];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!queue) return [report stringByAppendingString:@"FAIL: command queue unavailable\n"];

    const std::array<uint8_t, 8> bc1 = {0x00, 0xf8, 0x00, 0x00, 0, 0, 0, 0}; // red
    const std::array<uint8_t, 8> bc1Srgb = {0x00, 0x80, 0, 0, 0, 0, 0, 0}; // mid red: checks sRGB linearization
    const std::array<uint8_t, 16> bc3 = {128, 128, 0, 0, 0, 0, 0, 0, 0x00, 0xf8, 0, 0, 0, 0, 0, 0};
    const std::array<uint8_t, 8> bc4 = {128, 128, 0, 0, 0, 0, 0, 0};
    const std::array<uint8_t, 16> bc5 = {128, 128, 0, 0, 0, 0, 0, 0, 64, 64, 0, 0, 0, 0, 0, 0};
    bool allPassed = true;
    for (int test = 0; test < 5; ++test) {
        alignas(16) uint8_t rgba[64] = {};
        alignas(16) uint64_t compressed[2] = {};
        MTLPixelFormat format;
        size_t blockBytes;
        NSString *name;
        simd_float4 expected;
        int channels;
        if (test <= 2) {
            if (test == 1) bcdec_bc3(bc3.data(), rgba, 16);
            else bcdec_bc1(test == 2 ? bc1Srgb.data() : bc1.data(), rgba, 16);
            const float sourceRed = rgba[0] / 255.0f;
            for (int i = 0; i < 16; ++i) std::swap(rgba[i * 4], rgba[i * 4 + 2]);
            if (test == 1) CompressEtc2Rgba(reinterpret_cast<const uint32_t *>(rgba), compressed, 1, 4, true);
            else CompressEtc2Rgb(reinterpret_cast<const uint32_t *>(rgba), compressed, 1, 4, true);
            format = test == 1 ? MTLPixelFormatEAC_RGBA8 : (test == 2 ? MTLPixelFormatETC2_RGB8_sRGB : MTLPixelFormatETC2_RGB8);
            blockBytes = test == 1 ? 16 : 8;
            name = test == 1 ? @"BC3 → ETC2 RGBA (alpha)" : (test == 2 ? @"BC1 → ETC2 sRGB" : @"BC1 → ETC2 RGB");
            expected = {1, 0, 0, test == 1 ? 128.0f / 255 : 1};
            if (test == 2) expected[0] = std::pow((sourceRed + 0.055f) / 1.055f, 2.4f);
            channels = 4;
        } else if (test == 3) {
            uint8_t r[16]; bcdec_bc4(bc4.data(), r, 4);
            for (int i = 0; i < 16; ++i) { rgba[i * 4 + 2] = r[i]; rgba[i * 4 + 3] = 255; }
            CompressEacR(reinterpret_cast<const uint32_t *>(rgba), compressed, 1, 4);
            format = MTLPixelFormatEAC_R11Unorm; blockBytes = 8;
            name = @"BC4 → EAC R11"; expected = {128.0f / 255, 0, 0, 1}; channels = 1;
        } else {
            uint8_t rg[32]; bcdec_bc5(bc5.data(), rg, 8);
            for (int i = 0; i < 16; ++i) { rgba[i * 4 + 2] = rg[i * 2]; rgba[i * 4 + 1] = rg[i * 2 + 1]; rgba[i * 4 + 3] = 255; }
            CompressEacRg(reinterpret_cast<const uint32_t *>(rgba), compressed, 1, 4);
            format = MTLPixelFormatEAC_RG11Unorm; blockBytes = 16;
            name = @"BC5 → EAC RG11"; expected = {128.0f / 255, 64.0f / 255, 0, 1}; channels = 2;
        }
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:4 height:4 mipmapped:NO];
        desc.storageMode = MTLStorageModeShared;
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        id<MTLBuffer> readback = [device newBufferWithLength:16 * sizeof(simd_float4) options:MTLResourceStorageModeShared];
        if (!texture || !readback) {
            [report appendFormat:@"FAIL: %@ allocation\n", name]; allPassed = false; continue;
        }
        [texture replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0 withBytes:compressed bytesPerRow:blockBytes];
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        if (!command || !encoder) {
            [report appendFormat:@"FAIL: %@ command encoding\n", name]; allPassed = false; continue;
        }
        [encoder setComputePipelineState:pipeline];
        [encoder setTexture:texture atIndex:0];
        [encoder setBuffer:readback offset:0 atIndex:0];
        [encoder dispatchThreads:MTLSizeMake(4, 4, 1) threadsPerThreadgroup:MTLSizeMake(4, 4, 1)];
        [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
        bool passed = command.status == MTLCommandBufferStatusCompleted;
        const simd_float4 *pixels = static_cast<const simd_float4 *>(readback.contents);
        float maxError = 0;
        for (int p = 0; p < 16; ++p) for (int c = 0; c < channels; ++c) {
            if (!std::isfinite(pixels[p][c])) passed = false;
            maxError = std::max(maxError, std::abs(pixels[p][c] - expected[c]));
        }
        passed &= maxError < 0.08f; // ETC2/EAC encoding is lossy.
        allPassed &= passed;
        [report appendFormat:@"%@: %@ (error %.3f)\n", passed ? @"PASS" : @"FAIL", name, maxError];
        if (command.error) [report appendFormat:@"%@\n", command.error];
    }
    [report appendFormat:@"\nTexture probe: %@\n\nThis is a hardware test, not the game.\n", allPassed ? @"PASS" : @"FAIL"];
    return report;
}

@interface ProbeController : UIViewController
@end
@implementation ProbeController
- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    UITextView *text = [[UITextView alloc] init];
    text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    text.text = @"Testing Metal textures…";
    text.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:text];
    [NSLayoutConstraint activateConstraints:@[
        [text.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:16],
        [text.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-16],
        [text.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [text.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor]]];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            NSString *report = RunProbe();
            NSURL *dir = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
            [report writeToURL:[dir URLByAppendingPathComponent:@"probe.txt"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
            NSLog(@"%@", report);
            dispatch_async(dispatch_get_main_queue(), ^{ text.text = report; });
        }
    });
}
@end

@interface ProbeDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation ProbeDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options
{
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[ProbeController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char **argv)
{
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(ProbeDelegate.class)); }
}
