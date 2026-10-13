# Experimental iPhone build

This builds an isolated prototype from the Metal/UIKit-capable
[iOS fork](https://github.com/Loganbogan9/UnleashedRecompiOS/tree/5b2348e0c556a35143f473c4277b89d526675a95)
and this Android checkout's BC texture converter. It is not yet a unified iOS
target in the Android project's root CMake configuration.

On 2026-10-11 the signed Release prototype launched on a physical iPhone 15
without a debugger. The user confirmed that the first level loads and Sonic
can be controlled. SE 3, sustained frame rate and level-load memory peaks have
not been tested. Detailed launch evidence is in
[IOS_PORTING.md](../docs/IOS_PORTING.md).

`prepare.py` pins and checks the platform source archives, reuses vendored
dependencies from this checkout, and applies the patches under `ios/patches`.
All generated code, downloaded sources and build outputs live under `out/ios`.
The original Android source/dependency trees are not edited.

The native Metal backend selects BC when supported, otherwise transcodes BC1–5
and BC7 to ETC2/EAC, preserving mip levels, arrays, cube faces and sRGB. The
converter comes from `UnleashedRecomp/gpu/video.cpp`; the integration copy is
`gpu/bc_texture_upload.inc`. Keep changes to the shared algorithm in sync.
BC6H remains unsupported on devices without BC. Signed BC4/5 currently use the
Android converter's biased UNORM fallback. ETC2 conversion is lossy and its
load-time CPU cost still needs profiling. No persistent conversion cache is used.

Initial iOS defaults: 30 FPS, resolution scale 0.5, no MSAA, 1024 shadows
(the fork's low-end defaults can reduce shadows further). These are requested
settings, not measured performance.

The iOS bundle declares a modern `UILaunchScreen` and hides the status bar.
Without a launch screen, the first prototype ran in a 480×320-point compatibility
window, with black side margins and a portrait status bar. The corrected build
uses the iPhone 15's full 2556×1179 drawable. `AspectRatio = "Auto"` fills this
window; cutscenes retain their separate aspect-ratio setting.

To show Apple's Metal HUD, enable `MetalHUD` in the game's video settings and
restart the app. The setting is applied before SDL creates its first Metal
device, so it also works when launching normally from the app icon. The HUD's
resolution is the presentation drawable size; `ResolutionScale` still controls
the internal game rendering resolution.

## Frame pacing

With a finite FPS cap and VSync enabled, the native iOS Metal path uses
`presentAfterMinimumDuration` and the configured CAMetalLayer drawable count.
The software sleep/yield limiter is bypassed in this mode, giving presentation
and producer throttling one clock. Other backends and VSync-off retain the
software limiter. Tested on iPhone 15 at 30 FPS: the user confirmed that the
periodic idle stutters disappeared. See the timing comparison in
[IOS_PORTING.md](../docs/IOS_PORTING.md).

For a diagnostic launch, pass `UNLEASHED_IOS_FRAME_DIAGNOSTICS=1` and optionally
`UNLEASHED_IOS_DIAGNOSTIC_FPS=30` as launch environment variables. Every 120
frames the persistent game log records actual drawable presentation intervals
and CPU frame-phase peaks; long pipeline compilations are logged separately.
`UNLEASHED_IOS_PRESENT_DURATION=0` restores software-only pacing for comparison.
Diagnostics are disabled on ordinary launches. Apple's guidance on minimum
presentation duration is [here](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/FrameRate.html).

At 0.5 scale and a 60 FPS request, two drawables held the tested scene at
30.05 FPS. Three drawables allowed approximately 60 FPS without reducing other
quality settings; the user confirmed smooth play in the level. On iOS, `Auto`
now always selects three drawables: with two, a heavier 30 FPS scene settled at
20 FPS (see the porting notes). Caps below 60 FPS use native minimum-duration
pacing whether or not VSync is enabled, because iOS never tears and the software
timer alternates 16.6/49.9 ms intervals. Caps of 60 FPS and higher with
`VSync = false` keep the software cap.

The earlier black screen was traced to SDL creating a window before its UIKit
scene connected (`scene=false`, `key=false`). Metal continued rendering into an
invisible window. The iOS event loop now attaches this existing window when a
foreground scene becomes available. For a device regression check, add
`UNLEASHED_IOS_FORCE_SCENE_RECOVERY=1` together with frame diagnostics: the window
is detached once at startup to exercise recovery. Ordinary launches never
detach it. At scale 1.0 the earlier town capture took roughly 40–42 ms GPU time;
the 15 FPS synchronized plateau did not establish an A16 hardware ceiling.

Diagnostics also aggregate GPU execution time, draw/render-pass counts, UIKit
window state, thermal state and Low Power Mode. Thermal values are 0 nominal,
1 fair, 2 serious, 3 critical. See the measured captures in
[IOS_PORTING.md](../docs/IOS_PORTING.md).

For per-render-pass GPU investigation, also enable
`UNLEASHED_IOS_GPU_PASS_DIAGNOSTICS=1`. The Metal backend samples one command
list per 120 lists, up to 256 passes, using stage-boundary timestamps calibrated
against the CPU clock. It logs the eight largest fragment-stage costs, target
sizes and draw counts. Stage sums can overlap and must not be added to infer
frame latency. Counter allocation and sampling are disabled on ordinary
launches and gracefully skipped when the device lacks the required counters.

At iPhone 15 native resolution, 1.0 scale means **2556×1179**, not the original
console's 720p. `DepthOfFieldQuality = "Auto"` also changes its filter with the
scaled resolution (the named 7×7 variant at 1179 lines versus 3×3 near 590 lines).
There is no separate exposed quality control in the tested in-game interface;
configuration-only comparisons must restore the original setting afterward.
See the ongoing 1.0-scale measurements in [IOS_PORTING.md](../docs/IOS_PORTING.md).

### MetalFX Spatial

Video → **MetalFX Upscaling** (`MetalFXUpscaling`, default On) upscales
with Spatial whenever ResolutionScale is below 1.0, and the iOS default
ResolutionScale is now 0.66. Toggling applies on the next frame without a restart.

Two resolve-copy optimizations sit below it, both default On and applied live:
**Skip Unused Copies** (`SkipUnusedCopies`) skips end-of-frame resolve copies of
target kinds the game never samples, and **Reuse Render Targets**
(`ReuseRenderTargets`) swaps render-target memory instead of copying it before
opaque full-screen filters. Turn either off if graphical artifacts appear;
`UNLEASHED_IOS_SKIP_UNREAD_RESOLVES` / `UNLEASHED_IOS_SWAP_RESOLVES` = 0/1
override them for A/B runs. Details: [IOS_PORTING.md](../docs/IOS_PORTING.md).
`UNLEASHED_IOS_METALFX_SPATIAL=1`/`0` in the launch environment overrides the
setting for A/B runs. The final LDR backbuffer is rendered at the scaled
dimensions; MetalFX produces a native viewport texture. Host menus and UIKit
touch controls are native; the game HUD is upscaled with the image. Guest
backbuffer dimensions stay native, with viewport/scissor scaling only for the
smaller raster target, so HUD layout remains correct. The brightness/color-
correction presentation pass reads the upscaled texture. Unsupported
devices/scaler failures retain the original renderer.

On A16 with a cooler, Spatial from 0.6× held ~60 FPS for six minutes in the hub;
from 0.75× it fell to ~55 FPS after about two minutes. With both resolve
optimizations, 0.66× held 60.09 FPS for seven minutes and 0.75× stayed at 59–60
FPS but near the GPU budget. At a fixed GPU state
Spatial costs about +20% over the same input without it. Heavier levels and
SE 3 have not been tested. Details are in [IOS_PORTING.md](../docs/IOS_PORTING.md).

### Experimental MetalFX Temporal

`UNLEASHED_IOS_METALFX_TEMPORAL=1` requests Temporal below 1.0 scale when
MotionBlur is not Off. The backend checks support/scaler creation and uses full
scene depth, decoded half-resolution motion-blur velocity and 16-sample viewport
jitter. Without matching inputs it falls back to Spatial. The iOS deployment
target remains 16.4; A13 keeps Spatial when Temporal is unavailable.

A16 support and actual Temporal encoding in the hub are confirmed. Its first
Sonic trail disappeared after overriding the legacy object-motion clamp/cutoff
for Temporal's constant-buffer uploads. The guest constants remain intact;
ordinary shader draws use their unmodified uploads. The shared velocity map is
also used by game motion blur, so its blur behavior can change in this mode.
CSD HUD/text is drawn after Temporal at native resolution to exclude it from
history. Spatial retains its validated final-image path without that extra pass.

The input is final post-processed LDR; Apple recommends Temporal before
post-processing. There is no dedicated camera-cut detection yet. Full-level,
transparent-effect, disocclusion and scene-transition quality tests are pending.
The latest Nominal stationary segment reached 53.37 FPS / 17.831 ms GPU at 0.75×,
so Temporal is experimental and not the current 60-FPS recommendation. Its total
cost includes input conversion/native HUD; it is not a scaler-only benchmark.
iOS 18 and SE 3 runtime tests remain pending. See [the measurements](../docs/IOS_PORTING.md).

### iPad controls in Simulator

[Build the controls preview](control_preview/README.md) to inspect the same
UIKit drawing, safe-area layout and input code on different Simulator models.
It needs no game assets. This verifies layout, not device GPU performance,
thermal behavior, memory limits or finger comfort.

## Build

Requires Xcode with the iPhoneOS SDK and Metal Toolchain component, Python 3.12+,
CMake 3.20+ and Ninja. Sign with your own Xcode Apple Development team.
The existing checkout must contain its vendored dependencies, resources/API
submodules and your original `default.xex`, `default.xexp`, `shader.ar` under
`UnleashedRecompLib/private`.

From the repository root, substitute your own `TEAM_ID` and unique bundle ID:

```sh
python3 ios/prepare.py
cmake -S ios/host -B out/ios/host -G Ninja \
  -DIOS_SOURCE_ROOT="$PWD/out/ios/source" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build out/ios/host --parallel 6
python3 ios/generate.py
cmake -S out/ios/source -B out/ios/build -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.4 \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DUNLEASHED_RECOMP_IOS_DEVELOPMENT_TEAM=TEAM_ID \
  -DUNLEASHED_RECOMP_IOS_BUNDLE_ID=YOUR_UNIQUE_BUNDLE_ID
cmake --build out/ios/build --config Release --target UnleashedRecomp -- \
  -allowProvisioningUpdates -jobs 6
```

`prepare.py` refuses to overwrite an existing prepared source tree. For another
experiment use `--output out/ios-next` for both Python scripts and adjust CMake
paths. The native generator configuration embeds the prepared source path, so
its host build must be configured for that same source tree.

## Device data

For a tester who supplies their own signing certificate, run
`python3 ios/package_ipa.py` after the Release build. It packages a temporary
unsigned copy into `out/ios/dist`, removing the original signature and
provisioning profile. It does not modify the installed/tested app or add JIT,
increased-memory-limit, or extended-virtual-addressing entitlements. See the
[SE 3 tester instructions](SE3_TESTER.md) for signing, data and test profiles.

Copy your own installed `game`, `update`, and `patched` directories into the
app container's `Documents/UnleashedRecomp`. The patched executable can also be
taken from the host generator's `UnleashedRecompLib/private/default_patched.xex`
when its original game/update inputs match. The game assets are not bundled or
checked into Git. Optional DLC uses the fork's expected named DLC directories.

The diagnostic log is `Documents/UnleashedRecomp/unleashedrecomp.log` in the app
container. Check it after a normal signed launch before drawing conclusions from
a debugger run. The standalone hardware probe and its measured results are
documented in [IOS_PORTING.md](../docs/IOS_PORTING.md).
