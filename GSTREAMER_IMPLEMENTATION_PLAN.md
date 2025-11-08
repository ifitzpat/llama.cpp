# GStreamer Plugin Implementation Plan

**Project:** llama.cpp GStreamer Integration
**Branch:** `claude/integration-scenarios-ffi-gstreamer`
**Date:** 2025-11-08
**Target:** Production-ready GStreamer plugin with signal-based control

---

## Overview

Implement a GStreamer element that wraps llama.cpp for text generation, featuring:
- Standard GStreamer element with sink/src pads
- Optional control pad for dynamic parameter adjustment
- Rich signal system for inter-element communication
- Full model lifecycle management (load/unload/reload)
- Token-level streaming with metadata

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      GStreamer Pipeline                          │
│                                                                  │
│  ┌──────────┐      ┌─────────────────────────┐      ┌────────┐ │
│  │  Text    │─────▶│  GstLlama Element       │─────▶│  Sink  │ │
│  │  Source  │      │                         │      │        │ │
│  │          │      │  Properties:            │      │        │ │
│  └──────────┘      │  - model-path           │      └────────┘ │
│                    │  - temperature          │                 │
│  ┌──────────┐      │  - n-ctx, n-gpu-layers  │                 │
│  │  Control │─────▶│  - stream-tokens        │                 │
│  │  Source  │ ctrl │                         │                 │
│  │          │ pad  │  Signals:               │                 │
│  └──────────┘      │  - token-generated      │──┐              │
│                    │  - logit-probs          │  │ signals      │
│                    │  - generation-complete  │  │              │
│                    │  - model-loaded         │  │              │
│                    └─────────────────────────┘  │              │
│                                                 │              │
│  ┌──────────────────────────────────────────┐  │              │
│  │  Steering Element (optional)             │◀─┘              │
│  │  - Monitors signals                      │                 │
│  │  - Adjusts parameters dynamically        │                 │
│  └──────────────────────────────────────────┘                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Dependencies

### Required

1. **GStreamer 1.20+**
   - gstreamer-1.0
   - gstreamer-base-1.0
   - glib-2.0
   - gobject-2.0

2. **llama.cpp Core**
   - libllama (existing)
   - libcommon (existing)

3. **Build Tools**
   - Meson (preferred for GStreamer plugins) OR CMake
   - pkg-config
   - C++17 compiler

### Optional

1. **json-glib** - For structured control messages
2. **Common Lisp** (SBCL, CCL) - For FFI bindings (bonus phase)
3. **GStreamer Check** - For unit testing

---

## Phase 0: Foundation (C API Wrapper)

**Duration:** 3-4 days
**Why First:** GStreamer plugin needs a clean C API to wrap llama.cpp's C++ interface

### 0.1: Simple C API Design

**File:** `tools/ffi/llama_simple.h`

**Interface:**
```c
typedef struct llama_simple_context llama_simple_context;

// Lifecycle
llama_simple_context * llama_simple_init(const llama_simple_params * params);
void llama_simple_free(llama_simple_context * ctx);

// Model management
int llama_simple_load_model(llama_simple_context * ctx, const char * path,
                            const llama_simple_params * params);
int llama_simple_unload_model(llama_simple_context * ctx);

// Chat template
char * llama_simple_format_chat(llama_simple_context * ctx,
                                const llama_simple_chat_msg * messages,
                                int32_t num_messages,
                                bool add_generation_prompt);

// Streaming generation with callback
typedef bool (*llama_simple_token_callback)(void * user_data,
                                            const char * token_text,
                                            int token_id,
                                            float probability,
                                            int position);

int llama_simple_prompt_stream(llama_simple_context * ctx,
                               const char * prompt,
                               const llama_simple_gen_params * params,
                               llama_simple_token_callback callback,
                               void * user_data);

// Advanced: Logit bias
int llama_simple_set_logit_bias(llama_simple_context * ctx,
                                const llama_simple_logit_bias * biases,
                                int32_t num_biases);

// Advanced: Get top-k candidates before sampling (for signals)
typedef void (*llama_simple_logit_callback)(void * user_data,
                                            int position,
                                            const int32_t * token_ids,
                                            const float * probs,
                                            int32_t k);

int llama_simple_set_logit_callback(llama_simple_context * ctx,
                                    llama_simple_logit_callback callback,
                                    void * user_data);

// Utility
const char * llama_simple_get_error(llama_simple_context * ctx);
void llama_simple_free_string(char * str);
```

### 0.2: C API Implementation

**File:** `tools/ffi/llama_simple.cpp`

**Key Components:**

1. **Context Structure**
```cpp
struct llama_simple_context {
    // Core llama.cpp
    llama_model * model = nullptr;
    llama_context * ctx = nullptr;
    llama_vocab * vocab = nullptr;
    common_chat_templates_ptr chat_templates = nullptr;

    // State
    bool model_loaded = false;
    std::string last_error;

    // Parameters
    common_params params;

    // Callbacks
    llama_simple_token_callback token_cb = nullptr;
    void * token_cb_data = nullptr;
    llama_simple_logit_callback logit_cb = nullptr;
    void * logit_cb_data = nullptr;

    // Logit bias
    std::vector<llama_logit_bias> logit_biases;

    // Thread safety
    std::mutex mutex;
    std::atomic<bool> should_stop{false};
};
```

