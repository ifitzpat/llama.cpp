# CI/CD for llama_simple C API

This document describes the automated build and test workflows for the `llama_simple` C API wrapper.

## Overview

We have two complementary CI/CD workflows:

1. **Standard CMake Build** (`.github/workflows/build-ffi.yml`) - Fast, standard Ubuntu-based builds
2. **Guix Reproducible Build** (`.github/workflows/guix-build.yml`) - Reproducible builds using GNU Guix

## Workflow 1: Standard CMake Build (Recommended for CI)

**File:** `.github/workflows/build-ffi.yml`

### Triggers

- Push to `claude/gstreamer-implementation-*` or `main` branches
- Pull requests modifying FFI code
- Manual workflow dispatch

### Jobs

#### 1. `build-and-test` (Matrix Build)

Tests across multiple configurations:

| OS | Compiler | Build Type |
|----|----------|------------|
| Ubuntu 22.04 | GCC | Release |
| Ubuntu 22.04 | Clang | Release |
| Ubuntu 24.04 | GCC | Release, Debug |
| Ubuntu 24.04 | Clang | Release |

**Steps:**
1. Install dependencies (CMake, compilers, OpenMP, libcurl)
2. Build llama.cpp core libraries
3. Build llama_simple static library
4. Run all 19 unit tests
5. Check code formatting (clang-format)
6. Run static analysis (clang-tidy)
7. Upload artifacts (Release builds only)

**Duration:** ~5-10 minutes per configuration

#### 2. `build-with-model` (Integration Test)

Full integration test with real model:

**Steps:**
1. Cache/download gemma-3-270m-qat-Q4_0.gguf (231 MB)
2. Build llama.cpp + llama_simple
3. Run all tests including positive path tests
4. Verify model tests executed (not skipped)
5. Upload test logs

**Duration:** ~15-20 minutes (first run with download), ~5 minutes (cached)

**Model Cache:** Reuses cached model across runs (key: `gemma-3-270m-qat-Q4_0`)

#### 3. `quality-checks` (Code Quality)

Static analysis and style checks:

- **clang-format:** Enforces code formatting
- **cppcheck:** Static analysis for bugs and style issues
- **TODO/FIXME scan:** Lists unresolved code comments (warning only)

**Duration:** ~2 minutes

#### 4. `documentation` (Documentation Validation)

Checks documentation completeness:

- Verifies `TDD_DEVELOPMENT_LOG.md` exists
- Checks API header exists
- Generates API reference from headers
- Uploads extracted API signatures

**Duration:** ~1 minute

### Artifacts

Generated artifacts (retained for specified days):

| Artifact | Contents | Retention |
|----------|----------|-----------|
| `libllama_simple-gcc` | Static library (GCC) | 7 days |
| `libllama_simple-clang` | Static library (Clang) | 7 days |
| `test-report-*` | Test summaries per configuration | 30 days |
| `integration-test-output` | Full test output with model | 30 days |
| `api-reference` | Extracted API signatures | 30 days |

### Status Badges

Add to your README:

```markdown
![FFI Build](https://github.com/USERNAME/llama.cpp/actions/workflows/build-ffi.yml/badge.svg)
```

## Workflow 2: Guix Reproducible Build

**File:** `.github/workflows/guix-build.yml`

### Why Guix?

Provides **bit-for-bit reproducible builds** across different machines and times:

- Declarative package definitions (`guix.scm`)
- Hermetic build environments (no hidden dependencies)
- Cache sharing via Guix store
- Long-term build reproducibility

### Jobs

#### 1. `build-base` - Core llama.cpp

Builds `llama-cpp-base` package from `guix.scm`.

#### 2. `build-simple` - C API Wrapper

Builds `llama-simple` package, runs tests in Guix shell.

#### 3. `build-gstreamer` - GStreamer Plugin

Builds `gst-llama` plugin (Phase 1+).

#### 4. `build-all` - Complete Suite

Builds all packages together, generates dependency graph.

#### 5. `integration-test` - End-to-End Tests

Tests complete integration (C API + GStreamer pipelines).

### Guix Advantages

✅ **Reproducible:** Same inputs → same outputs, always
✅ **Isolated:** No dependency conflicts
✅ **Documented:** Build recipe is the documentation
✅ **Time-travel:** Can rebuild old commits exactly

### Guix Disadvantages

❌ **Slower:** First build downloads many dependencies
❌ **Complex:** Requires Guix knowledge to modify
❌ **Storage:** Large `/gnu/store` cache

**Recommendation:** Use standard CMake workflow for quick feedback, Guix for release builds.

## Local Development

### Quick Build (CMake)

```bash
# Build core llama.cpp
mkdir build && cd build
cmake .. -DGGML_OPENMP=ON
make -j$(nproc)
cd ..

# Build llama_simple
cd tools/ffi
mkdir build && cd build
cmake .. -DLLAMA_SIMPLE_BUILD_TESTS=ON
make -j$(nproc)

# Run tests
./test_llama_simple
```

