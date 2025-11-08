# Spinning Off GStreamer-LLaMA as a Standalone Project

This guide explains how to create a standalone `gstreamer-llama` project with llama.cpp as a dependency.

## Overview

The GStreamer plugin is currently embedded in the llama.cpp repository at `tools/gstreamer/`. To spin it off, we'll create an independent repository that depends on llama.cpp's C API (`llama_simple`).

## Repository Structure

```
gstreamer-llama/
├── .github/
│   └── workflows/
│       ├── build.yml                  # Main build CI
│       └── test.yml                   # Testing CI
├── src/
│   ├── gstllama.h                     # Plugin header
│   ├── gstllama.c                     # Plugin implementation
│   └── meson.build                    # Source build config
├── tests/
│   ├── test_plugin.c                  # Plugin registration tests
│   ├── meson.build                    # Test build config
│   └── test_requirements.txt          # Python test dependencies (if needed)
├── examples/
│   ├── signal-example.c               # Signal usage example
│   ├── control-pad-example.c          # Control pad example
│   └── Makefile                       # Example build system
├── scripts/
│   ├── build-standalone.sh            # Standalone build script
│   ├── test-plugin.sh                 # Plugin verification
│   └── test-pipeline.sh               # Pipeline examples
├── docs/
│   ├── README.md                      # Main documentation (moved from root)
│   ├── QUICKSTART.md                  # Quick start guide
│   └── API.md                         # API reference (extract from README)
├── subprojects/                       # Meson subprojects (for llama.cpp)
├── .gitignore
├── .clang-format                      # Code formatting
├── LICENSE                            # LGPL-2.0 (GStreamer compatible)
├── README.md                          # Project overview
├── meson.build                        # Root build config
├── meson_options.txt                  # Build options
└── CONTRIBUTING.md                    # Contribution guidelines
```

## Step-by-Step Instructions

### Step 1: Create New Repository

```bash
# Create new directory
mkdir gstreamer-llama
cd gstreamer-llama
git init

# Create basic structure
mkdir -p src tests examples scripts docs subprojects .github/workflows
```

### Step 2: Copy Files from llama.cpp

From `llama.cpp/tools/gstreamer/`:

```bash
# Navigate to llama.cpp repo
cd /path/to/llama.cpp

# Copy source files
cp tools/gstreamer/gstllama.h gstreamer-llama/src/
cp tools/gstreamer/gstllama.c gstreamer-llama/src/
cp tools/gstreamer/meson.build gstreamer-llama/src/

# Copy test files
cp tools/gstreamer/tests/test_plugin.c gstreamer-llama/tests/
cp tools/gstreamer/tests/meson.build gstreamer-llama/tests/

# Copy examples
cp tools/gstreamer/examples/signal-example.c gstreamer-llama/examples/
cp tools/gstreamer/examples/control-pad-example.c gstreamer-llama/examples/
cp tools/gstreamer/examples/Makefile gstreamer-llama/examples/

# Copy scripts
cp tools/gstreamer/build-standalone.sh gstreamer-llama/scripts/
cp tools/gstreamer/test-plugin.sh gstreamer-llama/scripts/
cp tools/gstreamer/test-pipeline.sh gstreamer-llama/scripts/

# Copy documentation
cp tools/gstreamer/README.md gstreamer-llama/docs/
cp tools/gstreamer/QUICKSTART.md gstreamer-llama/docs/

# Copy formatting configuration
cp .clang-format gstreamer-llama/
```

### Step 3: Create Root meson.build

Create `gstreamer-llama/meson.build`:

