#!/usr/bin/env bash

# MKV Chapter Editor - Automated Build Script (Linux & macOS)

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

echo ""
echo "==========================================="
echo "  MKV Chapter Editor - Build Script"
echo "==========================================="
echo ""

deps_satisfied() {
    command -v cmake >/dev/null 2>&1 &&
    (command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1) &&
    command -v pkg-config >/dev/null 2>&1 &&
    pkg-config --exists mpv >/dev/null 2>&1
}

if [ "$SKIP_DEPS" -eq 1 ]; then
    echo "Skipping dependency installation (--skip-deps)."
elif deps_satisfied; then
    echo "All build dependencies already satisfied; skipping package installation."
else
    OS_NAME="$(uname -s)"
    case "$OS_NAME" in
        Darwin*)
            echo "Detected macOS."
            if ! command -v brew >/dev/null 2>&1; then
                echo "Error: Homebrew is required to install dependencies on macOS."
                echo "Please install it from https://brew.sh and run this script again."
                exit 1
            fi
            echo "Installing dependencies via Homebrew..."
            brew install cmake mpv mkvtoolnix pkg-config
            ;;
        Linux*)
            echo "Detected Linux."
            if command -v apt-get >/dev/null 2>&1; then
                echo "Debian/Ubuntu system detected. Installing dependencies via APT..."
                sudo apt-get update
                sudo apt-get install -y cmake g++ libmpv-dev mkvtoolnix pkg-config
            elif command -v pacman >/dev/null 2>&1; then
                echo "Arch Linux system detected. Installing dependencies via Pacman..."
                sudo pacman -S --needed --noconfirm cmake gcc mpv mkvtoolnix-cli pkgconf
            elif command -v dnf >/dev/null 2>&1; then
                echo "Fedora system detected. Installing dependencies via DNF..."
                echo "Note: 'mpv-devel' requires RPM Fusion Free enabled on Fedora."
                sudo dnf install -y cmake gcc-c++ mpv-devel mkvtoolnix pkgconf
            elif command -v zypper >/dev/null 2>&1; then
                echo "openSUSE system detected. Installing dependencies via Zypper..."
                sudo zypper install -y cmake gcc-c++ mpv-devel mkvtoolnix pkg-config
            else
                echo "Warning: Could not auto-detect package manager."
                echo "Please ensure cmake, a C++17 compiler, pkg-config, and libmpv dev headers are installed."
            fi
            ;;
        *)
            echo "Unsupported OS ($OS_NAME) for automated dependency install. Please compile manually."
            exit 1
            ;;
    esac
fi

if ! command -v mkvmerge >/dev/null 2>&1; then
    echo "Warning: 'mkvmerge' not found. Editing and XML export will work, but remuxing requires MKVToolNix."
fi

echo ""
echo "Configuring and compiling project..."
echo ""

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

echo ""
echo "Running unit tests..."
ctest --test-dir build --output-on-failure

cp build/chapter-editor ./chapter-editor

echo ""
echo "==========================================="
echo "  SUCCESS! Build complete."
echo "==========================================="
echo ""
echo "You can now run the application:"
echo "  ./chapter-editor /path/to/your/video.mkv"
echo ""