2. **Streaming with Logit Callback**
```cpp
// During generation, before each sample:
if (ctx->logit_cb) {
    // Get logits
    float * logits = llama_get_logits_ith(ctx->ctx, -1);
    int n_vocab = llama_n_vocab(ctx->model);

    // Get top-k
    std::vector<std::pair<int, float>> top_k = get_top_k(logits, n_vocab, 10);

    // Prepare arrays
    std::vector<int32_t> token_ids;
    std::vector<float> probs;
    for (const auto & [id, prob] : top_k) {
        token_ids.push_back(id);
        probs.push_back(prob);
    }

    // Callback
    ctx->logit_cb(ctx->logit_cb_data, position,
                  token_ids.data(), probs.data(), top_k.size());
}

// Sample token
llama_token token = common_sampler_sample(sampler, ctx->ctx, -1);

// Token callback
if (ctx->token_cb) {
    std::string token_text = common_token_to_piece(ctx->vocab, token);
    float prob = get_token_probability(sampler, token);

    bool should_continue = ctx->token_cb(
        ctx->token_cb_data,
        token_text.c_str(),
        token,
        prob,
        position
    );

    if (!should_continue) {
        break;  // User stopped generation
    }
}
```

### 0.3: Build Integration

**CMakeLists.txt:**
```cmake
# Simple C API wrapper
add_library(llama_simple SHARED
    tools/ffi/llama_simple.cpp
)

target_include_directories(llama_simple PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(llama_simple PRIVATE
    llama
    common
)

set_target_properties(llama_simple PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VERSION 1.0.0
    SOVERSION 1
)

install(TARGETS llama_simple
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES tools/ffi/llama_simple.h
    DESTINATION include
)
```

### 0.4: Testing

**File:** `tools/ffi/tests/test_simple_api.c`

**Tests:**
- Context lifecycle (init/free)
- Model load/unload
- Chat template formatting
- Basic generation
- Callback invocation
- Error handling
- Memory leak testing (valgrind)

**Success Criteria:**
- ✅ All tests pass
- ✅ No memory leaks
- ✅ Clean API boundaries
- ✅ Error handling works

---

## Phase 1: Basic GStreamer Element

**Duration:** 4-5 days
**Goal:** Minimal functional element with sink/src pads

### 1.1: Element Boilerplate

**File:** `tools/gstreamer/gstllama.h`

```c
#ifndef __GST_LLAMA_H__
#define __GST_LLAMA_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "llama_simple.h"

G_BEGIN_DECLS

#define GST_TYPE_LLAMA (gst_llama_get_type())
G_DECLARE_FINAL_TYPE(GstLlama, gst_llama, GST, LLAMA, GstElement)

struct _GstLlama {
    GstElement parent;

    // Pads
    GstPad * sinkpad;
    GstPad * srcpad;

    // Properties
    gchar * model_path;
    gint n_ctx;
    gint n_gpu_layers;
    gfloat temperature;
    gfloat top_p;
    gint top_k;
    gint max_tokens;
    gboolean stream_tokens;

    // State
    gboolean model_loaded;
    GMutex lock;
    GCond cond;

    // llama.cpp context
    llama_simple_context * llama_ctx;

    // Generation state
    gboolean generating;
};

G_END_DECLS

#endif
```

**File:** `tools/gstreamer/gstllama.c`

### 1.2: Pad Templates and Caps

```c
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("text/plain, charset=utf-8")
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("text/plain, charset=utf-8")
);
```

### 1.3: Properties

```c
enum {
    PROP_0,
    PROP_MODEL_PATH,
    PROP_N_CTX,
    PROP_N_GPU_LAYERS,
    PROP_TEMPERATURE,
    PROP_TOP_P,
    PROP_TOP_K,
    PROP_MAX_TOKENS,
    PROP_STREAM_TOKENS,
    PROP_LAST
};

static void
gst_llama_class_init(GstLlamaClass * klass)
{
    GObjectClass * gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass * element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_llama_set_property;
    gobject_class->get_property = gst_llama_get_property;
    gobject_class->finalize = gst_llama_finalize;

    g_object_class_install_property(gobject_class, PROP_MODEL_PATH,
        g_param_spec_string("model", "Model Path",
            "Path to GGUF model file",
            NULL,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_N_CTX,
        g_param_spec_int("n-ctx", "Context Size",
            "Context size in tokens",
            0, 1048576, 2048,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_TEMPERATURE,
        g_param_spec_float("temperature", "Temperature",
            "Sampling temperature",
            0.0, 2.0, 0.7,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_STREAM_TOKENS,
        g_param_spec_boolean("stream-tokens", "Stream Tokens",
            "Stream individual tokens instead of complete response",
            TRUE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    // ... more properties ...

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    gst_element_class_set_static_metadata(element_class,
        "LLaMA Text Generator",
        "Filter/Text/AI",
        "Generate text using llama.cpp language models",
        "llama.cpp contributors");

    element_class->change_state = gst_llama_change_state;
}
```