```meson
project('gstreamer-llama', 'c',
  version : '0.1.0',
  license : 'LGPL-2.0',
  meson_version : '>= 0.59',
  default_options : [
    'warning_level=2',
    'buildtype=debugoptimized',
    'c_std=c11'
  ])

# Project information
project_name = meson.project_name()
project_version = meson.project_version()

# Install directories
datadir = get_option('datadir')
plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

# Compiler
cc = meson.get_compiler('c')

# Dependencies
gst_req = '>= 1.20.0'
gst_dep = dependency('gstreamer-1.0', version : gst_req, required : true)
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req, required : true)
json_glib_dep = dependency('json-glib-1.0', version : '>= 1.0', required : true)

# llama.cpp dependency - try pkg-config first, then subproject
llama_simple_dep = dependency('llama-simple', required : false)
if not llama_simple_dep.found()
  # Try to find as library
  llama_simple_lib = cc.find_library('llama_simple', required : false)

  if not llama_simple_lib.found()
    # Fall back to subproject
    message('llama_simple not found, using subproject')
    llama_cpp_proj = subproject('llama.cpp')
    llama_simple_dep = llama_cpp_proj.get_variable('llama_simple_dep')
  else
    llama_simple_dep = declare_dependency(
      dependencies : llama_simple_lib,
      include_directories : include_directories(get_option('llama_include_dir'))
    )
  endif
endif

# Build subdirectories
subdir('src')

if get_option('tests').enabled()
  subdir('tests')
endif

if get_option('examples').enabled()
  subdir('examples')
endif

# Summary
summary({
  'prefix': get_option('prefix'),
  'libdir': get_option('libdir'),
  'plugins install dir': plugins_install_dir,
  'Build tests': get_option('tests').enabled(),
  'Build examples': get_option('examples').enabled(),
}, section: 'Directories')

summary({
  'GStreamer': gst_dep.version(),
  'json-glib': json_glib_dep.version(),
  'llama_simple': 'found',
}, section: 'Dependencies')
```

### Step 4: Create meson_options.txt

Create `gstreamer-llama/meson_options.txt`:

```meson
option('tests', type : 'feature', value : 'auto',
  description : 'Build tests')

option('examples', type : 'feature', value : 'auto',
  description : 'Build examples')

option('llama_include_dir', type : 'string', value : '/usr/include',
  description : 'Directory containing llama_simple.h (if using system library)')

option('llama_lib_dir', type : 'string', value : '/usr/lib',
  description : 'Directory containing libllama_simple (if using system library)')
```

### Step 5: Update src/meson.build

Update `gstreamer-llama/src/meson.build`:

```meson
# Plugin source files
plugin_sources = [
  'gstllama.c',
]

plugin_c_args = ['-DHAVE_CONFIG_H']

# Configuration data
cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())
cdata.set_quoted('PACKAGE', meson.project_name())
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', '1.0')
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer llama.cpp Plugin')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://github.com/youruser/gstreamer-llama')
configure_file(output : 'config.h', configuration : cdata)

# Build plugin
gstllama = library('gstllama',
  plugin_sources,
  c_args: plugin_c_args,
  include_directories : [include_directories('.')],
  dependencies : [gst_dep, gstbase_dep, json_glib_dep, llama_simple_dep],
  install : true,
  install_dir : plugins_install_dir,
)
```

### Step 6: Set Up llama.cpp as Dependency

**Option A: Git Submodule (Recommended)**

```bash
cd gstreamer-llama

# Add llama.cpp as submodule
git submodule add https://github.com/ggerganov/llama.cpp.git subprojects/llama.cpp
git submodule update --init --recursive

# Create wrap file for meson
cat > subprojects/llama.cpp.wrap << 'EOF'
[wrap-git]
url = https://github.com/ggerganov/llama.cpp.git
revision = head
depth = 1

[provide]
llama_simple_dep = llama_simple_dep
EOF
```

Create `subprojects/llama.cpp/meson.build` (minimal wrapper):

```meson
project('llama.cpp', 'cpp', 'c')

# Build llama_simple library
llama_simple_src = [
  'tools/ffi/llama_simple.cpp',
]

llama_core_src = [
  'src/llama.cpp',
  'src/llama-vocab.cpp',
  'src/llama-grammar.cpp',
  'src/llama-sampling.cpp',
  'ggml/src/ggml.c',
  'ggml/src/ggml-alloc.c',
  'ggml/src/ggml-backend.c',
  'ggml/src/ggml-quants.c',
  # Add other necessary sources
]

llama_simple_lib = library('llama_simple',
  llama_simple_src + llama_core_src,
  include_directories : [
    include_directories('include'),
    include_directories('ggml/include'),
    include_directories('tools/ffi'),
  ],
  install : false,
)

llama_simple_dep = declare_dependency(
  link_with : llama_simple_lib,
  include_directories : include_directories('tools/ffi'),
)
```

**Option B: System Library**

Users would install llama.cpp separately:

```bash
# Users build and install llama.cpp
cd /path/to/llama.cpp
cd tools/ffi
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make
sudo make install

# Then build gstreamer-llama
cd /path/to/gstreamer-llama
meson setup build -Dllama_include_dir=/usr/local/include
ninja -C build
```

### Step 7: Create GitHub Actions Workflows

Create `.github/workflows/build.yml`:

```yaml
name: Build and Test

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build:
    strategy:
      matrix:
        os: [ubuntu-22.04, ubuntu-24.04]
        compiler: [gcc, clang]
        buildtype: [debug, release]
        exclude:
          - os: ubuntu-22.04
            buildtype: debug

    runs-on: ${{ matrix.os }}

    steps:
    - name: Checkout
      uses: actions/checkout@v4
      with:
        submodules: recursive

    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y \
          libgstreamer1.0-dev \
          libgstreamer-plugins-base1.0-dev \
          libjson-glib-dev \
          meson \
          ninja-build \
          clang-format \
          cmake

    - name: Setup compiler
      run: |
        if [ "${{ matrix.compiler }}" = "clang" ]; then
          echo "CC=clang" >> $GITHUB_ENV
          echo "CXX=clang++" >> $GITHUB_ENV
        fi

    - name: Build llama_simple
      run: |
        cd subprojects/llama.cpp/tools/ffi
        mkdir build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=${{ matrix.buildtype }}
        make -j$(nproc)
        sudo make install

    - name: Configure
      run: |
        meson setup build \
          --buildtype=${{ matrix.buildtype }} \
          -Dtests=enabled \
          -Dexamples=enabled

    - name: Build
      run: ninja -C build

    - name: Test
      run: meson test -C build --verbose

    - name: Check formatting
      if: matrix.compiler == 'gcc' && matrix.buildtype == 'release'
      run: |
        clang-format --dry-run --Werror \
          src/gstllama.c \
          src/gstllama.h \
          tests/test_plugin.c

    - name: Upload build artifacts
      if: failure()
      uses: actions/upload-artifact@v4
      with:
        name: build-logs-${{ matrix.os }}-${{ matrix.compiler }}-${{ matrix.buildtype }}
        path: |
          build/meson-logs/
          build/testlog.txt
```

Create `.github/workflows/test.yml`:

```yaml
name: Plugin Integration Tests

on:
  push:
    branches: [ main ]
  pull_request:
    branches: [ main ]

jobs:
  integration-test:
    runs-on: ubuntu-24.04

    steps:
    - name: Checkout
      uses: actions/checkout@v4
      with:
        submodules: recursive

    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y \
          libgstreamer1.0-dev \
          libgstreamer-plugins-base1.0-dev \
          libjson-glib-dev \
          gstreamer1.0-tools \
          meson \
          ninja-build \
          cmake \
          wget

    - name: Download test model
      run: |
        mkdir -p models/test
        cd models/test
        wget -q https://huggingface.co/ggml-org/models/resolve/main/tinyllamas/stories260K.gguf \
          -O test-model.gguf

    - name: Build llama_simple
      run: |
        cd subprojects/llama.cpp/tools/ffi
        mkdir build && cd build
        cmake ..
        make -j$(nproc)
        sudo make install
        sudo ldconfig

    - name: Build plugin
      run: |
        meson setup build
        ninja -C build

    - name: Install plugin locally
      run: |
        export GST_PLUGIN_PATH=$PWD/build/src:$GST_PLUGIN_PATH
        echo "GST_PLUGIN_PATH=$GST_PLUGIN_PATH" >> $GITHUB_ENV

    - name: Test plugin discovery
      run: gst-inspect-1.0 llama

    - name: Run test scripts
      run: |
        cd scripts
        chmod +x test-plugin.sh
        ./test-plugin.sh

    - name: Test example pipeline
      run: |
        echo "Once upon a time" > /tmp/prompt.txt

        timeout 60 gst-launch-1.0 \
          filesrc location=/tmp/prompt.txt ! \
          llama model=$PWD/models/test/test-model.gguf max-tokens=20 ! \
          filesink location=/tmp/output.txt

        cat /tmp/output.txt
        test -s /tmp/output.txt
```

### Step 8: Create README.md

Create `gstreamer-llama/README.md`:

```markdown
# GStreamer LLaMA Plugin

A GStreamer plugin for text generation using llama.cpp language models.

## Features

- Native GStreamer integration for LLM text generation
- Real-time token streaming
- Signal-based event system
- Runtime parameter adjustment via control pad
- Comprehensive error handling
- Production-ready robustness

## Installation

### Dependencies

- GStreamer >= 1.20.0
- json-glib >= 1.0
- llama.cpp (built with llama_simple)
- meson >= 0.59
- ninja

### Build from Source

```bash
# Clone with submodules
git clone --recursive https://github.com/youruser/gstreamer-llama.git
cd gstreamer-llama

# Build
meson setup build
ninja -C build

