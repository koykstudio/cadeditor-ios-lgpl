# LGPL sources for CAD Editor for iOS

**CAD Editor** (iPhone and iPad, by KoykStudio) uses two libraries under the **GNU Lesser General Public License**:

| Library | Version we ship | Licence | In the app |
|---|---|---|---|
| **Open CASCADE Technology (OCCT)** | 7.9.3, upstream tag [`V7_9_3`](https://github.com/Open-Cascade-SAS/OCCT/releases/tag/V7_9_3) (tag object `adb3db9a5948`, commit `a016080bf6738d6aeae020badee4e888ad1540a5`), **unmodified** | LGPL 2.1 with the Open CASCADE exception 1.0 | `Frameworks/OCCT.framework` |
| **PlaneGCS** (FreeCAD's sketch solver, `src/Mod/Sketcher/App/planegcs`) | FreeCAD 1.1.4, tag [`1.1.4`](https://github.com/FreeCAD/FreeCAD/tree/1.1.4/src/Mod/Sketcher/App/planegcs) (commit `4fd3bf320d9566a27e60069fc8387448aaa3a094`), with our patch and C wrapper | LGPL 2 or later (we distribute it under 2.1 or later) | `Frameworks/PlaneGCS.framework` |

This repository holds what the LGPL asks us to provide for them: the exact source we build from, our changes, the
build scripts, and how to use your own build with the app. It is the source offer named in the app's
Settings > About > Licences. It applies to CAD Editor 1.0 and stays available for at least three years after the
last version of the app that ships these library versions.

## What is here

| Path | What |
|---|---|
| `scripts/build-occt.sh` | Builds OCCT 7.9.3 for iOS (device or simulator) as static toolkit libraries with CMake and Ninja |
| `scripts/build-frameworks.sh` | Links those toolkits into the dynamic `OCCT.framework`, builds the dynamic `PlaneGCS.framework`, and makes the `.xcframework`s |
| `planegcs/planegcs/` | The PlaneGCS sources, byte-identical to FreeCAD 1.1.4 |
| `planegcs/patches/koyk.patch` | Our only change to PlaneGCS (below) |
| `planegcs/stubs/` | Three small headers that stand in for FreeCAD's own headers outside FreeCAD (`FCConfig.h`, `SketcherGlobal.h`, `Base/Console.h`) |
| `planegcs/PlaneGcsC.cpp` | Our C API over PlaneGCS, compiled into `PlaneGCS.framework` (LGPL 2.1 or later). The same file is used by our Windows app, so its header comment says `PlaneGcs.dll` |
| `third_party/nlohmann/` | nlohmann/json 3.11.3 (MIT), the JSON header the wrapper uses |
| `abi/occt-imports-cadeditor-1.0.txt` | The 235 OCCT symbols the app imports from `OCCT.framework` (see "Using your own build") |
| `licenses/` | The upstream licence texts: OCCT's `LICENSE_LGPL_21.txt` and `OCCT_LGPL_EXCEPTION.txt`, FreeCAD's LGPL 2.1 text, Eigen's MPL 2.0, nlohmann/json's MIT |
| `LICENSE` | LGPL 2.1, the licence of everything KoykStudio wrote in this repository (scripts, patch, stubs, wrapper) |

### Release 1.0: the exact source archives

The [release **1.0**](https://github.com/koykstudio/cadeditor-ios-lgpl/releases/tag/1.0) (matching app version 1.0)
has the source archives the app's libraries are built from, so they stay available even if an upstream link changes:

| Asset | SHA-256 | What |
|---|---|---|
| `OCCT-7_9_3.tar.gz` | `5ecf094ec6b12d5413dfb851d8c3590c354058aee556e32e408bdfbf8c357d57` | GitHub's archive of OCCT tag `V7_9_3`, the file `scripts/build-occt.sh` downloads and checks |
| `FreeCAD-1.1.4-planegcs.tar.gz` | `034ca750d2887f98902660f98c086a11bc18cccbebb061f60c119af5400305b8` | The unmodified `planegcs` folder of FreeCAD 1.1.4 with FreeCAD's licence file |
| `eigen-3.4.0.tar.gz` | `8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72` | Eigen 3.4.0 (MPL 2.0; header-only, compiled into PlaneGCS with `EIGEN_MPL2_ONLY`) |
| `SHA256SUMS.txt` | | The hashes above |

The patch, stubs, wrapper and scripts are in this repository at tag `1.0`.

## What we changed

- **OCCT: nothing.** The source is the upstream tag. Only the build is configured: static toolkits
  (`BUILD_LIBRARY_TYPE=Static`), no Draw, DETools, FreeType, Tcl/Tk, OpenGL/GLES, Xlib or FreeImage, compiled with
  `-fvisibility=default`, then the 29 toolkits the app uses are linked whole (`-all_load`) into one dynamic library,
  `OCCT.framework`, that exports all their symbols.
- **PlaneGCS: `planegcs/patches/koyk.patch`.**
  - Boost removed: `GCS.cpp` used Boost.Graph only to number the connected components of the constraint graph; the
    patch replaces it with a small union-find that numbers components the same way (in the order of their lowest
    vertex). `Geo.h` drops an unused Boost include.
  - Headers that FreeCAD supplies through its precompiled header (`<cmath>`, `<map>`, `<vector>`, `<cassert>`) are
    included directly, and `SketcherGlobal.h` is found on the include path instead of FreeCAD's tree.
  - The solver itself is unchanged.
- **PlaneGCS wrapper: `planegcs/PlaneGcsC.cpp`**, a flat C API (`pg_solve`, `pg_drag_begin`, `pg_drag_step`,
  `pg_drag_end`, `pg_free`, `pg_version`) taking and returning JSON. Its first comment documents the protocol.

## How the app links them

- `OCCT.framework` and `PlaneGCS.framework` are **separate dynamic frameworks** (Mach-O dylibs, arm64) embedded in the
  app's `Frameworks/` folder, with the install names `@rpath/OCCT.framework/OCCT` and
  `@rpath/PlaneGCS.framework/PlaneGCS`. **No OCCT or PlaneGCS code is linked statically into the app's own binaries.**
- **OCCT** is used by the app's own modelling bridge, `OcctModel.framework` (KoykStudio's code, a "work that uses the
  Library", not part of this offer), which is dynamically linked against `OCCT.framework` and imports the C++ symbols
  listed in `abi/occt-imports-cadeditor-1.0.txt`.
- **PlaneGCS** is opened by path at run time (.NET's `NativeLibrary.Load`, i.e. `dlopen`) and its six `pg_*` C
  functions are looked up by name (`dlsym`).
- The app's main executable lists both frameworks as dynamic dependencies but imports no symbols from them.

## Building

On a Mac with Xcode (CAD Editor 1.0 was built with Xcode 27.1 / AppleClang 21, CMake 4.4.5, Ninja 1.13.2; iOS 17.0
deployment target, arm64 only):

```sh
scripts/build-occt.sh device        # about 15 minutes; build/occt/device
scripts/build-occt.sh sim           # optional: the simulator slice
scripts/build-frameworks.sh device  # build/native/device/{OCCT,PlaneGCS}.framework
scripts/build-frameworks.sh sim
scripts/build-frameworks.sh xcframework   # needs both slices
```

The scripts download OCCT and Eigen from upstream and check the SHA-256 above; you can also put the release assets in
place yourself (`build/occt/OCCT-7_9_3/` unpacked, `build/planegcs/eigen.tgz`). Built this way, from this repository,
the device `OCCT.framework/OCCT` and `PlaneGCS.framework/PlaneGCS` binaries are byte-identical to the unsigned ones
we embed in CAD Editor 1.0 (the copies in the app differ only by Apple's code signature).

To build a **modified** version, change the source (`build/occt/OCCT-7_9_3/` for OCCT; `planegcs/` or the patch for
PlaneGCS) and run the same scripts.

## Using your own build with the app

Because both libraries are dynamic frameworks, a modified build can take the place of ours without relinking the app:

1. Build `OCCT.framework` and/or `PlaneGCS.framework` for the device slice as above, with your changes.
2. In the app package (`Payload/CADEditor.app` inside the `.ipa`), replace `Frameworks/OCCT.framework/OCCT` and/or
   `Frameworks/PlaneGCS.framework/PlaneGCS` (and the `Info.plist` next to it if you like) with yours.
3. Re-sign the frameworks and the app with your own Apple signing identity and a provisioning profile for your
   device (for example `codesign --force --sign "<identity>"` on each framework, then on the app with its
   entitlements), and install it on your device.

Interface compatibility: a replacement **PlaneGCS** must export the six `pg_*` functions with the same JSON protocol.
A replacement **OCCT** must export every symbol in `abi/occt-imports-cadeditor-1.0.txt` with the same C++ ABI, which
in practice means a build from OCCT 7.9.x sources with the compiler and settings above. iOS requires every app to be
signed, so step 3 needs an Apple developer signing identity (a free Apple account is enough for your own devices).
Copies downloaded from the App Store are encrypted by Apple, which is outside our control.

Nothing in CAD Editor's licence agreement limits the rights the LGPL gives you for these libraries, including
modifying them and reverse engineering the app to debug such modifications.

## Other components in the app

Everything else in CAD Editor is either KoykStudio's own code or under permissive licences, listed with their texts in
the app (Settings > About > Licences): ACadSharp and CSUtilities (MIT), the .NET runtime / NativeAOT (MIT),
LibTessDotNet (SGI Free Software License B 2.0), nlohmann/json (MIT), Eigen (MPL 2.0, header-only, inside
PlaneGCS.framework), sentry-cocoa (MIT), and the Hershey fonts (Usenet distribution). None of them is LGPL or GPL.

## Contact

support@koykstudio.com. On request we also send this source by other means, for no more than the cost of sending
it, for at least three years after the last distribution of this app version.
