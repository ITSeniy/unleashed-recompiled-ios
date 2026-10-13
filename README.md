# Unleashed Recompiled for iOS

An experimental iPhone/iPad port of [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp), built on top of the [Android port](https://github.com/SansNope/UnleashedRecomp-Android) and the Metal/UIKit groundwork of [Loganbogan9/UnleashedRecompiOS](https://github.com/Loganbogan9/UnleashedRecompiOS). The game runs through static recompilation, not Xbox 360 emulation, so it needs no JIT.

> [!IMPORTANT]
> This project does not include the game. You must supply files from your own legally acquired copy of *Sonic Unleashed* for Xbox 360.

> [!WARNING]
> This is a work in progress. It has been played through on one device (iPhone 15). There are no prebuilt IPAs yet: you build the app yourself on a Mac and sideload it.

## Status

- Boots and plays: hub worlds, day and night stages, cutscenes, saves.
- Native Metal renderer with Xbox 360 BC textures transcoded to ETC2 on devices without BC support.
- Touch controls on top of the game; host menus and controls stay at native resolution.
- **MetalFX Spatial** upscaling: the game renders at a lower internal resolution and Apple's upscaler brings it to the screen's native resolution.
- Frame-pacing fixes for 30 and 60 FPS caps, including the iPhone panel's real 60.09 Hz refresh.
- Two render-target optimizations that cut GPU work by about 17% (see [Settings](#settings)).

Measured on iPhone 15 (A16), default settings, hub area: a sustained 60 FPS over seven minutes, with a clip-on cooler. Full measurements and the reasoning behind every default are in [docs/IOS_PORTING.md](docs/IOS_PORTING.md).

## System requirements

| | Requirement |
| --- | --- |
| OS | iOS / iPadOS **16.4** or newer (developed on iOS 27) |
| Chip | **A13 Bionic or newer** (Apple GPU family 6, Tier 2 argument buffers). Older chips cannot create the renderer. |
| Tested | iPhone 15 (A16) only. iPhone SE 3 (A15) has a test build but no result yet. Other devices are untested. |
| Recommended for 60 FPS | A16-class or newer. A13–A15 are untested; expect to use 30 FPS or a lower resolution scale. |
| Memory | The game uses about 0.9–1 GB at default settings. Devices with 4 GB of RAM or more are recommended; 3 GB devices (e.g. iPhone 11, SE 2) are untested. No increased-memory-limit entitlement is required on the tested device. |
| Storage | Room for your game files (several GB) plus about 100 MB for the app. |
| To build | A Mac with Xcode (iPhoneOS SDK and Metal Toolchain), Python 3.12+, CMake 3.20+, Ninja, and an Apple ID (free or paid). |

iPad layouts were checked in the Simulator only. Bluetooth game controllers have not been tested yet.

## Build the app

The port is built from a pinned upstream iOS tree plus the patches in [`ios/`](ios). The full procedure is in [ios/README.md](ios/README.md). In short, from the repository root:

1. Put your own `default.xex`, `default.xexp` and `shader.ar` in `UnleashedRecompLib/private/`.
2. Run `python3 ios/prepare.py`, build the host tools, then `python3 ios/generate.py`.
3. Configure the Xcode project with your Team ID and a unique bundle ID, and build the `UnleashedRecomp` target in Release.
4. Either install straight from Xcode to a connected device, or package an IPA for sideloading:

   ```sh
   python3 ios/package_ipa.py
   ```

   This writes an **unsigned** IPA to `out/ios/dist/`. The sideloading tool signs it with your Apple ID.

Never commit or share `UnleashedRecompLib/private/`, `UnleashedRecompLib/ppc/` or anything under `out/`: they contain or are derived from the game.

## Install on your device

### Option 1: install from Xcode

If you built the app yourself, the Xcode build is already signed with your team. Install it with Xcode's *Devices and Simulators* window, or:

```sh
xcrun devicectl device install app --device <device> "out/ios/build/UnleashedRecomp/Release-iphoneos/Unleashed Recompiled.app"
```

### Option 2: sideload the IPA

Any Apple ID sideloader works. The tools below are the same ones the [MeloNX](https://starforgejo.dev/projects/MeloNX) guide uses; unlike MeloNX, this port needs **no JIT** (no StikDebug) and no extra entitlements.

- **[PlumeImpactor](https://github.com/claration/Impactor/releases)** (recommended, from a computer): sign in with your Apple ID, import the IPA, select your device and click **Install**.
- **[SideStore](https://sidestore.io/)** or **AltStore Classic** (on the device): install the IPA through them so you can refresh the signature without a computer. If you use PlumeImpactor, sign in with the same Apple ID.
- **[TrollStore](https://github.com/opa334/TrollStore)** (only on iOS versions it supports): installs permanently without re-signing. Not required.

After the first install:

1. Turn on **Settings → Privacy & Security → Developer Mode** and restart when iOS asks. Development-signed apps will not open without it on iOS 16 and later.
2. With a **free** Apple ID the signature expires after 7 days and you can have at most 3 sideloaded apps. Refresh through SideStore/AltStore, or reinstall. A paid developer account signs for a year.

## Add your game files

1. Install the game with the official Unleashed Recompiled installer on PC (or with the Android port). This produces the `game`, `update` and `patched` folders, plus optional DLC folders.
2. Launch the iOS app once so it creates its data folder, then close it.
3. Copy those folders into **On My iPhone → Unleashed Recompiled → UnleashedRecomp**, using the Files app or Finder (select the device, then **Files**).
4. Start the app.

Saves, settings and the log (`unleashedrecomp.log`) live in the same folder. You can back them up or move them between devices.

## Settings

The iOS-specific options are under **Options → Video**:

| Setting | Default | What it does |
| --- | --- | --- |
| Resolution Scale | 66% | Internal rendering resolution. The biggest performance lever. |
| MetalFX Upscaling | On | Upscales the internal resolution to the screen with Apple MetalFX Spatial. Only active below 100%. |
| Skip Unused Copies | On | Skips end-of-frame copies of render targets the game never reads. Turn it off if you see flickering or image areas that do not update. |
| Reuse Render Targets | On | Swaps render-target memory instead of copying it before full-screen effects. Turn it off if post-processing (bloom, depth of field, motion blur) looks corrupted. |
| FPS | 30 | 60 FPS needs a fast device; 30 FPS saves battery and heat. |
| Metal HUD | Off | Apple's frame-time overlay. Takes effect after a restart. |

These apply immediately, except Metal HUD. Guidance for iPhone 15–class devices:

- **60 FPS:** 66% with MetalFX (default).
- **Sharper at 60 FPS:** up to 75%, but it sits at the edge of the GPU budget once the phone warms up.
- **Native 100%:** realistic only at 30 FPS. At 60 FPS it holds only while the phone is cool.

## Known issues

- Autosaves and loading new areas can cause short hitches (roughly 70–300 ms). They come from the game's own CPU work, not from the GPU.
- Long sessions at high resolution scales slow down as the phone heats up. iOS lowers the GPU clock well before it reports a "Fair" or "Serious" thermal state.
- Temporal MetalFX is experimental and only available through a launch flag.

For bug reports, attach `unleashedrecomp.log` from the app folder and describe the device, iOS version and settings.

## For developers

- [ios/README.md](ios/README.md): build pipeline, patches and diagnostic launch flags (`UNLEASHED_IOS_FRAME_DIAGNOSTICS`, pass and resolve profiling, MetalFX overrides).
- [docs/IOS_PORTING.md](docs/IOS_PORTING.md): the full engineering log, covering DVFS-aware measurement at a fixed GPU clock, per-pass costs, frame-pacing fixes and the resolve-copy work.
- [tools/ios_probe](tools/ios_probe): a standalone Metal capability probe for new devices.
- The Android sources in this repository are unchanged from upstream; see the [Android port](https://github.com/SansNope/UnleashedRecomp-Android) for its documentation.

## AI-assisted development

Like the Android port, most of the iOS work was written with AI tools: OpenAI Codex for the initial bring-up, and Anthropic's Claude (Opus 5.5) for the performance investigation and optimizations. The human maintainer set the direction, tested every build on a real iPhone, and decided what ships. Detailed reports with the log attached are the most useful contribution.

## Legal

This repository contains no *Sonic Unleashed* game assets. It is not affiliated with or endorsed by SEGA, Microsoft, Apple, or the upstream Unleashed Recompiled team.

The project is distributed under GPL-3.0, following the upstream project. Third-party components retain their own licenses.

## Credits

- [hedge-dev and the Unleashed Recompiled contributors](https://github.com/hedge-dev/UnleashedRecomp): the original static recompilation project
- [SansNope](https://github.com/SansNope) and the [Android port](https://github.com/SansNope/UnleashedRecomp-Android) contributors: the base of this fork
- [Loganbogan9/UnleashedRecompiOS](https://github.com/Loganbogan9/UnleashedRecompiOS): the Metal and UIKit iOS groundwork this port builds on
- [ITSeniy](https://github.com/ITSeniy): iOS port, device testing and maintenance
- [renderbag/plume](https://github.com/renderbag/plume): the renderer abstraction, with its Metal backend
- [MeloNX](https://starforgejo.dev/projects/MeloNX): the sideloading guide this README follows
- Everyone who tests builds and sends logs
