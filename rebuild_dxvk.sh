#!/bin/bash
set -ex

cd /mnt/d/Rotating-art-Launcher/dxvk-master

# Clean old build
rm -rf build.android

# Configure with meson
/usr/local/bin/meson setup \
    --cross-file build-android-arm64.txt \
    --buildtype release \
    --prefix /tmp/dxvk-android \
    -Dnative_sdl2=enabled \
    -Dnative_sdl3=disabled \
    -Dnative_glfw=disabled \
    build.android

# Build with ninja
cd build.android
ninja -j4

echo "Build completed!"
