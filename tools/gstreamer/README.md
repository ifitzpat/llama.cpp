# GStreamer llama.cpp Plugin

**Phase:** 5A - JSON Chat Messages
**Status:** ✅ Implementation Complete

## Overview

`gst-llama` is a GStreamer plugin that provides text generation capabilities using llama.cpp language models. It integrates llama.cpp into GStreamer pipelines, allowing LLM text generation to be part of multimedia workflows.

## Features

- **Text input/output pads** - Accepts text prompts, outputs generated text
- **JSON chat messages** - OpenAI-compatible chat format with automatic template formatting
- **Streaming support** - Can stream individual tokens or complete responses
- **Configurable properties** - Full control over generation parameters
- **Per-request parameters** - Override settings via JSON for each request
- **Model loading** - Loads GGUF models at READY→PAUSED transition
- **Thread-safe** - Proper locking for concurrent access
- **GObject signals** - Real-time events for tokens, generation progress, and model lifecycle
- **Control pad** - Runtime parameter adjustment via JSON control messages
- **Error handling** - Comprehensive error detection, timeout protection, and graceful recovery
- **Production-ready** - Input validation, detailed error messages, robust state management

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
| `generation-timeout` | int | 300 | Timeout for generation in seconds (0=no timeout) |

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

## Control Pad

The `llama` element provides a **request pad** named `ctrl` for runtime parameter adjustment via JSON control messages.

### Control Pad Basics

- **Name:** `ctrl`
- **Type:** Request pad (created on demand)
- **Direction:** Sink (input)
- **Caps:** `application/x-llama-control`
- **Purpose:** Send JSON control messages to adjust parameters during generation

### Creating the Control Pad

```c
// Request the control pad
GstPad *ctrl_pad = gst_element_request_pad_simple(llama, "ctrl");

// Link a source element to send control messages
gst_pad_link(source_pad, ctrl_pad);

// When done, release the pad
gst_element_release_request_pad(llama, ctrl_pad);
gst_object_unref(ctrl_pad);
```

### Control Message Format

Control messages are JSON objects sent as buffers to the control pad:

```json
{
  "command": "set_params",
  "temperature": 1.2,
  "top_p": 0.95,
  "top_k": 50,
  "max_tokens": 200,
  "repeat_penalty": 1.15,
  "seed": 42
}
```

### Available Commands

#### set_params

Update generation parameters at runtime.

**Format:**
```json
{
  "command": "set_params",
  "temperature": <float>,      // Optional: sampling temperature (0.0-2.0)
  "top_p": <float>,            // Optional: nucleus sampling (0.0-1.0)
  "top_k": <int>,              // Optional: top-K sampling
  "max_tokens": <int>,         // Optional: maximum tokens to generate
  "repeat_penalty": <float>,   // Optional: repetition penalty
  "seed": <int>                // Optional: random seed
}
```

**Example:**
```json
{"command": "set_params", "temperature": 0.9, "max_tokens": 50}
```

### Using the Control Pad

#### Method 1: appsrc Element

```c
// Create appsrc for sending control messages
GstElement *ctrlsrc = gst_element_factory_make("appsrc", "ctrlsrc");
g_object_set(ctrlsrc,
             "caps", gst_caps_from_string("application/x-llama-control"),
             NULL);

// Request control pad and link
GstPad *ctrl_pad = gst_element_request_pad_simple(llama, "ctrl");
GstPad *src_pad = gst_element_get_static_pad(ctrlsrc, "src");
gst_pad_link(src_pad, ctrl_pad);
gst_object_unref(src_pad);

// Send control message
const char *msg = "{\"command\": \"set_params\", \"temperature\": 1.2}";
GstBuffer *buffer = gst_buffer_new_allocate(NULL, strlen(msg), NULL);
gst_buffer_fill(buffer, 0, msg, strlen(msg));

GstFlowReturn ret;
g_signal_emit_by_name(ctrlsrc, "push-buffer", buffer, &ret);
gst_buffer_unref(buffer);
```

#### Method 2: Named Pipes (FIFO)

```bash
# Terminal 1: Create pipes and start pipeline
mkfifo /tmp/prompt /tmp/control /tmp/output

gst-launch-1.0 \
  filesrc location=/tmp/prompt ! llama name=gen model=model.gguf ! filesink location=/tmp/output \
  filesrc location=/tmp/control ! application/x-llama-control ! gen.ctrl

# Terminal 2: Send prompt and control messages
echo "Tell me a story" > /tmp/prompt

# Increase creativity mid-generation
echo '{"command": "set_params", "temperature": 1.5}' > /tmp/control

# Read output
cat /tmp/output
```

#### Method 3: File Source

```bash
# Create control message file
echo '{"command": "set_params", "temperature": 0.8, "max_tokens": 100}' > ctrl.json

# Pipeline with control
gst-launch-1.0 \
  filesrc location=prompt.txt ! llama name=gen model=model.gguf ! filesink location=output.txt \
  filesrc location=ctrl.json ! application/x-llama-control ! gen.ctrl
```

