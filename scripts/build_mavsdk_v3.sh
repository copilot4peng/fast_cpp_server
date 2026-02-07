#!/bin/bash
# set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."

MAVSDK_DIR="$ROOT_DIR/external/mavsdk_v3"
BUILD_DIR="$MAVSDK_DIR/build"
INSTALL_DIR="$ROOT_DIR/build/mavsdk_dist"

THIRD_PARTY_INSTALL="$BUILD_DIR/third_party/install"

echo "=============================="
echo "Building MAVSDK v3 (static)"
echo "MAVSDK source : $MAVSDK_DIR"
echo "Build dir     : $BUILD_DIR"
echo "Install dir   : $INSTALL_DIR"
echo "=============================="

if [ ! -d "$MAVSDK_DIR" ]; then
    echo "ERROR: MAVSDK directory not found: $MAVSDK_DIR"
    exit 1
fi

mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"

cd "$BUILD_DIR"

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DENABLE_BACKEND_UDP=ON \
    -DENABLE_BACKEND_TCP=ON \
    -DENABLE_BACKEND_SERIAL=ON

cmake --build . -- -j$(nproc)

echo "---- Installing MAVSDK core ----"
cmake --install . --prefix "$INSTALL_DIR"

echo "---- Installing MAVSDK third_party ----"

# lib/*.a
mkdir -p "$INSTALL_DIR/lib"
if [ -f "$THIRD_PARTY_INSTALL/lib/liblibevents.a" ]; then
    cp -av "$THIRD_PARTY_INSTALL/lib/liblibevents.a" "$INSTALL_DIR/lib/libevents.a"
else
    echo "WARNING: liblibevents.a not found, skipping"
fi
cp -av "$THIRD_PARTY_INSTALL/lib/"*.a "$INSTALL_DIR/lib/"

# cmake config files
mkdir -p "$INSTALL_DIR/lib/cmake"
cp -av "$THIRD_PARTY_INSTALL/lib/cmake/"* "$INSTALL_DIR/lib/cmake/"

# pkg-config (optional, but harmless)
if [ -d "$THIRD_PARTY_INSTALL/lib/pkgconfig" ]; then
    mkdir -p "$INSTALL_DIR/lib/pkgconfig"
    cp -av "$THIRD_PARTY_INSTALL/lib/pkgconfig/"* "$INSTALL_DIR/lib/pkgconfig/"
fi

echo "=============================="
echo "MAVSDK v3 installed successfully"
echo "Prefix: $INSTALL_DIR"
echo "=============================="

cd "$ROOT_DIR"