### 1.4: State Changes

```c
static GstStateChangeReturn
gst_llama_change_state(GstElement * element, GstStateChange transition)
{
    GstLlama * self = GST_LLAMA(element);
    GstStateChangeReturn ret;

    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            // Initialize llama backend
            llama_backend_init();

            // Create context
            llama_simple_params params = {0};
            params.n_ctx = self->n_ctx;
            params.n_gpu_layers = self->n_gpu_layers;
            params.n_threads = -1;
            params.seed = -1;
            params.verbose = FALSE;

            self->llama_ctx = llama_simple_init(&params);
            if (!self->llama_ctx) {
                GST_ERROR_OBJECT(self, "Failed to initialize llama context");
                return GST_STATE_CHANGE_FAILURE;
            }

            // Load model if path is set
            if (self->model_path) {
                if (gst_llama_load_model_internal(self) != 0) {
                    return GST_STATE_CHANGE_FAILURE;
                }
            }
            break;

        case GST_STATE_CHANGE_READY_TO_PAUSED:
            self->generating = FALSE;
            break;

        default:
            break;
    }

    ret = GST_ELEMENT_CLASS(gst_llama_parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    switch (transition) {
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            if (self->generating) {
                self->generating = FALSE;
                // Wait for generation to stop
            }
            break;

        case GST_STATE_CHANGE_READY_TO_NULL:
            if (self->llama_ctx) {
                if (self->model_loaded) {
                    llama_simple_unload_model(self->llama_ctx);
                    self->model_loaded = FALSE;
                }
                llama_simple_free(self->llama_ctx);
                self->llama_ctx = NULL;
            }
            llama_backend_free();
            break;

        default:
            break;
    }

    return ret;
}
```

### 1.5: Chain Function (Basic Generation)

```c
static GstFlowReturn
gst_llama_chain(GstPad * pad, GstObject * parent, GstBuffer * buf)
{
    GstLlama * self = GST_LLAMA(parent);
    GstMapInfo map;
    GstFlowReturn ret = GST_FLOW_OK;

    g_mutex_lock(&self->lock);

    if (!self->model_loaded) {
        GST_ERROR_OBJECT(self, "No model loaded");
        g_mutex_unlock(&self->lock);
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(self, "Failed to map input buffer");
        g_mutex_unlock(&self->lock);
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    gchar * prompt = g_strndup((const gchar *)map.data, map.size);
    gst_buffer_unmap(buf, &map);
    gst_buffer_unref(buf);

    GST_DEBUG_OBJECT(self, "Received prompt: %s", prompt);

    if (self->stream_tokens) {
        ret = gst_llama_generate_stream(self, prompt);
    } else {
        ret = gst_llama_generate_complete(self, prompt);
    }

    g_free(prompt);
    g_mutex_unlock(&self->lock);

    return ret;
}
```

### 1.6: Token Streaming

```c
typedef struct {
    GstLlama * self;
    GstFlowReturn ret;
} TokenCallbackData;

static gboolean
token_callback(gpointer user_data,
               const gchar * token_text,
               gint token_id,
               gfloat probability,
               gint position)
{
    TokenCallbackData * data = (TokenCallbackData *)user_data;
    GstLlama * self = data->self;

    if (strlen(token_text) > 0) {
        GstBuffer * out_buf = gst_buffer_new_allocate(NULL, strlen(token_text), NULL);
        GstMapInfo map;

        gst_buffer_map(out_buf, &map, GST_MAP_WRITE);
        memcpy(map.data, token_text, strlen(token_text));
        gst_buffer_unmap(out_buf, &map);

        // Set timestamp
        GST_BUFFER_PTS(out_buf) = GST_CLOCK_TIME_NONE;
        GST_BUFFER_DTS(out_buf) = GST_CLOCK_TIME_NONE;

        // Push downstream
        GstFlowReturn ret = gst_pad_push(self->srcpad, out_buf);
        if (ret != GST_FLOW_OK) {
            data->ret = ret;
            return FALSE;
        }
    }

    return TRUE;  // Continue generation
}

static GstFlowReturn
gst_llama_generate_stream(GstLlama * self, const gchar * prompt)
{
    TokenCallbackData data = { self, GST_FLOW_OK };

    llama_simple_gen_params params = {0};
    params.temperature = self->temperature;
    params.top_p = self->top_p;
    params.top_k = self->top_k;
    params.max_tokens = self->max_tokens;
    params.repeat_penalty = 1.1;
    params.stop_words = NULL;
    params.num_stop_words = 0;

    int result = llama_simple_prompt_stream(
        self->llama_ctx,
        prompt,
        &params,
        token_callback,
        &data
    );

    if (result != 0) {
        GST_ERROR_OBJECT(self, "Generation failed: %s",
                        llama_simple_get_error(self->llama_ctx));
        return GST_FLOW_ERROR;
    }

    return data.ret;
}
```