### Control Pad Example

See `examples/control-pad-example.c` for a complete example:

```c
// Request control pad
GstPad *ctrl_pad = gst_element_request_pad_simple(llama, "ctrl");

// Link appsrc
gst_pad_link(ctrlsrc_pad, ctrl_pad);

// Send control message
const char *msg = "{\"command\": \"set_params\", \"temperature\": 1.2}";
GstBuffer *buf = gst_buffer_new_allocate(NULL, strlen(msg), NULL);
gst_buffer_fill(buf, 0, msg, strlen(msg));
g_signal_emit_by_name(ctrlsrc, "push-buffer", buf, &ret);
gst_buffer_unref(buf);

// Release pad when done
gst_element_release_request_pad(llama, ctrl_pad);
gst_object_unref(ctrl_pad);
```

Build and run:
```bash
cd tools/gstreamer/examples
make
./control-pad-example model.gguf input.txt output.txt
```

### Use Cases

1. **Adaptive Temperature** - Adjust creativity during generation
2. **Token Limits** - Change max_tokens based on content
3. **Parameter Tuning** - Find optimal settings interactively
4. **Conditional Generation** - Different params for different prompt types
5. **Interactive Control** - User-controlled parameter adjustment

### Thread Safety

- All parameter updates are thread-safe (protected by mutex)
- Parameters take effect on the **next** generation
- Current generation continues with existing parameters
- No interruption of ongoing generation

## Build Instructions

### Prerequisites

```bash
# Install GStreamer development libraries
sudo apt-get install \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libjson-glib-dev \
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

### 6. JSON Chat Messages (OpenAI-Compatible)

The llama element supports JSON chat message format for multi-turn conversations. This uses the model's built-in chat template (Jinja) and allows per-request parameter overrides.

**JSON Format:**

```json
{
  "messages": [
    {"role": "system", "content": "You are a helpful assistant."},
    {"role": "user", "content": "What is the capital of France?"}
  ],
  "temperature": 0.7,
  "max_tokens": 100,
  "top_p": 0.95,
  "top_k": 40,
  "repeat_penalty": 1.1
}
```

**Fields:**

- `messages` (required): Array of message objects with `role` and `content`
  - `role`: "system", "user", or "assistant"
  - `content`: Message text
- `temperature` (optional): Override element's temperature property
- `max_tokens` (optional): Override element's max-tokens property
- `top_p` (optional): Override element's top-p property
- `top_k` (optional): Override element's top-k property
- `repeat_penalty` (optional): Override element's repeat-penalty property

**Pipeline Example:**

```bash
# Create JSON file with chat messages
cat > chat.json << 'EOF'
{
  "messages": [
    {"role": "system", "content": "You are a helpful assistant."},
    {"role": "user", "content": "Explain quantum computing in simple terms."}
  ],
  "temperature": 0.8,
  "max_tokens": 150
}
EOF

# Send JSON to llama element
gst-launch-1.0 \
    filesrc location=chat.json ! \
    application/json ! \
    llama model=/path/to/model.gguf ! \
    filesink location=response.txt
```

**Python Example:**

```python
import gi
import json
gi.require_version('Gst', '1.0')
from gi.repository import Gst

Gst.init(None)

# Create JSON chat request
chat_request = {
    "messages": [
        {"role": "system", "content": "You are a helpful coding assistant."},
        {"role": "user", "content": "Write a Python function to reverse a string."}
    ],
    "temperature": 0.7,
    "max_tokens": 200
}

json_data = json.dumps(chat_request)

# Create pipeline
pipeline = Gst.Pipeline.new("json-chat")
appsrc = Gst.ElementFactory.make("appsrc", "source")
llama = Gst.ElementFactory.make("llama", "generator")
filesink = Gst.ElementFactory.make("filesink", "sink")

# Configure elements
caps = Gst.Caps.from_string("application/json")
appsrc.set_property("caps", caps)
llama.set_property("model", "/path/to/model.gguf")
filesink.set_property("location", "response.txt")

# Build pipeline
pipeline.add(appsrc)
pipeline.add(llama)
pipeline.add(filesink)
appsrc.link(llama)
llama.link(filesink)

# Push JSON data
pipeline.set_state(Gst.State.PLAYING)
buffer = Gst.Buffer.new_allocate(None, len(json_data), None)
buffer.fill(0, json_data.encode())
appsrc.emit("push-buffer", buffer)
appsrc.emit("end-of-stream")

# Wait for completion
bus = pipeline.get_bus()
msg = bus.timed_pop_filtered(
    Gst.CLOCK_TIME_NONE,
    Gst.MessageType.ERROR | Gst.MessageType.EOS
)

