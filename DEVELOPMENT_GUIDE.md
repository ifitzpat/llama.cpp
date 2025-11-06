# Model Management Feature - Development Guide

This guide explains the development infrastructure and workflows for the model management feature.

## Available Tools

### Compilers & Build Tools ✅
- **GCC 13.3.0** - Modern C++17 support
- **Clang 18.1.3** - LLVM toolchain with latest features
- **CMake 3.28.3** - Build system (project requires 3.14+)
- **Make 4.3** + **Ninja** - Build executors
- **Python 3.11.14** - For test framework

### Code Quality Tools ✅
- **clang-tidy 18.1.3** - Static analysis
- **clang-format 18.1.3** - Code formatting
- **valgrind 3.22.0** - Memory leak detection
- **gdb** - Debugging
- **gcov** - Code coverage

### Testing Framework ✅
- **pytest 8.3.3** - Python-based testing (already used by llama.cpp)
- Existing test infrastructure in `tools/server/tests/`
- Fixtures and utilities for server testing

## Development Workflow Scripts

### 1. Pre-commit Hook (`.git/hooks/pre-commit`)

Automatically runs before each commit:
- ✅ **Code formatting** check (clang-format)
- ✅ **Static analysis** (clang-tidy) on changed files
- ✅ **Syntax check** (quick compilation test)
- ✅ **Common issues** detection (TODOs, debug code, trailing whitespace)

**Usage:**
```bash
# Automatically runs on: git commit
# To bypass (NOT recommended): git commit --no-verify

# To auto-fix formatting issues:
clang-format -i tools/server/server.cpp
```

### 2. Build Script (`scripts/build-server.sh`)

Intelligent build with log management:
- Redirects build output to `logs/build.log`
- Extracts errors to `logs/build_errors.log`
- Extracts warnings to `logs/build_warnings.log`
- Shows summary without overwhelming output

**Usage:**
```bash
# Clean build (Release mode)
./scripts/build-server.sh clean

# Debug build
./scripts/build-server.sh debug

# Build and run tests
./scripts/build-server.sh test

# Build and run clang-tidy
./scripts/build-server.sh tidy

# Combine options
./scripts/build-server.sh clean debug test
```

**Output:**
```
=== Build Results ===
Duration: 45s
Full log: logs/build.log
✓ Build successful
Binary: build/bin/llama-server (12M)
⚠ 23 warnings (see logs/build_warnings.log)
```

### 3. TDD Workflow Script (`scripts/tdd-workflow.sh`)

Test-driven development helper:
- Run specific tests or all model management tests
- Watch mode for continuous testing during development
- Integrates with build script

**Usage:**
```bash
# Run all model management tests
./scripts/tdd-workflow.sh

# Run specific test
./scripts/tdd-workflow.sh --test test_model_status_shows_current_model

# Watch mode (re-run tests on file changes)
./scripts/tdd-workflow.sh --watch
```

**Watch Mode** (requires `inotify-tools`):
```bash
# Install inotify-tools (optional, for watch mode)
sudo apt-get install inotify-tools

# Then run watch mode
./scripts/tdd-workflow.sh --watch
```

## Test-Driven Development Approach

### Test File: `tools/server/tests/unit/test_model_management.py`

Comprehensive test suite defining expected behavior:

**Test Classes:**
- `TestModelManagementBasics` - Feature enablement, basic status
- `TestModelLoading` - Load endpoint, validation, job creation
- `TestJobTracking` - Job status, polling, progress
- `TestModelUnloading` - Unload functionality
- `TestGracefulRequestHandling` - In-flight request management
- `TestErrorRecovery` - Rollback and error states
- `TestConcurrency` - Sequential job processing
- `TestConfigurationParameters` - n_ctx, n_gpu_layers, etc.
- `TestQueueManagement` - Queue statistics and behavior
- `TestPerformance` - Load/unload cycles (marked as slow)

### TDD Workflow

**Red-Green-Refactor Cycle:**

1. **Red** - Write failing test
```bash
# Run specific test (will fail)
./scripts/tdd-workflow.sh --test test_load_model_returns_job_id
```

2. **Green** - Implement minimal code to pass
```bash
# Edit tools/server/server.cpp
# Pre-commit hook runs automatically on commit
git add tools/server/server.cpp
git commit -m "Implement job creation for model load"
```

3. **Refactor** - Clean up code
```bash
# Build and test
./scripts/build-server.sh test

# Or use watch mode for continuous feedback
./scripts/tdd-workflow.sh --watch
```

## Recommended Development Process

### Initial Setup

```bash
# 1. Install Python test dependencies
cd tools/server/tests
pip install -r requirements.txt
cd ../../..

# 2. Initial build with compile_commands.json for clang-tidy
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
         -DBUILD_SHARED_LIBS=OFF \
         -DLLAMA_CURL=OFF
cd ..

# 3. Build server
./scripts/build-server.sh
```

### Daily Development Workflow

