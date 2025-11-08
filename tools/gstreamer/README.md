# GStreamer llama.cpp Plugin

**Phase:** 5B - Dynamic Model Loading
**Status:** ✅ Implementation Complete

## Overview

`gst-llama` is a GStreamer plugin that provides text generation capabilities using llama.cpp language models. It integrates llama.cpp into GStreamer pipelines, allowing LLM text generation to be part of multimedia workflows.

## Features

- **Text input/output pads** - Accepts text prompts, outputs generated text
- **JSON chat messages** - OpenAI-compatible chat format with automatic template formatting
- **Dynamic model loading** - Load/unload models at runtime via control pad
- **Asynchronous operations** - Non-blocking model loading with progress signals
- **Buffer queuing** - Optional buffering during model transitions
- **Streaming support** - Can stream individual tokens or complete responses
- **Configurable properties** - Full control over generation parameters
- **Per-request parameters** - Override settings via JSON for each request
- **State management** - Robust model state tracking (UNLOADED/LOADING/READY/ERROR)
- **Thread-safe** - Proper locking for concurrent access
- **GObject signals** - Real-time events for tokens, generation, and model lifecycle
- **Control pad** - Runtime parameter adjustment and model management via JSON
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
| `enable-buffer-queue` | boolean | FALSE | Queue buffers during model transitions |
| `max-queued-buffers` | int | 10 | Max buffers to queue (0=unlimited) |

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

**Emitted:** After model is unloaded (via control pad or PAUSED→READY transition)

**Use case:** Cleanup, free resources, track model lifecycle

### model-loading

```c
void user_function(GstElement *element,
                   gchar *model_path,
                   gpointer user_data);
```

**Emitted:** When asynchronous model loading starts (via control pad `load_model` command)
**Parameters:**
- `model_path` - Path to the model being loaded

**Use case:** Show loading UI, display progress indicator, log model changes

### model-load-progress

```c
void user_function(GstElement *element,
                   gfloat progress,
                   gchar *message,
                   gpointer user_data);
```

**Emitted:** During asynchronous model loading to report progress
**Parameters:**
- `progress` - Loading progress from 0.0 to 1.0
- `message` - Human-readable progress message (e.g., "Initializing model")

**Use case:** Update progress bars, display status messages, estimate completion time

### model-load-failed

```c
void user_function(GstElement *element,
                   gchar *error_message,
                   gpointer user_data);
```

**Emitted:** When asynchronous model loading fails
**Parameters:**
- `error_message` - Detailed error description

**Use case:** Display error to user, log failures, trigger fallback behavior

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

#### load_model

Load a model asynchronously without restarting the pipeline. The element will emit signals during loading.

**Format:**
```json
{
  "command": "load_model",
  "model_path": <string>,      // Required: path to GGUF model file
  "n_ctx": <int>,              // Optional: context size (default: 2048)
  "n_gpu_layers": <int>        // Optional: GPU layers (default: 0)
}
```

**Examples:**
```json
// Load with defaults
{"command": "load_model", "model_path": "/models/llama-7b.gguf"}

// Load with custom parameters
{"command": "load_model", "model_path": "/models/llama-13b.gguf", "n_ctx": 4096, "n_gpu_layers": 32}
```

**Behavior:**
- Returns immediately (non-blocking)
- Emits `model-loading` signal when starting
- Emits `model-load-progress` signals during loading
- Emits `model-loaded` signal on success
- Emits `model-load-failed` signal on failure
- Model state transitions: `UNLOADED` → `LOADING` → `READY` (or `ERROR`)
- If buffer queuing is enabled, queued buffers will be processed after loading

**States:**
- Can be called when model state is `UNLOADED`, `READY`, or `ERROR`
- Cannot be called when state is `LOADING` or `UNLOADING`

#### unload_model

Unload the current model to free resources.

**Format:**
```json
{
  "command": "unload_model"
}
```

**Example:**
```json
{"command": "unload_model"}
```

**Behavior:**
- Executes synchronously (fast operation)
- Emits `model-unloaded` signal when complete
- Model state transitions: `READY` → `UNLOADING` → `UNLOADED`
- Frees model resources immediately
- Pipeline can continue running; use `load_model` to load a new model

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

### 7. Dynamic Model Loading

The llama element supports loading and unloading models at runtime via the control pad, without restarting the pipeline. This enables model switching, resource management, and multi-model workflows.