### Reproducible Build (Guix)

```bash
# Install Guix (one-time)
# See: https://guix.gnu.org/manual/en/html_node/Binary-Installation.html

# Build llama_simple
guix build -f guix.scm llama-simple

# Enter development shell
guix shell -D -f guix.scm

# Build and test manually
cd tools/ffi
mkdir build && cd build
cmake .. -DLLAMA_SIMPLE_BUILD_TESTS=ON
make -j$(nproc)
./test_llama_simple
```

## Pre-commit Checks

Install pre-commit hook (automatic on `git commit`):

```bash
# Already installed in .git/hooks/pre-commit
# Runs automatically:
# - clang-format (code formatting)
# - clang-tidy (static analysis)
# - Syntax validation
# - TODO/FIXME scan
```

**Tip:** Run manually before pushing:

```bash
.git/hooks/pre-commit
```

## Troubleshooting

### Test Failure: "Model not found"

**Symptom:** Tests skip positive path tests
**Cause:** Model path incorrect
**Fix:** Update `TEST_MODEL_PATH` in `tests/test_llama_simple.c` to absolute path

### Build Failure: "undefined reference to `ggml_*`"

**Symptom:** Linker errors for ggml symbols
**Cause:** Missing ggml libraries or wrong link order
**Fix:** Ensure CMakeLists.txt has:

```cmake
target_link_libraries(llama_simple
    common
    llama
    -Wl,--start-group
    ggml-base
    ggml-cpu
    ggml
    -Wl,--end-group
    gomp
)
```

### Build Failure: "cannot find -lgomp"

**Symptom:** OpenMP library missing
**Cause:** libgomp not installed
**Fix:**

```bash
# Ubuntu/Debian
sudo apt-get install libgomp1

# Fedora/RHEL
sudo dnf install libgomp
```

### Guix Build: "Guix daemon not running"

**Symptom:** `guix build` fails
**Cause:** Guix daemon not started
**Fix:**

```bash
sudo systemctl start guix-daemon
# Or for non-systemd:
sudo guix-daemon --build-users-group=guixbuild &
```

### Formatting Errors: "code not formatted"

**Symptom:** Pre-commit hook fails on formatting
**Cause:** Code not formatted with clang-format
**Fix:**

```bash
cd tools/ffi
clang-format -i llama_simple.cpp llama_simple.h tests/test_llama_simple.c
```

## Monitoring Builds

### GitHub Actions UI

1. Go to repository → **Actions** tab
2. Select workflow (e.g., "Build FFI (llama_simple)")
3. View recent runs and their status

### Command Line (GitHub CLI)

```bash
# Install GitHub CLI
gh auth login

# List recent runs
gh run list --workflow=build-ffi.yml

# Watch a specific run
gh run watch <run-id>

# Download artifacts
gh run download <run-id>
```

### Custom Monitoring Script

Use the monitoring scripts from earlier work:

```bash
# Monitor latest build
./scripts/ci/monitor-guix-build.sh

# Trigger manual build
./scripts/ci/trigger-guix-build.sh
```

## Performance Benchmarks

Typical CI/CD times (GitHub Actions, ubuntu-24.04):

| Stage | Duration | Cached |
|-------|----------|--------|
| Checkout + Dependencies | 30s | 20s |
| Build llama.cpp core | 3-5 min | 3-5 min |
| Build llama_simple | 30s | 20s |
| Run unit tests (no model) | 5s | 5s |
| Download model (231 MB) | 2-3 min | 0s |
| Run tests with model | 30s | 30s |
| Static analysis | 1-2 min | 1-2 min |
| **Total (first run)** | **10-15 min** | - |
| **Total (cached model)** | **8-12 min** | - |

Guix builds (first run): **30-60 minutes** (many dependencies)
Guix builds (cached): **5-10 minutes**

## Best Practices

### For Contributors

1. **Run tests locally** before pushing
2. **Check formatting** with pre-commit hook
3. **Monitor CI** after pushing - fix failures quickly
4. **Test with model** at least once locally
5. **Update tests** when changing API

### For Maintainers

1. **Review CI logs** on PRs
2. **Require passing tests** before merging
3. **Keep model cache** to speed up integration tests
4. **Monitor artifact storage** (auto-cleanup after 7-30 days)
5. **Update workflows** when dependencies change

## Future Enhancements

Potential improvements:

- [ ] Add code coverage reporting (gcov/lcov)
- [ ] Add performance benchmarks (generation tokens/sec)
- [ ] Add memory leak detection (valgrind)
- [ ] Add cross-compilation (ARM, RISC-V)
- [ ] Add container builds (Docker)
- [ ] Add release automation (GitHub Releases)

---

**Last Updated:** 2025-11-08
**Workflows:** `build-ffi.yml`, `guix-build.yml`
**Status:** ✅ Phase 0 complete, CI/CD operational