### 1.7: Build System (Meson)

**File:** `tools/gstreamer/meson.build`

```meson
project('gst-llama', 'c', 'cpp',
  version : '1.0.0',
  license : 'MIT',
  default_options : ['c_std=c11', 'cpp_std=c++17'])

# Dependencies
gst_dep = dependency('gstreamer-1.0', version : '>=1.20')
gst_base_dep = dependency('gstreamer-base-1.0', version : '>=1.20')
glib_dep = dependency('glib-2.0')
gobject_dep = dependency('gobject-2.0')

# llama_simple (built separately)
llama_simple_dep = dependency('llama_simple',
  fallback : ['llama_simple', 'llama_simple_dep'])

# Plugin sources
plugin_sources = [
  'gstllama.c',
  'gstllamaplugin.c',
]

# Build plugin
gst_llama = library('gstllama',
  plugin_sources,
  dependencies : [gst_dep, gst_base_dep, glib_dep, gobject_dep, llama_simple_dep],
  install : true,
  install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
)
```

### 1.8: Testing

**File:** `tools/gstreamer/tests/test_basic.c`

**Tests:**
- Element creation
- Property get/set
- State transitions (NULL→READY→PAUSED→PLAYING→NULL)
- Basic text processing
- Model load/unload

**Pipeline Tests:**
```bash
# Test 1: Simple generation
echo "Hello, how are you?" | \
gst-launch-1.0 fdsrc fd=0 ! \
  llama model=/path/to/model.gguf ! \
  filesink location=output.txt

# Test 2: Stream tokens to stdout
echo "Write a haiku:" | \
gst-launch-1.0 fdsrc fd=0 ! \
  llama model=/path/to/model.gguf stream-tokens=true ! \
  fdsink fd=1
```

**Success Criteria:**
- ✅ Element registers successfully
- ✅ Properties work
- ✅ State changes succeed
- ✅ Basic generation works
- ✅ Tokens stream correctly

---

## Phase 2: Signal System

**Duration:** 3-4 days
**Goal:** Rich signal-based communication

### 2.1: Define Signals

```c
enum {
    SIGNAL_TOKEN_GENERATED,
    SIGNAL_LOGIT_PROBS,
    SIGNAL_GENERATION_STARTED,
    SIGNAL_GENERATION_COMPLETE,
    SIGNAL_MODEL_LOADED,
    SIGNAL_MODEL_UNLOADED,
    LAST_SIGNAL
};

static guint gst_llama_signals[LAST_SIGNAL] = { 0 };

static void
gst_llama_class_init(GstLlamaClass * klass)
{
    // ... existing code ...

    /**
     * GstLlama::token-generated:
     * @llama: the llama element
     * @token: (transfer none): token text
     * @token_id: token ID
     * @probability: token probability
     * @position: position in sequence
     */
    gst_llama_signals[SIGNAL_TOKEN_GENERATED] =
        g_signal_new("token-generated",
            G_TYPE_FROM_CLASS(klass),
            G_SIGNAL_RUN_LAST,
            0, NULL, NULL, NULL,
            G_TYPE_NONE,
            4,
            G_TYPE_STRING,   // token
            G_TYPE_INT,      // token_id
            G_TYPE_FLOAT,    // probability
            G_TYPE_INT);     // position

    /**
     * GstLlama::logit-probs:
     * @llama: the llama element
     * @position: current position
     * @token_ids: (transfer none) (array length=k): token IDs
     * @probs: (transfer none) (array length=k): probabilities
     * @k: number of candidates
     */
    gst_llama_signals[SIGNAL_LOGIT_PROBS] =
        g_signal_new("logit-probs",
            G_TYPE_FROM_CLASS(klass),
            G_SIGNAL_RUN_LAST,
            0, NULL, NULL, NULL,
            G_TYPE_NONE,
            4,
            G_TYPE_INT,      // position
            G_TYPE_POINTER,  // token_ids array
            G_TYPE_POINTER,  // probs array
            G_TYPE_INT);     // k

    /**
     * GstLlama::generation-complete:
     * @llama: the llama element
     * @full_text: (transfer none): complete generated text
     * @num_tokens: number of tokens generated
     * @stop_reason: (transfer none): reason for stopping
     */
    gst_llama_signals[SIGNAL_GENERATION_COMPLETE] =
        g_signal_new("generation-complete",
            G_TYPE_FROM_CLASS(klass),
            G_SIGNAL_RUN_LAST,
            0, NULL, NULL, NULL,
            G_TYPE_NONE,
            3,
            G_TYPE_STRING,   // full_text
            G_TYPE_INT,      // num_tokens
            G_TYPE_STRING);  // stop_reason

    /**
     * GstLlama::model-loaded:
     * @llama: the llama element
     * @model_path: (transfer none): path to loaded model
     */
    gst_llama_signals[SIGNAL_MODEL_LOADED] =
        g_signal_new("model-loaded",
            G_TYPE_FROM_CLASS(klass),
            G_SIGNAL_RUN_LAST,
            0, NULL, NULL, NULL,
            G_TYPE_NONE,
            1,
            G_TYPE_STRING);  // model_path
}
```

