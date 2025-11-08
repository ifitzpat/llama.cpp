# Integration Scenarios Analysis

This document analyzes two integration scenarios for llama.cpp based on the model management feature implemented in `claude/implement-model-loading-unloading-011CUrgj9DxaFqUMTWowxyBi`.

**Status:** Design Analysis
**Date:** 2025-11-08
**Branch:** `claude/integration-scenarios-ffi-gstreamer`

---

## Table of Contents

1. [Scenario 1: Common Lisp FFI Wrapper](#scenario-1-common-lisp-ffi-wrapper)
2. [Scenario 2: GStreamer Filter Plugin](#scenario-2-gstreamer-filter-plugin)
3. [Shared Considerations](#shared-considerations)
4. [Implementation Roadmap](#implementation-roadmap)

---

## Scenario 1: Common Lisp FFI Wrapper

### Overview

Create a simple C API wrapper around llama.cpp that can be called via Foreign Function Interface (FFI) from Common Lisp (or other FFI-capable languages).

### Requirements

1. **Model Management**: Load and unload models dynamically
2. **Chat Template Support**: Format prompts using model's Jinja template
3. **Token Streaming**: Stream tokens as they're generated
4. **JSON Response**: Return complete responses as JSON
5. **Memory Safety**: Clean FFI boundaries with proper lifecycle management

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                   Common Lisp Application                    │
│  (CFFI, cl-autowrap, or similar FFI library)                │
└────────────────────────┬────────────────────────────────────┘
                         │ FFI calls
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              llama_simple.h (C API Wrapper)                  │
│                                                              │
│  - llama_simple_context* llama_simple_init()                │
│  - int llama_simple_load_model(...)                         │
│  - int llama_simple_unload_model(...)                       │
│  - char* llama_simple_prompt(...)                           │
│  - int llama_simple_prompt_stream(callback, ...)            │
│  - void llama_simple_free(...)                              │
└────────────────────────┬────────────────────────────────────┘
                         │ C++ implementation
                         ▼
┌─────────────────────────────────────────────────────────────┐
│            llama_simple.cpp (Implementation)                 │
│                                                              │
│  Uses existing llama.cpp API:                               │
│  - llama_model_load()                                       │
│  - llama_context_create()                                   │
│  - common_chat_templates_apply()                            │
│  - llama_decode()                                           │
│  - llama_sampler_sample()                                   │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                    llama.cpp Core                            │
│  (libllama.so / llama.dll)                                  │
└─────────────────────────────────────────────────────────────┘
```

### Proposed C API

#### Header: `llama_simple.h`

```c
#ifndef LLAMA_SIMPLE_H
#define LLAMA_SIMPLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to context
typedef struct llama_simple_context llama_simple_context;

// Callback for token streaming: returns false to stop generation
typedef bool (*llama_simple_token_callback)(
    void * user_data,
    const char * token_text,
    int token_id,
    bool is_final
);

// Error codes
enum llama_simple_error {
    LLAMA_SIMPLE_OK = 0,
    LLAMA_SIMPLE_ERROR_INIT_FAILED = -1,
    LLAMA_SIMPLE_ERROR_MODEL_LOAD_FAILED = -2,
    LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED = -3,
    LLAMA_SIMPLE_ERROR_INVALID_PARAMS = -4,
    LLAMA_SIMPLE_ERROR_OUT_OF_MEMORY = -5,
    LLAMA_SIMPLE_ERROR_GENERATION_FAILED = -6,
};

// Initialization parameters
struct llama_simple_params {
    int32_t n_ctx;              // Context size (0 = model default)
    int32_t n_gpu_layers;       // GPU layers (-1 = all)
    int32_t n_threads;          // CPU threads (-1 = auto)
    int32_t seed;               // RNG seed (-1 = random)
    bool verbose;               // Verbose logging
};

// Generation parameters
struct llama_simple_gen_params {
    int32_t max_tokens;         // Max tokens to generate (-1 = unlimited)
    float temperature;          // Sampling temperature
    float top_p;                // Top-p sampling
    int32_t top_k;              // Top-k sampling
    float repeat_penalty;       // Repetition penalty
    const char ** stop_words;   // Null-terminated array of stop strings
    int32_t num_stop_words;     // Number of stop words
};

// Chat message (for chat template formatting)
struct llama_simple_chat_msg {
    const char * role;          // "system", "user", "assistant"
    const char * content;       // Message content
};

/**
 * Initialize a new llama.cpp context.
 *
 * @param params Initial parameters (can be NULL for defaults)
 * @return Opaque context handle, or NULL on failure
 */
llama_simple_context * llama_simple_init(
    const struct llama_simple_params * params
);

/**
 * Load a GGUF model file.
 *
 * @param ctx Context handle
 * @param model_path Path to GGUF file
 * @param params Model-specific parameters (can be NULL)
 * @return Error code
 */
int llama_simple_load_model(
    llama_simple_context * ctx,
    const char * model_path,
    const struct llama_simple_params * params
);

/**
 * Unload the current model.
 *
 * @param ctx Context handle
 * @return Error code
 */
int llama_simple_unload_model(
    llama_simple_context * ctx
);

/**
 * Format a chat prompt using the model's chat template (Jinja).
 *
 * @param ctx Context handle
 * @param messages Array of chat messages
 * @param num_messages Number of messages
 * @param add_generation_prompt Add "assistant:" prefix
 * @return Formatted prompt (caller must free with llama_simple_free_string)
 */
char * llama_simple_format_chat(
    llama_simple_context * ctx,
    const struct llama_simple_chat_msg * messages,
    int32_t num_messages,
    bool add_generation_prompt
);

/**
 * Generate a complete response (blocking, returns JSON).
 *
 * @param ctx Context handle
 * @param prompt Input prompt (plain text or chat-formatted)
 * @param params Generation parameters (can be NULL)
 * @return JSON response string (caller must free with llama_simple_free_string)
 *
 * JSON format:
 * {
 *   "text": "Generated response text",
 *   "tokens": 42,
 *   "stop_reason": "eos" | "max_tokens" | "stop_word",
 *   "timings": {
 *     "prompt_tokens": 10,
 *     "predicted_tokens": 42,
 *     "total_ms": 1234.5,
 *     "tokens_per_sec": 33.8
 *   }
 * }
 */
char * llama_simple_prompt_json(
    llama_simple_context * ctx,
    const char * prompt,
    const struct llama_simple_gen_params * params
);

/**
 * Generate a streaming response (callback for each token).
 *
 * @param ctx Context handle
 * @param prompt Input prompt (plain text or chat-formatted)
 * @param params Generation parameters (can be NULL)
 * @param callback Function called for each token
 * @param user_data User data passed to callback
 * @return Error code
 */
int llama_simple_prompt_stream(
    llama_simple_context * ctx,
    const char * prompt,
    const struct llama_simple_gen_params * params,
    llama_simple_token_callback callback,
    void * user_data
);

/**
 * Get the last error message.
 *
 * @param ctx Context handle
 * @return Error message string (do not free)
 */
const char * llama_simple_get_error(
    llama_simple_context * ctx
);

/**
 * Get model information as JSON.
 *
 * @param ctx Context handle
 * @return JSON string (caller must free with llama_simple_free_string)
 *
 * JSON format:
 * {
 *   "model_name": "llama-2-7b-chat",
 *   "n_vocab": 32000,
 *   "n_ctx_train": 4096,
 *   "n_ctx": 2048,
 *   "n_params": 7000000000,
 *   "chat_template": "..."
 * }
 */
char * llama_simple_get_model_info(
    llama_simple_context * ctx
);

/**
 * Free a string returned by this API.
 *
 * @param str String to free
 */
void llama_simple_free_string(
    char * str
);

/**
 * Free the context and all associated resources.
 *
 * @param ctx Context handle
 */
void llama_simple_free(
    llama_simple_context * ctx
);

#ifdef __cplusplus
}
#endif

#endif // LLAMA_SIMPLE_H
```

### Implementation Outline

#### Key Components

1. **Context Structure** (`llama_simple.cpp`)
```cpp
struct llama_simple_context {
    // Core llama.cpp objects
    llama_model * model = nullptr;
    llama_context * ctx = nullptr;
    llama_vocab * vocab = nullptr;
    common_chat_templates_ptr chat_templates = nullptr;

    // State
    bool model_loaded = false;
    std::string last_error;

    // Parameters
    llama_simple_params params;
    common_params common_params;

    // For thread safety (if needed)
    std::mutex mutex;
};
```

2. **Model Loading Implementation**
```cpp
extern "C" int llama_simple_load_model(
    llama_simple_context * ctx,
    const char * model_path,
    const struct llama_simple_params * params
) {
    if (!ctx || !model_path) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);

    try {
        // Set up common_params
        ctx->common_params.model.path = model_path;
        ctx->common_params.n_ctx = params ? params->n_ctx : 0;
        ctx->common_params.n_gpu_layers = params ? params->n_gpu_layers : -1;

        // Load model
        llama_init_result result = common_init_from_params(ctx->common_params);
        if (!result.model) {
            ctx->last_error = "Failed to load model";
            return LLAMA_SIMPLE_ERROR_MODEL_LOAD_FAILED;
        }

        ctx->model = result.model;
        ctx->ctx = result.context;
        ctx->vocab = common_vocab_init_from_model(result.model);

        // Initialize chat templates
        ctx->chat_templates = common_chat_templates_init(
            ctx->model, "", "", ""
        );

        ctx->model_loaded = true;
        return LLAMA_SIMPLE_OK;

    } catch (const std::exception & e) {
        ctx->last_error = e.what();
        return LLAMA_SIMPLE_ERROR_MODEL_LOAD_FAILED;
    }
}
```

3. **Chat Template Formatting**
```cpp
extern "C" char * llama_simple_format_chat(
    llama_simple_context * ctx,
    const struct llama_simple_chat_msg * messages,
    int32_t num_messages,
    bool add_generation_prompt
) {
    if (!ctx || !ctx->model_loaded || !messages) {
        return nullptr;
    }

    try {
        // Convert C structs to C++ types
        std::vector<common_chat_msg> msgs;
        for (int32_t i = 0; i < num_messages; i++) {
            common_chat_msg msg;
            msg.role = messages[i].role;
            msg.content = messages[i].content;
            msgs.push_back(msg);
        }

        // Apply chat template
        common_chat_templates_inputs inputs;
        inputs.messages = msgs;
        inputs.add_generation_prompt = add_generation_prompt;

        common_chat_params result = common_chat_templates_apply(
            ctx->chat_templates.get(),
            inputs
        );

        // Return as heap-allocated C string
        char * output = strdup(result.prompt.c_str());
        return output;

    } catch (const std::exception & e) {
        ctx->last_error = e.what();
        return nullptr;
    }
}
```

4. **Streaming Generation**
```cpp
extern "C" int llama_simple_prompt_stream(
    llama_simple_context * ctx,
    const char * prompt,
    const struct llama_simple_gen_params * params,
    llama_simple_token_callback callback,
    void * user_data
) {
    if (!ctx || !ctx->model_loaded || !prompt || !callback) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);

    try {
        // Tokenize prompt
        auto tokens = common_tokenize(ctx->vocab, prompt, true);

        // Create batch
        llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());

        // Decode prompt
        if (llama_decode(ctx->ctx, batch) != 0) {
            ctx->last_error = "Failed to decode prompt";
            return LLAMA_SIMPLE_ERROR_GENERATION_FAILED;
        }

        // Create sampler
        auto sparams = common_sampler_params_from_gparams(ctx->common_params);
        if (params) {
            sparams.temp = params->temperature;
            sparams.top_p = params->top_p;
            sparams.top_k = params->top_k;
            sparams.penalty_repeat = params->repeat_penalty;
        }
        auto smpl = common_sampler_init(ctx->model, sparams);

        // Generation loop
        int n_generated = 0;
        int max_tokens = params ? params->max_tokens : -1;

        while (true) {
            // Sample next token
            llama_token token = common_sampler_sample(smpl, ctx->ctx, -1);

            // Check for EOS
            if (llama_token_is_eog(ctx->model, token)) {
                callback(user_data, "", token, true);
                break;
            }

            // Decode token to text
            std::string token_text = common_token_to_piece(ctx->vocab, token);

            // Call user callback
            bool should_continue = callback(
                user_data,
                token_text.c_str(),
                token,
                false
            );

            if (!should_continue) {
                break;
            }

            // Check max tokens
            n_generated++;
            if (max_tokens > 0 && n_generated >= max_tokens) {
                callback(user_data, "", token, true);
                break;
            }

            // Decode next token
            batch = llama_batch_get_one(&token, 1);
            if (llama_decode(ctx->ctx, batch) != 0) {
                ctx->last_error = "Failed to decode during generation";
                common_sampler_free(smpl);
                return LLAMA_SIMPLE_ERROR_GENERATION_FAILED;
            }
        }

        common_sampler_free(smpl);
        return LLAMA_SIMPLE_OK;

    } catch (const std::exception & e) {
        ctx->last_error = e.what();
        return LLAMA_SIMPLE_ERROR_GENERATION_FAILED;
    }
}
```

### Common Lisp FFI Example

```lisp
;;;; Example using CFFI (Common Foreign Function Interface)