**Use Cases:**
- Switch between different models based on task requirements
- Free GPU memory when model is not actively needed
- Handle model loading errors gracefully without pipeline restart
- Implement model warmup strategies
- Build multi-model pipelines with resource sharing

#### Basic Example (Python)

```python
import gi
import json
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GLib

Gst.init(None)

# Create pipeline with unloaded model
pipeline = Gst.Pipeline.new("dynamic-loading")
llama = Gst.ElementFactory.make("llama", "generator")
llama.set_property("enable-buffer-queue", True)  # Queue buffers during loading
filesink = Gst.ElementFactory.make("filesink", "sink")
filesink.set_property("location", "output.txt")

# Create control source
ctrlsrc = Gst.ElementFactory.make("appsrc", "ctrlsrc")
caps = Gst.Caps.from_string("application/x-llama-control")
ctrlsrc.set_property("caps", caps)

# Build pipeline
pipeline.add(llama)
pipeline.add(filesink)
pipeline.add(ctrlsrc)
llama.link(filesink)

# Link control pad
ctrl_pad = llama.request_pad_simple("ctrl")
src_pad = ctrlsrc.get_static_pad("src")
src_pad.link(ctrl_pad)

# Connect to signals
def on_model_loading(element, model_path):
    print(f"Loading model: {model_path}")

def on_model_load_progress(element, progress, message):
    print(f"Progress: {progress*100:.1f}% - {message}")

def on_model_loaded(element, model_path):
    print(f"Model loaded: {model_path}")

def on_model_load_failed(element, error_msg):
    print(f"Load failed: {error_msg}")

llama.connect("model-loading", on_model_loading)
llama.connect("model-load-progress", on_model_load_progress)
llama.connect("model-loaded", on_model_loaded)
llama.connect("model-load-failed", on_model_load_failed)

# Start pipeline
pipeline.set_state(Gst.State.PLAYING)

# Send load_model command
load_cmd = {
    "command": "load_model",
    "model_path": "/models/llama-7b.gguf",
    "n_ctx": 2048,
    "n_gpu_layers": 0
}

cmd_json = json.dumps(load_cmd).encode()
buffer = Gst.Buffer.new_allocate(None, len(cmd_json), None)
buffer.fill(0, cmd_json)
ctrlsrc.emit("push-buffer", buffer)

# Wait for loading to complete
# In real application, use signals to track completion
import time
time.sleep(5)

print("Model ready!")

# Later: unload model to free resources
unload_cmd = {"command": "unload_model"}
cmd_json = json.dumps(unload_cmd).encode()
buffer = Gst.Buffer.new_allocate(None, len(cmd_json), None)
buffer.fill(0, cmd_json)
ctrlsrc.emit("push-buffer", buffer)

# Cleanup
pipeline.set_state(Gst.State.NULL)
```

#### Model Switching Example

```python
# Switch between different models without restarting pipeline

def switch_model(llama_element, ctrlsrc, model_path):
    """Switch to a different model"""
    # Unload current model
    unload_cmd = {"command": "unload_model"}
    send_control_message(ctrlsrc, unload_cmd)
    time.sleep(1)  # Wait for unload

    # Load new model
    load_cmd = {
        "command": "load_model",
        "model_path": model_path,
        "n_ctx": 2048
    }
    send_control_message(ctrlsrc, load_cmd)

def send_control_message(ctrlsrc, cmd_dict):
    """Helper to send control messages"""
    cmd_json = json.dumps(cmd_dict).encode()
    buffer = Gst.Buffer.new_allocate(None, len(cmd_json), None)
    buffer.fill(0, cmd_json)
    ctrlsrc.emit("push-buffer", buffer)

# Example usage
switch_model(llama, ctrlsrc, "/models/llama-13b.gguf")
# Wait for loading signals...
switch_model(llama, ctrlsrc, "/models/codellama-7b.gguf")
```

#### C Example

