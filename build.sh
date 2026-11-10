#!/bin/bash
# Локальная сборка Synapse.dylib на macOS (зеркало .github/workflows/build.yml)
set -euo pipefail

# 1. Собираем Dobby статической библиотекой под iphoneos arm64
cmake -S Src/Dobby -B dobby-build \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_SYSTEM_PROCESSOR=arm64 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk iphoneos --show-sdk-path)" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build dobby-build --target dobby_static -j"$(sysctl -n hw.ncpu)"

# 2. Компилируем дилб
xcrun -sdk iphoneos clang++ -arch arm64 -miphoneos-version-min=14.0 -std=c++17 -fobjc-arc -O2 \
  -dynamiclib \
  -I Src -I Src/ImGui -I Src/ImGui/Backends -I Src/Memory -I Src/Dobby/include \
  Src/main.mm \
  Src/Features/Visuals/visuals.mm \
  Src/Features/Aim/aim.mm \
  Src/Features/Rage/rage.mm \
  Src/Features/Misc/misc.mm \
  Src/Hooks/hooks.mm \
  Src/il2cpp/il2cpp.mm \
  Src/Memory/KittyAsm.cpp \
  Src/Memory/KittyIOFile.cpp \
  Src/Memory/KittyMemory.cpp \
  Src/Memory/KittyPtrValidator.cpp \
  Src/Memory/KittyScanner.cpp \
  Src/Memory/KittyUtils.cpp \
  Src/Memory/MemoryBackup.cpp \
  Src/Memory/MemoryPatch.cpp \
  Src/ImGui/imgui.cpp \
  Src/ImGui/imgui_draw.cpp \
  Src/ImGui/imgui_tables.cpp \
  Src/ImGui/imgui_widgets.cpp \
  Src/ImGui/Backends/imgui_impl_metal.mm \
  dobby-build/libdobby.a \
  Src/Memory/Deps/Keystone/libs-ios/arm64/libkeystone.a \
  -framework UIKit -framework Foundation -framework Metal -framework MetalKit -framework QuartzCore -framework CoreGraphics \
  -o Synapse.dylib

file Synapse.dylib
