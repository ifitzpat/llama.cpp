# GStreamer llama.cpp Plugin

**Phase:** 2 - Signal System
**Status:** ✅ Implementation Complete

## Overview

`gst-llama` is a GStreamer plugin that provides text generation capabilities using llama.cpp language models. It integrates llama.cpp into GStreamer pipelines, allowing LLM text generation to be part of multimedia workflows.

## Features

- **Text input/output pads** - Accepts text prompts, outputs generated text
- **Streaming support** - Can stream individual tokens or complete responses
- **Configurable properties** - Full control over generation parameters
- **Model loading** - Loads GGUF models at READY→PAUSED transition
- **Thread-safe** - Proper locking for concurrent access
- **GObject signals** - Real-time events for tokens, generation progress, and model lifecycle

## Architecture

```
┌─────────────┐         ┌──────────────┐         ┌──────────────┐
│  Text Input │ ──sink→ │   gst-llama  │ ──src→ │  Text Output │
│   (prompt)  │         │   (element)  │         │ (generation) │
└─────────────┘         └──────────────┘         └──────────────┘
                              ↓
                        llama_simple
                        (C API wrapper)
                              ↓
                        llama.cpp core
```

## Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `model` | string | NULL | Path to GGUF model file |
| `n-ctx` | int | 2048 | Context size in tokens |
| `n-gpu-layers` | int | 0 | Number of GPU layers |
| `n-threads` | int | -1 | Number of threads (-1=auto) |
| `temperature` | float | 0.7 | Sampling temperature |
| `top-p` | float | 0.9 | Nucleus sampling |
| `top-k` | int | 40 | Top-K sampling |
| `repeat-penalty` | float | 1.1 | Repetition penalty |
| `max-tokens` | int | 512 | Maximum tokens to generate |
| `stream-tokens` | boolean | TRUE | Stream individual tokens |
| `seed` | int | -1 | Random seed (-1=random) |

## Signals

The `llama` element emits the following GObject signals for real-time monitoring and control:

### token-generated

```c
void user_function(GstElement *element,
                   gchar *token,
                   gint token_id,
                   gfloat probability,
                   gint position,
                   gpointer user_data);
```

**Emitted:** During text generation for each token
**Parameters:**
- `token` - The generated token text
- `token_id` - Token ID in model vocabulary
- `probability` - Token probability (0.0-1.0)
- `position` - Position in generated sequence

**Use case:** Real-time token monitoring, custom streaming output

### generation-started

```c
void user_function(GstElement *element,
                   gchar *prompt,
                   gpointer user_data);
```

**Emitted:** When text generation begins
**Parameters:**
- `prompt` - The input prompt text

**Use case:** Track generation start, measure latency

### generation-complete

```c
void user_function(GstElement *element,
                   gchar *full_text,
                   gint num_tokens,
                   gchar *stop_reason,
                   gpointer user_data);
```

**Emitted:** When text generation completes
**Parameters:**
- `full_text` - Complete generated text (placeholder in current implementation)
- `num_tokens` - Number of tokens generated
- `stop_reason` - Reason for stopping ("completed", "eos", "max_tokens")

**Use case:** Track generation completion, collect statistics

### model-loaded

```c
void user_function(GstElement *element,
                   gchar *model_path,
                   gpointer user_data);
```

**Emitted:** After model successfully loads (READY→PAUSED transition)
**Parameters:**
- `model_path` - Path to the loaded model file

**Use case:** Confirm model loading, trigger dependent operations

### model-unloaded

```c
void user_function(GstElement *element,
                   gpointer user_data);
```

**Emitted:** After model is unloaded (PAUSED→READY transition)

**Use case:** Cleanup, free resources, track model lifecycle

### Signal Example

See `examples/signal-example.c` for a complete example:

```c
// Connect to signals
g_signal_connect(llama, "token-generated",
                 G_CALLBACK(on_token_generated), NULL);
g_signal_connect(llama, "model-loaded",
                 G_CALLBACK(on_model_loaded), NULL);

// Callback function
static void on_token_generated(GstElement *element,
                                const gchar *token,
                                gint token_id,
                                gfloat probability,
                                gint position,
                                gpointer user_data) {
    g_print("[Token %d] '%s' (prob=%.4f)\n",
            position, token, probability);
}
```

To build and run the example:

```bash
cd tools/gstreamer/examples
make
./signal-example model.gguf input.txt output.txt
```

## Build Instructions

### Prerequisites

```bash
# Install GStreamer development libraries
sudo apt-get install \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    meson \
    ninja-build

# Build llama_simple first
cd tools/ffi
mkdir build && cd build
cmake .. -DLLAMA_SIMPLE_BUILD_TESTS=ON
make -j$(nproc)
cd ../../..
```

### Build Plugin

```bash
cd tools/gstreamer
meson setup build
meson compile -C build
```

### Install Plugin

```bash
# Install system-wide
sudo meson install -C build

# Or set GST_PLUGIN_PATH for local use
export GST_PLUGIN_PATH=$PWD/build:$GST_PLUGIN_PATH
```

### Run Tests

```bash
meson test -C build --verbose
```