```bash
# Option 1: Manual TDD
# Write test -> run test -> implement -> commit -> repeat
./scripts/tdd-workflow.sh --test <test_name>
# ... edit code ...
git commit  # pre-commit hook runs
./scripts/tdd-workflow.sh --test <test_name>

# Option 2: Watch Mode (recommended for rapid iteration)
./scripts/tdd-workflow.sh --watch
# Edit files, save, tests auto-run

# Option 3: Build-focused
./scripts/build-server.sh clean debug
# ... edit code ...
./scripts/build-server.sh
# Check logs/build.log for details
```

### Before Committing

```bash
# 1. Run all tests
./scripts/build-server.sh test

# 2. Run static analysis
./scripts/build-server.sh tidy

# 3. Check for memory leaks (if binary changed)
valgrind --leak-check=full --log-file=logs/valgrind.log \
    ./build/bin/llama-server --help
grep "definitely lost\|ERROR SUMMARY" logs/valgrind.log

# 4. Commit (pre-commit hook runs automatically)
git add .
git commit -m "feat: implement model loading endpoint"
```

## Quick Reference

### Running Tests

```bash
# All model management tests
cd tools/server/tests
./tests.sh unit/test_model_management.py -v

# Specific test
./tests.sh unit/test_model_management.py::test_load_model_returns_job_id -v

# With debug output
DEBUG=1 ./tests.sh unit/test_model_management.py -s -v -x

# Slow tests (performance)
SLOW_TESTS=1 ./tests.sh unit/test_model_management.py -v
```

### Code Quality Checks

```bash
# Format check
clang-format --dry-run --Werror tools/server/server.cpp

# Auto-format
clang-format -i tools/server/server.cpp

# Static analysis
clang-tidy tools/server/server.cpp -p build/

# Syntax only (fast)
g++ -std=c++17 -fsyntax-only -I./include -I./common -I./ggml/include \
    tools/server/server.cpp
```

### Build Commands

```bash
# Quick incremental build
cd build && make llama-server -j$(nproc)

# Full rebuild with logs
./scripts/build-server.sh clean

# Just check for errors
./scripts/build-server.sh 2>&1 | grep -i error

# Build with different compilers
CC=clang CXX=clang++ ./scripts/build-server.sh
```

### Debugging

```bash
# Debug with gdb
gdb --args ./build/bin/llama-server --help

# Test with external server (for debugging)
# Terminal 1:
gdb --args ./build/bin/llama-server --host 127.0.0.1 --port 8080 ...
# Terminal 2:
DEBUG_EXTERNAL=1 ./tools/server/tests/tests.sh unit/test_model_management.py::test_name -v

# Memory check
valgrind --leak-check=full --show-leak-kinds=all \
    ./build/bin/llama-server --help
```

## Log Files Location

All logs are written to `logs/` directory:

- `build.log` - Full build output
- `build_errors.log` - Compilation errors only
- `build_warnings.log` - Compilation warnings only
- `clang_tidy.log` - Static analysis results
- `cmake_config.log` - CMake configuration output
- `pytest_*.log` - Test execution logs
- `valgrind.log` - Memory check results

## Tips for Token-Efficient Development

Since we're working in a token-limited environment:

1. **Use logs instead of reading raw output**
   ```bash
   # Good: Redirect to file, grep results
   make > build.log 2>&1
   grep "error:" build.log

   # Bad: Print everything to stdout
   make
   ```

2. **Focus on errors, not warnings initially**
   ```bash
   ./scripts/build-server.sh | grep -i error
   ```

3. **Run targeted tests**
   ```bash
   # Good: Test specific functionality
   ./scripts/tdd-workflow.sh --test test_specific_function

   # Less efficient: Run all tests every time
   ./scripts/build-server.sh test
   ```

4. **Use watch mode for rapid iteration**
   ```bash
   # Set up once, edit files, automatic feedback
   ./scripts/tdd-workflow.sh --watch
   ```

5. **Incremental builds**
   ```bash
   # Only rebuild changed files
   cd build && make llama-server -j$(nproc)
   ```

## Next Steps

1. ✅ Development infrastructure set up
2. ⏭️ Start implementing from revised plan Phase 1
3. Follow TDD approach with existing tests
4. Use build script for efficient development
5. Commit frequently with pre-commit checks

## Common Issues

### Build Fails with CURL Error
**Solution:** Build script already disables CURL (`-DLLAMA_CURL=OFF`)

### clang-tidy Complains About compile_commands.json
**Solution:**
```bash
cd build
cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### Tests Can't Find Server Binary
**Solution:**
```bash
./scripts/build-server.sh  # Build first
```

### Pre-commit Hook Fails
**Solution:**
```bash
# Fix formatting
clang-format -i <file>

# Or bypass temporarily (not recommended)
git commit --no-verify
```

---

**Ready to start implementing!** 🚀

Use: `./scripts/tdd-workflow.sh --watch` and start coding.