### 2.2: Emit Signals During Generation

Update `token_callback` to emit signals:

```c
static gboolean
token_callback(gpointer user_data,
               const gchar * token_text,
               gint token_id,
               gfloat probability,
               gint position)
{
    TokenCallbackData * data = (TokenCallbackData *)user_data;
    GstLlama * self = data->self;

    // Emit token-generated signal
    g_signal_emit(self, gst_llama_signals[SIGNAL_TOKEN_GENERATED], 0,
                  token_text, token_id, probability, position);

    // ... rest of function ...
}
```

Add logit callback:

```c
static void
logit_callback(gpointer user_data,
               gint position,
               const gint32 * token_ids,
               const gfloat * probs,
               gint k)
{
    GstLlama * self = GST_LLAMA(user_data);

    // Emit logit-probs signal
    g_signal_emit(self, gst_llama_signals[SIGNAL_LOGIT_PROBS], 0,
                  position, token_ids, probs, k);
}

// Register callback before generation
llama_simple_set_logit_callback(self->llama_ctx, logit_callback, self);
```

### 2.3: Bus Messages

Post structured messages for pipeline-wide communication:

```c
static void
gst_llama_post_token_message(GstLlama * self,
                              const gchar * token,
                              gint token_id,
                              gfloat probability)
{
    GstStructure * s = gst_structure_new("llama-token",
        "token", G_TYPE_STRING, token,
        "token-id", G_TYPE_INT, token_id,
        "probability", G_TYPE_FLOAT, probability,
        "timestamp", G_TYPE_UINT64, g_get_monotonic_time(),
        NULL);

    GstMessage * msg = gst_message_new_element(GST_OBJECT(self), s);
    gst_element_post_message(GST_ELEMENT(self), msg);
}
```

### 2.4: Testing Signals

**File:** `tools/gstreamer/tests/test_signals.c`

```c
static void
on_token_generated(GstLlama * llama,
                   const gchar * token,
                   gint token_id,
                   gfloat probability,
                   gint position,
                   gpointer user_data)
{
    gint * count = (gint *)user_data;
    (*count)++;
    g_print("[%d] Token: %s (id=%d, prob=%.4f)\n",
            position, token, token_id, probability);
}

static void
test_signals(void)
{
    GstElement * llama = gst_element_factory_make("llama", NULL);
    gint token_count = 0;

    g_object_set(llama, "model", "/path/to/model.gguf", NULL);

    g_signal_connect(llama, "token-generated",
                     G_CALLBACK(on_token_generated), &token_count);

    // Send prompt and verify signals emitted
    // ...

    g_assert_cmpint(token_count, >, 0);
}
```

**Success Criteria:**
- ✅ All signals defined correctly
- ✅ Signals emit at proper times
- ✅ Signal handlers receive correct data
- ✅ Bus messages work
- ✅ No memory leaks in signal emission

---

## Phase 3: Control Pad

**Duration:** 3-4 days
**Goal:** Dynamic parameter control via separate input pad

### 3.1: Control Pad Template

```c
static GstStaticPadTemplate ctrl_template = GST_STATIC_PAD_TEMPLATE(
    "ctrl",
    GST_PAD_SINK,
    GST_PAD_REQUEST,  // Optional, created on demand
    GST_STATIC_CAPS("application/x-llama-control; application/json")
);
```

### 3.2: Control Pad Creation

```c
static GstPad *
gst_llama_request_new_pad(GstElement * element,
                          GstPadTemplate * templ,
                          const gchar * name,
                          const GstCaps * caps)
{
    GstLlama * self = GST_LLAMA(element);

    if (templ == gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(element), "ctrl")) {
        if (self->ctrlpad) {
            GST_WARNING_OBJECT(self, "Control pad already exists");
            return NULL;
        }

        self->ctrlpad = gst_pad_new_from_static_template(&ctrl_template, "ctrl");
        gst_pad_set_chain_function(self->ctrlpad, gst_llama_ctrl_chain);
        gst_pad_set_event_function(self->ctrlpad, gst_llama_ctrl_event);
        gst_element_add_pad(GST_ELEMENT(self), self->ctrlpad);

        return self->ctrlpad;
    }

    return NULL;
}
```

### 3.3: Control Message Protocol (JSON)