## Usage Examples

### 1. Inspect Plugin

```bash
gst-inspect-1.0 llama
```

### 2. Simple Pipeline (File to File)

```bash
gst-launch-1.0 \
    filesrc location=prompt.txt ! \
    text/plain ! \
    llama model=/path/to/model.gguf temperature=0.7 max-tokens=100 ! \
    filesink location=output.txt
```

### 3. Interactive Pipeline

```bash
# Terminal input → LLM → Terminal output
gst-launch-1.0 \
    fdsrc fd=0 ! \
    text/plain ! \
    llama model=/path/to/model.gguf stream-tokens=true ! \
    fdsink fd=1
```

### 4. Property Configuration

```bash
gst-launch-1.0 \
    filesrc location=prompt.txt ! \
    llama \
        model=/models/gemma-3-270m-qat-Q4_0.gguf \
        n-ctx=4096 \
        temperature=0.9 \
        top-p=0.95 \
        top-k=50 \
        max-tokens=200 \
        stream-tokens=false ! \
    filesink location=response.txt
```

### 5. Python Example

```python
import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst

Gst.init(None)

# Create pipeline
pipeline = Gst.parse_launch("""
    filesrc location=prompt.txt !
    text/plain !
    llama model=/path/to/model.gguf temperature=0.7 !
    filesink location=output.txt
""")

# Run
pipeline.set_state(Gst.State.PLAYING)
bus = pipeline.get_bus()
msg = bus.timed_pop_filtered(
    Gst.CLOCK_TIME_NONE,
    Gst.MessageType.ERROR | Gst.MessageType.EOS
)

pipeline.set_state(Gst.State.NULL)
```

## Pad Capabilities

### Sink Pad

- **Name:** `sink`
- **Direction:** Sink (input)
- **Caps:** `text/plain, charset=utf-8`
- **Description:** Accepts text prompts for generation

### Source Pad

- **Name:** `src`
- **Direction:** Source (output)
- **Caps:** `text/plain, charset=utf-8`
- **Description:** Outputs generated text (streamed or complete)

## State Transitions

| Transition | Action |
|------------|--------|
| NULL → READY | Initialize llama.cpp backend |
| READY → PAUSED | Load model if `model` property set |
| PAUSED → PLAYING | Ready to process buffers |
| PLAYING → PAUSED | Continue accepting buffers |
| PAUSED → READY | Unload model |
| READY → NULL | Free llama.cpp context |

## Error Handling

The element can emit errors in the following cases:

- **RESOURCE/OPEN_READ:** Failed to load model
- **CORE/FAILED:** No model loaded when trying to generate
- **STREAM/FAILED:** Generation failed

Example error handling in Python:

```python
bus = pipeline.get_bus()
msg = bus.timed_pop_filtered(Gst.CLOCK_TIME_NONE, Gst.MessageType.ERROR)
if msg:
    err, debug = msg.parse_error()
    print(f"Error: {err.message}")
```

## Performance Notes

- **Model loading:** Happens at READY→PAUSED transition (can take 5-30s)
- **First token latency:** ~100-500ms depending on prompt length and model size
- **Token throughput:** ~10-100 tokens/sec (CPU), ~100-1000 tokens/sec (GPU)
- **Streaming:** Set `stream-tokens=true` for real-time output

## Debugging

Enable GStreamer debug output:

```bash
# All debug
export GST_DEBUG=llama:5

# Plugin registration only
export GST_DEBUG=llama:3

# With GStreamer core debug
export GST_DEBUG=*:3,llama:5

# Run pipeline
gst-launch-1.0 ... (your pipeline)
```

## Limitations (Phase 2)

- **Single model:** Only one model at a time
- **No control pad:** Phase 3 will add control pad for parameter adjustment
- **Basic text I/O:** Advanced features (chat templates, etc.) in later phases
- **Limited statistics:** generation-complete signal currently uses placeholders for full_text

## Files

```
tools/gstreamer/
├── gstllama.h                     # Plugin header
├── gstllama.c                     # Plugin implementation (600+ lines)
├── meson.build                    # Build configuration
├── meson_options.txt              # Build options
├── README.md                      # This file
├── QUICKSTART.md                  # Standalone build/test guide
├── build-standalone.sh            # Standalone build script
├── test-plugin.sh                 # Plugin verification tests
├── test-pipeline.sh               # Pipeline examples
├── examples/
│   ├── signal-example.c           # Signal usage example
│   └── Makefile                   # Example build configuration
└── tests/
    ├── test_plugin.c              # Plugin registration test
    └── meson.build                # Test build configuration
```

## Next Phases

**Phase 3:** Control Pad
- Dedicated control pad for steering
- Runtime logit bias adjustment
- Stop/resume control

**Phase 4:** Advanced Features
- Chat template support
- Multi-turn conversations
- Adaptive steering

## License

LGPL-2.0 (to match GStreamer licensing)

## Contributing

See main llama.cpp [CONTRIBUTING.md](../../CONTRIBUTING.md)

---

**Last Updated:** 2025-11-08
**Phase:** 2 - Signal System
**Status:** ✅ Complete