# Install
sudo ninja -C build install
```

## Quick Start

See [docs/QUICKSTART.md](docs/QUICKSTART.md) for detailed usage instructions.

```bash
# Simple pipeline
gst-launch-1.0 \
  filesrc location=prompt.txt ! \
  llama model=model.gguf temperature=0.7 ! \
  filesink location=output.txt
```

## Documentation

- [Full Documentation](docs/README.md)
- [Quick Start Guide](docs/QUICKSTART.md)
- [API Reference](docs/API.md)
- [Examples](examples/)

## License

LGPL-2.0 (compatible with GStreamer)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md)

## Credits

Built on [llama.cpp](https://github.com/ggerganov/llama.cpp) by Georgi Gerganov and contributors.
```

### Step 9: Create .gitignore

Create `gstreamer-llama/.gitignore`:

```gitignore
# Build directories
build/
builddir/
*.o
*.so
*.a

# Meson
meson-private/
meson-logs/
compile_commands.json

# Test artifacts
*.log
testlog.txt
/tmp/

# IDE
.vscode/
.idea/
*.swp
*.swo
*~

# Examples built files
examples/signal-example
examples/control-pad-example

# System
.DS_Store
```

### Step 10: Create LICENSE

Create `gstreamer-llama/LICENSE`:

```
GNU LESSER GENERAL PUBLIC LICENSE
Version 2.0, February 1991

[Full LGPL-2.0 text]
```

### Step 11: Update Source Files and Paths

Several paths need to be updated when spinning off. Here's a comprehensive list:

#### src/gstllama.h

```c
// BEFORE (in llama.cpp repo):
#include "../ffi/llama_simple.h"

// AFTER (in standalone repo):
#include <llama_simple.h>
```

#### src/gstllama.c

No changes needed - already uses relative includes correctly:
```c
#ifdef HAVE_CONFIG_H
#    include "config.h"
#endif

#include "gstllama.h"
```

#### src/meson.build

```meson
# BEFORE (in llama.cpp repo):
include_directories : [include_directories('.'), include_directories('../ffi')],

# AFTER (in standalone repo):
include_directories : [include_directories('.')],
# llama_simple include is handled by the dependency
```

Also update the package origin:
```meson
# BEFORE:
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://github.com/ggml-org/llama.cpp')

# AFTER:
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://github.com/youruser/gstreamer-llama')
```

#### scripts/build-standalone.sh

```bash
# BEFORE (relative paths from llama.cpp/tools/gstreamer/):
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GSTREAMER_DIR="$SCRIPT_DIR"
LLAMA_FFI_DIR="$SCRIPT_DIR/../ffi"

# AFTER (paths in standalone repo):
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GSTREAMER_DIR="$SCRIPT_DIR/.."
LLAMA_CPP_DIR="$GSTREAMER_DIR/subprojects/llama.cpp"
LLAMA_FFI_DIR="$LLAMA_CPP_DIR/tools/ffi"

# Update build llama_simple section:
if [ ! -f "$LLAMA_FFI_DIR/build/libllama_simple.so" ]; then
    echo "Building llama_simple..."
    cd "$LLAMA_FFI_DIR"
    mkdir -p build && cd build
    cmake .. -DLLAMA_SIMPLE_BUILD_TESTS=OFF
    make -j$(nproc)
else
    echo "✓ llama_simple already built"
fi
```

#### scripts/test-plugin.sh

```bash
# BEFORE:
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$SCRIPT_DIR/build/src"

# AFTER (same structure, just document it):
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GSTREAMER_DIR="$SCRIPT_DIR/.."
PLUGIN_DIR="$GSTREAMER_DIR/build/src"
```

#### scripts/test-pipeline.sh

```bash
# BEFORE:
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# AFTER (add plugin path setup):
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GSTREAMER_DIR="$SCRIPT_DIR/.."
PLUGIN_DIR="$GSTREAMER_DIR/build/src"

export GST_PLUGIN_PATH="$PLUGIN_DIR:$GST_PLUGIN_PATH"
```

#### examples/Makefile

No path changes needed - already uses pkg-config correctly.

#### docs/README.md

Update all repository references:

```markdown
# BEFORE:
git clone https://github.com/ggerganov/llama.cpp.git
cd llama.cpp/tools/gstreamer

# AFTER:
git clone --recursive https://github.com/youruser/gstreamer-llama.git
cd gstreamer-llama
```

Update build instructions:
```markdown
# BEFORE:
cd tools/gstreamer
./build-standalone.sh

# AFTER:
cd gstreamer-llama
meson setup build
ninja -C build
```

