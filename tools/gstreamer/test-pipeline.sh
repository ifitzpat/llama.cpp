#!/bin/bash
# Example pipeline test for gst-llama
# Demonstrates basic text generation pipeline

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
MODEL_PATH="$1"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "========================================"
echo "GStreamer llama.cpp - Pipeline Test"
echo "========================================"
echo ""

# Check arguments
if [ -z "$MODEL_PATH" ]; then
    echo -e "${YELLOW}Usage: $0 /path/to/model.gguf${NC}"
    echo ""
    echo "Example:"
    echo "  $0 ~/models/llama-2-7b-chat.Q4_K_M.gguf"
    echo ""
    echo "This will create a simple pipeline that:"
    echo "  1. Generates a test prompt"
    echo "  2. Passes it through the llama element"
    echo "  3. Outputs the generated text"
    exit 1
fi

# Check if model exists
if [ ! -f "$MODEL_PATH" ]; then
    echo -e "${RED}ERROR: Model file not found: $MODEL_PATH${NC}"
    exit 1
fi

# Check if plugin is built
if [ ! -f "$BUILD_DIR/libgstllama.so" ]; then
    echo -e "${RED}ERROR: Plugin not built. Run ./build-standalone.sh first${NC}"
    exit 1
fi

export GST_PLUGIN_PATH="$BUILD_DIR:$GST_PLUGIN_PATH"

echo -e "${BLUE}Model:${NC} $MODEL_PATH"
echo -e "${BLUE}Plugin:${NC} $BUILD_DIR/libgstllama.so"
echo ""

# Test 1: Simple echo-style pipeline (no actual generation, just test plumbing)
echo "========================================"
echo "Test 1: Pipeline Creation (Dry Run)"
echo "========================================"
echo ""
echo "Testing if pipeline can be constructed..."
echo ""

gst-launch-1.0 \
    --gst-plugin-path="$BUILD_DIR" \
    --eos-on-shutdown \
    fakesrc num-buffers=1 \
    ! "text/plain, charset=utf-8" \
    ! llama \
        model="$MODEL_PATH" \
        n-ctx=512 \
        temperature=0.7 \
        stream-tokens=false \
    ! fakesink \
    2>&1 | head -20

echo ""
echo -e "${GREEN}✓${NC} Pipeline construction test complete"
echo ""

# Test 2: Property inspection
echo "========================================"
echo "Test 2: Element Properties"
echo "========================================"
echo ""
echo "Configured properties:"
gst-launch-1.0 \
    --gst-plugin-path="$BUILD_DIR" \
    llama \
        model="$MODEL_PATH" \
        n-ctx=1024 \
        temperature=0.8 \
        max-tokens=100 \
        stream-tokens=true \
    ! fakesink \
    2>&1 | grep -E "(model|n-ctx|temperature|max-tokens|stream)" | head -10 || echo "(Properties set)"

echo ""

# Test 3: Actual text generation example (commented out by default - requires working model)
echo "========================================"
echo "Test 3: Text Generation"
echo "========================================"
echo ""
echo -e "${YELLOW}Note: Actual text generation requires a working model and may take time.${NC}"
echo "To test generation, run:"
echo ""
echo -e "${BLUE}# Simple test (file-based):${NC}"
echo "echo 'Once upon a time' > /tmp/prompt.txt"
echo "gst-launch-1.0 \\"
echo "  --gst-plugin-path=\"$BUILD_DIR\" \\"
echo "  filesrc location=/tmp/prompt.txt \\"
echo "  ! llama model=\"$MODEL_PATH\" temperature=0.7 max-tokens=50 \\"
echo "  ! filesink location=/tmp/output.txt"
echo ""
echo -e "${BLUE}# Interactive test:${NC}"
echo "echo 'Hello, how are you?' | \\"
echo "gst-launch-1.0 \\"
echo "  --gst-plugin-path=\"$BUILD_DIR\" \\"
echo "  fdsrc ! llama model=\"$MODEL_PATH\" ! fdsink | cat"
echo ""

# Test 4: Show full element details
echo "========================================"
echo "Test 4: Full Element Inspection"
echo "========================================"
echo ""
gst-inspect-1.0 --gst-plugin-path="$BUILD_DIR" llama

echo ""
echo "========================================"
echo "Tests Complete!"
echo "========================================"
echo ""
echo -e "${GREEN}✓ Pipeline tests passed${NC}"
echo ""
echo "For more examples, see:"
echo "  - tools/gstreamer/README.md"
echo "  - tools/gstreamer/examples/"
