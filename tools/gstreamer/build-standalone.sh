#!/bin/bash
# Standalone build script for gst-llama plugin
# This script builds the GStreamer plugin independently of Guix

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "========================================"
echo "GStreamer llama.cpp Plugin - Standalone Build"
echo "========================================"
echo ""
echo "Repository: $REPO_ROOT"
echo "Build directory: $BUILD_DIR"
echo ""

# Check for required tools
echo "[1/6] Checking build tools..."
if ! command -v meson &> /dev/null; then
    echo "ERROR: meson not found. Install with:"
    echo "  Ubuntu/Debian: sudo apt-get install meson"
    echo "  Fedora: sudo dnf install meson"
    echo "  Arch: sudo pacman -S meson"
    exit 1
fi

if ! command -v ninja &> /dev/null; then
    echo "ERROR: ninja not found. Install with:"
    echo "  Ubuntu/Debian: sudo apt-get install ninja-build"
    echo "  Fedora: sudo dnf install ninja-build"
    echo "  Arch: sudo pacman -S ninja"
    exit 1
fi

if ! command -v pkg-config &> /dev/null; then
    echo "ERROR: pkg-config not found. Install with:"
    echo "  Ubuntu/Debian: sudo apt-get install pkg-config"
    exit 1
fi

echo "✓ meson version: $(meson --version)"
echo "✓ ninja version: $(ninja --version)"
echo "✓ pkg-config version: $(pkg-config --version)"
echo ""

# Check for GStreamer
echo "[2/6] Checking GStreamer dependencies..."
if ! pkg-config --exists gstreamer-1.0; then
    echo "ERROR: GStreamer not found. Install with:"
    echo "  Ubuntu/Debian: sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev"
    echo "  Fedora: sudo dnf install gstreamer1-devel gstreamer1-plugins-base-devel"
    echo "  Arch: sudo pacman -S gstreamer gst-plugins-base"
    exit 1
fi

GST_VERSION=$(pkg-config --modversion gstreamer-1.0)
echo "✓ GStreamer version: $GST_VERSION"
echo ""

# Check for llama_simple library
echo "[3/6] Checking llama_simple library..."
LLAMA_SIMPLE_LIB="$REPO_ROOT/tools/ffi/build/libllama_simple.a"
if [ ! -f "$LLAMA_SIMPLE_LIB" ]; then
    echo "WARNING: llama_simple library not found at $LLAMA_SIMPLE_LIB"
    echo "Building llama_simple first..."
    cd "$REPO_ROOT/tools/ffi"
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    echo "✓ llama_simple built successfully"
else
    echo "✓ llama_simple library found: $LLAMA_SIMPLE_LIB"
fi
echo ""

# Setup build directory
echo "[4/6] Setting up build directory..."
if [ -d "$BUILD_DIR" ]; then
    echo "Removing existing build directory..."
    rm -rf "$BUILD_DIR"
fi

cd "$SCRIPT_DIR"
meson setup "$BUILD_DIR" \
    --buildtype=debugoptimized \
    --prefix=/usr/local \
    -Dtests=enabled

echo "✓ Build directory configured"
echo ""

# Build the plugin
echo "[5/6] Building gst-llama plugin..."
meson compile -C "$BUILD_DIR" -v

echo "✓ Plugin built successfully"
echo ""

# Show build results
echo "[6/6] Build summary"
echo "========================================"
echo "Plugin file: $BUILD_DIR/libgstllama.so"
echo ""
echo "To install system-wide (requires sudo):"
echo "  sudo meson install -C $BUILD_DIR"
echo ""
echo "To test without installing:"
echo "  export GST_PLUGIN_PATH=$BUILD_DIR"
echo "  gst-inspect-1.0 llama"
echo ""
echo "To run tests:"
echo "  meson test -C $BUILD_DIR --verbose"
echo ""
echo "✓ Build complete!"