```c
static GstFlowReturn
gst_llama_ctrl_chain(GstPad * pad, GstObject * parent, GstBuffer * buf)
{
    GstLlama * self = GST_LLAMA(parent);
    GstMapInfo map;

    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    // Parse JSON control message
    JsonParser * parser = json_parser_new();
    GError * error = NULL;

    if (!json_parser_load_from_data(parser, (const gchar *)map.data, map.size, &error)) {
        GST_ERROR_OBJECT(self, "Failed to parse control JSON: %s", error->message);
        g_error_free(error);
        gst_buffer_unmap(buf, &map);
        gst_buffer_unref(buf);
        g_object_unref(parser);
        return GST_FLOW_ERROR;
    }

    JsonNode * root = json_parser_get_root(parser);
    JsonObject * obj = json_node_get_object(root);

    const gchar * command = json_object_get_string_member(obj, "command");

    g_mutex_lock(&self->lock);

    if (g_str_equal(command, "set_params")) {
        gst_llama_handle_set_params(self, obj);
    } else if (g_str_equal(command, "set_logit_bias")) {
        gst_llama_handle_set_logit_bias(self, obj);
    } else if (g_str_equal(command, "reset_bias")) {
        gst_llama_handle_reset_bias(self);
    } else if (g_str_equal(command, "load_model")) {
        gst_llama_handle_load_model(self, obj);
    } else if (g_str_equal(command, "unload_model")) {
        gst_llama_handle_unload_model(self);
    } else {
        GST_WARNING_OBJECT(self, "Unknown control command: %s", command);
    }

    g_mutex_unlock(&self->lock);

    gst_buffer_unmap(buf, &map);
    gst_buffer_unref(buf);
    g_object_unref(parser);

    return GST_FLOW_OK;
}
```

### 3.4: Control Handlers

```c
static void
gst_llama_handle_set_params(GstLlama * self, JsonObject * obj)
{
    if (json_object_has_member(obj, "temperature")) {
        self->temperature = json_object_get_double_member(obj, "temperature");
        GST_INFO_OBJECT(self, "Updated temperature: %.2f", self->temperature);
    }

    if (json_object_has_member(obj, "top_p")) {
        self->top_p = json_object_get_double_member(obj, "top_p");
        GST_INFO_OBJECT(self, "Updated top_p: %.2f", self->top_p);
    }

    if (json_object_has_member(obj, "max_tokens")) {
        self->max_tokens = json_object_get_int_member(obj, "max_tokens");
        GST_INFO_OBJECT(self, "Updated max_tokens: %d", self->max_tokens);
    }
}

static void
gst_llama_handle_set_logit_bias(GstLlama * self, JsonObject * obj)
{
    if (!json_object_has_member(obj, "biases")) {
        GST_WARNING_OBJECT(self, "set_logit_bias missing 'biases' field");
        return;
    }

    JsonObject * biases = json_object_get_object_member(obj, "biases");

    // Build array of llama_simple_logit_bias
    GArray * bias_array = g_array_new(FALSE, FALSE, sizeof(llama_simple_logit_bias));

    JsonObjectIter iter;
    const gchar * token_str;
    JsonNode * value_node;

    json_object_iter_init(&iter, biases);
    while (json_object_iter_next(&iter, &token_str, &value_node)) {
        gfloat bias_value = json_node_get_double(value_node);

        // Tokenize string to get token ID(s)
        // For simplicity, store string and resolve during generation
        llama_simple_logit_bias bias;
        bias.token_str = g_strdup(token_str);
        bias.bias = bias_value;

        g_array_append_val(bias_array, bias);
    }

    // Apply to llama context
    llama_simple_set_logit_bias(self->llama_ctx,
                                (llama_simple_logit_bias *)bias_array->data,
                                bias_array->len);

    GST_INFO_OBJECT(self, "Applied %d logit bias entries", bias_array->len);

    g_array_free(bias_array, TRUE);
}
```

### 3.5: Testing Control Pad

**Pipeline with control:**

```bash
# Terminal 1: Create named pipes
mkfifo /tmp/llama_prompt
mkfifo /tmp/llama_control
mkfifo /tmp/llama_output

# Start pipeline
gst-launch-1.0 \
  filesrc location=/tmp/llama_prompt ! llama name=gen model=/models/chat.gguf ! filesink location=/tmp/llama_output \
  filesrc location=/tmp/llama_control ! gen.ctrl

# Terminal 2: Send control commands
echo '{"command": "set_params", "temperature": 1.2}' > /tmp/llama_control
echo "Tell me a creative story:" > /tmp/llama_prompt
cat /tmp/llama_output

echo '{"command": "set_params", "temperature": 0.1}' > /tmp/llama_control
echo "Write Python code to sort a list:" > /tmp/llama_prompt
cat /tmp/llama_output
```

**Success Criteria:**
- ✅ Control pad creates on request
- ✅ JSON parsing works
- ✅ Parameters update correctly
- ✅ Logit bias applies
- ✅ Thread-safe updates

---

## Phase 4: Advanced Features

**Duration:** 3-4 days
**Goal:** Production-ready features

### 4.1: Logit Bias Support

Extend `llama_simple.h`:

```c
struct llama_simple_logit_bias {
    const char * token_str;  // Token text (will be tokenized)
    float bias;              // Bias value (+/- inf)
};
```

Implementation in `llama_simple.cpp`:

```cpp
int llama_simple_set_logit_bias(llama_simple_context * ctx,
                                const llama_simple_logit_bias * biases,
                                int32_t num_biases)
{
    if (!ctx || !biases) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);

    ctx->logit_biases.clear();

    for (int32_t i = 0; i < num_biases; i++) {
        // Tokenize string
        auto tokens = common_tokenize(ctx->vocab, biases[i].token_str, false);

        for (auto token_id : tokens) {
            llama_logit_bias bias;
            bias.token = token_id;
            bias.bias = biases[i].bias;
            ctx->logit_biases.push_back(bias);
        }
    }

    return 0;
}

// Apply during sampling
if (!ctx->logit_biases.empty()) {
    auto bias_sampler = llama_sampler_init_logit_bias(
        llama_model_n_vocab(ctx->model),
        ctx->logit_biases.size(),
        ctx->logit_biases.data()
    );
    llama_sampler_chain_add(sampler, bias_sampler);
}
```

### 4.2: Steering Element (Companion)

**File:** `tools/gstreamer/gstllamasteering.c`

Standalone element that monitors llama's signals and adjusts parameters:

```c
struct _GstLlamaSteering {
    GstElement parent;

    GstElement * llama_elem;  // Reference to llama

    // Steering parameters
    gfloat base_temperature;
    gfloat min_entropy;
    gfloat max_entropy;
    gboolean enable_adaptive;
};

static void
on_logit_probs(GstLlama * llama,
               gint position,
               const gint32 * token_ids,
               const gfloat * probs,
               gint k,
               gpointer user_data)
{
    GstLlamaSteering * self = GST_LLAMA_STEERING(user_data);

    if (!self->enable_adaptive) {
        return;
    }

    // Calculate entropy
    gfloat entropy = 0.0;
    for (gint i = 0; i < k; i++) {
        if (probs[i] > 0.0) {
            entropy -= probs[i] * log2f(probs[i]);
        }
    }

    // Adjust temperature based on entropy
    gfloat new_temp = self->base_temperature;

    if (entropy < self->min_entropy) {
        // Model too confident, increase temp
        new_temp = self->base_temperature * 1.5;
    } else if (entropy > self->max_entropy) {
        // Model uncertain, decrease temp
        new_temp = self->base_temperature * 0.7;
    }

    new_temp = CLAMP(new_temp, 0.1, 2.0);

    g_object_set(llama, "temperature", new_temp, NULL);
}
```

### 4.3: Error Handling & Recovery

- Connection loss handling
- Model loading failures
- Out-of-memory errors
- Generation timeout
- Graceful degradation

### 4.4: Performance Optimization

- Buffer pooling
- Zero-copy where possible
- Async property updates
- Lock optimization

### 4.5: Documentation

**File:** `tools/gstreamer/README.md`

- Installation instructions
- Pipeline examples
- Property reference
- Signal reference
- Control message protocol
- Troubleshooting

**Success Criteria:**
- ✅ Logit bias working
- ✅ Steering element functional
- ✅ Robust error handling
- ✅ Good performance
- ✅ Complete documentation

---

## Phase 5: Testing & Integration

**Duration:** 2-3 days

### 5.1: Unit Tests

**File:** `tools/gstreamer/tests/meson.build`

```meson
gst_check_dep = dependency('gstreamer-check-1.0', required : false)

if gst_check_dep.found()
    tests = [
        'test_basic',
        'test_signals',
        'test_control',
        'test_logit_bias',
    ]

    foreach t : tests
        exe = executable(t, t + '.c',
            dependencies : [gst_dep, gst_check_dep, glib_dep],
            link_with : gst_llama)

        test(t, exe)
    endforeach
endif
```

### 5.2: Integration Tests

**Pipeline tests:**

1. Simple generation
2. Streaming tokens
3. Control pad parameter updates
4. Signal-based steering
5. Multi-element pipelines
6. Error scenarios

### 5.3: Performance Tests

- Latency measurement
- Throughput (tokens/sec)
- Memory usage
- CPU usage
- GPU utilization

### 5.4: Valgrind Testing

```bash
G_SLICE=always-malloc G_DEBUG=gc-friendly \
valgrind --leak-check=full --show-leak-kinds=all \
  gst-launch-1.0 ... llama ... !
```

### 5.5: CI Integration

Add to `.github/workflows/gstreamer.yml`:

```yaml
name: GStreamer Plugin Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            libgstreamer1.0-dev \
            libgstreamer-plugins-base1.0-dev \
            meson ninja-build

      - name: Build
        run: |
          cd tools/gstreamer
          meson setup build
          meson compile -C build

      - name: Test
        run: |
          cd tools/gstreamer
          meson test -C build --verbose
```

**Success Criteria:**
- ✅ All unit tests pass
- ✅ Integration tests pass
- ✅ No memory leaks
- ✅ Performance acceptable
- ✅ CI passing

---

## Phase 6 (Optional): Common Lisp Bindings

**Duration:** 2-3 days
**Status:** BONUS - Only if time permits

### 6.1: CFFI Wrapper

**File:** `tools/ffi/lisp/llama-simple.lisp`

Bindings for `llama_simple.h` API.

