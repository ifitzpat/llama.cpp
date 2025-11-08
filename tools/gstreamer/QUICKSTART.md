# GStreamer llama.cpp Plugin - Quick Start Guide

This guide will help you build and test the GStreamer plugin for llama.cpp in standalone mode (without Guix).

## Prerequisites

### Required Packages

**Ubuntu/Debian:**
```bash
sudo apt-get install \
    build-essential \
    cmake \
    meson \
    ninja-build \
    pkg-config \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-base
```

**Fedora:**
```bash
sudo dnf install \
    gcc gcc-c++ \
    cmake \
    meson \
    ninja-build \
    pkg-config \
    gstreamer1-devel \
    gstreamer1-plugins-base-devel \
    gstreamer1-tools
```

**Arch Linux:**
```bash
sudo pacman -S \
    base-devel \
    cmake \
    meson \
    ninja \
    pkg-config \
    gstreamer \
    gst-plugins-base
```

## Build Steps

### Step 1: Build llama.cpp Core

First, build the main llama.cpp library if you haven't already:

```bash
cd /path/to/llama.cpp
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..
```

### Step 2: Build llama_simple C API

```bash
cd tools/ffi
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ../../..
```

### Step 3: Build GStreamer Plugin

```bash
cd tools/gstreamer
./build-standalone.sh
```

This script will:
- Check for required dependencies
- Verify GStreamer installation
- Build llama_simple if needed
- Configure and build the plugin with Meson
- Show installation instructions

## Testing

### Test 1: Plugin Discovery

```bash
cd tools/gstreamer
./test-plugin.sh
```

This will verify:
- ✓ Plugin is discoverable by GStreamer
- ✓ All properties are defined
- ✓ Sink and source pads exist
- ✓ Element can be instantiated

### Test 2: Pipeline Construction

```bash
./test-pipeline.sh /path/to/your/model.gguf
```

This will:
- ✓ Test pipeline creation
- ✓ Verify property configuration
- ✓ Show example pipelines
- ✓ Display full element inspection

## Usage Examples

### Example 1: Simple Text Generation (File-Based)

```bash
# Set plugin path
export GST_PLUGIN_PATH=/path/to/llama.cpp/tools/gstreamer/build

# Create a prompt file
echo "Once upon a time in a land far away" > /tmp/prompt.txt

# Run pipeline
gst-launch-1.0 \
    filesrc location=/tmp/prompt.txt \
    ! llama \
        model=/path/to/model.gguf \
        temperature=0.7 \
        max-tokens=100 \
        stream-tokens=true \
    ! filesink location=/tmp/output.txt

# View output
cat /tmp/output.txt
```

### Example 2: Interactive Generation (stdin/stdout)

```bash
export GST_PLUGIN_PATH=/path/to/llama.cpp/tools/gstreamer/build

echo "Hello, my name is" | \
gst-launch-1.0 \
    fdsrc \
    ! llama \
        model=/path/to/model.gguf \
        temperature=0.8 \
        max-tokens=50 \
    ! fdsink \
    | cat
```

### Example 3: Using All Properties

```bash
gst-launch-1.0 \
    filesrc location=prompt.txt \
    ! llama \
        model=/path/to/model.gguf \
        n-ctx=2048 \
        n-gpu-layers=32 \
        n-threads=8 \
        temperature=0.7 \
        top-p=0.9 \
        top-k=40 \
        repeat-penalty=1.1 \
        max-tokens=256 \
        stream-tokens=true \
        seed=42 \
    ! filesink location=output.txt
```

## Available Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `model` | string | NULL | Path to GGUF model file |
| `n-ctx` | int | 2048 | Context size in tokens |
| `n-gpu-layers` | int | 0 | Number of layers to offload to GPU |
| `n-threads` | int | -1 | Number of threads (-1 = auto) |
| `temperature` | float | 0.7 | Sampling temperature |
| `top-p` | float | 0.9 | Nucleus sampling threshold |
| `top-k` | int | 40 | Top-K sampling parameter |
| `repeat-penalty` | float | 1.1 | Repetition penalty |
| `max-tokens` | int | 512 | Maximum tokens to generate |
| `stream-tokens` | boolean | TRUE | Stream tokens individually |
| `seed` | int | -1 | Random seed (-1 = random) |

## Installation (Optional)

To install the plugin system-wide:

```bash
cd tools/gstreamer/build
sudo meson install
```

After installation, the plugin will be available globally without setting `GST_PLUGIN_PATH`.

## Troubleshooting

### Plugin not found

```bash
# Verify plugin file exists
ls -lh tools/gstreamer/build/libgstllama.so

# Check GStreamer plugin path
echo $GST_PLUGIN_PATH

# Try explicit path
gst-inspect-1.0 --gst-plugin-path=tools/gstreamer/build llama
```

### Model loading fails

- Verify model path is correct and accessible
- Check model format is GGUF
- Ensure sufficient RAM/VRAM
- Try with CPU only first (`n-gpu-layers=0`)

### Build errors

```bash
# Clean build
rm -rf tools/gstreamer/build
./build-standalone.sh

# Check dependencies
pkg-config --modversion gstreamer-1.0
pkg-config --modversion gstreamer-base-1.0
```

## Debugging

Enable GStreamer debug output:

```bash
# Show all debug messages
export GST_DEBUG=3

# Show only llama element messages
export GST_DEBUG=llama:5

# Run with debug
gst-launch-1.0 --gst-debug=llama:5 ...
```

## Next Steps

- See `README.md` for complete documentation
- Check `examples/` directory for more complex pipelines
- Read `GSTREAMER_IMPLEMENTATION_PLAN.md` for architecture details
- Explore Phase 2 features (signals, control pads)

## Getting Help

- Check logs: `cat tools/gstreamer/build/meson-logs/meson-log.txt`
- Inspect element: `gst-inspect-1.0 llama`
- View properties: `gst-inspect-1.0 llama | grep -A2 "Properties:"`
- Report issues on GitHub with build logs
