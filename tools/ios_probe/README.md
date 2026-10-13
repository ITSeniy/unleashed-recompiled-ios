# iPhone Metal / BC texture probe

This standalone iOS app uses this repository's `bcdec` and `etcpak` sources. It
converts synthetic BC1, BC3, BC4 and BC5 blocks to ETC2/EAC, uploads them to Metal,
samples them on the GPU, and compares every pixel against the expected color.
It also tests an sRGB texture, reports BC support / argument-buffer tier / the
current process memory budget, and briefly reserves and releases the 4 GiB guest
virtual window without making that memory resident.

This is **not an iOS game port**. Passing proves only these codec/Metal tests.
It does not prove shader-cache compatibility, mip/array/cubemap uploads, renderer
synchronization, game performance, or the game's peak memory footprint. No game
files or JIT entitlement are required.

## First launch

1. Connect and trust the iPhone, enable Developer Mode, and add an Apple Account
   in Xcode Settings → Apple Accounts.
2. Open `UnleashedMetalProbe.xcodeproj` in Xcode.
3. Select the UnleashedMetalProbe target → Signing & Capabilities. Select your
   Personal Team, leaving Automatically manage signing enabled. If Xcode reports
   a bundle identifier collision, change `io.github.itseniy.UnleashedMetalProbe`
   to a unique identifier.
4. Choose the physical iPhone in the run destination, then press ⌘R.
5. Read the report on the phone. It is also saved as `Documents/probe.txt` in the
   app container and printed to the Xcode console. In Device Hub / Devices and
   Simulators, download the app container to retrieve it.

An unsigned device build can be checked without a team:

```sh
xcodebuild -project tools/ios_probe/UnleashedMetalProbe.xcodeproj \
  -scheme UnleashedMetalProbe -configuration Release \
  -destination 'generic/platform=iOS' -derivedDataPath out/ios-probe \
  CODE_SIGNING_ALLOWED=NO build
```

Do not install an unsigned build; launch the signed version through Xcode.