### 6.2: High-Level API

```lisp
(defun generate-text (model-path prompt &key (temperature 0.7))
  (with-llama-context (:model model-path :n-ctx 2048)
    (prompt prompt :temperature temperature)))
```

### 6.3: Examples

- Simple generation
- Chat interface
- Token streaming
- Logit bias

**Success Criteria:**
- ✅ FFI bindings work
- ✅ High-level API usable
- ✅ Examples run
- ✅ Documentation complete

---

## Deliverables Checklist

### Phase 0: C API Wrapper
- [ ] `tools/ffi/llama_simple.h` - C API header
- [ ] `tools/ffi/llama_simple.cpp` - Implementation
- [ ] `tools/ffi/CMakeLists.txt` - Build config
- [ ] `tools/ffi/tests/test_simple_api.c` - Tests
- [ ] `tools/ffi/README.md` - Documentation

### Phase 1: Basic Element
- [ ] `tools/gstreamer/gstllama.h` - Element header
- [ ] `tools/gstreamer/gstllama.c` - Element implementation
- [ ] `tools/gstreamer/gstllamaplugin.c` - Plugin registration
- [ ] `tools/gstreamer/meson.build` - Build config
- [ ] `tools/gstreamer/tests/test_basic.c` - Basic tests

### Phase 2: Signals
- [ ] Signal definitions in `gstllama.c`
- [ ] Signal emission code
- [ ] Bus message posting
- [ ] `tools/gstreamer/tests/test_signals.c` - Signal tests

### Phase 3: Control Pad
- [ ] Control pad template
- [ ] Control message parsing (JSON)
- [ ] Control handlers
- [ ] `tools/gstreamer/tests/test_control.c` - Control tests

### Phase 4: Advanced
- [ ] Logit bias implementation
- [ ] `tools/gstreamer/gstllamasteering.c` - Steering element
- [ ] Error handling
- [ ] Performance optimization
- [ ] `tools/gstreamer/README.md` - Full documentation
- [ ] `tools/gstreamer/examples/` - Pipeline examples

### Phase 5: Testing
- [ ] Unit test suite
- [ ] Integration tests
- [ ] Performance benchmarks
- [ ] Valgrind tests
- [ ] CI configuration

### Phase 6 (Optional): Lisp
- [ ] `tools/ffi/lisp/llama-simple.lisp` - CFFI bindings
- [ ] `tools/ffi/lisp/examples/` - Lisp examples
- [ ] `tools/ffi/lisp/README.md` - Lisp documentation

---

## Timeline

| Phase | Duration | Cumulative |
|-------|----------|------------|
| Phase 0: C API | 3-4 days | 4 days |
| Phase 1: Basic Element | 4-5 days | 9 days |
| Phase 2: Signals | 3-4 days | 13 days |
| Phase 3: Control Pad | 3-4 days | 17 days |
| Phase 4: Advanced | 3-4 days | 21 days |
| Phase 5: Testing | 2-3 days | 24 days |
| **Total (Core)** | **~24 days** | **~5 weeks** |
| Phase 6: Lisp (Optional) | 2-3 days | 27 days |

---

## Success Metrics

### Functionality
- ✅ Element registers and loads in GStreamer
- ✅ Models load/unload successfully
- ✅ Text generation works (streaming and complete)
- ✅ Signals emit correctly with accurate data
- ✅ Control pad accepts and applies commands
- ✅ Logit bias affects generation as expected

### Quality
- ✅ No memory leaks (valgrind clean)
- ✅ No crashes under normal operation
- ✅ Thread-safe (no race conditions)
- ✅ Proper error handling and reporting
- ✅ All tests pass

### Performance
- ✅ < 5% overhead vs. direct llama.cpp usage
- ✅ Token streaming latency < 10ms
- ✅ Signal emission < 1ms per token
- ✅ Control message latency < 100ms

### Usability
- ✅ Clear documentation
- ✅ Working examples
- ✅ Easy installation
- ✅ Intuitive API

---

## Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| GStreamer API complexity | High | Start simple, iterate |
| Thread safety issues | High | Careful locking, testing |
| Memory leaks | Medium | Valgrind, cleanup tests |
| Performance overhead | Medium | Profile, optimize |
| ABI compatibility | Low | Use stable C API |

---

## Next Steps

1. **Review this plan** - Confirm approach
2. **Set up development environment** - Install GStreamer dev packages
3. **Begin Phase 0** - Implement C API wrapper
4. **Iterate** - Test each phase before moving forward

---

## Questions to Resolve

1. **Build System:** Meson (GStreamer standard) or CMake (llama.cpp standard)?
   - **Recommendation:** Meson for plugin, CMake for C API

2. **Control Message Format:** JSON only or support binary too?
   - **Recommendation:** JSON first, binary if needed

3. **Steering Element:** Separate plugin or built-in?
   - **Recommendation:** Separate for modularity

4. **Lisp Bindings:** Include in this branch or separate?
   - **Recommendation:** Separate branch if pursued

---

**Ready to begin implementation?**