pipeline.set_state(Gst.State.NULL)
```

**How It Works:**

1. Element detects JSON input (content starts with `{`)
2. Parses JSON and extracts `messages` array
3. Builds `llama_simple_chat_msg` structures
4. Calls `llama_simple_format_chat()` to apply model's chat template
5. Extracts per-request parameters (if provided)
6. Uses formatted prompt for generation

**Benefits:**

- **Chat template support**: Automatically formats messages using the model's Jinja template
- **Stateless**: Upstream component manages conversation history
- **OpenAI-compatible**: Same JSON format as OpenAI Chat Completions API
- **Flexible parameters**: Override generation settings per request
- **Multi-turn conversations**: Natural handling of system/user/assistant messages

**See also:** `examples/json-chat-example.c` for a complete C example

## Pad Capabilities

### Sink Pad

- **Name:** `sink`
- **Direction:** Sink (input)
- **Caps:** `text/plain, charset=utf-8` or `application/json`
- **Description:** Accepts plain text prompts or JSON chat messages for generation

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

The plugin includes comprehensive error handling and recovery mechanisms.

### Error Types

The element can emit errors in the following cases:

**Model Loading Errors:**
- **RESOURCE/NOT_FOUND:** Model file doesn't exist at specified path
- **RESOURCE/OPEN_READ:** Model path is not a regular file (e.g., directory)
- **RESOURCE/FAILED:** Failed to initialize llama context (out of memory)
- **RESOURCE/READ:** Failed to load GGUF model (corrupted or incompatible file)

**Generation Errors:**
- **CORE/FAILED:** No model loaded when trying to generate
- **STREAM/FAILED:** Generation failed or timeout exceeded

### Timeout Protection

Generation can be time-limited using the `generation-timeout` property:

```bash
# Set 60 second timeout
gst-launch-1.0 \
    filesrc location=prompt.txt ! \
    llama model=model.gguf generation-timeout=60 ! \
    filesink location=output.txt
```

**Timeout Behavior:**
- Default: 300 seconds (5 minutes)
- Set to 0 to disable timeout
- When exceeded, generation is aborted and `generation-complete` signal emits with `stop_reason="timeout"`
- GST_ELEMENT_ERROR is posted to the bus

### Error Recovery

**Automatic Recovery:**
- Buffer allocation failures abort generation gracefully
- Timeout aborts don't crash the pipeline
- State transitions cleanup ongoing generation
- Failed model loads don't leave element in invalid state

**Manual Recovery:**
- Pipeline can be restarted after errors
- Model can be changed via property and state transition
- Control pad can adjust parameters after errors

### Error Handling Example

**Python:**
```python
bus = pipeline.get_bus()
msg = bus.timed_pop_filtered(Gst.CLOCK_TIME_NONE, Gst.MessageType.ERROR)
if msg:
    err, debug = msg.parse_error()
    print(f"Error: {err.message}")
    print(f"Debug: {debug}")

    # Attempt recovery
    pipeline.set_state(Gst.State.NULL)
    pipeline.set_state(Gst.State.PLAYING)
```

**C:**
```c
GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                             GST_MESSAGE_ERROR);
if (msg) {
    GError *err;
    gchar *debug_info;
    gst_message_parse_error(msg, &err, &debug_info);

    g_printerr("Error from %s: %s\n",
               GST_OBJECT_NAME(msg->src), err->message);
    g_printerr("Debug info: %s\n", debug_info ? debug_info : "none");

    g_error_free(err);
    g_free(debug_info);
    gst_message_unref(msg);
}
```

### Validation

**Control Message Validation:**
- Temperature: 0.0-2.0
- Top-P: 0.0-1.0
- Top-K: >= 0
- Max tokens: > 0
- Repeat penalty: >= 0.0

Invalid values are rejected with GST_WARNING and not applied.

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

## Limitations

- **Single model:** Only one model at a time per element instance
- **Basic text I/O:** Advanced features (chat templates, multi-turn conversations) not yet implemented
- **Limited statistics:** generation-complete signal uses placeholders for full_text tracking
- **Control commands:** Currently only `set_params` supported (logit bias and steering require C API extensions)

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
│   ├── control-pad-example.c      # Control pad example
│   └── Makefile                   # Example build configuration
└── tests/
    ├── test_plugin.c              # Plugin registration test
    └── meson.build                # Test build configuration
```

## Next Phases

**Phase 4:** Advanced Features (Future)
- Chat template support
- Multi-turn conversations
- Logit bias support (requires C API extension)
- Adaptive steering

## License

LGPL-2.0 (to match GStreamer licensing)

## Contributing

See main llama.cpp [CONTRIBUTING.md](../../CONTRIBUTING.md)

---

**Last Updated:** 2025-11-08
**Phase:** 4 - Error Handling & Production Features
**Status:** ✅ Complete

**Implemented Phases:**
- ✅ Phase 1: Basic GStreamer Element (text I/O, properties, state management)
- ✅ Phase 2: Signal System (5 signals for real-time events)
- ✅ Phase 3: Control Pad (runtime parameter adjustment)
- ✅ Phase 4: Error Handling & Recovery (timeout, validation, error messages)
