#!/usr/bin/env python3
"""Build a Simulator-only UIKit preview from the prepared game's touch view."""
from pathlib import Path
import plistlib, platform, subprocess, re

ROOT = Path(__file__).resolve().parents[2]
stage = ROOT / 'out/ios/source'
output = ROOT / 'out/ios/control-preview'
output.mkdir(parents=True, exist_ok=True)
view = (stage / 'UnleashedRecomp/ui/ios_touch_controls.mm').read_text()
view = view[view.index('static std::atomic<float>'):view.index('static UnleashedTouchControlsView* g_touchControls;')]
(output / 'production_touch_view.inc').write_text(view)
# Only the Xbox controller ABI/constants are needed; no guest kernel is linked.
xbox = (stage / 'tools/XenonRecomp/XenonUtils/xbox.h').read_text()
defines = '\n'.join(re.findall(r'^#define XAMINPUT_GAMEPAD_.*$', xbox, re.M))
struct = re.search(r'typedef struct _XAMINPUT_GAMEPAD\s*\{.*?\} XAMINPUT_GAMEPAD, \*PXAMINPUT_GAMEPAD;', xbox, re.S).group()
(output / 'xbox.h').write_text('#pragma once\n#include <cstdint>\n'+defines+'\n'+struct+'\n')
bundle = output / 'Unleashed Controls Preview.app'
bundle.mkdir(exist_ok=True)
info = {'CFBundleIdentifier':'io.github.itseniy.UnleashedControlsPreview','CFBundleExecutable':'ControlsPreview',
        'CFBundleName':'Controls Preview','CFBundleVersion':'1','CFBundleShortVersionString':'1.0',
        'CFBundlePackageType':'APPL','MinimumOSVersion':'16.4','UIDeviceFamily':[1,2],
        'UILaunchScreen':{},'UIRequiresFullScreen':True,'UIViewControllerBasedStatusBarAppearance':True,
        'UIApplicationSceneManifest': {'UIApplicationSupportsMultipleScenes':False, 'UISceneConfigurations': {'UIWindowSceneSessionRoleApplication':[{'UISceneConfigurationName':'Preview', 'UISceneDelegateClassName':'PreviewSceneDelegate'}]}},
        'UISupportedInterfaceOrientations':['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'],
        'UISupportedInterfaceOrientations~ipad':['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight']}
with (bundle / 'Info.plist').open('wb') as f: plistlib.dump(info,f)
arch = 'arm64' if platform.machine() == 'arm64' else 'x86_64'
sdk = subprocess.check_output(['xcrun','--sdk','iphonesimulator','--show-sdk-path'],text=True).strip()
subprocess.run(['xcrun','--sdk','iphonesimulator','clang++','-target',arch+'-apple-ios16.4-simulator',
    '-isysroot',sdk,'-std=c++20','-fobjc-arc','-I'+str(output),'-I'+str(stage/'UnleashedRecomp'),
    str(ROOT/'ios/control_preview/main.mm'),'-framework','UIKit','-framework','Foundation','-framework','CoreGraphics','-framework','QuartzCore',
    '-o',str(bundle/'ControlsPreview')],check=True)
subprocess.run(['codesign','--force','--sign','-',str(bundle)],check=True)
print(bundle)
