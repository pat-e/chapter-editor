#!/usr/bin/env bash

# MKV Chapter Editor - Automated Build Script

# Exit immediately if a command exits with a non-zero status

set -e

echo ""
echo "  MKV Chapter Editor - Build Script"
echo ""
echo ""

# 1. Detect OS and grab necessary dependencies

if [ "$(uname)" == "Darwin" ]; then
echo "Detected macOS."
if ! command -v brew &> /dev/null; then
echo "Error: Homebrew is required to grab dependencies on macOS."
echo "Please install it from https://brew.sh and run this script again."
exit 1
fi
echo "Grabbing dependencies via Homebrew..."
brew install cmake mpv mkvtoolnix pkg-config
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
echo "Detected Linux."
# Debian / Ubuntu based
if command -v apt-get &> /dev/null; then
echo "Debian/Ubuntu system detected. Grabbing dependencies via APT..."
sudo apt-get update
sudo apt-get install -y cmake g++ libmpv-dev mkvtoolnix pkg-config
# Arch based
elif command -v pacman &> /dev/null; then
echo "Arch Linux system detected. Grabbing dependencies via Pacman..."
sudo pacman -S --needed --noconfirm cmake gcc mpv mkvtoolnix-cli pkgconf
# Fedora / RedHat based
elif command -v dnf &> /dev/null; then
echo "Fedora system detected. Grabbing dependencies via DNF..."
sudo dnf install -y cmake gcc-c++ mpv-devel mkvtoolnix pkgconf
else
echo "Warning: Could not auto-detect package manager."
echo "Please ensure CMake, a C++ compiler, pkg-config, and libmpv-dev are installed."
fi
else
echo "Unsupported OS for automated build script. Please compile manually."
exit 1
fi

echo ""
echo ""
echo "  Dependencies met. Compiling project..."
echo ""

# 2. Setup the build directory and compile via CMake

mkdir -p build
cd build
cmake ..
cmake --build .

# 3. Move the compiled binary out to the root directory for user convenience

echo ""
echo "Finalizing setup..."
cp chapter-editor ../
cd ..

echo ""
echo ""
echo "  SUCCESS! Build complete."
echo ""
echo "You can now run the application:"
echo "  ./chapter-editor /path/to/your/video.mkv"
echo ""