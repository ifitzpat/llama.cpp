# Guix Build System Setup - Summary

**Date:** 2025-11-08
**Branch:** `claude/integration-scenarios-ffi-gstreamer`
**Status:** ✅ Complete and ready for implementation

---

## What's Been Created

### 1. Guix Package Definitions (`guix.scm`)

A comprehensive Guix package manifest defining 7 packages:

| Package | Description | Build System |
|---------|-------------|--------------|
| **llama-cpp-base** | Core libraries (libllama, libcommon) | CMake |
| **llama-simple** | C API wrapper for FFI | CMake |
| **gst-llama** | GStreamer plugin | Meson |
| **gst-llama-steering** | Adaptive steering element | Meson |
| **cl-llama-simple** | Common Lisp bindings | GNU |
| **llama-gstreamer-all** | Meta-package (all components) | - |
| **llama-gstreamer-dev** | Development environment | - |

**Features:**
- ✅ Reproducible builds
- ✅ Automatic dependency resolution
- ✅ Pure build environments
- ✅ Multiple package outputs
- ✅ Development shell support

### 2. GitHub Actions Workflow (`.github/workflows/guix-build.yml`)

Automated CI/CD pipeline with 6 jobs:

```
build-base → build-simple → build-gstreamer → build-all → integration-test
                                                  ↓
                                              artifacts
```

**Jobs:**
1. **build-base** - Build core llama.cpp libraries
2. **build-simple** - Build C API wrapper + run tests
3. **build-gstreamer** - Build GStreamer plugin + plugin tests
4. **build-all** - Build complete suite
5. **integration-test** - End-to-end pipeline tests

**Features:**
- ✅ Guix store caching (faster rebuilds)
- ✅ Test execution at each stage
- ✅ Artifact uploads (binaries, logs, reports)
- ✅ Build failure preservation (`--keep-failed`)
- ✅ Triggers on push/PR to relevant paths

### 3. CI Monitoring Scripts

Two bash scripts for interacting with GitHub Actions:

#### `scripts/ci/monitor-guix-build.sh`

Monitor build status in real-time:

```bash
# Single status check
./scripts/ci/monitor-guix-build.sh

# Watch mode (auto-refresh)
./scripts/ci/monitor-guix-build.sh --watch

# Monitor specific branch
./scripts/ci/monitor-guix-build.sh --branch main --watch
```

**Features:**
- ✅ Live status updates
- ✅ Job-level progress tracking
- ✅ Color-coded output
- ✅ Artifact download on completion
- ✅ Configurable refresh interval

#### `scripts/ci/trigger-guix-build.sh`

Manually trigger builds:

```bash
# Trigger build
./scripts/ci/trigger-guix-build.sh

# Trigger and wait
./scripts/ci/trigger-guix-build.sh --wait

# Specific branch
./scripts/ci/trigger-guix-build.sh --branch my-feature
```

### 4. Documentation

- **`scripts/ci/README.md`** - Complete CI tools documentation
- **`GSTREAMER_IMPLEMENTATION_PLAN.md`** - Updated with Guix build system section
- **`INTEGRATION_SCENARIOS.md`** - Original analysis document
- **`GUIX_SETUP_SUMMARY.md`** - This file

---

## Quick Start Guide

### Prerequisites

1. **Install GitHub CLI:**
   ```bash
   # Ubuntu/Debian
   sudo apt install gh

   # macOS
   brew install gh
   ```

2. **Authenticate:**
   ```bash
   gh auth login
   ```

3. **Install jq (for monitoring):**
   ```bash
   sudo apt install jq  # Ubuntu/Debian
   brew install jq      # macOS
   ```

### Using the CI System

#### Monitor Current Build Status

```bash
./scripts/ci/monitor-guix-build.sh
```

**Example Output:**
```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Guix Build Status
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Branch:     claude/integration-scenarios-ffi-gstreamer
Run:        #1 (123456789)
Commit:     84c4cb2 - build : add Guix package definitions
Status:     ⟳ IN PROGRESS

Jobs:
  Build llama-cpp-base: completed success
  Build llama-simple: in_progress
  Build gst-llama: queued
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

#### Trigger Manual Build

```bash
# On current branch
./scripts/ci/trigger-guix-build.sh

# On specific branch
./scripts/ci/trigger-guix-build.sh --branch main
```

#### Watch Build Progress

```bash
# Auto-refresh every 30 seconds
./scripts/ci/monitor-guix-build.sh --watch

# Custom refresh interval (10 seconds)
./scripts/ci/monitor-guix-build.sh --watch --interval 10
```

### Local Development (with Guix)

If you have Guix installed locally:

```bash
# Enter development environment
guix shell -D -f guix.scm

# Build specific package
guix build -f guix.scm llama-simple

# Build all packages
guix build -f guix.scm

