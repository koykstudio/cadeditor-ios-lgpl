#!/bin/bash
# Builds Open CASCADE Technology 7.9.3 (upstream release tag V7_9_3, unmodified) as static libraries for one iOS
# slice. scripts/build-frameworks.sh then links them into OCCT.framework, a dynamic framework (LGPL 2.1 + exception).
# This is the script CAD Editor 1.0 is built with; only the paths differ (everything goes to ./build here).
# About 15 minutes per slice on an M-series Mac.
#   scripts/build-occt.sh device|sim
# Needs: Xcode (CAD Editor 1.0: Xcode 27.1, AppleClang 21), CMake (4.4.5), Ninja (1.13.2), curl, shasum.
# To use a modified OCCT, put your tree at build/occt/OCCT-7_9_3 before running (the download is skipped).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SLICE=${1:?device|sim}
B=$ROOT/build/occt
SRC=$B/OCCT-7_9_3
if [ ! -d "$SRC" ]; then
  mkdir -p "$B"
  # The same archive is attached to this repository's release 1.0 as OCCT-7_9_3.tar.gz.
  curl -sSL -o "$B/occt.tgz" https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_9_3.tar.gz
  echo "5ecf094ec6b12d5413dfb851d8c3590c354058aee556e32e408bdfbf8c357d57  $B/occt.tgz" | shasum -a 256 -c -
  tar -xzf "$B/occt.tgz" -C "$B"
fi
OUT=$B/$SLICE
if [ "$SLICE" = device ]; then SYSROOT=iphoneos; else SYSROOT=iphonesimulator; fi
TK="TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool TKFillet TKOffset TKFeat TKMesh TKShHealing TKXSBase TKDE TKDESTEP TKDEIGES TKDESTL TKLCAF TKCAF TKXCAF TKCDF TKService TKV3d TKVCAF TKHLR"
mkdir -p "$OUT"
cmake -S "$SRC" -B "$OUT/build" -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=$SYSROOT -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_LIBRARY_TYPE=Static -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_DETools=OFF \
  -DUSE_FREETYPE=OFF -DUSE_TK=OFF -DUSE_GLES2=OFF -DUSE_OPENGL=OFF -DUSE_XLIB=OFF -DUSE_FREEIMAGE=OFF \
  -DBUILD_USE_PCH=OFF -DBUILD_DOC_Overview=OFF -DINSTALL_DIR="$OUT/install" \
  -DCMAKE_CXX_FLAGS="-fvisibility=default -Wno-deprecated-declarations" > "$OUT.configure.log" 2>&1
ninja -C "$OUT/build" -j8 $TK > "$OUT.build.log" 2>&1
echo "built OCCT $SLICE"
