#!/usr/bin/env bash
# Build script for llama-server with log management and analysis
# Usage: ./scripts/build-server.sh [clean|test|debug|release]

set -e

# Configuration
BUILD_DIR="build"
LOG_DIR="logs"
BUILD_LOG="$LOG_DIR/build.log"
ERROR_LOG="$LOG_DIR/build_errors.log"
WARNING_LOG="$LOG_DIR/build_warnings.log"
TIDY_LOG="$LOG_DIR/clang_tidy.log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Parse arguments
BUILD_TYPE="Release"
CLEAN=0
RUN_TESTS=0
RUN_TIDY=0

for arg in "$@"; do
    case $arg in
        clean)
            CLEAN=1
            ;;
        test)
            RUN_TESTS=1
            ;;
        debug)
            BUILD_TYPE="Debug"
            ;;
        release)
            BUILD_TYPE="Release"
            ;;
        tidy)
            RUN_TIDY=1
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [clean|test|debug|release|tidy]"
            exit 1
            ;;
    esac
done

# Create log directory
mkdir -p "$LOG_DIR"

echo -e "${BLUE}=== llama-server Build Script ===${NC}"
echo "Build type: $BUILD_TYPE"
echo "Logs will be written to: $LOG_DIR/"
echo ""

# Clean if requested
if [ $CLEAN -eq 1 ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
    rm -f "$LOG_DIR"/*.log
    echo "Clean complete"
    echo ""
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Step 1: Configure CMake
echo -e "${BLUE}=== Step 1: Configuring CMake ===${NC}"
cd "$BUILD_DIR"

CMAKE_CONFIG_LOG="../$LOG_DIR/cmake_config.log"

if cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DLLAMA_CURL=OFF \
    -DGGML_CUDA=OFF \
    -DGGML_METAL=OFF \
    > "$CMAKE_CONFIG_LOG" 2>&1; then
    echo -e "${GREEN}✓ CMake configuration successful${NC}"
else
    echo -e "${RED}✗ CMake configuration failed${NC}"
    echo "See: $CMAKE_CONFIG_LOG"
    tail -20 "$CMAKE_CONFIG_LOG"
    exit 1
fi

cd ..
echo ""

# Step 2: Build llama-server
echo -e "${BLUE}=== Step 2: Building llama-server ===${NC}"
echo "This may take several minutes..."

START_TIME=$(date +%s)

# Build with output to log file
if cmake --build "$BUILD_DIR" --target llama-server -j$(nproc) > "$BUILD_LOG" 2>&1; then
    BUILD_SUCCESS=1
else
    BUILD_SUCCESS=0
fi

END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

# Analyze build log
grep -i "error:" "$BUILD_LOG" > "$ERROR_LOG" 2>/dev/null || true
grep -i "warning:" "$BUILD_LOG" > "$WARNING_LOG" 2>/dev/null || true

ERROR_COUNT=$(wc -l < "$ERROR_LOG" 2>/dev/null || echo 0)
WARNING_COUNT=$(wc -l < "$WARNING_LOG" 2>/dev/null || echo 0)

echo ""
echo -e "${BLUE}=== Build Results ===${NC}"
echo "Duration: ${DURATION}s"
echo "Full log: $BUILD_LOG"

if [ $BUILD_SUCCESS -eq 1 ]; then
    echo -e "${GREEN}✓ Build successful${NC}"

    # Check binary
    if [ -f "$BUILD_DIR/bin/llama-server" ]; then
        BINARY_SIZE=$(du -h "$BUILD_DIR/bin/llama-server" | cut -f1)
        echo "Binary: $BUILD_DIR/bin/llama-server ($BINARY_SIZE)"
    fi

    if [ $WARNING_COUNT -gt 0 ]; then
        echo -e "${YELLOW}⚠ $WARNING_COUNT warnings (see $WARNING_LOG)${NC}"
        echo ""
        echo "Top warnings:"
        head -10 "$WARNING_LOG"
    fi
else
    echo -e "${RED}✗ Build failed with $ERROR_COUNT errors${NC}"
    echo ""
    echo "Recent errors:"
    tail -20 "$ERROR_LOG"
    echo ""
    echo "Full error log: $ERROR_LOG"
    exit 1
fi

echo ""

# Step 3: Run clang-tidy (optional)
if [ $RUN_TIDY -eq 1 ]; then
    echo -e "${BLUE}=== Step 3: Running clang-tidy ===${NC}"

    if [ -f "$BUILD_DIR/compile_commands.json" ]; then
        echo "Analyzing tools/server/server.cpp..."

        clang-tidy tools/server/server.cpp -p "$BUILD_DIR/" > "$TIDY_LOG" 2>&1 || true

        TIDY_ERRORS=$(grep -c "error:" "$TIDY_LOG" 2>/dev/null || echo 0)
        TIDY_WARNINGS=$(grep -c "warning:" "$TIDY_LOG" 2>/dev/null || echo 0)

        echo "Results: $TIDY_ERRORS errors, $TIDY_WARNINGS warnings"

        if [ $TIDY_ERRORS -gt 0 ]; then
            echo -e "${RED}Errors found:${NC}"
            grep "error:" "$TIDY_LOG" | head -10
        fi

        if [ $TIDY_WARNINGS -gt 0 ]; then
            echo -e "${YELLOW}Warnings found:${NC}"
            grep "warning:" "$TIDY_LOG" | head -5
        fi

        echo "Full report: $TIDY_LOG"
    else
        echo -e "${YELLOW}⚠ compile_commands.json not found${NC}"
    fi
    echo ""
fi

# Step 4: Run tests (optional)
if [ $RUN_TESTS -eq 1 ]; then
    echo -e "${BLUE}=== Step 4: Running server tests ===${NC}"

    if [ -f "tools/server/tests/tests.sh" ]; then
        TEST_LOG="$LOG_DIR/pytest.log"

        echo "Running pytest (this may take a while)..."
        if cd tools/server/tests && ./tests.sh > "../../../$TEST_LOG" 2>&1; then
            echo -e "${GREEN}✓ Tests passed${NC}"
        else
            echo -e "${RED}✗ Tests failed${NC}"
            echo "See: $TEST_LOG"
            tail -30 "$TEST_LOG"
            exit 1
        fi
        cd ../../..
    else
        echo -e "${YELLOW}⚠ Test script not found${NC}"
    fi
    echo ""
fi

# Summary
echo -e "${GREEN}=== Build Complete ===${NC}"
echo ""
echo "Binary: $BUILD_DIR/bin/llama-server"
echo "Logs: $LOG_DIR/"
echo ""
echo "Next steps:"
echo "  • Run server: $BUILD_DIR/bin/llama-server --help"
echo "  • Run tests:  ./scripts/build-server.sh test"
echo "  • Run tidy:   ./scripts/build-server.sh tidy"
echo ""