```c
#include <gst/gst.h>
#include <json-glib/json-glib.h>

// Signal callbacks
static void on_model_loading(GstElement *element, gchar *model_path, gpointer user_data) {
    g_print("Loading: %s\n", model_path);
}

static void on_model_load_progress(GstElement *element, gfloat progress, gchar *message, gpointer user_data) {
    g_print("Progress: %.1f%% - %s\n", progress * 100, message);
}

static void on_model_loaded(GstElement *element, gchar *model_path, gpointer user_data) {
    g_print("Loaded: %s\n", model_path);

    // Signal that we're ready (set flag, emit custom signal, etc.)
    gboolean *ready = (gboolean *)user_data;
    *ready = TRUE;
}

static void on_model_load_failed(GstElement *element, gchar *error_msg, gpointer user_data) {
    g_print("Failed: %s\n", error_msg);
}

int main(int argc, char *argv[]) {
    GstElement *pipeline, *llama, *ctrlsrc, *filesink;
    GstPad *ctrl_pad, *src_pad;
    gboolean model_ready = FALSE;

    gst_init(&argc, &argv);

    // Create elements
    pipeline = gst_pipeline_new("dynamic");
    llama = gst_element_factory_make("llama", "generator");
    ctrlsrc = gst_element_factory_make("appsrc", "ctrlsrc");
    filesink = gst_element_factory_make("filesink", "sink");

    // Configure
    g_object_set(llama, "enable-buffer-queue", TRUE, NULL);
    g_object_set(filesink, "location", "output.txt", NULL);
    g_object_set(ctrlsrc, "caps", gst_caps_from_string("application/x-llama-control"), NULL);

    // Connect signals
    g_signal_connect(llama, "model-loading", G_CALLBACK(on_model_loading), NULL);
    g_signal_connect(llama, "model-load-progress", G_CALLBACK(on_model_load_progress), NULL);
    g_signal_connect(llama, "model-loaded", G_CALLBACK(on_model_loaded), &model_ready);
    g_signal_connect(llama, "model-load-failed", G_CALLBACK(on_model_load_failed), NULL);

    // Build pipeline
    gst_bin_add_many(GST_BIN(pipeline), llama, ctrlsrc, filesink, NULL);
    gst_element_link(llama, filesink);

    // Link control pad
    ctrl_pad = gst_element_request_pad_simple(llama, "ctrl");
    src_pad = gst_element_get_static_pad(ctrlsrc, "src");
    gst_pad_link(src_pad, ctrl_pad);
    gst_object_unref(src_pad);

    // Start pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // Send load_model command
    const char *load_json =
        "{\"command\":\"load_model\","
        "\"model_path\":\"/models/llama-7b.gguf\","
        "\"n_ctx\":2048}";

    GstBuffer *buffer = gst_buffer_new_allocate(NULL, strlen(load_json), NULL);
    gst_buffer_fill(buffer, 0, load_json, strlen(load_json));

    GstFlowReturn ret;
    g_signal_emit_by_name(ctrlsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);

    // Wait for model to load (simplified)
    g_print("Waiting for model to load...\n");
    while (!model_ready) {
        g_usleep(100000);  // 100ms
    }

    g_print("Model ready, pipeline can now process data!\n");

    // Cleanup
    gst_element_release_request_pad(llama, ctrl_pad);
    gst_object_unref(ctrl_pad);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
```

#### Buffer Queuing During Loading

When `enable-buffer-queue` is enabled, buffers sent to the sink pad during model loading are queued and processed once the model becomes ready:

```python
# Enable buffer queuing
llama.set_property("enable-buffer-queue", True)
llama.set_property("max-queued-buffers", 10)  # 0 = unlimited

# Start pipeline
pipeline.set_state(Gst.State.PLAYING)

# Send load command (non-blocking)
send_control_message(ctrlsrc, {
    "command": "load_model",
    "model_path": "/models/llama-7b.gguf"
})

# Can immediately send data - it will be queued
appsrc.emit("push-buffer", data_buffer)

# Once model is loaded (model-loaded signal), queued buffers are processed automatically
```

**Without buffer queuing:**
- Buffers sent during loading return `FLOW_ERROR`
- Upstream must wait for `model-loaded` signal

**With buffer queuing:**
- Buffers sent during loading are queued (up to `max-queued-buffers`)
- Automatically processed when model becomes `READY`
- Simplifies pipeline logic, no need to coordinate timing

#### Model States

The element tracks model state with the following transitions:

```
UNLOADED ──load_model──> LOADING ──success──> READY
                            │
                            └──failure──> ERROR

READY ──unload_model──> UNLOADING ──> UNLOADED

ERROR ──load_model──> LOADING (retry)
```

**State Behaviors:**
- `UNLOADED`: No model loaded, can call `load_model`
- `LOADING`: Async loading in progress, buffers queued if enabled
- `READY`: Model loaded, can process data or call `unload_model`
- `UNLOADING`: Unloading in progress (very brief)
- `ERROR`: Load failed, can retry with `load_model`

**See also:** `examples/dynamic-loading-example.c` for a complete C example

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