(defpackage :llama-simple
  (:use :cl :cffi))

(in-package :llama-simple)

;; Load shared library
(define-foreign-library libllama-simple
  (:unix "libllama_simple.so")
  (:darwin "libllama_simple.dylib")
  (:windows "llama_simple.dll")
  (t (:default "libllama_simple")))

(use-foreign-library libllama-simple)

;; Define C structures
(defcstruct llama-simple-params
  (n-ctx :int32)
  (n-gpu-layers :int32)
  (n-threads :int32)
  (seed :int32)
  (verbose :bool))

(defcstruct llama-simple-gen-params
  (max-tokens :int32)
  (temperature :float)
  (top-p :float)
  (top-k :int32)
  (repeat-penalty :float)
  (stop-words :pointer)
  (num-stop-words :int32))

(defcstruct llama-simple-chat-msg
  (role :string)
  (content :string))

;; Define foreign functions
(defcfun ("llama_simple_init" %llama-simple-init) :pointer
  (params :pointer))

(defcfun ("llama_simple_load_model" %llama-simple-load-model) :int
  (ctx :pointer)
  (model-path :string)
  (params :pointer))

(defcfun ("llama_simple_format_chat" %llama-simple-format-chat) :string
  (ctx :pointer)
  (messages :pointer)
  (num-messages :int32)
  (add-generation-prompt :bool))

