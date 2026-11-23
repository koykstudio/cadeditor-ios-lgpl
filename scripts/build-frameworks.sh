#!/bin/bash
# Builds the two LGPL frameworks CAD Editor for iOS embeds, for one slice (device | sim):
#   - OCCT static libraries from scripts/build-occt.sh                  -> OCCT.framework     (LGPL 2.1 + OCCT exception)
#   - planegcs (FreeCAD 1.1.4) + patches/koyk.patch + PlaneGcsC.cpp     -> PlaneGCS.framework (LGPL 2.1 or later)
# Both are dynamic frameworks (Mach-O dylibs, install names @rpath/OCCT.framework/OCCT and
# @rpath/PlaneGCS.framework/PlaneGCS), so they can be replaced in the app package with your own build.
# Then `scripts/build-frameworks.sh xcframework` joins both slices into build/native/*.xcframework.
# These are the commands CAD Editor 1.0 is built with; the app's build also links its own OcctModel.framework
# (not part of this repository) against OCCT.framework in the same script.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SLICE=${1:?device|sim|xcframework}
MINOS=17.0
OUT=$ROOT/build/native

if [ "$SLICE" = xcframework ]; then
  for fw in OCCT PlaneGCS; do
    rm -rf "$OUT/$fw.xcframework"
    xcodebuild -create-xcframework -framework "$OUT/device/$fw.framework" -framework "$OUT/sim/$fw.framework" \
      -output "$OUT/$fw.xcframework" >/dev/null
  done
  echo "xcframeworks in $OUT"; exit 0
fi

if [ "$SLICE" = device ]; then SDK=iphoneos; TARGET=arm64-apple-ios$MINOS; PLATFORM=iPhoneOS
else SDK=iphonesimulator; TARGET=arm64-apple-ios$MINOS-simulator; PLATFORM=iPhoneSimulator; fi
OCCT=$ROOT/build/occt/$SLICE/build
OCCT_LIB=$OCCT/mac64/clang/lib
D=$OUT/$SLICE
mkdir -p "$D"
CXX="xcrun -sdk $SDK clang++ -target $TARGET -std=c++17 -O2 -fPIC"

plist() { # name bundle-id version
  cat > "$D/$1.framework/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleDevelopmentRegion</key><string>en</string>
<key>CFBundleExecutable</key><string>$1</string>
<key>CFBundleIdentifier</key><string>$2</string>
<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
<key>CFBundleName</key><string>$1</string>
<key>CFBundlePackageType</key><string>FMWK</string>
<key>CFBundleShortVersionString</key><string>$3</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundleSupportedPlatforms</key><array><string>$PLATFORM</string></array>
<key>MinimumOSVersion</key><string>$MINOS</string>
</dict></plist>
EOF
}

# ---- OCCT.framework: every toolkit the app uses, whole (-all_load), with all symbols exported --------------------
TK="TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool TKFillet TKOffset TKFeat TKMesh TKShHealing TKXSBase TKDE TKDESTEP TKDEIGES TKDESTL TKLCAF TKCAF TKXCAF TKCDF TKService TKV3d TKVCAF TKHLR"
rm -rf "$D/OCCT.framework"; mkdir -p "$D/OCCT.framework"
LIBS=""; for t in $TK; do LIBS="$LIBS $OCCT_LIB/lib$t.a"; done
$CXX -dynamiclib -Wl,-all_load $LIBS -install_name @rpath/OCCT.framework/OCCT \
  -framework Foundation -framework CoreFoundation -framework UIKit -o "$D/OCCT.framework/OCCT"
plist OCCT com.koykstudio.app.cadeditor.occt 7.9.3

# ---- PlaneGCS.framework -----------------------------------------------------------------------------------------
PG=$ROOT/planegcs
PGB=$ROOT/build/planegcs
if [ ! -f "$PGB/eigen-3.4.0/Eigen/Core" ]; then
  mkdir -p "$PGB"
  # Eigen 3.4.0 headers (MPL 2.0; EIGEN_MPL2_ONLY). Also attached to release 1.0 as eigen-3.4.0.tar.gz.
  curl -sSL -o "$PGB/eigen.tgz" https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
  echo "8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72  $PGB/eigen.tgz" | shasum -a 256 -c - >/dev/null
  tar -xzf "$PGB/eigen.tgz" -C "$PGB" eigen-3.4.0/Eigen eigen-3.4.0/COPYING.MPL2
fi
# The pristine FreeCAD files, patched in the build folder. The wrapper includes nlohmann/json through the path it
# has in the app's source tree (../OcctModel/third_party/nlohmann/json.hpp), so it is staged with that layout.
rm -rf "$PGB/src"; mkdir -p "$PGB/src/wrapper" "$PGB/src/OcctModel/third_party"
cp -R "$PG/planegcs" "$PGB/src/"
(cd "$PGB/src" && patch -s -p1 < "$PG/patches/koyk.patch")
cp "$PG/PlaneGcsC.cpp" "$PGB/src/wrapper/"
cp -R "$ROOT/third_party/nlohmann" "$PGB/src/OcctModel/third_party/"
rm -rf "$D/PlaneGCS.framework"; mkdir -p "$D/PlaneGCS.framework"
xcrun -sdk $SDK clang++ -target $TARGET -std=c++20 -O2 -fPIC -w -fvisibility=hidden -DEIGEN_MPL2_ONLY -DEIGEN_NO_DEBUG \
  -I"$PGB/src" -I"$PGB/src/planegcs" -I"$PG/stubs" -I"$PGB/eigen-3.4.0" \
  "$PGB/src/planegcs/Constraints.cpp" "$PGB/src/planegcs/GCS.cpp" "$PGB/src/planegcs/Geo.cpp" \
  "$PGB/src/planegcs/SubSystem.cpp" "$PGB/src/planegcs/qp_eq.cpp" "$PGB/src/wrapper/PlaneGcsC.cpp" \
  -dynamiclib -install_name @rpath/PlaneGCS.framework/PlaneGCS -lc++ -o "$D/PlaneGCS.framework/PlaneGCS"
plist PlaneGCS com.koykstudio.app.cadeditor.planegcs 1.1.4

ls -la "$D"/OCCT.framework/* "$D"/PlaneGCS.framework/* | awk '{print $5, $9}'
