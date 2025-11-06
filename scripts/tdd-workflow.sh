#!/usr/bin/env bash
# Test-Driven Development workflow helper
# Run this script during development to get rapid feedback

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
TEST_FILE="tools/server/tests/unit/test_model_management.py"
LOG_DIR="logs"

mkdir -p "$LOG_DIR"

echo -e "${BLUE}=== TDD Workflow for Model Management ===${NC}"
echo ""

# Parse arguments
RUN_TEST=""
WATCH_MODE=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --watch)
            WATCH_MODE=1
            shift
            ;;
        --test)
            RUN_TEST="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--watch] [--test <test_name>]"
            exit 1
            ;;
    esac
done

run_tests() {
    echo -e "${BLUE}=== Running Tests ===${NC}"

    # Check if build exists
    if [ ! -f "build/bin/llama-server" ]; then
        echo -e "${YELLOW}⚠ Server binary not found, building...${NC}"
        ./scripts/build-server.sh
    fi

    # Run tests
    cd tools/server/tests

    if [ -n "$RUN_TEST" ]; then
        # Run specific test
        echo "Running specific test: $RUN_TEST"
        TEST_LOG="../../../$LOG_DIR/pytest_$(date +%s).log"

        if ./tests.sh "unit/test_model_management.py::$RUN_TEST" -v > "$TEST_LOG" 2>&1; then
            echo -e "${GREEN}✓ Test passed${NC}"
            grep -E "PASSED|FAILED|ERROR" "$TEST_LOG" | tail -10
        else
            echo -e "${RED}✗ Test failed${NC}"
            echo ""
            echo "Recent output:"
            tail -30 "$TEST_LOG"
            echo ""
            echo "Full log: $TEST_LOG"
            cd ../../..
            return 1
        fi
    else
        # Run all model management tests
        echo "Running all model management tests..."
        TEST_LOG="../../../$LOG_DIR/pytest_$(date +%s).log"

        if ./tests.sh unit/test_model_management.py -v > "$TEST_LOG" 2>&1; then
            echo -e "${GREEN}✓ All tests passed${NC}"
            grep -E "passed|failed|error" "$TEST_LOG" | tail -5
        else
            echo -e "${RED}✗ Some tests failed${NC}"
            echo ""
            echo "Failed tests:"
            grep "FAILED" "$TEST_LOG" || echo "See log for details"
            echo ""
            echo "Full log: $TEST_LOG"
            cd ../../..
            return 1
        fi
    fi

    cd ../../..
    return 0
}

watch_mode() {
    echo -e "${YELLOW}=== Watch Mode ===${NC}"
    echo "Watching for changes in tools/server/*.cpp and *.h files..."
    echo "Press Ctrl+C to stop"
    echo ""

    # Initial run
    if run_tests; then
        LAST_STATUS="passed"
    else
        LAST_STATUS="failed"
    fi

    # Watch for changes
    while true; do
        # Wait for file changes
        inotifywait -q -r -e modify,create tools/server/*.cpp tools/server/*.h 2>/dev/null || {
            echo -e "${YELLOW}⚠ inotifywait not installed, using polling${NC}"
            sleep 5
        }

        echo ""
        echo -e "${BLUE}=== Change detected, re-running tests ===${NC}"

        # Rebuild
        echo "Rebuilding..."
        if ./scripts/build-server.sh > "$LOG_DIR/rebuild.log" 2>&1; then
            echo -e "${GREEN}✓ Build successful${NC}"

            # Run tests
            if run_tests; then
                if [ "$LAST_STATUS" = "failed" ]; then
                    echo -e "${GREEN}🎉 Tests now passing!${NC}"
                fi
                LAST_STATUS="passed"
            else
                LAST_STATUS="failed"
            fi
        else
            echo -e "${RED}✗ Build failed${NC}"
            tail -20 "$LOG_DIR/rebuild.log"
            LAST_STATUS="failed"
        fi

        echo ""
        echo "Waiting for next change..."
    done
}

# Main execution
if [ $WATCH_MODE -eq 1 ]; then
    watch_mode
else
    run_tests
fi