(defcfun ("llama_simple_prompt_json" %llama-simple-prompt-json) :string
  (ctx :pointer)
  (prompt :string)
  (params :pointer))

(defcfun ("llama_simple_free_string" %llama-simple-free-string) :void
  (str :pointer))

(defcfun ("llama_simple_free" %llama-simple-free) :void
  (ctx :pointer))

;; High-level Lisp API
(defclass llama-context ()
  ((handle :initarg :handle :accessor llama-handle)))

(defun make-llama-context (&key (n-ctx 2048) (n-gpu-layers -1))
  "Create a new llama.cpp context"
  (with-foreign-object (params '(:struct llama-simple-params))
    (setf (foreign-slot-value params '(:struct llama-simple-params) 'n-ctx) n-ctx)
    (setf (foreign-slot-value params '(:struct llama-simple-params) 'n-gpu-layers) n-gpu-layers)
    (let ((handle (%llama-simple-init params)))
      (when (null-pointer-p handle)
        (error "Failed to initialize llama context"))
      (make-instance 'llama-context :handle handle))))

(defun load-model (ctx model-path)
  "Load a GGUF model"
  (let ((result (%llama-simple-load-model (llama-handle ctx) model-path (null-pointer))))
    (when (/= result 0)
      (error "Failed to load model: ~A" model-path))))

(defun format-chat (ctx messages)
  "Format chat messages using model's template"
  (let* ((num-msgs (length messages))
         (msgs-array (foreign-alloc '(:struct llama-simple-chat-msg) :count num-msgs)))
    (loop for msg in messages
          for i from 0
          do (setf (foreign-slot-value (mem-aptr msgs-array '(:struct llama-simple-chat-msg) i)
                                       '(:struct llama-simple-chat-msg)
                                       'role)
                   (getf msg :role))
             (setf (foreign-slot-value (mem-aptr msgs-array '(:struct llama-simple-chat-msg) i)
                                       '(:struct llama-simple-chat-msg)
                                       'content)
                   (getf msg :content)))
    (let ((result (%llama-simple-format-chat (llama-handle ctx) msgs-array num-msgs t)))
      (foreign-free msgs-array)
      result)))

(defun prompt (ctx text &key (temperature 0.7) (max-tokens 512))
  "Generate a response (returns JSON string)"
  (with-foreign-object (params '(:struct llama-simple-gen-params))
    (setf (foreign-slot-value params '(:struct llama-simple-gen-params) 'max-tokens) max-tokens)
    (setf (foreign-slot-value params '(:struct llama-simple-gen-params) 'temperature) (coerce temperature 'single-float))
    (%llama-simple-prompt-json (llama-handle ctx) text params)))

;; Example usage
(defun example ()
  (let ((ctx (make-llama-context :n-ctx 2048 :n-gpu-layers 32)))
    (load-model ctx "/path/to/model.gguf")

    ;; Format chat messages
    (let* ((messages '((:role "system" :content "You are a helpful assistant.")
                      (:role "user" :content "What is the capital of France?")))
           (prompt-text (format-chat ctx messages))
           (response (prompt ctx prompt-text :temperature 0.7 :max-tokens 100)))
      (format t "Response: ~A~%" response))))
```

### FFI Considerations

1. **Memory Management**
   - All strings returned by API must be freed with `llama_simple_free_string()`
   - Context must be freed with `llama_simple_free()`
   - Use RAII wrappers in high-level bindings

2. **Thread Safety**
   - Context uses internal mutex for thread safety
   - Multiple contexts can be used from different threads
   - Same context should not be used concurrently

3. **Error Handling**
   - Functions return error codes (int)
   - Use `llama_simple_get_error()` for detailed messages
   - NULL returns indicate errors for pointer-returning functions

4. **ABI Compatibility**
   - Use `extern "C"` for all exported functions
   - Use C-compatible types (no C++ classes in API)
   - Fixed-size integers (`int32_t`, etc.)
   - Opaque pointers for complex types

### Build Configuration

**CMakeLists.txt addition:**
```cmake
# Simple C API wrapper library
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

# Set symbol visibility
set_target_properties(llama_simple PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Install library
install(TARGETS llama_simple
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES tools/ffi/llama_simple.h
    DESTINATION include
)
```

---

## Scenario 2: GStreamer Filter Plugin

### Overview

Create a GStreamer element that wraps llama.cpp, allowing integration into GStreamer pipelines for multimedia applications.

### Requirements

1. **Input Pad (Prompt)**: Accept text prompts with chat template formatting
2. **Output Pad (Tokens)**: Stream generated tokens as buffers
3. **Control Mechanism**: Load/unload models via properties or separate control pad
4. **Pipeline Integration**: Standard GStreamer element lifecycle
5. **State Management**: Handle NULL, READY, PAUSED, PLAYING states

### Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                   GStreamer Pipeline                          │
│                                                              │
│  ┌──────────┐      ┌────────────┐      ┌──────────────┐    │
│  │  Text    │─────▶│  llama     │─────▶│   Text       │    │
│  │  Source  │      │  Element   │      │   Sink       │    │
│  │          │      │            │      │   (File/     │    │
│  └──────────┘      │  sinkpad   │      │   Socket)    │    │
│                    │  srcpad    │      └──────────────┘    │
│  ┌──────────┐      │  ctrlpad   │                          │
│  │  Control │─────▶│  (props)   │                          │
│  │  Source  │      └────────────┘                          │
│  └──────────┘                                               │
└──────────────────────────────────────────────────────────────┘
```

### GStreamer Element Design

#### Element Class Structure

```c
// gstllama.h

#ifndef __GST_LLAMA_H__
#define __GST_LLAMA_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_LLAMA (gst_llama_get_type())
G_DECLARE_FINAL_TYPE(GstLlama, gst_llama, GST, LLAMA, GstElement)

struct _GstLlama {
    GstElement parent;

    // Pads
    GstPad * sinkpad;     // Text input (prompts)
    GstPad * srcpad;      // Text output (tokens)
    GstPad * ctrlpad;     // Control messages (optional)

    // Properties
    gchar * model_path;
    gint n_ctx;
    gint n_gpu_layers;
    gfloat temperature;
    gint max_tokens;
    gboolean stream_tokens;

    // State
    gboolean model_loaded;
    GMutex lock;
    GCond cond;

    // llama.cpp context (opaque)
    gpointer llama_ctx;

    // Generation state
    gboolean generating;
    GThread * gen_thread;
};

G_END_DECLS

#endif // __GST_LLAMA_H__
```

#### Pad Templates

```c
// Sink pad: accepts text/plain
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("text/plain, charset=utf-8")
);

// Source pad: produces text/plain
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("text/plain, charset=utf-8")
);

// Control pad: accepts application/x-llama-control
static GstStaticPadTemplate ctrl_template = GST_STATIC_PAD_TEMPLATE(
    "ctrl",
    GST_PAD_SINK,
    GST_PAD_REQUEST,  // Optional control pad
    GST_STATIC_CAPS("application/x-llama-control")
);
```

#### Properties

```c
enum {
    PROP_0,
    PROP_MODEL_PATH,
    PROP_N_CTX,
    PROP_N_GPU_LAYERS,
    PROP_TEMPERATURE,
    PROP_MAX_TOKENS,
    PROP_STREAM_TOKENS,
    PROP_LAST
};

static void
gst_llama_class_init(GstLlamaClass * klass)
{
    GObjectClass * gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass * gstelement_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_llama_set_property;
    gobject_class->get_property = gst_llama_get_property;
    gobject_class->finalize = gst_llama_finalize;

    // Properties
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

    // ... more properties

    // Pad templates
    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);
    gst_element_class_add_static_pad_template(gstelement_class, &ctrl_template);

    // Metadata
    gst_element_class_set_static_metadata(gstelement_class,
        "LLaMA Text Generator",
        "Filter/Text",
        "Generate text using llama.cpp models",
        "Your Name <your@email.com>");

    // State change handler
    gstelement_class->change_state = gst_llama_change_state;
}
```

#### Chain Function (Processing)

```c
static GstFlowReturn
gst_llama_chain(GstPad * pad, GstObject * parent, GstBuffer * buf)
{
    GstLlama * self = GST_LLAMA(parent);
    GstMapInfo map;
    GstFlowReturn ret = GST_FLOW_OK;

    // Check if model is loaded
    g_mutex_lock(&self->lock);
    if (!self->model_loaded) {
        GST_ERROR_OBJECT(self, "No model loaded");
        g_mutex_unlock(&self->lock);
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    // Extract prompt from buffer
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

    // Generate response
    if (self->stream_tokens) {
        // Stream tokens one by one
        ret = gst_llama_generate_stream(self, prompt);
    } else {
        // Generate complete response and send as single buffer
        ret = gst_llama_generate_complete(self, prompt);
    }

    g_free(prompt);
    g_mutex_unlock(&self->lock);

    return ret;
}

static GstFlowReturn
gst_llama_generate_stream(GstLlama * self, const gchar * prompt)
{
    // Callback context for token streaming
    struct TokenCallbackData {
        GstLlama * self;
        GstFlowReturn ret;
    };

    TokenCallbackData data = { self, GST_FLOW_OK };

    // Use llama_simple_prompt_stream with callback
    auto token_callback = [](void * user_data, const char * token_text,
                            int token_id, bool is_final) -> bool {
        TokenCallbackData * data = (TokenCallbackData *)user_data;
        GstLlama * self = data->self;

        if (strlen(token_text) > 0) {
            // Create buffer for this token
            GstBuffer * out_buf = gst_buffer_new_allocate(NULL, strlen(token_text), NULL);
            GstMapInfo map;

            gst_buffer_map(out_buf, &map, GST_MAP_WRITE);
            memcpy(map.data, token_text, strlen(token_text));
            gst_buffer_unmap(out_buf, &map);

            // Add timestamp
            GST_BUFFER_PTS(out_buf) = GST_CLOCK_TIME_NONE;
            GST_BUFFER_DTS(out_buf) = GST_CLOCK_TIME_NONE;

            // Push buffer downstream
            GstFlowReturn ret = gst_pad_push(self->srcpad, out_buf);
            if (ret != GST_FLOW_OK) {
                data->ret = ret;
                return false;  // Stop generation
            }
        }

        if (is_final) {
            // Send EOS or final marker
            GST_DEBUG_OBJECT(self, "Generation complete");
        }

        return true;  // Continue generation
    };

    // Call llama_simple API (assumes we have a simple C wrapper)
    llama_simple_gen_params params;
    params.temperature = self->temperature;
    params.max_tokens = self->max_tokens;
    params.top_p = 0.95;
    params.top_k = 40;
    params.repeat_penalty = 1.1;
    params.stop_words = NULL;
    params.num_stop_words = 0;

    int result = llama_simple_prompt_stream(
        (llama_simple_context *)self->llama_ctx,
        prompt,
        &params,
        token_callback,
        &data
    );

    if (result != 0) {
        GST_ERROR_OBJECT(self, "Generation failed");
        return GST_FLOW_ERROR;
    }

    return data.ret;
}
```

#### State Changes

```c
static GstStateChangeReturn
gst_llama_change_state(GstElement * element, GstStateChange transition)
{
    GstLlama * self = GST_LLAMA(element);
    GstStateChangeReturn ret;

    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            // Initialize llama.cpp backend
            GST_DEBUG_OBJECT(self, "Initializing llama backend");
            llama_backend_init();

            // Create context
            llama_simple_params params;
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
                int result = llama_simple_load_model(
                    (llama_simple_context *)self->llama_ctx,
                    self->model_path,
                    &params
                );
                if (result != 0) {
                    GST_ERROR_OBJECT(self, "Failed to load model: %s", self->model_path);
                    return GST_STATE_CHANGE_FAILURE;
                }
                self->model_loaded = TRUE;
            }
            break;

        case GST_STATE_CHANGE_READY_TO_PAUSED:
            // Prepare for processing
            self->generating = FALSE;
            break;

        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            // Start processing
            break;

        default:
            break;
    }

    // Chain up to parent class
    ret = GST_ELEMENT_CLASS(gst_llama_parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    switch (transition) {
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            // Pause processing
            break;

        case GST_STATE_CHANGE_PAUSED_TO_READY:
            // Stop processing
            if (self->generating) {
                // Cancel ongoing generation
                self->generating = FALSE;
            }
            break;

        case GST_STATE_CHANGE_READY_TO_NULL:
            // Cleanup
            if (self->llama_ctx) {
                if (self->model_loaded) {
                    llama_simple_unload_model((llama_simple_context *)self->llama_ctx);
                    self->model_loaded = FALSE;
                }
                llama_simple_free((llama_simple_context *)self->llama_ctx);
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

#### Control Pad (Dynamic Model Loading)

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

    // Parse control message (simple key=value format or JSON)
    // Format: "load:/path/to/model.gguf" or "unload"
    gchar * cmd = g_strndup((const gchar *)map.data, map.size);
    gst_buffer_unmap(buf, &map);
    gst_buffer_unref(buf);

    GST_DEBUG_OBJECT(self, "Control command: %s", cmd);

    g_mutex_lock(&self->lock);

    if (g_str_has_prefix(cmd, "load:")) {
        const gchar * model_path = cmd + 5;  // Skip "load:"

        // Unload current model if loaded
        if (self->model_loaded) {
            llama_simple_unload_model((llama_simple_context *)self->llama_ctx);
            self->model_loaded = FALSE;
        }

        // Load new model
        llama_simple_params params;
        params.n_ctx = self->n_ctx;
        params.n_gpu_layers = self->n_gpu_layers;
        params.n_threads = -1;
        params.seed = -1;
        params.verbose = FALSE;

        int result = llama_simple_load_model(
            (llama_simple_context *)self->llama_ctx,
            model_path,
            &params
        );

        if (result == 0) {
            self->model_loaded = TRUE;
            g_free(self->model_path);
            self->model_path = g_strdup(model_path);
            GST_INFO_OBJECT(self, "Loaded model: %s", model_path);
        } else {
            GST_ERROR_OBJECT(self, "Failed to load model: %s", model_path);
        }

    } else if (g_str_equal(cmd, "unload")) {
        if (self->model_loaded) {
            llama_simple_unload_model((llama_simple_context *)self->llama_ctx);
            self->model_loaded = FALSE;
            GST_INFO_OBJECT(self, "Unloaded model");
        }
    } else {
        GST_WARNING_OBJECT(self, "Unknown control command: %s", cmd);
    }

    g_free(cmd);
    g_mutex_unlock(&self->lock);

    return GST_FLOW_OK;
}
```

### GStreamer Pipeline Examples

#### Example 1: Simple Text Generation

```bash
gst-launch-1.0 \
    filesrc location=prompt.txt ! \
    llama model=/path/to/model.gguf n-ctx=2048 temperature=0.7 stream-tokens=true ! \
    filesink location=output.txt
```

#### Example 2: Interactive Pipeline with Control

```bash
gst-launch-1.0 \
    fdsrc fd=0 ! \
    queue ! \
    llama name=gen n-ctx=4096 ! \
    fdsink fd=1 \
    \
    fdsrc fd=3 ! \
    gen.ctrl
```

Control via file descriptor 3:
```bash
echo "load:/models/llama-2-7b.gguf" >&3
echo "Hello, how are you?" | ./pipeline
echo "unload" >&3
```

#### Example 3: Chat Application with Named Pipes

```bash
# Create FIFOs
mkfifo /tmp/llama_prompt
mkfifo /tmp/llama_output

# Start pipeline
gst-launch-1.0 \
    filesrc location=/tmp/llama_prompt ! \
    llama model=/models/chat-model.gguf temperature=0.8 stream-tokens=true ! \
    filesink location=/tmp/llama_output &

# Client
while true; do
    read -p "You: " prompt
    echo "$prompt" > /tmp/llama_prompt
    cat /tmp/llama_output &
done
```

#### Example 4: Integration with Audio (TTS Pipeline)

```bash
# Generate text from prompt, then convert to speech
gst-launch-1.0 \
    filesrc location=prompt.txt ! \
    llama model=/models/storyteller.gguf max-tokens=500 ! \
    tee name=t ! \
    queue ! filesink location=story.txt \
    t. ! queue ! \
    espeak ! \
    autoaudiosink
```

### Plugin Registration

```c
// gstllamaplugin.c

#include "gstllama.h"

static gboolean
plugin_init(GstPlugin * plugin)
{
    return gst_element_register(plugin, "llama",
        GST_RANK_NONE, GST_TYPE_LLAMA);
}

#define PACKAGE "llama_gstreamer"
#define VERSION "1.0.0"
#define LICENSE "MIT"
#define DESCRIPTION "LLaMA text generation GStreamer plugin"
#define ORIGIN "https://github.com/ggml-org/llama.cpp"

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    llama,
    DESCRIPTION,
    plugin_init,
    VERSION,
    LICENSE,
    PACKAGE,
    ORIGIN
)
```

### Build Configuration (Meson)

```meson
# meson.build for GStreamer plugin

project('gst-llama', 'c', 'cpp',
  version : '1.0.0',
  license : 'MIT',
  default_options : ['c_std=c11', 'cpp_std=c++17'])

# Dependencies
gst_dep = dependency('gstreamer-1.0', version : '>=1.20')
gst_base_dep = dependency('gstreamer-base-1.0', version : '>=1.20')
llama_dep = dependency('llama', fallback : ['llama', 'llama_dep'])

# Plugin sources
plugin_sources = [
  'gstllama.c',
  'gstllamaplugin.c',
]

# Build plugin
gst_llama = library('gstllama',
  plugin_sources,
  dependencies : [gst_dep, gst_base_dep, llama_dep],
  install : true,
  install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
)
```

### GStreamer Considerations

1. **Buffer Management**
   - Use `gst_buffer_new_allocate()` for output buffers
   - Proper reference counting with `gst_buffer_ref/unref()`
   - Map/unmap for memory access

2. **Thread Safety**
   - GStreamer is multi-threaded by design
   - Use `GMutex` for protecting llama.cpp context
   - Chain functions may be called from different threads

3. **Timestamps**
   - Set `GST_BUFFER_PTS` and `GST_BUFFER_DTS` appropriately
   - For text streams, timestamps may be less critical
   - Use `GST_CLOCK_TIME_NONE` if no timing info

4. **Caps Negotiation**
   - Define proper capabilities (text/plain with UTF-8)
   - Implement `fixate_caps` if needed
   - Handle format changes

5. **Flow Control**
   - Return `GST_FLOW_OK` on success
   - Return `GST_FLOW_ERROR` on errors
   - Handle `GST_FLOW_EOS` properly

---

## Shared Considerations

### Dependencies

Both scenarios require:

1. **llama.cpp Core Library**
   - libllama.so / llama.dll
   - Include headers (llama.h, common.h, chat.h)

2. **Build System Updates**
   - CMake targets for simple C wrapper
   - GStreamer: Meson or CMake with FindGStreamer

3. **Runtime Dependencies**
   - CUDA/ROCm for GPU support (optional)
   - Standard C++ runtime

### Performance

1. **Memory Overhead**
   - Wrapper adds minimal overhead (< 1%)
   - Main cost is llama.cpp model itself

2. **Latency**
   - FFI calls: ~100ns per call (negligible)
   - GStreamer buffering: ~1-10ms
   - Dominant cost: Model inference

3. **Throughput**
   - Limited by model size and hardware
   - Wrapper doesn't significantly impact tokens/sec

### Security

1. **Path Validation**
   - Validate model paths (no "../" traversal)
   - Whitelist allowed directories
   - Check GGUF magic numbers

2. **Resource Limits**
   - Limit max_tokens to prevent DoS
   - Timeout for long generations
   - Memory limits via n_ctx

3. **Input Sanitization**
   - Validate UTF-8 encoding
   - Limit prompt length
   - Escape special characters if needed

### Error Handling

1. **Graceful Degradation**
   - Return error codes, don't crash
   - Provide detailed error messages
   - Allow recovery from errors

2. **Logging**
   - Use llama.cpp's logging system
   - FFI: stderr or callback-based logging
   - GStreamer: GST_DEBUG, GST_INFO, GST_ERROR macros

### Testing

1. **Unit Tests**
   - Test each API function independently
   - Mock llama.cpp for fast tests
   - Valgrind for memory leaks

2. **Integration Tests**
   - Test with real models
   - Test FFI from actual Lisp/Python/etc.
   - Test GStreamer pipelines

3. **Performance Tests**
   - Benchmark FFI overhead
   - Benchmark GStreamer buffer handling
   - Profile with perf/gprof

---

## Implementation Roadmap

### Phase 1: C API Wrapper (FFI Foundation)

**Duration:** 1-2 weeks

1. **Create Simple C API** (3-4 days)
   - Implement `llama_simple.h` interface
   - Wrap core llama.cpp functions
   - Handle memory management
   - Add error handling

2. **Chat Template Integration** (2 days)
   - Expose `common_chat_templates_apply()`
   - Format messages with Jinja
   - Return formatted prompts

3. **Token Streaming** (2-3 days)
   - Implement callback-based streaming
   - Handle generation loop
   - Proper cancellation

4. **Testing & Documentation** (2 days)
   - Unit tests for C API
   - Example programs in C
   - API documentation

**Deliverables:**
- `tools/ffi/llama_simple.h`
- `tools/ffi/llama_simple.cpp`
- `tools/ffi/examples/simple_example.c`
- `tools/ffi/README.md`

### Phase 2: Common Lisp FFI Bindings

**Duration:** 1 week

1. **CFFI Bindings** (2-3 days)
   - Define foreign structures
   - Wrap C functions
   - Handle string encoding (UTF-8)

2. **High-Level Lisp API** (2 days)
   - CLOS wrapper classes
   - Automatic memory management
   - Lispy interface

3. **Examples & Documentation** (2 days)
   - Simple chat example
   - Streaming example
   - Package documentation

**Deliverables:**
- `tools/ffi/lisp/llama-simple.lisp`
- `tools/ffi/lisp/examples/chat.lisp`
- `tools/ffi/lisp/README.md`

### Phase 3: GStreamer Plugin

**Duration:** 2-3 weeks

1. **Basic Element Structure** (3-4 days)
   - GObject boilerplate
   - Pad templates
   - Properties
   - State changes

2. **Text Processing** (3-4 days)
   - Chain function for prompts
   - Token streaming to output pad
   - Buffer management

3. **Control Pad** (2-3 days)
   - Model load/unload via control messages
   - Dynamic reconfiguration
   - Error reporting

4. **Testing & Examples** (3-4 days)
   - Unit tests with gst-check
   - Pipeline examples
   - Documentation

**Deliverables:**
- `tools/gstreamer/gstllama.h`
- `tools/gstreamer/gstllama.c`
- `tools/gstreamer/meson.build`
- `tools/gstreamer/examples/`
- `tools/gstreamer/README.md`

### Phase 4: Integration & Polish

**Duration:** 1 week

1. **Cross-Platform Testing** (2-3 days)
   - Linux, macOS, Windows
   - Different architectures (x86_64, ARM)
   - Different Lisp implementations (SBCL, CCL, ECL)

2. **Performance Optimization** (2 days)
   - Profile FFI overhead
   - Optimize buffer allocations
   - Reduce copies

3. **Documentation** (2 days)
   - Architecture overview
   - Integration guide
   - Best practices

**Deliverables:**
- `docs/FFI_INTEGRATION.md`
- `docs/GSTREAMER_INTEGRATION.md`
- CI/CD test suite

---

## Estimated Effort

| Component | Lines of Code | Time | Complexity |
|-----------|--------------|------|------------|
| Simple C API | ~800 LOC | 1 week | Medium |
| Common Lisp Bindings | ~400 LOC | 1 week | Low |
| GStreamer Plugin | ~1200 LOC | 2-3 weeks | High |
| Tests & Docs | ~600 LOC | 1 week | Low |
| **Total** | **~3000 LOC** | **5-6 weeks** | **Medium** |

---

## Next Steps

1. **Review this analysis** - Validate approach and requirements
2. **Choose starting point** - FFI wrapper or GStreamer (FFI recommended first)
3. **Set up build infrastructure** - CMake/Meson targets
4. **Begin Phase 1** - Implement simple C API wrapper

---

## Questions for Consideration

1. **FFI Wrapper**
   - Should we support other languages (Python, Ruby, Node.js)?
   - Do we need async/non-blocking API variants?
   - Should we provide a shared library ABI guarantee?

2. **GStreamer Plugin**
   - Do we need bidirectional communication (user feedback during generation)?
   - Should we support multiple concurrent prompts (batch processing)?
   - Do we need CUDA stream integration for zero-copy?

3. **General**
   - Should this be upstreamed to main llama.cpp repo?
   - Do we need versioning/compatibility guarantees?
   - What's the target use case (education, production, research)?

---

**End of Analysis**
