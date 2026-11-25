#!/usr/bin/env bash

# MKV Chapter Editor - Automated Build Script for Windows (MSYS2 Clang64)

set -euo pipefail

SKIP_DEPS=0
for arg in "$@"; do
    case "$arg" in
        --skip-deps)
            SKIP_DEPS=1
            ;;
        -h|--help)
            echo "Usage: $0 [--skip-deps]"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--skip-deps]"
            exit 1
            ;;
    esac
done

echo "=================================================="
echo "  MKV Chapter Editor - Windows Build (Clang64)"
echo "=================================================="
echo ""

if [ "${MSYSTEM:-}" != "CLANG64" ]; then
    echo "ERROR: Incorrect environment detected."
    echo "This script must be run from the 'MSYS2 Clang64' terminal."
    echo "Please open your Start Menu, search for 'MSYS2 Clang64', launch it,"
    echo "navigate to this directory, and run the script again."
    exit 1
fi

deps_satisfied() {
    command -v cmake >/dev/null 2>&1 &&
    command -v ninja >/dev/null 2>&1 &&
    command -v clang++ >/dev/null 2>&1 &&
    command -v pkg-config >/dev/null 2>&1 &&
    pkg-config --exists mpv >/dev/null 2>&1
}

if [ "$SKIP_DEPS" -eq 1 ]; then
    echo "Skipping dependency installation (--skip-deps)."
elif deps_satisfied; then
    echo "All build dependencies already satisfied; skipping pacman."
else
    echo "Updating MSYS2 package database and installing dependencies..."
    pacman -S --needed --noconfirm \
        mingw-w64-clang-x86_64-toolchain \
        mingw-w64-clang-x86_64-cmake \
        mingw-w64-clang-x86_64-ninja \
        mingw-w64-clang-x86_64-pkgconf \
        mingw-w64-clang-x86_64-mpv \
        mingw-w64-clang-x86_64-mkvtoolnix
fi

echo ""
echo "Configuring and compiling project..."
echo ""

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

echo ""
echo "Running unit tests..."
ctest --test-dir build --output-on-failure

cp build/chapter-editor.exe ./chapter-editor.exe

echo ""
echo "=================================================="
echo "  SUCCESS! Build complete."
echo "=================================================="
echo ""
echo "You can now run the application from this terminal:"
echo "  ./chapter-editor.exe /path/to/your/video.mkv"
echo ""
echo "Note: To run this .exe outside of the MSYS2 terminal, copy the required"
echo "DLLs (such as libmpv-2.dll) from /clang64/bin into this folder."
echo ""