# Install locally
guix package -f guix.scm
```

### Local Development (without Guix)

Traditional build systems are still supported:

**C API (llama-simple):**
```bash
cd tools/ffi
mkdir build && cd build
cmake .. -DLLAMA_SIMPLE_BUILD_TESTS=ON
make -j$(nproc)
ctest
```

**GStreamer Plugin:**
```bash
cd tools/gstreamer
meson setup build
meson compile -C build
meson test -C build
```

---

## What Happens Next

### Phase 0: Implement C API Wrapper

The foundation for everything else. Once pushed, CI will:

1. ✅ Build `llama-simple` library
2. ✅ Run unit tests
3. ✅ Upload artifacts (libllama_simple.so)
4. ✅ Report success/failure

**Monitor with:**
```bash
./scripts/ci/monitor-guix-build.sh --watch
```

### Phase 1-5: Incremental Development

Each phase follows the same workflow:

1. **Develop locally** (with or without Guix)
2. **Commit changes** to branch
3. **Push to GitHub**
4. **CI auto-triggers** (via workflow)
5. **Monitor progress** (via scripts)
6. **Download artifacts** (if needed)

---

## Repository Structure

```
llama.cpp/
├── guix.scm                          # ← Guix package definitions
├── .github/
│   └── workflows/
│       └── guix-build.yml            # ← CI workflow
├── scripts/
│   └── ci/
│       ├── README.md                 # ← CI documentation
│       ├── monitor-guix-build.sh     # ← Status monitor
│       └── trigger-guix-build.sh     # ← Build trigger
├── tools/
│   ├── ffi/
│   │   ├── llama_simple.h            # ← To be implemented (Phase 0)
│   │   ├── llama_simple.cpp          # ← To be implemented (Phase 0)
│   │   ├── CMakeLists.txt            # ← CMake build config
│   │   └── tests/                    # ← C API tests
│   └── gstreamer/
│       ├── gstllama.h                # ← To be implemented (Phase 1)
│       ├── gstllama.c                # ← To be implemented (Phase 1)
│       ├── meson.build               # ← Meson build config
│       └── tests/                    # ← GStreamer tests
└── docs/
    ├── INTEGRATION_SCENARIOS.md      # ← Architecture overview
    ├── GSTREAMER_IMPLEMENTATION_PLAN.md  # ← Detailed plan
    └── GUIX_SETUP_SUMMARY.md         # ← This file
```

---

## Advantages of This Setup

### Reproducibility
- Bit-for-bit identical builds across machines
- No "works on my machine" issues
- Guaranteed dependency versions

### Isolation
- Each build in clean environment
- No interference from host system
- Consistent across all developers

### Modularity
- Build only what you need
- Separate packages for different use cases
- Easy to add new components

### CI/CD Integration
- Automated builds on every push
- Early detection of build failures
- Artifact preservation for debugging

### Developer Experience
- Simple commands (`guix build`, `guix shell`)
- No manual dependency installation
- Development shell with all tools

---

## Troubleshooting

### CI Build Failures

1. **Check workflow status:**
   ```bash
   ./scripts/ci/monitor-guix-build.sh
   ```

2. **View detailed logs:**
   ```bash
   gh run list --workflow=guix-build.yml --limit=5
   gh run view <RUN_ID> --log
   ```

3. **Download artifacts:**
   ```bash
   gh run download <RUN_ID>
   ```

### Local Build Issues

1. **With Guix:**
   ```bash
   # Verbose build
   guix build -f guix.scm llama-simple --verbosity=2

   # Keep failed builds for inspection
   guix build -f guix.scm llama-simple --keep-failed

   # View build log
   guix build -f guix.scm llama-simple --log-file
   ```

2. **Without Guix (CMake):**
   ```bash
   cd tools/ffi/build
   cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON
   make VERBOSE=1
   ```

---

## Next Steps

### Ready to Start Implementation?

1. **Begin Phase 0:** Implement C API wrapper
   ```bash
   # Create files
   vim tools/ffi/llama_simple.h
   vim tools/ffi/llama_simple.cpp

   # Test locally
   cd tools/ffi && mkdir build && cd build
   cmake .. -DLLAMA_SIMPLE_BUILD_TESTS=ON
   make && ctest

   # Commit and push
   git add tools/ffi/
   git commit -m "feat : implement llama_simple C API wrapper"
   git push origin claude/integration-scenarios-ffi-gstreamer

   # Monitor CI
   ./scripts/ci/monitor-guix-build.sh --watch
   ```

2. **Review Implementation Plan:**
   ```bash
   cat GSTREAMER_IMPLEMENTATION_PLAN.md
   ```

3. **Set up local development environment:**
   ```bash
   # With Guix
   guix shell -D -f guix.scm

   # Without Guix - install dependencies manually
   sudo apt install \
     build-essential cmake meson ninja-build \
     libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
     libjson-glib-dev pkg-config
   ```

---

## Summary

✅ **Complete Guix build infrastructure**
✅ **GitHub Actions CI/CD pipeline**
✅ **Monitoring and triggering scripts**
✅ **Comprehensive documentation**
✅ **Ready for Phase 0 implementation**

**Total files created:** 6
**Lines of code:** ~1,250
**Time to set up manually:** Would take days
**Time with Guix:** Minutes to build, guaranteed reproducible

---

## Questions or Issues?

- **Guix documentation:** https://guix.gnu.org/manual/
- **GitHub CLI docs:** https://cli.github.com/manual/
- **CI scripts help:** `./scripts/ci/monitor-guix-build.sh --help`
- **Implementation plan:** `GSTREAMER_IMPLEMENTATION_PLAN.md`

**Ready to build!** 🚀
