# Simulator touch controls preview

Run `python3 ios/control_preview/build.py` after preparing the iOS source.
The output is `out/ios/control-preview/Unleashed Controls Preview.app`.
Install it on a booted Simulator with `xcrun simctl install booted <app>` and
launch `io.github.itseniy.UnleashedControlsPreview`.

The build extracts the actual UIKit touch view and uses the game's unchanged
`TouchGamepad` header. It exercises layout, safe areas, rotation and input state
without the game's Metal backend or assets. The plain background is intentional.
It does not measure game frame rate, device memory limits or thermal behavior.
Mouse gestures also do not establish whether simultaneous controls are comfortable
with fingers on a real iPad. Rebuild after changing the game touch view/header.


The preview also exports `Documents/controls-layout.png` and
`controls-layout.json` using the live UIKit view hierarchy after layout. These
show the actual app surface and safe-area values, excluding Simulator's device
canvas (which can remain portrait and letterbox a landscape-only app). Retrieve
its data container with `xcrun simctl get_app_container <device> io.github.itseniy.UnleashedControlsPreview data`.

Checked on iOS 26.5 Simulator: iPad mini (A17 Pro), 1133×744 points; iPad Pro 13
(M5), 1376×1032 points. Both had a 25-point bottom safe area and no control overlap.
The game itself was not run in Simulator. Do not use either preview to infer
that the game supports the simulator's advertised iPad chip or runs at a given FPS.