Update all inline examples that reference file paths:
```markdown
# BEFORE:
See `tools/gstreamer/examples/signal-example.c`

# AFTER:
See `examples/signal-example.c`
```

#### docs/QUICKSTART.md

Similar updates to README.md:

```markdown
# BEFORE:
cd llama.cpp/tools/gstreamer
make -C examples

# AFTER:
cd gstreamer-llama
ninja -C build
cd examples && make
```

Update all paths from `tools/gstreamer/` to root-relative paths.

#### GitHub Actions Workflows

Update model download paths:

```yaml
# BEFORE (if any hardcoded paths):
working-directory: tools/gstreamer

# AFTER:
working-directory: .
```

Update build commands:
```yaml
# BEFORE:
- name: Build
  run: |
    cd tools/gstreamer
    ./build-standalone.sh

# AFTER:
- name: Build
  run: |
    meson setup build
    ninja -C build
```

## Complete Path Migration Checklist

- [ ] `src/gstllama.h` - Change `#include "../ffi/llama_simple.h"` to `#include <llama_simple.h>`
- [ ] `src/meson.build` - Remove `../ffi` from include_directories
- [ ] `src/meson.build` - Update GST_PACKAGE_ORIGIN URL
- [ ] `scripts/build-standalone.sh` - Update LLAMA_FFI_DIR path to use subprojects/
- [ ] `scripts/test-plugin.sh` - Update PLUGIN_DIR relative path
- [ ] `scripts/test-pipeline.sh` - Add GST_PLUGIN_PATH export
- [ ] `docs/README.md` - Replace all `llama.cpp/tools/gstreamer/` with repo root
- [ ] `docs/README.md` - Update git clone commands
- [ ] `docs/README.md` - Update build commands
- [ ] `docs/QUICKSTART.md` - Update all relative paths
- [ ] `.github/workflows/*.yml` - Update working-directory paths
- [ ] `.github/workflows/*.yml` - Update build commands
- [ ] README.md (root) - Set correct repository URLs
- [ ] CONTRIBUTING.md - Update clone and build commands

## Automated Path Update Script

Create `tools/update-paths.sh` to automate path updates:

```bash
#!/bin/bash
# Run this after copying files to update all paths

set -e

echo "Updating paths for standalone repository..."

# Update gstllama.h
sed -i 's|#include "../ffi/llama_simple.h"|#include <llama_simple.h>|g' src/gstllama.h

# Update src/meson.build
sed -i "s|include_directories('\.\./ffi')||g" src/meson.build
sed -i "s|'https://github.com/ggml-org/llama.cpp'|'https://github.com/youruser/gstreamer-llama'|g" src/meson.build

# Update documentation
find docs -type f -name "*.md" -exec sed -i 's|llama\.cpp/tools/gstreamer/||g' {} \;
find docs -type f -name "*.md" -exec sed -i 's|cd tools/gstreamer|cd gstreamer-llama|g' {} \;

# Update scripts
sed -i 's|LLAMA_FFI_DIR="$SCRIPT_DIR/../ffi"|LLAMA_CPP_DIR="$SCRIPT_DIR/../subprojects/llama.cpp"\nLLAMA_FFI_DIR="$LLAMA_CPP_DIR/tools/ffi"|g' scripts/build-standalone.sh

echo "✓ Path updates complete"
echo "Manual review recommended for:"
echo "  - GitHub Actions workflows"
echo "  - README.md repository URLs"
echo "  - Documentation examples"
```

Run after copying files:
```bash
chmod +x tools/update-paths.sh
./tools/update-paths.sh
```

### Step 12: Create CONTRIBUTING.md

Create `gstreamer-llama/CONTRIBUTING.md`:

