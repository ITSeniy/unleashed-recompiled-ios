# iPhone 15 / SE (3rd generation) investigation

Status on 2026-10-11: an isolated iOS prototype builds and runs on the user's
physical **iPhone 15 (A16)**. The user confirmed that the first level loads and
Sonic can be controlled with the touch controls. This uses a pinned iOS platform
fork plus this Android branch's BC→ETC2/EAC converter; the Android project's
main CMake target has not been changed into a unified iOS target. See
[ios/README.md](../ios/README.md) for preparation and build instructions.

The standalone `tools/ios_probe/UnleashedMetalProbe.xcodeproj` builds for arm64
iPhone devices with Xcode 26.5 / iPhoneOS 26.5 and has been launched successfully
on the user's physical iPhone 15. All five synthetic texture tests and the 4 GiB
virtual mapping passed. That is a hardware test, not a game compatibility claim.

## What the 15 Pro limit actually means

The upstream [iOS PR #1767](https://github.com/hedge-dev/UnleashedRecomp/pull/1767)
identifies native BC/DXT texture compression as the reason for its minimum
iPhone 15 Pro. It also reports synchronization, depth-buffer and performance
problems; its author says the depth problem does not occur with the native Metal
backend. Removing the texture restriction does not resolve those other issues.

Apple's [Metal feature tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf)
place A15 and A16 in Apple8, and A17 Pro in Apple9. Native BC textures on iPhone
start with Apple9. ETC/EAC and ASTC work on earlier families. Tier 2 argument
buffers start with Apple6. Therefore A15/A16 do not require an argument-buffer
emulation path just because they lack BC textures.

The newer [Loganbogan9 iOS fork](https://github.com/Loganbogan9/UnleashedRecompiOS)
reports A16 testing and has a native Metal backend, UIKit integration, an iOS
picker, and startup diagnostics. The prototype prepares commit
`5b2348e0c556a35143f473c4277b89d526675a95` in an isolated output directory, with
reviewable patches under `ios/patches`. Its original BC fallback in
`gpu/video.cpp` can decode to RGBA8
and retains only mip 0 in that path; it also has an ASTC override cache. The
fallback runs under an iOS compile guard rather than selecting it exclusively
when the device lacks BC support. Those choices should not be copied wholesale
for the lower-memory SE target.

## Existing Android work to reuse

This branch already has the useful texture conversion in
`UnleashedRecomp/gpu/video.cpp`:

| Source | Target on a GPU with ETC2/EAC support | Bytes per 4×4 block |
| --- | --- | --- |
| Opaque BC1 | ETC2 RGB8 | 8 → 8 |
| BC1 with punch-through alpha | ETC2 RGBA8 | 8 → 16 |
| BC2 / BC3 / BC7 | ETC2 RGBA8 | 16 → 16 |
| BC4 UNORM | EAC R11 UNORM | 8 → 8 |
| BC5 UNORM | EAC RG11 UNORM | 16 → 16 |

The load path preserves mip levels, arrays and cube faces, and uses a temporary
pixel buffer per subresource rather than keeping every texture as RGBA8. sRGB
color formats select sRGB ETC2 targets. ETC2 recompression is lossy and costs CPU
time at load; a persistent conversion cache is a later optimization, after the
upload path is verified. The current SNORM fallback biases values to UNORM and
BC6H has no supported fallback; those cases need explicit handling if encountered.

`thirdparty/plume/plume_metal.cpp` already maps ETC2/EAC formats and has an iOS
capability query for BC textures. However, `supportsBCTextureCompression` needs
availability handling below iOS 16.4. Capability selection must depend on the
actual GPU, preserving native BC for supported devices.

## Requirements for a future unified Android/iOS target

1. Add a distinct iOS target to the root/game CMake configuration, with UIKit,
   an iOS plist, SDL iOS entry point, static dependencies and device signing.
   The current Apple target bundles a macOS MoltenVK dylib and selects desktop
   platform sources. `plume_apple.mm` imports AppKit and IOKit unconditionally.
2. Integrate the native Metal renderer and iOS-compiled shader cache. The current
   game uses SPIR-V/DXIL; the mere presence of `plume_metal.cpp` does not make its
   shaders Metal-compatible. The Metal-capable XenosRecomp variant and the game
   shader cache layout must be aligned. Host tools run on macOS, not on iOS.
3. Adapt sandbox paths, the picker/installer, process restart handling, UIKit's
   main event loop, controller/touch input and background/foreground transitions.
4. Route unsupported BC through the existing ETC2/EAC converter and verify Metal
   upload row pitches, small mips, cube maps, arrays, alpha and sRGB. The probe
   checks only synthetic single-block 2D uploads with GPU sampling.
5. Measure real startup/level-load/steady-state resident memory and CPU/GPU frame
   time first on iPhone 15, then on SE 3. The 4 GiB guest window is a virtual
   reservation, not 4 GiB of resident RAM. A mapping failure and a jetsam kill
   require different fixes. No memory entitlement should be assumed necessary
   before seeing the actual result under normal signed launch.

Begin performance experiments at 30 FPS, low resolution scale, no MSAA and modest
shadow resolution. These are initial profiling settings, not a claimed frame
rate. SE 3 compatibility and playable performance remain unverified.

## First physical device

A physical iPhone 15 was paired over USB, with Developer Mode enabled and device
debug services available. The device reports iOS 27.0; the installed Xcode is
26.5. Pairing and debug-service readiness alone do not prove app launch support.
The user signed and launched the probe through Xcode successfully. Its report was
retrieved directly from the app's `Documents/probe.txt` container file:

- GPU: Apple A16 GPU / Apple8; native BC: unavailable.
- Argument buffers: Tier 2. Confirmed with the corrected display in Release.
- Current process budget at probe startup: 3065 MiB (advisory, not a promise).
- 4 GiB read/write anonymous virtual reservation: PASS; immediately unmapped.
- BC1 → ETC2 RGB and BC3 → ETC2 RGBA including alpha: PASS, max error 0.008.
- BC1 → ETC2 sRGB, checking mid-red linearization: PASS, max error 0.008.
- BC4 → EAC R11: PASS, max error below 0.0005.
- BC5 → EAC RG11: PASS, max error 0.001.

The initial report is saved locally at `out/ios-probe/iphone15-probe.txt`. A
signed Release build was subsequently installed and launched with `devicectl`,
without attaching a debugger; the same tests all passed and the reported process
budget remained 3065 MiB. That report is saved at
`out/ios-probe/iphone15-release-probe.txt`. Performance and the game memory peak
are untested. No custom memory/JIT entitlement was added to the probe.

## First actual game build

The optimized arm64 iPhoneOS app was built with Xcode 26.5, signed with the
user's development team, installed and launched using `devicectl`. No debugger
was attached and the generated app entitlements contain no custom memory/JIT
entitlement. The bundle occupies approximately 92 MiB on the Mac. The app and
its build outputs remain under `out/ios`; signing settings are local to this
experiment.

The user's Android installation supplied 2,964 game files (6,116,247,853 bytes)
and 36 update files (2,999,604 bytes). The original executable and update patch
hashes match the inputs used for the native generators. The generated patched
executable and the game/update directories were copied to the iOS app's
`Documents/UnleashedRecomp`. The Android installation was read only.

The game shader cache contains 1,385 unique shaders with iPhoneOS AIR target
`air64-apple-ios16.4`. Build preparation was repeated from pinned archives and
verified to reproduce the edited prototype sources. The upstream Metal patches
also apply successfully alongside the ETC2/EAC changes.

The first normal signed launch records:

- SDL UIKit window and Metal drawable creation succeeded.
- Native BC support is false; ETC2/EAC support is true; max texture dimension
  is 16,384. These are actual runtime device capabilities.
- Guest address-space and executable loading succeeded.
- Process footprint before entering the guest was 204.5 MiB; this is **not**
  the memory consumption or peak of a loaded level.
- A later pipeline-loading wait completed at 39.9 seconds with a 638.0 MiB
  footprint and 2,434.0 MiB reported available to the process. This is a single
  diagnostic snapshot, not a measured maximum across a level or play session.
- The iOS display loop requested 30 FPS. This is **not** a measured frame rate.
- The user confirmed a visible title/game screen, then a working first level
  with controllable Sonic. Visual correctness of every scene, long play
  sessions, CPU/GPU frame times and level-load memory peaks remain unmeasured.

The prototype preserves source DDS mip levels instead of using the fork's
old mip-0 RGBA fallback or ASTC override/prewarming cache. The iOS low-end
configuration also keeps MSAA disabled. iPhone SE (3rd generation) has **not**
been tested and its performance/memory limits remain an open question.

Local reports: `out/ios/iphone15-first-launch.log`,
`out/ios/iphone15-gameplay.log`, `out/ios/iphone15-config.toml`.

## Fullscreen window and Metal HUD

The user's Device Hub screenshot showed an approximately 3:2 game surface,
large black side margins, and a portrait status bar. The first build's log
reported a 1440×960 drawable: a legacy 480×320-point window at 3× scale. Adding
the `UILaunchScreen` dictionary and `UIStatusBarHidden = true` to the bundle,
while retaining SDL's view-controller status-bar handling, resolved this on the
physical iPhone 15. No SDL orientation override was required.

The rebuilt, signed app was installed over the existing app, preserving game
data, and launched without a debugger or launch environment overrides. Its log
now reports a 2556×1179 drawable. A screenshot retrieved over USB using
`pymobiledevice3 developer dvt screenshot --userspace` confirms full-screen
touch controls and no visible status bar.

`MetalHUD` was already true in the device configuration, but its environment
setting was applied after SDL initialized the Metal window. It is now applied
before `GameWindow::Init`. The normal launch screenshot confirms Apple's HUD
is visible: A16, 2556×1179, and about 30 FPS during the loading screen. Loading
screen timing does not establish gameplay performance. The 30 FPS cap and 0.5
internal resolution scale were retained for this check. The drawable size shown
by the HUD is not the scaled internal scene resolution.

Local evidence: `out/ios/screenshots/iphone15-user-before-layout.png`,
`out/ios/screenshots/iphone15-after-layout-hud.png`,
`out/ios/iphone15-layout-hud.log`, `out/ios/iphone15-layout-launch.json`.
Preparation from clean pinned archives reproduced all six game patch files,
including the fullscreen and HUD changes, byte for byte.

The user confirmed that the HUD is visible and the game fills the screen. A
subsequent USB screenshot during the first stage shows gameplay across the
entire landscape image, 30.05 FPS, GPU time 16.37 ms, frame interval 33.28 ms,
and app memory 910.72 MB. These are one-frame HUD snapshots, not a sustained
benchmark or a level-load memory peak. The capture is saved locally as
`out/ios/screenshots/iphone15-fullscreen-hud-check.png`.
A later in-engine cutscene screenshot retains intentional 16:9 side margins
because `CutsceneAspectRatio = "Original"`, while the app window and HUD remain
2556×1179. It shows 29.92 FPS and 16.54 ms GPU time and is saved as
`out/ios/screenshots/iphone15-fullscreen-hud-cutscene.png`.
The final town screenshot, `out/ios/screenshots/iphone15-fullscreen-hud-latest.png`,
shows the full-width game view without notifications or status bar: 29.92 FPS,
13.84 ms GPU time, 33.42 ms frame interval, and 879.61 MB app memory.

## Idle stutters and presentation cadence

The user reported periodic stutters even while Sonic stood still. A signed
diagnostic build measured `MTLDrawable.presentedTime` through presented handlers,
alongside CPU frame-phase peaks. This distinguishes actual on-screen cadence
from the HUD's average FPS and GPU completion times. Detailed timing is opt-in
and logs aggregate windows of 120 frames rather than writing every frame.

The original software limiter maintained approximately 30 FPS, but immediate
Metal presentation produced alternating 16.6/49.9 ms display intervals. Adding
minimum-duration presentation improved this initially, but a longer capture
still showed periodic short/long intervals and the user reported remaining
stutters. The final variant bypasses the separate sleep/yield limiter while
capped Metal presentation with VSync is active. CAMetalLayer's drawable pool
provides producer backpressure, and now honors the game's selected two buffers
instead of always leaving the layer at its default. The existing pool-slot
rotation and drawable lifetime fixes are retained.

The iPhone 15 was tested at 30 FPS, resolution scale 0.5 and VSync enabled, with
the same idle town scene. Summary windows ending before 25 seconds after launch
were excluded to reduce startup/loading contamination:

| Mode | Measured intervals | Short (<26.7 ms) | Long (>41.7 ms) | Mean interval |
| --- | ---: | ---: | ---: | ---: |
| Original software limiter | 840 | 47 | 49 | 33.320 ms |
| Minimum duration plus software limiter | 4,320 | 99 | 113 | 33.337 ms |
| Minimum duration, native producer pacing, two drawables | 2,160 | 0 | 7 | 33.337 ms |

Thus irregular intervals fell from 11.43% to 0.32% in these captures; the runs
have different lengths and are not a controlled thermal benchmark. Most final
120-frame windows were consistently about 33.28 ms; there were still seven
isolated approximately 49.9 ms intervals, with none over 58.3 ms. The user
confirmed that the visible idle stutters disappeared with the final variant.
This is evidence for the tested scene/settings, not a guarantee for every level,
loading transition or FPS setting. The final screenshot shows 29.93 FPS and
18.44 ms GPU time, a single HUD snapshot.

The final signed Release was installed and launched without a debugger. Clean
preparation reproduces all game changes and the four modified Metal source/header
files; all eight ordered Metal dependency patches apply successfully. Android
sources remain unchanged. The additional patch is `ios/patches/plume-ios-pacing.patch`.

Local logs: `out/ios/iphone15-pacing-baseline.log`,
`out/ios/iphone15-pacing-fixed.log`, `out/ios/iphone15-native-pacing.log`.
The persistent log appends across launches, so only the latest launch in each
capture was analyzed. Aggregate data is saved in
`out/ios/iphone15-pacing-comparison.json`; screenshots are
`out/ios/screenshots/iphone15-pacing-baseline.png`,
`out/ios/screenshots/iphone15-pacing-fixed.png`, and
`out/ios/screenshots/iphone15-native-pacing.png`.

The approach follows [Apple's frame-rate guidance](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/FrameRate.html).

## Scale 1.0 and the fixed 15 FPS plateau

The user increased resolution scale from 0.5 to approximately 1.0
(`0.9999995` in the saved configuration), retaining VSync, the 30 FPS cap,
`TripleBuffering = "Auto"`, no MSAA and original shadows. This increases the
scaled render-target pixel count by approximately four times. The presentation
drawable remains 2556×1179 in both cases.

A USB screenshot in the town scene showed 15.02 FPS, GPU time 39.67 ms and a
66.57 ms frame interval. The timing log repeatedly measured 120-frame windows
at 66.56–66.58 ms. CPU submission peaks were roughly 2 ms, command-fence waits
were small, and most of the frame delay was waiting to acquire a drawable.
The GPU time exceeds the 33.33 ms budget for 30 FPS; it does not establish that
the renderer is optimally using the GPU. The fixed 15 FPS plateau is consistent
with the synchronized two-drawable pipeline repeatedly missing alternate
30 FPS presentation slots. GPU work and presentation/producer scheduling both
contribute; treating 15 FPS as the raw A16 hardware throughput would be incorrect.

A control experiment changed only `TripleBuffering` to `On` and relaunched the
same binary. The device produced a persistent black screen; the capture had no
HUD/touch controls, and the log stopped producing frame-timing summaries after
the first few presents. This identifies an unresolved failure in that mode,
not a usable triple-buffer benchmark. Its exact synchronization/lifecycle cause
has not been established. No performance comparison was inferred from it.

The original configuration was restored, preserving the user's scale 1.0 and
30 FPS cap, and the app relaunched. A fresh USB screenshot verified normal game
rendering again: 15.05 FPS, GPU time 41.55 ms and a 66.43 ms frame interval.
Game data and saves were untouched. No binary/source changes were made for
this experiment. Further GPU pass profiling and investigation of the
three-drawable startup path are separate outstanding work.

Local evidence: `out/ios/iphone15-scale1-config.toml`,
`out/ios/iphone15-scale1-timing.log`, `out/ios/iphone15-scale1-triple.log`,
`out/ios/iphone15-scale1-restored.log`; screenshots are
`out/ios/screenshots/iphone15-scale1-hud.png`,
`out/ios/screenshots/iphone15-scale1-triple-black.png`, and
`out/ios/screenshots/iphone15-scale1-restored.png`.

See [the probe instructions](../tools/ios_probe/README.md). It records GPU texture
sampling results, argument-buffer tier, native BC support, process memory budget
and the 4 GiB virtual reservation result in `Documents/probe.txt`.

## 0.5 scale / 60 FPS and the startup black screen

The subsequent 60 FPS investigation separated display scheduling from GPU work.
With scale approximately 0.5, a 60 FPS software cap, VSync off and two drawables,
the tested town scene remained at 30.05 FPS: 1,320 displayed intervals averaged
33.281 ms. CPU submission peaks were about 1.5 ms, while drawable acquisition
peaked near 26 ms. A HUD snapshot measured 18.87 ms GPU time. These measurements
do not establish a 30 FPS hardware limit.

Changing only the drawable pool to three allowed real approximately 60 FPS.
The first successful HUD capture showed 60.10 FPS, 14.42 ms GPU time and a
16.64 ms displayed interval. A longer mixed gameplay capture recorded 8,280
intervals averaging 16.809 ms (59.49 FPS), with 81 long intervals including
movement/transitions. This is not a claim that every frame or scene meets
16.7 ms. The user confirmed very smooth movement and level play. Other graphics
settings were preserved, including original shadows, no MSAA, bilinear GI and
16× anisotropic filtering.

The code now selects three drawables in iOS `Auto` at 60 FPS or higher, retaining
the tested two-drawable profile for lower caps. Selection occurs at graphics
device creation, so switching between the profiles currently requires a restart.
The tested 60 FPS configuration is `ResolutionScale = 0.5`, `FPS = 60`,
`TripleBuffering = "Auto"`, `VSync = false`, `MaxFrameLatency = 2`. The user also
confirmed smoothness with this updated Auto build. On-level GPU diagnostics
measured roughly 13–14 ms mean execution time. Thermal state was `fair` (1) and
Low Power Mode was off; the user described the phone as slightly warm and
comfortable. These sequential captures are not a controlled or sustained
thermal benchmark, and GPU clock changes were not measured.

The Auto capture before the scene repair contains 3,840 post-transition
intervals averaging 16.686 ms (59.93 FPS), with 11 long intervals. On the final
scene-repaired build the user also tested a night level and confirmed smooth
image/control with very rare stutters. Its HUD snapshot shows 60.09 FPS,
14.29 ms GPU time and a 16.64 ms interval. The complete selected mixed capture
contains 5,280 intervals averaging 17.589 ms (56.85 FPS), with 250 long
intervals and a 216.334 ms maximum. Those slower sections remain to be profiled;
the snapshot and typical cadence must not be reported as an uninterrupted
60 FPS benchmark. Evidence: `out/ios/iphone15-60-final-gameplay.log` and
`out/ios/screenshots/iphone15-60-final-gameplay.png`.
Several slower sections had mean GPU times of 17.5–18.1 ms, exceeding the
60 FPS budget. Other windows contained CPU frame-interval peaks over 800 ms.
The source of those CPU pauses has not been established, so the remaining
hitches must not all be attributed to either the GPU or thermal throttling.

The black startup with three drawables was reproduced with window diagnostics:
`scene=false`, `key=false`, portrait bounds 393×852, despite continuous GPU
rendering. The SDL fallback window had been created before a UIKit scene
connected and remained invisible. This is a window lifecycle failure, not
evidence that triple buffering fails in Metal. The iOS event loop now attaches
the existing window to the foreground scene on the main thread, retrying from
the display link if the scene is not ready. The Metal view and touch controls
stay on the same window. A forced scene-detachment device check exercised this
path and restored `scene=true`, `key=true`, landscape 852×393 bounds, visible
controls and real presentation timestamps.
Three additional consecutive startup checks passed. One reproduced the natural
SDL fallback-window race and recovered automatically; the other two already
had their scene attached. These checks cover startup presentation, not level
play. Screenshots and logs are `out/ios/iphone15-scene-repeat2` through `repeat4`;
the check results are in `out/ios/iphone15-scene-repeat-results.json`.

Bounded diagnostics record actual `MTLDrawable.presentedTime` intervals and
command-buffer GPU execution time, following Apple's
[GPU timing contract](https://developer.apple.com/documentation/metal/mtlcommandbuffer/gpustarttime).
They also record draw/render-pass counts and UIKit/thermal state; they are off
on ordinary launches. Optional per-pass sampling was added in the subsequent
native-resolution investigation below.

Local evidence: `out/ios/iphone15-60-investigation.json`,
`out/ios/iphone15-60-baseline.log`, `out/ios/iphone15-60-triple-running.log`,
`out/ios/iphone15-60-auto.log`, `out/ios/iphone15-60-repeat2.log` (reproduced
invisible window), and `out/ios/iphone15-scene-forced.log` (recovery).

## Unsigned IPA for SE 3 testing (2026-10-11)

`ios/package_ipa.py` packaged the same tested Release executable into
`out/ios/dist/UnleashedRecompiled-iOS-SE3-test-unsigned.ipa` (54,834,697 bytes,
52.3 MiB). It removes signing material only from a temporary copy. Archive
integrity, executable permissions, the arm64 CPU type, iPhoneOS minimum 16.4,
absence of a code-signature load command, and the unchanged original binary
hash were verified. No runtime test of the re-signed package or SE 3 has yet
been performed.

The original executable's XML code-signature slot contains only
`application-identifier`, `com.apple.developer.team-identifier` and
`get-task-allow`. There are no custom JIT, unsigned-executable-memory,
increased-memory-limit or extended-virtual-addressing entitlements. Guest
memory uses a 4 GiB read/write virtual mapping, not a JIT executable mapping
or 4 GiB of resident RAM. Actual address-space and physical-memory limits on
SE 3 still require a device test. The unsigned IPA contains no profile or
signature; the tester's signing tool must supply identifiers and entitlements
matching their own provisioning profile.

The archive contains six app files and no full game-data directories or saved
config. Initial settings therefore remain 0.5 scale, 30 FPS, Auto and VSync on.
Tester instructions are in [SE3_TESTER.md](../ios/SE3_TESTER.md); checksums and
packaging metadata are beside the IPA in `out/ios/dist`.

## Native-resolution investigation (2026-10-11)

The new comparison retained a 60 FPS request, VSync off, Auto buffering, no MSAA,
original shadows and motion blur, bilinear GI, and 16× anisotropic filtering.
The user was asked to remain at the same town location without moving the
camera. These are short sequential captures, not a sustained or peak A16
benchmark: scene animation, NPC positions and GPU clock changes were not
controlled. Thermal state is the OS assessment; it does not measure GPU clock
frequency or establish the exact amount of throttling.

The selected windows aggregate 120-frame batches ending in the final 20 seconds
of each saved capture. Batch boundaries may include earlier frames. Display FPS
is 1000 divided by the weighted mean actual presentation interval; reciprocal
GPU time alone is not displayed FPS.

| Capture | Scale / quality | Thermal state | Mean GPU time | Display FPS |
| --- | --- | --- | --- | --- |
| Initial stationary baseline | 1.0 / Auto | 2 serious | 22.95 ms | 43.40 |
| Same-session lower-resolution comparison | 0.5 / Auto | 2 serious | 14.63 ms | 59.94 |
| Later pass-profile baseline | 1.0 / Auto | 0 nominal | 18.51 ms | 50.21 |
| Native color-blit experiment | 1.0 / Auto | 0 nominal | 22.90 ms | 43.29 |
| Blit-disabled reverse control | 1.0 / Auto | 1 fair | 22.68 ms | 42.28 |
| Temporary lighter DOF filter | 1.0 / Low | 2 serious | 27.08 ms | 36.26 |
| Restored render, detailed counters disabled | 1.0 / Auto | 2 serious | 18.89 ms | 48.61 |
| Later 0.75-scale capture | 0.75 / Auto | 2 serious | 20.07 ms | 48.73 |

The warm baseline pair changed only `ResolutionScale` in the saved configuration.
However, unchanged `DepthOfFieldQuality = "Auto"` selects different shader
variants: the native 2556×1179 target selects `gaussian_blur_7x7`, while a 0.5
scale selects `gaussian_blur_3x3`. The shader names describe the variants;
`gaussian_blur.hlsli` samples 7 or 3 interpolated offsets per invocation, not
49 or 9 texture reads. Resolution sensitivity therefore includes an automatic
effect-quality change as well as pixel count.

A sampled baseline frame contains 67 render passes. The largest scene pass
costs roughly 2.9 ms in the earlier nominal capture; several single-draw
full-resolution passes each take roughly 0.5–1.2 ms in the fragment stage.
Vertex-stage sums are around 2.2 ms and fragment-stage sums around 15.4 ms;
the stages can overlap, so these are not additive wall-time costs. Pass numbers
alone do not identify every effect or establish how many milliseconds an
individual optimization will save.

Equal-size, equal-format non-MSAA color copies were temporarily replaced with
native Metal texture blits, retaining shader copies for conversions/depth/MSAA.
This reduced render passes from 67 to 40 but did not demonstrate a performance
benefit. The nominal-state experiment was slower than the earlier nominal
baseline; the reverse control had changing GPU costs, thermal state and draw
counts, so a precise causal percentage cannot be assigned. The entire blit
experiment was removed from the final source and installed app. The configuration
was restored to DOF Auto. The Low-filter comparison remains inconclusive because
its thermal state differed; it does not prove the filter is slower or ineffective.

The restored 1.0-scale run with detailed pass sampling disabled still exceeded
the 16.7 ms GPU budget. CPU submission peaks were around 1.3 ms, with significant
waits for fences/drawables. This supports a GPU-side workload bottleneck in the
current renderer, rather than a hidden fixed 30 FPS cap. It does not establish
that all of A16's hardware capacity is being used efficiently, or that native
60 FPS is impossible after rendering changes. Presentation intervals at 60 Hz
alternate between roughly 16.6 and 33.3 ms when frames miss a refresh, which can
make the displayed rate fluctuate sharply.

`plume-ios-gpu-profile.patch` adds optional stage-boundary sampling, gated by both
frame and pass diagnostic flags. One command list per 120 is sampled, with at
most 256 passes; normal launches do not allocate counter buffers or add GPU
sampling. GPU ticks are calibrated using paired CPU/GPU timestamps, and counters
are resolved only after command-buffer completion. See Apple's
[counter sampling instructions](https://developer.apple.com/documentation/metal/sampling-gpu-data-into-counter-sample-buffers)
and [timestamp conversion guidance](https://developer.apple.com/documentation/metal/converting-gpu-timestamps-into-cpu-time).
Xcode's trace-device listing reported this phone offline despite working
CoreDevice install/log/screenshot access, so a complete Metal System Trace was
not obtained in this session.

The restored Release build succeeded. A fresh prepared tree reproduces all seven
modified game files; all nine Metal dependency patches apply in sequence and
reproduce the compiled profiler's source/header. The existing SE 3 unsigned IPA
was preserved and does not include these newer diagnostics. Evidence is in
`out/ios/iphone15-1x-current-investigation.json`, the capture logs referenced
there, and `out/ios/screenshots/iphone15-1x-blit-profile.png` /
`iphone15-1x-dof-low-profile.png`.

A 0.75-scale trial (approximately 1917×884) changed only ResolutionScale in
saved configuration. Initial post-menu batches reached 60 FPS at about 15 ms
GPU time, but a later 20-second selection averaged 48.73 FPS and 20.07 ms GPU
time. Thermal state remained serious; geometry/camera and clocks are not fully
controlled. This is not a stable 60 FPS profile or proof that reducing
resolution causes a slowdown. Final device configuration was restored to the
user's original 1.0 scale, 60 FPS, Auto buffering, VSync off and DOF Auto, and
the restored build was launched without diagnostic environment variables.


## Instruments connection and spatial upscaling (2026-10-12)

The user reported sustained 60 FPS at 0.75 scale during level play before the
phone reached thermal throttling. This updates the interpretation of the later
warm 0.75-scale capture; that capture is not a cold-device throughput limit.

On the next session `xctrace list devices` reported the paired phone online.
A PID-attached Game Performance recording and a separate Time Profiler recording
both succeeded. The previous connection failure was not reproduced; its exact
cause remains unestablished. Commands outside the filesystem sandbox can access
CoreDevice/Instruments. No Xcode replacement or developer-settings change was
needed for these successful recordings.

The first capture uses scale 0.75000024, cap 60, VSync off and Auto buffering.
The Game Performance trace reports nominal thermal state. Its exported GPU
execution table retains only about 1.188 seconds of active data (8.811–9.999 s),
so whole-recording CPU/GPU utilization must not be inferred from that short
retained window. Clipping scheduler-state intervals to it, the main guest thread
is running about 25.7% of the time and blocked about 73.0%; the render thread is
running about 11.9% and blocked about 87.8%. A separate CPU recording collected
8,283 one-millisecond samples across all app threads. Its dominant guest-thread
leaf occurs under `Video::Present`; some kernel symbols remain unresolved.
These observations support available CPU headroom in this scene at 0.75 scale,
not an exclusion of CPU bottlenecks in all levels or during rare hitches.

Vertex and fragment execution intervals cover about 19.1% and 56.6% of that GPU
window respectively; these channels overlap and their occupancy is not total
GPU utilization. The default template did not enable GPU limiter counters or
shader profiling. Its GPU-performance-state table labels Minimum as induced due
to device conditions; that is not a measured clock frequency or proof of thermal
throttling. Evidence: `out/ios/iphone15-instruments-20261012-analysis.json`, the
Game Performance / Time Profiler `.trace` files and their exported XML tables.
The screenshot was taken later and shows 55.05 FPS / 17.62 ms GPU, not the
performance of every frame in either recording.

MetalFX Spatial is supported by the Apple8 family used by A16 (and A15); see
Apple's [feature tables](https://developer.apple.com/metal/capabilities/).
It can improve the presentation of a lower-resolution render. It has its own GPU
cost, does not recover all missing detail, and does not accelerate CPU logic or
prove native-resolution 60 FPS. Apple's recommended spatial insertion point is
[after tone mapping](https://developer.apple.com/videos/play/wwdc2022/10103/).

An opt-in prototype was added behind `UNLEASHED_IOS_METALFX_SPATIAL=1` and a tenth
isolated Plume patch. It prepares a cached perceptual-LDR scaler, draws the game
backbuffer at the configured lower resolution, encodes the upscale before native
host UI, then presents with the existing brightness/color-correction path.
The game HUD is scaled with the frame in this initial prototype; UIKit controls
and host menus remain native. Non-heap input/output Metal textures use tracked
hazards, and textures plus the scaler are retained to GPU completion because
Plume's command buffers use unretained references. Scaler creation failure or
lack of device support falls back to the original full-size final target.
Normal launches keep the original path.

The prototype Release build succeeded. A fresh prepare reproduces seven game
files and applies all ten Metal dependency patches; its scaler/profiler sources
match the build inputs. The previously tested signed app is preserved in
`out/ios/pre-metalfx-working-release`.

After the user's ten-minute cooling break, the original installed app launched
at 1.0 scale / 60 FPS. An early uninstrumented log window at app time 30–50 s
reported nominal thermal state, mean GPU batch time 19.17 ms and presentation
rate 49.53 FPS. These figures are not the whole Instruments recording. The
subsequent joint Game Performance trace spans 31.085 s and already starts in
Fair state; it transitions to Serious at 23.459 s. Exported active GPU events
cover only the last 9.662 s. In that retained GPU window the main guest thread
runs about 17.4% and blocks about 81.1%, while the render thread runs about 13.8%
and blocks about 85.7%. Fragment active intervals cover 72.0% and vertex 12.4%;
these overlap and are not total utilization. Hardware limiter counters remain
unselected. This supports a GPU-side cost in this scene, but does not establish
ALU versus bandwidth limits or exclude brief CPU hitches. Data:
`out/ios/iphone15-cold-native-20261012-analysis.json` and its `.trace`/XML/log files.
The filename describes the cooling protocol, not a nominal state throughout.

The opt-in MetalFX build was installed and ran on A16. Both the persistent log
and Apple's HUD confirm Spatial input 1917×884 and output 2556×1179. The first
integration exposed smaller guest backbuffer dimensions to UI layout and
cropped/enlarged game UI. The fix keeps native guest dimensions and scales only
the raster viewport and scissor on the smaller final target; switching targets
invalidates these states. The user confirmed that the corrected interface looks
normal. Host UI stays native, while the game HUD is still part of the upscaled
frame. The corrected Release build passed and its seven-file game patch was
reapplied to clean reference files with exact source equality.

A short stationary segment at app time 29–35.4 s reached 59.97 FPS with mean GPU
batch time 14.93 ms and 1,007 draws per frame. Logged thermal state was Serious.
A later screenshot in the same launch shows 43.45 FPS / 20.17 ms GPU, so this is
not a sustained-60 claim. Coarse thermal categories do not disclose clock
changes within a category. No controlled same-clock 0.75-scale baseline versus
MetalFX comparison has established incremental scaler cost or a quality gain.
Report: `out/ios/iphone15-metalfx-20261012-analysis.json`; screenshot:
`out/ios/screenshots/iphone15-metalfx-ui-fixed-20261012.png`.

The phone is left running the corrected opt-in prototype at 0.75 scale / 60 FPS,
Auto buffering, VSync off and DOF Auto. Ordinary launches still default to the
original upscale path because MetalFX is enabled only by the diagnostic launch
environment. No JIT or memory entitlement was added. The existing SE 3 IPA was
preserved; this MetalFX candidate has not been tested on SE 3.


## MetalFX feature minimums checked against Apple tables (2026-10-12)

The user confirmed 60 FPS in the hub before throttling with the corrected
prototype; a complete level/playthrough test remains pending. This is distinct
from the later warm HUD snapshot. No new device launch was made during this
compatibility check while the phone cooled.

The official Metal Feature Set Tables PDF, dated May 21, 2026, lists:

| Feature | Minimum hardware family | First A-series generation |
| --- | --- | --- |
| MetalFX Spatial upscaling | Apple3 | A9 |
| MetalFX Temporal upscaling | Apple7 | A14 |
| MetalFX frame interpolation | Apple5 | A12 |
| MetalFX denoised upscaling | Apple9 | A17 Pro |

These are hardware-family thresholds, not whole-device/OS compatibility promises.
SDK headers declare the Metal 3 spatial/temporal descriptors available on iOS 16;
frame interpolation is an iOS 26 API, so OS availability raises its practical
minimum on iPhone. Runtime `supportsDevice:` and scaler creation remain the
checks for a concrete device and OS. A15 and A16 are Apple8; therefore the current
Apple table does not support the claim that A16 can only use Spatial. Spatial
was chosen for the first prototype because its input integration is simpler.
Temporal had not yet been tested at this compatibility check; subsequent
runtime results are recorded below.

The current renderer rejects a Metal device without `bufferDeviceAddress`, which
Plume enables on iOS 16+ only when `argumentBuffersSupport` is Tier 2. Apple lists
Tier 2 starting at Apple6/A13. Combined with this build's iOS 16.4 deployment
target, A13 is the inferred minimum from the inspected required capabilities,
not a validated minimum for all shaders, memory peaks or playable performance.
Only A16 has been tested; A15/SE 3 remains a device test. Requiring Temporal
would raise the inferred hardware floor to A14; the optional prototype keeps
Spatial for A13 and needs scene depth, motion vectors and jittered color. Denoised scaling is a different feature with an Apple9 floor.

Sources: [Apple GPU and feature tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf)
(pages 2, 4–6), [MetalFX integration guidance](https://developer.apple.com/videos/play/wwdc2022/10103/),
and the installed iPhoneOS SDK MetalFX framework headers for API availability.


## Optional Temporal and Simulator controls (2026-10-12)

The latest prototype adds `UNLEASHED_IOS_METALFX_TEMPORAL=1` without raising the
iOS 16.4 target. The ordinary launch still enables neither scaler. A16/iOS 27
passed the runtime support check and created the temporal scaler for
1917×884 → 2556×1179. Earlier scene attempts stayed in Spatial fallback: the
motion-blur pass actually binds 958×442 packed motion and 958×442 depth. The
capture was therefore changed to track original, non-MSAA 1917×884 scene depth
and resample/decode the packed UV motion map into RG16Float at input resolution.
The updated capture now logs full 1917×884 depth and actual Temporal encoding
in the hub. The later object-motion upload change removed the Sonic trail according to the
user; broader quality and sustained performance remain pending verification.

The integration uses a 16-sample Halton viewport jitter, reverse-Z depth and
negative input-pixel motion scales for the game's current-minus-previous UV
field. It resets history after a missing Temporal frame, mode/scale/size change.
No dedicated camera-cut detection exists yet. The game's object velocity clamps
and discards small velocities for motion blur; it is not a full-quality temporal
motion field. The input is final post-processed LDR, whereas Apple's guidance
places Temporal before post-processing. This prototype must be evaluated for
moving objects, camera motion, disocclusions, transparent effects and camera cuts.

In Temporal mode, CSD HUD/text detection moves those draws after upscaling at native resolution;
the host menus and UIKit touch controls were already drawn at native resolution.
No JIT, increased-memory-limit or extended-virtual-addressing entitlement was
added. The existing signed Spatial app and the older unsigned SE 3 IPA are preserved.

A separate `ios/control_preview` app runs the production UIKit touch view and
TouchGamepad code in Simulator without the game/Metal/assets. iOS 26.5 Simulator
was checked on iPad mini (A17 Pro), 1133×744 pt, and iPad Pro 13 (M5), 1376×1032 pt.
Both had 25 pt bottom safe areas and visible controls without overlap. Snapshots
are exported directly from the live UIKit view hierarchy, excluding Simulator's
portrait device canvas/letterboxing. This checks app layout, not physical finger
reach, game FPS, memory-pressure survival or thermal throttling. The full game
was not built or run for Simulator.

References: [Simulator and device differences](https://developer.apple.com/library/archive/documentation/IDEs/Conceptual/iOS_Simulator_Guide/TestingontheiOSSimulator/TestingontheiOSSimulator.html),
[MetalFX integration](https://developer.apple.com/videos/play/wwdc2022/10103/).


### Active-cooling check and Sonic motion correction (2026-10-12 morning)

A cooler was installed by the user. The first resumed Temporal scene ran below
60 FPS with Nominal thermal state. ObjectVelocityPS originally caps UV velocity
at 0.025 and discards values below 0.0015. For Temporal, its constant-buffer upload
now uses limit 1.0 and cutoff 0.0 while preserving the guest constants and
restoring the ordinary upload on shader changes. The user confirmed the Sonic
trail disappeared. The shared map is also used by motion blur, so its blur may
change; this remains an experimental integration, not a dedicated motion field.

A stationary Temporal segment (log sample ends 43–61 s) averaged 17.831 ms GPU,
53.370 displayed FPS, thermal Nominal. The sequential Spatial segment (70–94.2 s)
averaged 17.470 ms GPU, 53.274 displayed FPS, thermal Fair. Cameras/draw counts
were only approximately matched, and thermal states differed. These runs do not
establish the incremental Temporal cost or cooler effectiveness; no off-cooler
control with equal starting conditions exists. They show that the latest
Spatial path also misses the 16.667 ms GPU budget, so attributing all the loss to
Temporal is unsupported. A saved pre-Temporal Spatial build is being checked for
regression. GPU clock frequency remains unmeasured.

Evidence: `out/ios/iphone15-temporal-20261012-analysis.json`,
`iphone15-temporal-cooler-motion-fix.log`, `iphone15-spatial-cooler.log`.


### Final Spatial control

The saved Spatial build first returned approximately 60 FPS at Nominal, then
later averaged 55.287 FPS / 17.305 ms GPU at Fair (sample ends 80–94.5 s). This
means its early 60-FPS result did not by itself prove a regression in the newer
build. Native CSD interception was nevertheless removed from Spatial to restore
its validated final-image path; it remains in Temporal to exclude UI from history.
The new Spatial build averaged 59.947 FPS / 14.714 ms GPU in a short Nominal
stationary segment (28–46.5 s), 68 render passes versus the earlier 69. The user
confirmed normal image and controls. This short result does not establish
sustained 60 FPS or an isolated HUD-pass cost because starting thermal conditions
and camera/draw counts differed. The phone was left running this new Spatial build,
0.75× / 60 FPS / Auto / VSync off, with Metal HUD. Temporal remains opt-in.

The observation that Fair coincided with lower FPS does not measure the GPU
frequency or prove causation. [Apple defines Fair as slightly elevated thermals](https://developer.apple.com/documentation/foundation/processinfo/thermalstate-swift.enum/fair);
it is not a direct clock-frequency measurement. Active cooling did not prevent
the saved baseline from reaching Fair in this run; an equal-condition cooler on/off
comparison is still needed to establish how much it extends sessions.


### Same-launch Nominal → Fair snapshot check

The user requested a current snapshot and a check whether the transition itself
caused the slowdown. `iphone15-spatial-fair-check.log` contains the same final
Spatial launch across the transition, with the cooler reportedly active.
Fair was first sampled at 76.427 s (thermal sampled approximately every 2 s).
Timing rows average 120 frames:

| Sample end times | Thermal state | GPU mean | Displayed FPS |
| --- | --- | --- | --- |
| 64–74.5 s | Nominal | 14.779 ms | 59.932 |
| 78–86.5 s | Fair | 14.707 ms | 60.000 |
| 95–110 s | Fair | 17.391 ms | 55.657 |
| 317–338.5 s | Fair | 17.508 ms | 53.858 |

The screenshot shows 54.22 FPS / 17.56 ms GPU / 18.44 ms frame interval. Thus the
reported transition did not immediately lower frame rate: ~60 FPS persisted in
Fair, and GPU slowdown followed later while the thermal category stayed Fair.
This rules out interpreting Fair as a discrete FPS cap in this run. It does not
rule out thermal/energy management acting within the Fair category; GPU clocks
and causality remain unmeasured. Draw counts changed slightly, and exact camera
immobility was not independently verified.

Evidence: `out/ios/iphone15-fair-transition-20261012-analysis.json`,
`out/ios/screenshots/iphone15-spatial-fair-check.png`.


### User-requested 0.5× / 60 FPS control without MetalFX

Only ResolutionScale was changed from 0.75 to 0.5; FPS 60, Auto buffering,
VSync off, DOF Auto, original motion blur and other graphics settings were
preserved. Both Spatial and Temporal launch flags were explicitly set to 0.
The runtime config was copied back for verification, and the new log segment
contains no MetalFX preparation/encoding messages. The user kept the cooler and
returned to the windmill scene, then left Sonic/camera still.

By app time 310.056 s, every sampled thermal state was Nominal. Timing rows with
sample ends 60–309.5 s averaged 59.948 displayed FPS and 14.606 ms GPU. Early
60–120 s and late 240–309.5 s windows were almost unchanged: 59.948 vs 59.939 FPS,
14.589 vs 14.593 ms GPU. The screenshot shows 60.10 FPS, 14.72 ms GPU and 16.64 ms
frame interval, with no MetalFX panel. No progressive slowdown occurred in this
roughly five-minute launch / four-minute stable measurement window.

This is consistent with load/thermal-related loss in the heavier path, but it
does not isolate thermal causality: both scale and MetalFX changed, and Fair was
not reached. A same-scene 0.75× run without MetalFX would isolate the scaler from
resolution/load more cleanly. GPU clocks remain unmeasured; complete gameplay
and longer sessions were not tested. The phone was left at 0.5× / 60 FPS with
MetalFX disabled.

Evidence: `out/ios/iphone15-halfscale-no-metalfx-20261012-analysis.json`,
`out/ios/iphone15-halfscale-no-metalfx.log`,
`out/ios/screenshots/iphone15-halfscale-no-metalfx-long-check.png`.


### 0.75× / 60 FPS control without MetalFX

The user requested the next control at 0.75×. Only resolution scale was changed
from the prior 0.5× run; FPS 60, Auto buffering, VSync off and other graphics
settings were preserved. Spatial and Temporal remained explicitly disabled.
The user returned to the same windmill scene and left Sonic/camera still, keeping
the cooler regime unchanged.

At app time 365.096 s, Fair had persisted for 223.65 s since its first sample at
141.446 s. Timing sample ends 60–130 s (Nominal) averaged 59.951 FPS / 14.919 ms
GPU; sample ends 150–365.096 s (Fair) averaged 59.927 FPS / 14.906 ms GPU. The final
60-s Fair window averaged 59.944 FPS / 14.912 ms GPU. Thus no progressive GPU-time
or FPS degradation occurred over this approximately six-minute launch, despite
more than 3 min 40 s in Fair.

This is stronger evidence that the Fair category alone does not explain the
previous 0.75× Spatial slowdown to ~54 FPS / ~17.5 ms GPU. Both paths use the same
latest app build and scale; MetalFX was the main rendering-mode difference.
Still, earlier/new runs have approximately matched cameras, varying NPC/draw
counts, sequential power history and unmeasured starting temperatures. Precise
GPU frequency and causal thermal differences are unknown. Profile the MetalFX
path's actual cost and sustained power behavior before declaring either a
Spatial bug or a purely thermal cause. The observation is for this static hub,
not a full-level/sustained-play guarantee.

The phone was left running 0.75× / 60 FPS / Auto / VSync off with both MetalFX
modes disabled. Evidence: `out/ios/iphone15-threequarter-no-metalfx-20261012-analysis.json`,
`out/ios/iphone15-threequarter-no-metalfx.log`,
`out/ios/screenshots/iphone15-threequarter-no-metalfx-long-check.png`.


## Spatial CPU/GPU trace after the no-MetalFX controls (2026-10-12)

At the user's request, Spatial was enabled at unchanged 0.75× / 60 FPS / Auto /
VSync off with the cooler. The user returned to the same windmill and reported
60 FPS initially. PID 10155 was attached by Instruments without restarting it.
The first Game Performance recording inherited a 5-second rolling window and
its saved metadata duration was 10 s, with only 2.611 s of app GPU intervals.
A second recording explicitly requested `--window 30s`: metadata duration
31.071571 s; app GPU intervals retained 22.449165–31.071236 s (8.622071 s).
Counter Set was null, Shader Timeline disabled, Induced GPU Performance State
Default. Consequently this is CPU/scheduler/encoder timing, not an ALU-versus-
bandwidth limiter analysis or a measurement of exact GPU MHz.

The slow trace's encoder IDs were matched between application encoders and GPU
intervals; per-encoder active intervals were unioned to avoid duplicate/overlapping
channel events. Across 434 observed instances:

| Spatial encoder | Mean GPU active interval union | Mean elapsed GPU span | Mean CPU encode |
| --- | --- | --- | --- |
| MetalFX_Scale | 2.426 ms | 3.507 ms | 0.01195 ms |
| MetalFX_Sharpen | 0.754 ms | 4.157 ms | 0.01309 ms |

These active times total about 3.18 ms, but are not an isolated added frame-time
benchmark: stages/pipelined GPU work overlap, and elapsed spans include gaps.
The primary guest game thread was Running 18.4%, Blocked 79.2%; the render thread
Running 14.3%, Blocked 84.7% over the same retained GPU window. Their sampled
symbols identify guest update work and FlushRenderStateForRenderThread/AGX encoding,
respectively. No CPU saturation is apparent in these principal threads.

The trace reports Fair throughout the retained window. GPU performance-state
intervals reported Minimum for 96.8% of their covered duration and Medium for
3.2%, with narrative "due to active device conditions". These are categorical
states, not frequency measurements. State intervals cover 7.892 s of the 8.622 s
GPU window; percentages use reported-state coverage, not the whole recording.
The GPU performance-state info reports consistent-state-enabled No.

The app log before profiling (30–80 s) averaged 59.932 FPS / 14.795 ms GPU; later
uninstrumented windows averaged 55.647 FPS / 17.234 ms GPU (180–220 s) and 55.435
FPS / 17.290 ms GPU (850–915 s), at Fair. Thus the lower FPS persists after tracing,
but exact numbers within Instruments can still be perturbed. The earlier same-
scale no-MetalFX run held ~59.93 FPS / ~14.91 ms GPU for prolonged Fair. The current
Spatial GPU path and its several-ms work are the immediate optimization target;
this does not prove peak A16 capability, a Spatial correctness bug, or exclusion
of all thermal/energy policy effects.

### Temporary Maximum request: not a validated maximum-capability benchmark

Available Device Conditions initially showed no active GPU/network/thermal
profile. A DVT GPUPerformanceStateMax request was acknowledged, and a separate
30-s Game Performance trace was recorded without restarting the game. It did not
produce an observed Maximum state: the exported device-state rows stayed state 1
(Minimum), desired state 0; consistent state remained disabled. No meaningful FPS
increase was seen. This request is therefore not evidence of performance at a
successfully applied Maximum state. The condition client was stopped, clear was
called explicitly, and the final condition catalog confirms all profiles inactive.
No GPU override was left enabled. Exact application/OS reasons remain unknown.

Files: `out/ios/iphone15-spatial-075-slow-20261012.trace`,
`out/ios/iphone15-spatial-instruments-20261012-analysis.json`,
`out/ios/iphone15-spatial-slow-analysis.json`,
`out/ios/iphone15-spatial-075-gpu-max-20261012.trace`, and exported tables.

Apple explains that GPU performance state is managed using thermals, settings,
utilization and other conditions, and documents induced consistent-state profiling:
[Metal profiling/device conditions](https://developer.apple.com/videos/play/wwdc2021/10157/).
The phone is left running Spatial 0.75× / 60 FPS as requested, in normal device
performance policy, with no active synthetic condition.


## GPU time follows the frame budget, not the workload (2026-10-13)

Same build, same windmill scene, 0.75×, no MetalFX, VSync off, Auto buffering,
cooler, thermal Nominal (0) throughout both launches. Only `FPS` changed.

| Cap | Drawables (Auto) | Stationary window | Draws | Mean GPU time | Displayed FPS |
| --- | --- | --- | --- | --- | --- |
| 30 | 2 | 46–95 s | 970 | 26.40 ms | 20.03 |
| 60 | 3 | 51–83 s | 949 | 14.92 ms | 59.97 |

Nearly identical work took 1.77× longer on the GPU when less throughput was
requested. iOS clocks the GPU down until command buffers fill roughly 88–90% of
the frame interval. This explains why earlier 60 FPS runs reported ~14.6–14.9 ms
regardless of scale (0.5× and 0.75×) or Spatial: Metal HUD / command-buffer GPU
time measures the chosen clock, not rendering cost. Comparisons of passes or
modes need a fixed GPU performance state (Device Conditions / induced state).

Under this model the Spatial slowdown is consistent with a lowered clock
ceiling under sustained power/thermal limits: 0.75× without MetalFX still fits
the reduced ceiling, while 0.75× + Spatial (≈3 ms of MetalFX work at the low
state, plus the full-resolution gamma/copy pass) and 1× do not. Not yet proven
with fixed-state measurements.

**Separate defect: 30 FPS cap trap.** With the 30 FPS cap, Auto selects two
drawables. In 0.75× the run settled into exactly 49.92 ms intervals (20 FPS, three
refreshes) with `acquireMax` ≈ 39 ms. The GPU was busy ~26 of 50 ms, so the
governor did not raise the clock, and the frame never fit the 33.3 ms slot. The
same scene runs at 60 FPS with three drawables. Default iOS settings use 30 FPS;
0.5× escaped the trap earlier only because the work is lighter.

Evidence: `out/ios/iphone15-dvfs-075-30fps.log`, `out/ios/iphone15-dvfs-075-60fps.log`
and matching `*-config.toml` / `*-launch.json`. The phone was left at 0.75× / 60 FPS
without MetalFX; the user's previous config (1.0×) is saved as
`out/ios/iphone15-dvfs-before-config.toml`.

### 30 FPS trap fix

Two changes in `game-ios-etc2.patch` / `video.cpp`:

1. iOS Metal `Auto` buffering always uses three drawables (previously two below
   60 FPS). This also removes the restart needed when switching FPS profiles.
2. Caps below 60 FPS use `presentAfterMinimumDuration` pacing even with VSync
   off. With three drawables and the software timer alone, the same scene averaged
   30 FPS but alternated 16.6/49.9 ms intervals (5–27 of every 120 frames), which
   the user saw as heavy stutter. Caps of 60 and above keep the previous behavior.

Verification on iPhone 15, 0.75×, 30 FPS, windmill, thermal Nominal:

| VSync | Window | Intervals | Displayed FPS | Short / long / severe | Mean GPU |
| --- | --- | ---: | ---: | --- | ---: |
| Off | 45–61 s | 600 | 30.05 | 0 / 0 / 0 | 23.6 ms |
| On (default) | 40–62 s, includes running | 720 | 30.05 | 0 / 0 / 0 | 23.3 ms |

The user confirmed exactly 30 FPS, smooth image and normal input latency in
both. The 60 FPS path is unchanged in code. The patch was checked to reproduce
the prepared source from the reference archive. Logs:
`out/ios/iphone15-trapfix-30-vsyncoff.log` (first fix only, stutter),
`iphone15-trapfix2-30-vsyncoff.log`, `iphone15-trapfix-30-vsyncon.log`. The phone was
left at 0.75× / 30 FPS / VSync on.


## Spatial writes directly into the drawable (2026-10-13, reverted)

**Reverted** after the fixed-clock measurement below showed no gain; the patches
were restored byte for byte to their previous state.

Without MetalFX, neutral gamma (`Brightness = 0.5`, no Xbox color correction)
already renders the final image straight into the drawable. Spatial previously
always upscaled into a separate 2556×1179 texture and then ran the gamma/copy
pass into the drawable. Now, when gamma is neutral, Temporal is inactive and the
viewport matches the swap chain, MetalFX writes directly into the drawable; host
UI is drawn on top as in the original direct path.

- Plume (`plume-ios-metalfx.patch`): `metalSpatialOutputCompatible` checks
  size, format and the scaler's required output usage on the actual drawable;
  `prepareMetalSpatialSwapChainOutput` clears `framebufferOnly` only if needed.
  On A16 the spatial scaler reports required output usage `0x5`
  (ShaderRead | RenderTarget), so `framebufferOnly` is turned off in Spatial runs only.
- Game: drawables acquired before that change fail the check and use the old
  intermediate path (logged once per mode change; observed for the first frame).
- `UNLEASHED_IOS_METALFX_DIRECT=0` restores the copy path for A/B checks.

Device result, 0.75× / 60 FPS / VSync off, cooler, windmill: the user confirmed
correct colors, game HUD, pause menu and touch controls. Render passes went from
68 to 67. Thirty-second buckets:

| App time | Mean GPU | Displayed FPS | Thermal |
| --- | ---: | ---: | --- |
| 30–120 s | 14.15–14.59 ms | 59.7–59.9 | Nominal |
| 120–150 s | 15.16 ms | 58.06 | Nominal |
| 150–240 s | 17.03–17.10 ms | 54.6–56.7 | Nominal |

The sustained step to ~17 ms GPU still occurs, now around 130 s and while the OS
still reports **Nominal**. Removing the copy did not fix it; the starting
conditions differ from earlier runs, so the delay from ~90 s is not a measured
gain. The thermal state category does not gate this step; the GPU clock ceiling
appears to drop for another power/temperature reason. 0.75× without MetalFX held
14.9 ms for six minutes. The remaining Spatial work (≈3 ms of MetalFX at the low
state) has to be measured at a fixed GPU performance state before choosing
between lower Spatial input scale and other options. Log:
`out/ios/iphone15-metalfx-direct.log`.


## Fixed GPU performance state: true Spatial cost (2026-10-13)

The user enabled Xcode Device Conditions → GPU Performance State → Minimum;
`dvt condition list` confirmed `GPUPerformanceStateMin` active. Same build,
0.75×, 60 FPS cap, VSync off, windmill, thermal Nominal throughout. Steady
windows (60–110 s) varied by less than ±0.3 ms:

| Run | Mode | Passes | Mean GPU at Minimum |
| --- | --- | ---: | ---: |
| A | No MetalFX | 67 | 41.95 ms |
| B | Spatial, direct drawable output | 67 | 50.65 ms |
| C | Spatial, intermediate + copy (`UNLEASHED_IOS_METALFX_DIRECT=0`) | 68 | 50.05 ms |

- Spatial costs **+8.1–8.7 ms at Minimum, about +20%**, even after the smaller
  backbuffer saves its final full-resolution game passes.
- Removing the copy pass gave **no gain** (B is 0.6 ms slower than C, within
  camera/draw-count noise or the cost of `framebufferOnly = NO`). On this TBDR
  GPU a single full-screen copy is cheap.

Converting the normal-clock observations into multiples of the Minimum clock
(assuming time ∝ 1/clock):

| Observation | GPU | Required clock |
| --- | ---: | ---: |
| No MetalFX, sustained 60 FPS | 14.9 ms | 41.95 / 14.9 ≈ 2.82× Min |
| Spatial, first ~2 min at 60 FPS | 14.6 ms | 50.3 / 14.6 ≈ 3.45× Min |
| Spatial after the step (~55 FPS) | 17.1 ms | 50.3 / 17.1 ≈ 2.94× Min |

The sustained clock ceiling in these runs is therefore ≈2.9× Min. No MetalFX at
0.75× needs ≈2.8× and barely fits (≈4% headroom); Spatial needs ≈3.45× and
drops to ~55 FPS once the ceiling falls. To sustain 60 FPS, a Spatial path must
cost about the same at Minimum as 0.75× without MetalFX (≈42 ms) or less, for
example with a lower Spatial input scale. Logs: `out/ios/iphone15-gpumin-{a-nofx,b-direct,c-copy}.log`.

### Spatial input scale at Minimum

Same Minimum state, Spatial with the copy path (identical to the restored
code), windmill. Steady windows ±0.3 ms:

| Run | Spatial input | Input size | Draws | Mean GPU at Minimum |
| --- | ---: | --- | ---: | ---: |
| C | 0.75× | 1917×884 | 967 | 50.05 ms |
| D | 0.66× | 1687×778 | 1011 | 43.4 ms |
| E | 0.60× | 1534×707 | 1011 | 38.2 ms |
| A (no MetalFX, reference) | 0.75× | — | 958 | 41.95 ms |

A linear fit in input pixel area gives about 17 ms fixed (MetalFX, full-size
copy/UI and resolution-independent work) plus ~58.5 ms × scale² at Minimum.
D/E had ~4.5% more draws than C (camera not identical), so their numbers are
slightly pessimistic relative to C.

Against the observed sustained ceiling (≈2.9× Minimum clock; 0.75× without
MetalFX needs ≈2.8× and held 60 FPS for six minutes):

- **0.66× Spatial** costs about as much as 0.75× without MetalFX: borderline,
  same ≈4% headroom.
- **0.60× Spatial** is ≈9% cheaper than that reference: the first Spatial
  setting with real margin for sustained 60 FPS in this scene.

These are hub-scene numbers; heavier levels and longer sessions still need a
normal-clock sustained check. Logs: `out/ios/iphone15-gpumin-{d-066,e-060}.log`.
The direct-output experiment was reverted, rebuilt and installed; the phone's
config was reset to 0.75× / 60 FPS / VSync off (MetalFX is enabled only by the
launch environment).


## Spatial 0.6× sustained run and the periodic micro-stutter (2026-10-13)

**Sustained.** Spatial 0.6× (1534×707 → 2556×1179), 60 FPS cap, VSync off, normal
GPU policy (Device Conditions cleared and verified), cooler, windmill: 30–360 s
held 59.86–59.97 FPS at 14.5–14.7 ms GPU with no step, thermal Nominal. The user
confirmed 60 FPS. Log: `out/ios/iphone15-spatial-060-long.log`.

**Micro-stutter.** The user reported exactly one micro-stutter about every
10 seconds in every mode tested so far. Each 120-frame window either had zero or
one ~33 ms interval; such a window recurred every 6–8 s (47 dropped refreshes in
17,880 frames). Every clean window averaged 16.640 ms, so the iPhone 15 panel runs
at ≈60.09 Hz, while the 60 FPS software limiter advanced by exactly 16.667 ms
(1/60 s). The producer slowly fell behind the display and periodically left one
refresh without a new frame. The same applies to any cap that is a whole number
of refreshes.

Fix: `ios_event_loop.mm` now publishes the real refresh period derived from the
CADisplayLink interval (divided by the whole number of refreshes it spans, then
smoothed), exposed as `GetIOSDisplayRefreshInterval()` in `ios_event_loop.h`. The
software limiter in `Video::Present` uses a whole number of real refreshes when
that is within 2% of 1/FPS; other caps keep the exact timer. `ios_event_loop.h`
was added to the patch file list (`game-ios-etc2.patch` now covers 8 files) and
`out/ios/regenerate_current_patches.py`.

After the fix (same settings): 1,680 frames at 60–86 s, **0** long intervals,
mean 16.639 ms (60.10 FPS). The user confirmed the stutter is gone. Log:
`out/ios/iphone15-limiter-refresh.log`. The 30 FPS path uses native
minimum-duration presentation and was already clean.


## MetalFX Upscaling menu setting (2026-10-13)

Spatial is now a regular iOS option: Video → **MetalFX Upscaling**, directly
below Resolution Scale (`MetalFXUpscaling` in `[Video]`, default On). The iOS
default `ResolutionScale` changed from 0.5 to 0.6, the input scale that held
60 FPS in the sustained Spatial test. Existing configs keep their saved scale.

- `config_def.h`: new iOS-only bool, new 0.6 default.
- `config_locale.cpp`: name/description in English, German, French, Spanish and
  Italian; Japanese falls back to English until a furigana translation is added.
- `options_menu.cpp`: row after Resolution Scale; no restart required.
- `video.cpp`: `IOSMetalFXRequested()` follows the setting every frame;
  `UNLEASHED_IOS_METALFX_SPATIAL=0/1` still overrides it. Temporal stays
  environment-only.
- `options_menu.cpp` and `config_locale.cpp` joined the game patch (10 files) and
  `out/ios/regenerate_current_patches.py`; the patch reproduces the prepared source.

Device check, launched without any MetalFX environment variable: Metal HUD showed
Spatial (1534×707 → 2556×1179), the option appeared as On with its description,
and toggling Off/On in the pause menu switched immediately without a restart
prompt or artifacts (user confirmed). The SE 3 IPA in `out/ios/dist` predates
this change.


## Native 1× at Minimum: how far from 60 FPS (2026-10-13)

Same protocol (Device Conditions `GPUPerformanceStateMin` verified active,
windmill, 60 FPS cap, VSync off, MetalFX disabled, thermal Nominal). Steady
window 60–120 s: **73.0–73.4 ms**, 970 draws, 67 passes. Log:
`out/ios/iphone15-gpumin-f-1x.log`.

| Scale (no MetalFX) | GPU at Minimum |
| --- | ---: |
| 0.75× | 41.95 ms |
| 1.0× | 73.2 ms |

A fit `fixed + k × scale²` gives ≈1.8 ms + 71.4 ms × scale²: the workload is
almost entirely per-pixel. 1.0×/0.75× = 1.745 vs a pixel ratio of 1.78.

| Requirement for 1× | Multiple of Minimum clock |
| --- | ---: |
| 60 FPS at 100% GPU busy | 73.2 / 16.67 ≈ 4.4× |
| Peak observed (cold 1×: 19.2 ms) | ≈ 3.8× |
| Sustained ceiling (Spatial step runs) | ≈ 2.9× |

So native 60 FPS does not fit even at the peak clock seen, and the sustained
ceiling gives about 25 ms per frame (≈40 FPS). To sustain 60 FPS at 1× the
per-pixel GPU cost must drop by roughly 35–40% (≈42–48 ms at Minimum), i.e. to
what 0.75× costs today. Clock multiples assume time ∝ 1/clock and come from a
few hub runs; they are estimates, not measured frequencies.


## Per-pass breakdown of native 1× at Minimum (2026-10-13)

The opt-in pass profiler (`UNLEASHED_IOS_GPU_PASS_DIAGNOSTICS=1`, one sampled
command list per 120) now lists every pass and names it after the shaders of its
first draw:

- Plume (`plume-ios-metalfx.patch`): `setMetalPassDiagnosticLabel()` stores a
  pending label that is attached to a pass at its first draw in sampled frames
  only; all passes are reported (previously the top eight).
- Game: guest shader names now come from the two shader-load hooks that were
  limited to `ASYNC_PSO_DEBUG` (`GuestShader::name`, `video.h` added to the patch,
  now 11 files). Host passes are labelled `host|ResolveCopyColor`,
  `host|ResolveCopyDepth`, `host|ImGui`, `host|GammaCopy`. After each resolve
  copy the port marks the guest pipeline dirty, so guest passes are relabelled
  before their first draw.

1×, Minimum, windmill, seven sampled frames (GPU 71–78 ms, fragment sum 63.6 ms;
stage intervals overlap, so sums are approximate):

| Category | Passes | Fragment at Minimum | Share |
| --- | ---: | ---: | ---: |
| **Port: EDRAM resolve copies** (`ExecutePendingStretchRectCommands`) | 28 | 25.1 ms | 39.5% |
| Game post-processing (tone map, bloom, DOF, motion blur) | 26 | 16.8 ms | 26.4% |
| Scene geometry (main, IgnoreLight, water) | 5 | 13.0 ms | 20.5% |
| Game's own copy passes (`FxCopyColor/Depth`) | 6 | 8.3 ms | 13.1% |
| Shadow maps (fragment; ~1.7 ms vertex) | 2 | 0.4 ms | 0.6% |

The seven full-resolution resolve copies alone cost 21.4 ms at Minimum; the two
largest (right after the main scene pass, and late in the frame) take 5.5 and
5.0 ms each, the others 2.0–2.4 ms. Most expensive guest passes: main scene
8.2 ms (322 draws), tone-map exposure 3.1 ms, quarter-resolution
`FirstDownSample4` 3.1 ms, water 2.4 ms, `AlternativeDOF` 2.3 ms.

Removing every resolve copy would bring 1× to roughly 48 ms at Minimum, about
the ≈2.9× sustained ceiling: by itself borderline for sustained 60 FPS, but it
is the single largest reducible cost. The same copies also run below 1×, so
Spatial/0.75× would benefit too. The earlier blit experiment
(`out/ios/surface-blit-experiment-video.cpp`, 67 → 40 passes) was judged under
DVFS and needs a fixed-clock re-test; aliasing a resolve target to its texture
(no copy) is the larger but riskier option. Logs:
`out/ios/iphone15-gpumin-1x-passes{,2,3}.log`, aggregate
`out/ios/iphone15-gpumin-1x-passes3.json`.

### Surface blit re-test at Minimum

The earlier blit experiment was ported as opt-in `UNLEASHED_IOS_SURFACE_BLIT=1`
(equal-size, equal-format single-sample color resolves use a Metal blit) and
measured with the same protocol (1×, Minimum, windmill). Passes fell from 67 to
40 and the render-pass fragment sum from ~63.5 to ~42 ms, but total GPU time
stayed **72.4–72.5 ms** versus 73.0–73.4 ms with shader copies (≈1%, within run
noise). The user saw no artifacts. The work only moved from render passes into
blit encoders: the resolve copies are memory-bandwidth bound (a full-resolution
32-bit copy moves ~24 MB, a 64-bit one ~48 MB). The experiment was removed again;
only avoiding the copies (aliasing a resolve target with its texture where the
game's access pattern allows it) can recover the ~20 ms. Log:
`out/ios/iphone15-gpumin-1x-blit.log`.


## Resolve copy analysis (2026-10-13)

The port already resolves lazily: `ProcStretchRect` only records
surface → texture; a texture with a pending resolve binds its source surface
directly. A copy runs only when
1. the source surface is **cleared** (`ProcClear`),
2. the source surface is **drawn into** (`FlushRenderStateForRenderThread`,
   copy-on-write), or
3. the frame ends (`Video::Present` enqueues `ExecutePendingStretchRectCommands`,
   which copies every pending non-depth surface).

`UNLEASHED_IOS_RESOLVE_DIAGNOSTICS=1` (new, opt-in) tags each copied texture with
its trigger and counts whether it is sampled (a `SetTexture` or a draw with it
bound in a slot) before a later resolve replaces it. Pass labels now carry the
trigger (`host|ResolveCopyColor@draw`). 1×, hub: standing, pause menu opened and
closed, running. Per frame, identical in all phases:

| Trigger | Large (≥1 Mpx) copies | Read before replaced | Small copies | Read |
| --- | ---: | --- | ---: | --- |
| clear | 0 | – | 0 | – |
| draw (copy-on-write) | 5 (15.1 Mpx) | all | 11 (3.9 Mpx) | 10 of 11 |
| frame end | 2 (6.0 Mpx) | **none** | 10 (1.9 Mpx) | all |

The two frame-end copies are full-resolution RGBA16F and RGBA8 targets
(passes #55 and #61, 4.96 + 2.33 ms at Minimum ≈ 10% of the 1× frame). They were
never sampled before the next frame resolved into them again, including while
the pause menu was open. An earlier count without the bound-slot check reported
one unread draw copy; that was a texture still bound in a slot.

Implications at Minimum, 1×:

- Frame-end copies that are never read: ~7.3 ms. Removing them needs either a
  prediction (skip textures that are always replaced unread, detect a stale read
  and fall back) or knowledge of the game's intent for these two targets.
- Draw-triggered large copies (#4 scene RGBA16F 5.5 ms, #5 depth 2.1 ms, #28,
  #36, #49 ~2.0–2.4 ms each; ~14 ms total) are all genuinely read after the
  surface is overwritten. Avoiding them would require handing the texture the
  surface's memory and giving the surface fresh memory (a swap), which is exact
  only when the next pass overwrites the whole surface without reading its old
  contents (e.g. opaque full-screen filters).

Logs: `out/ios/iphone15-1x-resolve-diag{,2}.log`.

### Skipping unread end-of-frame resolves (experimental, opt-in)

`UNLEASHED_IOS_SKIP_UNREAD_RESOLVES=1`: the frame-end flush skips the copy for a
destination texture whose previous 30 frame-end copies were all replaced by a new
resolve before anything sampled them (a `SetTexture`, or a draw with it bound in a
slot). The skipped texture is detached from the surface and its slots are rebound,
exactly as after a copy. If a skipped texture is ever sampled before being
resolved again, the port logs `Skipped resolve was sampled` and copies that
texture at every frame end from then on. Depth surfaces were already never
flushed. New `GuestTexture` fields are iOS-only; default behaviour is unchanged.

Same build, 1×, Minimum, windmill, back-to-back runs:

| Run | Passes | GPU at Minimum (60–110 s) |
| --- | ---: | ---: |
| Skip off | 67 | 73.2–73.6 ms |
| Skip on | 65 | **66.5–66.8 ms** (−6.7 ms, −9%) |

The skip path dropped exactly the two full-resolution frame-end copies
(2.00/frame, 6.03 Mpx/frame) in all windows, matching the 7.3 ms predicted from
the pass profile. The user stood, opened/closed the pause menu and ran: no
artifacts and no `was sampled` events. Hub only; other stages, cutscenes, stage
transitions and the Werehog/night pipeline still need checking before this can
become the default.

What it changes for 1×: ≈66.7 ms at Minimum needs ≈4.0× the Minimum clock for
60 FPS, still above the ≈3.8× peak and ≈2.9× sustained ceiling. Lower scales get
the same proportional saving (the skipped targets are render-resolution), e.g.
more headroom for 0.75× without MetalFX and for Spatial 0.66×.
Logs: `out/ios/iphone15-gpumin-1x-{skip,noskip}.log`.

#### Stage sessions with the skip enabled

Spatial 0.6× / 60 FPS, `UNLEASHED_IOS_SKIP_UNREAD_RESOLVES=1`. Session 1: Mazuri
Day Stage 1, Apotos Night Stage 1, Apotos Day Stage 2, full Apotos Day Stage 1.
Session 2: Apotos Day Stage 1 and Apotos Night Stage 1. The user saw no visual
artifacts, but the guard fired 5 + 2 times: a few seconds after a stage loads,
a skipped **RGBA8** render-resolution frame-end resolve texture is bound with
`SetTexture` while the pipeline is `RenderBuffer|AlternativeDOF` (the stage DOF
chain). Each event means one frame used stale content before that texture was
switched back to always-copy. The **RGBA16F** frame-end target was never sampled
in the hub or in any of these stages.

Conclusion: the RGBA8 frame-end copy is genuinely needed in stages and must not
be skipped. Skipping only the RGBA16F copy keeps most of the saving (≈5.0 of the
7.3 ms at 1× Minimum, ≈7%). Because guarded textures are new objects in every
stage, a production version would have to remember sampled targets by
(format, size) for the session (or persistently) so at most one stale frame can
ever happen per kind, or not use prediction at all. The option stays opt-in.

Separately, the user noticed hitches when boosting into enemies that explode.
CPU frame windows show single frames of 200–260 ms (e.g. 97.7 s, 170.3 s) at the
same time as guest worker-thread starts (`0x82DFA2B8`, `0x822E9FF8`) and, once,
`Saving achievements...` / `Saving persistent storage binary...`; no Metal
pipeline compile was logged. This points to CPU-side blocking (loading or save
I/O), unrelated to resolve skipping; not yet investigated. Logs:
`out/ios/iphone15-skip-levels{,2}.log`.


## Gameplay hitch investigation (2026-10-13)

The user reported stutters when boosting into enemies. Findings so far:

**Not the cause:** GPU load (the hitched frames' fence/acquire phases stay
normal), shader compilation (no `iOS pipeline compile` >10 ms outside stage
loading; in the latest launches none at all), file I/O (guest reads >8 ms are only
5–15 MB `.csb`/`.ar` loads at 8–18 ms), the resolve-copy experiments (same
pattern without them).

**Correlation:** across five logs, almost every `Saving achievements...` line
(written when the game's save icon appears, i.e. an autosave) is followed within
~2 s by a 70–650 ms frame. Autosaves mostly happen at transitions, but also during
play.

**What the game thread does in a hitch** (Time Profiler, 1 ms samples, frames
located exactly with a new `LongFrame` Points-of-Interest signpost):

- 69 ms frame at an autosave: the game thread is `Running` the whole time,
  ~50 of 58 samples inside the CRI audio middleware (`0x8314xxxx–0x8316xxxx`; the
  port already labels `sub_8314A310` as `CriAuObjLoc::AttachCueSheet`), entered via
  `sub_82E59070 → sub_82E5BD50 → sub_8314A410`.
- 77 ms frame at an area load: `Running`, recursive scene/resource creation
  (`sub_82E091A8`, `sub_82B64528/82B69168`), no CRI.

These are CPU bursts in the recompiled game code on the game thread, not
waits. `UnleashedRecompLib` Release builds with `-O2`, `-fno-strict-aliasing`,
`-ffp-model=strict`, as on PC. One earlier 905 ms frame (session 2) was spent
waiting for the render thread; it did not recur.

New opt-in diagnostics (all under `UNLEASHED_IOS_FRAME_DIAGNOSTICS=1`):
- `iOS long frame: … ms (present phases …)` plus an os_signpost `LongFrame`
  event (Points of Interest) for every frame over 50 ms;
- `iOS slow render command: <type> … ms, pipeline …` for any render-thread
  command over 15 ms;
- `iOS slow file read: … ms, … bytes, <file>` for guest reads over 8 ms
  (`kernel/io/file_system.cpp`, now in the game patch: 12 files).

Note: the Game Performance template keeps detailed thread/sample data only for
the last few seconds even with `--window`; use the Time Profiler template for
long captures. Logs/traces: `out/ios/iphone15-stutter{,2,3,4}*`,
`out/ios/iphone15-save-stutter*`.

#### Learning by kind, persisted (refined option A)

The skip decision now uses a resolve *kind*: format plus whether the target
covers the full render resolution (`round(viewport × ResolutionScale)`, ±1 px).
A kind is marked "sampled" when a texture of that kind is sampled after a
frame-end copy or after a skip; marked kinds are never skipped again and are
stored in `<user>/ios_sampled_resolve_kinds.txt` (`format × 2 + fullResolution`).
The first attempt keyed by format alone also matched small bloom targets of the
same formats, which are sampled every frame, and so disabled every skip.

Results (Spatial 0.6×, 60 FPS):
- First run: the bloom-size kinds were learned within seconds without any skip;
  exactly one `Skipped resolve was sampled` (full-resolution RGBA8, DOF) on the
  first stage; none afterwards across further day/night stages. The
  full-resolution RGBA16F frame-end copy was skipped throughout (1.00/frame).
- Relaunch with the option: the stored kinds were loaded; zero warnings in a day
  stage, RGBA8 never skipped, RGBA16F skipped.

So at most one stale frame per kind can ever occur on a device. Saving at 1×
Minimum is the RGBA16F copy, ≈5 ms (≈7%). Still opt-in
(`UNLEASHED_IOS_SKIP_UNREAD_RESOLVES=1`). Logs: `out/ios/iphone15-skip-kinds{,2,3}.log`.


## Option B: swap instead of copy-on-write (experimental, opt-in)

`UNLEASHED_IOS_RESOLVE_DIAGNOSTICS=1` now also classifies every draw-triggered
(copy-on-write) resolve by the state of the draw forcing it. In hub/day/night
sessions the full-resolution RGBA16F scene copy is always forced by blended,
depth-tested geometry (water, translucent objects) and so cannot be avoided; every
other draw-triggered copy (full-resolution RGBA8 before `FxCopyColor` and
`AlternativeDOF`, motion blur 767×353, light shafts 781×360, bloom chain, 1×1
luminance) is forced by an opaque full-viewport `DrawPrimitiveUP` 4-vertex strip
with no depth or alpha test.

`UNLEASHED_IOS_SWAP_RESOLVES=1`: for such draws the pending resolve texture takes
the surface's memory and the surface continues in the texture's old memory
(`IOSTrySwapResolve`, before `PopulateBarriersForStretchRect` in
`FlushRenderStateForRenderThread`). Conditions: single pending destination of the
same size/format, 2D, one mip, render-target usage, no view swizzle, no blending,
full colour write mask, depth test off or `ALWAYS`, no alpha test, full viewport
and scissor, and a 4-vertex strip whose positions are the viewport corners in clip
space (span exactly 2.0, within one pixel of [-1, 1], because guest quads carry
the D3D9 half-pixel offset that the translated shaders compensate) or in pixels.

Two bugs found during device testing:
1. The first check required exact ±1 clip coordinates and never matched.
2. Rewriting the existing descriptor slots corrupted the image (planet menu
   "mush", blue bloom): descriptors are bindless and read at GPU execution time, so
   already-encoded draws of the frame saw the new memory. Swaps now give both
   objects new descriptor slots; the old slots are freed in `DestructTempResources`
   once that frame's GPU work has completed.

Result (Spatial 0.6×): 11–14 swaps/frame (3.1–4.7 Mpx/frame); the user saw a clean
planet menu, hub, day and night stages. Remaining draw-triggered copies are only
the RGBA16F scene resolve. A one-frame purple screen before the SEGA logo and
rare pop-in were seen in a run where no swap happened, so they are unrelated.
Logs: `out/ios/iphone15-swap{,2,3}.log`.

### A and B at Minimum (1×, windmill, back-to-back runs, same build)

| Run | Passes | GPU at Minimum (steady 10-s windows) |
| --- | ---: | ---: |
| No option | 67 | 72.6–73.8 ms |
| B (`SWAP_RESOLVES`) | 53 | 64.0–66.6 ms (≈ −7.7 ms, −10%) |
| A + B (`SKIP_UNREAD_RESOLVES` + `SWAP_RESOLVES`) | 52 | **59.3–61.5 ms (≈ −12.8 ms, −17.5%)** |

B swapped 14 resolves/frame (12.9 Mpx/frame at 1×); A skipped the RGBA16F frame-end
copy (1/frame, 3.0 Mpx). Only the RGBA16F scene copy before water/blended
geometry remains a real copy.

In clock multiples: 1× now needs ≈3.6× Minimum for 60 FPS (peak seen ≈3.8×,
sustained ceiling ≈2.9×), so native 60 FPS is reachable only while the phone is
cool. Spatial 0.75× (≈50 ms at Minimum before) should drop to roughly the cost
that 0.75× without MetalFX had (≈42 ms), which held 60 FPS in sustained tests.
Not yet verified with a sustained normal-clock run. Logs:
`out/ios/iphone15-gpumin-ab-{base,b,ab}.log`.

### Sustained Spatial 0.75× with A + B (normal GPU policy)

0.75× / 60 FPS / VSync off, cooler, windmill, Device Conditions verified off,
`SWAP_RESOLVES` + `SKIP_UNREAD_RESOLVES` (14 swaps and 1 skip per frame), thermal
Nominal throughout. 30-s buckets:

| App time | Mean GPU | Displayed FPS | Long intervals |
| --- | ---: | ---: | ---: |
| 30–180 s | 14.6–14.8 ms | 60.08–60.10 | 0 |
| 180–240 s | 15.0–15.5 ms | 60.08–60.09 | 0 |
| 240–300 s | 15.75–15.8 ms | 59.2–59.5 | 17–28 |
| 300–360 s | 15.6–16.4 ms | 59.8–60.1 | 0–8 |

The user reported 60 FPS throughout. Without A/B the same setting fell to
≈55 FPS / 17.1 ms after ~130 s; now it stays at 59–60 FPS for six minutes, but GPU
time creeps towards the 16.7 ms budget after three minutes and occasional refreshes
are missed, so 0.75× is at the edge of the sustained ceiling rather than
comfortably inside it. Log: `out/ios/iphone15-spatial075-ab.log`.


## Resolve optimizations on by default; Spatial default 0.66× (2026-10-13)

Both optimizations are now Video settings below MetalFX Upscaling, default On
and read every frame: **Skip Unused Copies** (`SkipUnusedCopies`, option A) and
**Reuse Render Targets** (`ReuseRenderTargets`, option B). The descriptions ask
users to turn them off if flickering/stale areas or corrupted post-processing
appear. `UNLEASHED_IOS_SKIP_UNREAD_RESOLVES` / `UNLEASHED_IOS_SWAP_RESOLVES` = 0/1
still override the settings. Localised in English, German, French, Spanish and
Italian (Japanese falls back to English). The iOS default `ResolutionScale` is
now 0.66 (existing configs keep their value).

Sustained check, launched without any optimization environment variable (settings
only), 0.66× Spatial / 60 FPS / VSync off, cooler, windmill, thermal Nominal: the
user confirmed the new options were present and On; 60–480 s held **60.08–60.09
FPS with zero long intervals**, GPU 13.9–14.6 ms with no upward drift, 14 swaps and
1 skip per frame. Log: `out/ios/iphone15-spatial066-ab.log`.
