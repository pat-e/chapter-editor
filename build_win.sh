#!/usr/bin/env bash

# MKV Chapter Editor - Automated Build Script for Windows (MSYS2 Clang64)

# Exit immediately if a command exits with a non-zero status

set -e

echo "="
echo "  MKV Chapter Editor - Windows Build (Clang64)"
echo "="
echo ""

# 1. Verify we are actually running in the Clang64 environment

if [ "$MSYSTEM" != "CLANG64" ]; then
echo "ERROR: Incorrect environment detected."
echo "This script must be run from the 'MSYS2 Clang64' terminal."
echo "Please open your Start Menu, search for 'MSYS2 Clang64', launch it,"
echo "navigate to this directory, and run the script again."
exit 1
fi

# 2. Grab necessary dependencies via Pacman

echo "Updating MSYS2 package database and grabbing dependencies..."

# We install the clang toolchain, cmake, ninja (for faster building), mpv, and mkvtoolnix

pacman -S --needed --noconfirm \
mingw-w64-clang-x86_64-toolchain \
mingw-w64-clang-x86_64-cmake \
mingw-w64-clang-x86_64-ninja \
mingw-w64-clang-x86_64-pkgconf \
mingw-w64-clang-x86_64-mpv \
mingw-w64-clang-x86_64-mkvtoolnix

echo ""
echo ""
echo "  Dependencies met. Compiling project..."
echo ""

# 3. Setup the build directory and compile via CMake using Ninja

mkdir -p build
cd build
cmake -G Ninja ..
cmake --build .

# 4. Move the compiled binary out to the root directory

echo ""
echo "Finalizing setup..."

# On Windows, CMake outputs an .exe file

cp chapter-editor.exe ../
cd ..

echo ""
echo ""
echo "  SUCCESS! Build complete."
echo ""
echo "You can now run the application from this terminal:"
echo "  ./chapter-editor.exe /path/to/your/video.mkv"
echo ""
echo "Note: To run this .exe outside of the MSYS2 terminal, you would"
echo "need to copy the required DLLs (like libmpv-2.dll) into this folder."
echo ""