```markdown
# Contributing to GStreamer-LLaMA

## Development Setup

```bash
git clone --recursive https://github.com/youruser/gstreamer-llama.git
cd gstreamer-llama
meson setup build -Dtests=enabled -Dexamples=enabled
ninja -C build
```

## Code Style

- Follow GStreamer coding conventions
- Use `clang-format` for C code formatting
- Run `clang-format -i src/*.c src/*.h` before committing

## Testing

```bash
# Run unit tests
meson test -C build --verbose

# Run integration tests
cd scripts
./test-plugin.sh
./test-pipeline.sh
```

## Pull Request Process

1. Create a feature branch
2. Make your changes
3. Add tests for new features
4. Ensure all tests pass
5. Update documentation
6. Submit PR with clear description

## Commit Messages

Use conventional commit format:

```
feat: add new feature
fix: fix bug
docs: update documentation
test: add tests
chore: maintenance task
```
```

### Step 13: Initialize Git and Push

```bash
cd gstreamer-llama

# Initialize git
git init
git add .
git commit -m "Initial commit: GStreamer LLaMA plugin v0.1.0

Complete plugin with:
- Phase 1: Basic GStreamer element
- Phase 2: Signal system
- Phase 3: Control pad
- Phase 4: Error handling

Spun off from llama.cpp repository."

# Add remote and push
git remote add origin https://github.com/youruser/gstreamer-llama.git
git branch -M main
git push -u origin main

# Initialize submodule
git submodule add https://github.com/ggerganov/llama.cpp.git subprojects/llama.cpp
git commit -m "Add llama.cpp as submodule"
git push
```

## Testing the Spin-Off

### Local Testing

```bash
# Build
cd gstreamer-llama
meson setup build -Dtests=enabled
ninja -C build

# Run tests
meson test -C build --verbose

# Test plugin discovery
export GST_PLUGIN_PATH=$PWD/build/src:$GST_PLUGIN_PATH
gst-inspect-1.0 llama

# Test pipeline
echo "Hello" > /tmp/test.txt
gst-launch-1.0 \
  filesrc location=/tmp/test.txt ! \
  llama model=/path/to/model.gguf ! \
  filesink location=/tmp/out.txt
```

### CI Testing

Push to GitHub and verify:
- Build workflow runs successfully
- Tests pass on all matrix configurations
- Integration tests work with real models

## Package Distribution

### Debian/Ubuntu Package

Create `debian/` directory structure:

```bash
mkdir -p debian
cd debian

# Create control file
cat > control << EOF
Source: gstreamer1.0-llama
Section: libs
Priority: optional
Maintainer: Your Name <your.email@example.com>
Build-Depends: debhelper-compat (= 13),
               meson,
               libgstreamer1.0-dev,
               libgstreamer-plugins-base1.0-dev,
               libjson-glib-dev,
               libllama-simple-dev
Standards-Version: 4.6.0

Package: gstreamer1.0-llama
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}
Description: GStreamer plugin for LLM text generation
 This plugin integrates llama.cpp into GStreamer pipelines
 for text generation using language models.
EOF

# Build package
dpkg-buildpackage -us -uc
```

### Arch Linux PKGBUILD

Create `PKGBUILD`:

```bash
pkgname=gstreamer-llama
pkgver=0.1.0
pkgrel=1
pkgdesc="GStreamer plugin for LLM text generation"
arch=('x86_64')
url="https://github.com/youruser/gstreamer-llama"
license=('LGPL2')
depends=('gstreamer' 'json-glib' 'llama.cpp')
makedepends=('meson' 'ninja')
source=("$pkgname-$pkgver.tar.gz")

build() {
  cd "$pkgname-$pkgver"
  meson setup build --prefix=/usr
  ninja -C build
}

package() {
  cd "$pkgname-$pkgver"
  DESTDIR="$pkgdir" ninja -C build install
}
```

## Maintenance

### Updating llama.cpp Dependency

```bash
# Update submodule
cd subprojects/llama.cpp
git pull origin master
cd ../..
git add subprojects/llama.cpp
git commit -m "Update llama.cpp to latest version"
git push
```

### Release Process

1. Update version in `meson.build`
2. Update CHANGELOG.md
3. Create git tag: `git tag -a v0.1.0 -m "Release v0.1.0"`
4. Push tag: `git push origin v0.1.0`
5. Create GitHub release with artifacts

## Migration Notes

### For Users of llama.cpp's Embedded Plugin

If you were using the plugin from `llama.cpp/tools/gstreamer/`:

1. Install standalone plugin
2. Update build scripts to use system installation
3. No API changes - drop-in replacement

### Keeping Original in llama.cpp

You can maintain both:
- Keep simplified version in llama.cpp for integration testing
- Develop advanced features in standalone repo
- Periodic sync of core functionality

## Support

- Issues: https://github.com/youruser/gstreamer-llama/issues
- Discussions: https://github.com/youruser/gstreamer-llama/discussions
- Matrix: #gstreamer-llama:matrix.org (optional)

## Roadmap

Future enhancements for standalone project:
- [ ] Python bindings via GObject introspection
- [ ] Additional model format support
- [ ] Advanced sampling strategies
- [ ] Batch processing element
- [ ] GUI configuration tool
- [ ] Additional example pipelines
- [ ] Performance benchmarks
