# Unleashed Recompiled for iOS

Sonic Unleashed on an iPhone. Not emulated: this is [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp), the PC port that turns the Xbox 360 game into native code, brought to iOS. Because nothing is emulated, there's no JIT to fight with. You sideload the app like any other and play.

The port stands on the shoulders of two other projects: the [Android port](https://github.com/SansNope/UnleashedRecomp-Android), which this fork shares its code with, and [Loganbogan9's iOS fork](https://github.com/Loganbogan9/UnleashedRecompiOS), which did the Metal and UIKit groundwork.

> [!IMPORTANT]
> The game itself isn't included. You need the files from your own copy of *Sonic Unleashed* for Xbox 360.

> [!WARNING]
> It's still early days. I've played through it on one phone, an iPhone 15, and that's the only device I can vouch for. If you try it on something else, I'd love to hear how it went.

## What works

Pretty much the whole game: the hub worlds, day and night stages, cutscenes and saves. A few things I'm especially happy with:

- **A proper launcher.** Before the game starts, you can see whether your files are in place, tweak graphics, set up touch controls, manage mods and check for updates. It speaks your phone's language, from English to Japanese to Russian.
- **MetalFX upscaling** with presets that work like DLSS on PC. The game renders at a lower resolution and Apple's upscaler sharpens it back up to the screen.
- **Touch controls** shared with the Android port, including a layout editor, so you can put the buttons where your thumbs want them.
- **Smooth frame pacing** at 30 and 60 FPS. It even accounts for the iPhone screen actually refreshing at 60.09 Hz, not 60.

On my iPhone 15 with default settings, it holds a steady 60 FPS in the hub for minutes on end (with a clip-on cooler, to be fair). If you're curious how the defaults were picked, every measurement is written down in [docs/IOS_PORTING.md](docs/IOS_PORTING.md).

## What you need

- **iOS or iPadOS 16.4** or newer.
- **An A13 chip or newer** (iPhone 11 and later). Older chips can't run the renderer at all.
- **For 60 FPS**, something like the A16 in the iPhone 15. A13–A15 should run, but I haven't been able to test them yet; plan on 30 FPS or a lower preset there.
- **About 1 GB of free memory** while playing. Phones with 4 GB of RAM or more should be comfortable; 3 GB ones like the iPhone SE 2 are untested.
- **A few gigabytes of storage** for the game files, plus around 100 MB for the app.

iPad layouts have only been checked in the Simulator, and I haven't tried Bluetooth controllers yet.

## Getting it on your phone

Check the [Releases](https://github.com/ITSeniy/unleashed-recompiled-ios/releases) page for a ready-made IPA. It's unsigned on purpose: your sideloading tool signs it with your own Apple ID. If there's no release yet, or you'd rather build it yourself, see [Building it yourself](#building-it-yourself).

Any Apple ID sideloader will do. These are the ones the [MeloNX](https://starforgejo.dev/projects/MeloNX) guide recommends, and they work here too (minus the JIT step, which this port doesn't need):

- **[PlumeImpactor](https://github.com/claration/Impactor/releases)**, on your computer. Sign in with your Apple ID, drop in the IPA, pick your device and hit **Install**. This is the easiest route.
- **[SideStore](https://sidestore.io/)** or **AltStore Classic**, on the phone. Handy because they can refresh the app without a computer. If you also use PlumeImpactor, sign in with the same Apple ID.
- **[TrollStore](https://github.com/opa334/TrollStore)**, if your iOS version supports it. It installs the app permanently, no re-signing needed.

Two things to know after the first install:

1. iOS won't open sideloaded apps until you turn on **Settings → Privacy & Security → Developer Mode** and restart.
2. With a free Apple ID, the app stops opening after 7 days and you can only have 3 sideloaded apps at once. Refresh it through SideStore or AltStore, or just reinstall. A paid developer account stretches that to a year.

## Adding your game files

The easiest way is to copy an existing install over:

1. Install the game on a PC with the official Unleashed Recompiled installer (or use the files from the Android port). You'll end up with `game`, `update` and `patched` folders, plus DLC folders if you have them.
2. Open the iOS app once so it creates its folder.
3. Copy the folders into **On My iPhone → Unleashed Recompiled → UnleashedRecomp** with the Files app, or with Finder on a Mac (select your phone, then **Files**). The launcher's **Files** button takes you straight there.
4. Switch back to the app. It notices the files on its own; tap **Play**.

If you only have the original disc image, tap **Install game** in the launcher instead. It walks you through installing from your disc image, title update and DLC packages.

Your saves, settings and the log (`unleashedrecomp.log`) end up in the same folder, so backing up or moving to a new phone is just a matter of copying it.

### Don't lose everything on a reinstall

Here's an annoying iOS detail: when an app is deleted, its folder goes with it. And sometimes iOS forces you to delete the app first, for example when you install a build signed with a different Apple ID or a different tool over the old one. That would mean copying gigabytes of game files again.

To avoid that, keep your data outside the app. Create a folder in **On My iPhone** (say, "Unleashed Data"), then in the launcher go to **Game → Data folder** and choose it. The launcher moves everything over. If you ever reinstall, choose the same folder again and you're right back where you left off. Just don't pick a folder in iCloud Drive: iOS can quietly offload those files, and the game can't read them then.

## The launcher

The launcher opens every time before the game, and every change you make there is saved right away.

- **Game** shows whether the game, the update and each DLC pack are installed. It's also where you start the installer and choose the [data folder](#dont-lose-everything-on-a-reinstall).
- **Graphics** has the MetalFX presets, frame rate, quality options and a few overlays. Metal HUD applies right away here, while from the in-game menu it needs a restart.
- **Controls** decides when touch controls show up (automatically, always, or never), whether you steer the camera by swiping or with a right stick, and how fast it turns. **Arrange controls** starts the game straight in the layout editor.
- **Mods** lists the HMM/UMM mods in your `mods` folder. Turn them on and off, drag them into the order you want, open their settings if they have any, or install a new one from a ZIP or folder. **Codes** are the small built-in game tweaks you might know from desktop mod managers.
- **Updates** checks this fork's releases for a newer version.
- **Advanced** lets you skip the intro logos, turn on diagnostic logs, and share the log when something goes wrong.

Mods use the same `cpkredir.ini` format as the desktop mod managers, so existing setups carry over.

## Getting the most out of it

Most of the graphics options are the game's own. The iOS-specific ones are in the launcher's **Graphics** section and in-game under **Options → Video**:

- **MetalFX Upscaling** works like DLSS. **Quality** renders at 67% of the screen resolution and is the default. **Balanced** (58%), **Performance** (50%) and **Ultra Performance** (33%) trade sharpness for speed. **Off** skips MetalFX entirely.
- **Resolution Scale** follows the preset. Drag it yourself and the preset switches to **Custom**.
- **FPS** defaults to 30, which is kind to your battery. 60 needs a fast phone.
- **Skip Unused Copies** and **Reuse Render Targets** are two tricks that cut the GPU's work by about 17%. They're on by default. If you ever see flickering, frozen patches of the image or broken bloom, turn them off and let me know.
- **Show FPS**, **Show Profiler** and **Metal HUD** are there if you like numbers on screen.

There's no VSync option, because iOS never tears anyway.

On an iPhone 15, my advice: keep **Quality** at 60 FPS. You can push the resolution to around 75%, but once the phone warms up it'll start to struggle. Native 100% is really only realistic at 30 FPS.

## Known issues

- **Brief hitches** (roughly 70–300 ms) during autosaves and when entering a new area. That's the game's own CPU work, not the graphics.
- **Heat.** Long sessions at high resolutions slow down as the phone warms up. iOS quietly lowers the GPU clock well before it admits to being "Fair" or "Serious" on temperature.
- **Temporal MetalFX** is still experimental and hidden behind a launch flag.

Found something else? Open an issue and attach `unleashedrecomp.log`: in the launcher, **Advanced → Share log** does it in two taps. Mention your device, iOS version and settings, and I'll take a look.

## Building it yourself

You'll need a Mac with Xcode (including the iOS SDK and the Metal toolchain), Python 3.12+, CMake 3.20+, Ninja, and an Apple ID; a free one is fine.

iOS builds from the same game code as the Android port. Only three pieces differ: the renderer library, SDL and the shader translator, which `ios/prepare.py` downloads for you. Roughly, from the repository root:

1. Put your own `default.xex`, `default.xexp` and `shader.ar` into `UnleashedRecompLib/private/`.
2. Run `python3 ios/prepare.py`, build the host tools, then run `python3 ios/generate.py`.
3. Configure the Xcode project with your Team ID and a bundle ID of your own, and build the `UnleashedRecomp` target in Release.
4. Install it from Xcode, or package an unsigned IPA for a sideloading tool:

   ```sh
   python3 ios/package_ipa.py
   ```

   The IPA lands in `out/ios/dist/`.

The full walkthrough is in [ios/README.md](ios/README.md). One rule: never commit or share `UnleashedRecompLib/private/`, `UnleashedRecompLib/ppc/` or anything in `out/`, because they contain or are derived from the game.

## For developers

- [ios/README.md](ios/README.md) covers the build pipeline, the dependency patches and the diagnostic launch flags.
- [docs/IOS_PORTING.md](docs/IOS_PORTING.md) is the engineering log: how performance was measured at a locked GPU clock, what each render pass costs, the frame-pacing fixes and the render-target work.
- [tools/ios_probe](tools/ios_probe) is a small app that reports what a device's GPU supports.
- Android and iOS share one codebase, so improvements land on both. Platform-specific code sits behind `__ANDROID__` and `UNLEASHED_RECOMP_IOS`. For the Android side, see the [Android port's](https://github.com/SansNope/UnleashedRecomp-Android) documentation.

## A note on AI

Like the Android port, most of this was written with AI tools: OpenAI Codex for the first bring-up, and Anthropic's Claude (Opus 5.5) for the performance work, the launcher and the merge with Android. I ([ITSeniy](https://github.com/ITSeniy)) set the direction, tested every build on a real iPhone and decided what shipped. If you want to help, the most useful thing is a detailed bug report with the log attached.

## Legal

This repository contains no *Sonic Unleashed* game assets. The project isn't affiliated with or endorsed by SEGA, Microsoft, Apple or the Unleashed Recompiled team.

It's distributed under GPL-3.0, like the upstream project. Third-party components keep their own licenses.

## Thanks

- [hedge-dev and everyone behind Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp), for making any of this possible
- [SansNope](https://github.com/SansNope) and the [Android port](https://github.com/SansNope/UnleashedRecomp-Android) contributors, whose work this fork is built on
- [Loganbogan9](https://github.com/Loganbogan9/UnleashedRecompiOS), for the Metal and UIKit groundwork
- [renderbag/plume](https://github.com/renderbag/plume), the renderer and its Metal backend
- [MeloNX](https://starforgejo.dev/projects/MeloNX), whose sideloading guide inspired the one here
- Everyone who tests builds and sends logs
