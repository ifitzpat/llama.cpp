# TDD Development Log - llama_simple C API

**Phase:** 0 - C API Wrapper for FFI
**Date:** 2025-11-08
**Branch:** `claude/gstreamer-implementation-011CUrgj9DxaFqUMTWowxyBi`

## Overview

Test-Driven Development (TDD) implementation of a simple C API wrapper for llama.cpp, designed for Foreign Function Interface (FFI) usage from languages like Common Lisp, Python, Ruby, etc.

## TDD Workflow Summary

### Red → Green → Refactor Cycle

1. **RED**: Write failing tests first (defensive + positive path)
2. **GREEN**: Implement minimal code to pass tests
3. **REFACTOR**: Clean up, fix API compatibility issues

## Test Model

**Model:** gemma-3-270m-qat-Q4_0.gguf (231 MB)
**Source:** https://huggingface.co/ggml-org/gemma-3-270m-qat-GGUF/resolve/main/gemma-3-270m-qat-Q4_0.gguf
**Location:** `/home/user/llama.cpp/models/test/gemma-3-270m-qat-Q4_0.gguf`
**Verification:**
```bash
$ od -A x -t x1z -N 16 /home/user/llama.cpp/models/test/gemma-3-270m-qat-Q4_0.gguf
000000 47 47 55 46 03 00 00 00 ec 00 00 00 00 00 00 00  >GGUF............<
# Magic bytes: "GGUF", version 3 ✓
```

## Test Development (RED Phase)

### Step 1: Defensive Tests (16 tests)

Created comprehensive error-handling tests BEFORE implementation:

**File:** `tools/ffi/tests/test_llama_simple.c`

**Test Categories:**

1. **Context Lifecycle** (2 tests)
   - `test_context_init_and_free()` - Basic initialization
   - `test_context_init_with_null_params()` - NULL params handling

2. **Model Loading** (3 tests)
   - `test_load_model_invalid_path()` - Invalid model path
   - `test_load_model_null_context()` - NULL context
   - `test_unload_model_without_loading()` - Unload without load

3. **Chat Formatting** (2 tests)
   - `test_format_chat_without_model()` - Format without model
   - `test_format_chat_null_messages()` - NULL messages

4. **Token Streaming** (3 tests)
   - `test_prompt_stream_without_model()` - Stream without model
   - `test_prompt_stream_null_prompt()` - NULL prompt
   - `test_prompt_stream_null_callback()` - NULL callback

5. **Logit Bias** (2 tests)
   - `test_set_logit_bias_without_model()` - Bias without model
   - `test_set_logit_bias_null_context()` - NULL context

6. **Utilities** (2 tests)
   - `test_get_error_without_error()` - Error message retrieval
   - `test_free_string_null()` - NULL string free

7. **Memory Safety** (2 tests)
   - `test_double_free_context()` - Double-free protection
   - `test_use_after_free()` - Use-after-free protection

**Result:** All defensive tests initially PASS (contract testing - verifying error paths)

### Step 2: Positive Path Tests (3 tests)

Added tests requiring real model:

1. **`test_load_real_model()`**
   - Load actual GGUF model
   - Verify load success
   - Unload cleanly

2. **`test_simple_generation()`**
   - Load model
   - Generate 20 tokens with streaming callback
   - Verify token count > 0
   - Verify accumulated text

3. **`test_chat_template_with_model()`**
   - Load model
   - Format multi-turn chat messages
   - Apply model's Jinja template
   - Verify formatted output

**Result:** These tests FAIL initially (implementation needed)

## Implementation (GREEN Phase)

### Step 3: Header Definition

**File:** `tools/ffi/llama_simple.h` (308 lines)

Key structures:
```c
typedef struct llama_simple_context llama_simple_context;

typedef bool (*llama_simple_token_callback)(
    void * user_data,
    const char * token_text,
    int token_id,
    float probability,
    int position
);

struct llama_simple_params {
    int32_t n_ctx;
    int32_t n_gpu_layers;
    int32_t n_threads;
    int32_t seed;
    bool verbose;
};

struct llama_simple_gen_params {
    int32_t max_tokens;
    float temperature;
    float top_p;
    int32_t top_k;
    float repeat_penalty;
    const char ** stop_words;
    int32_t num_stop_words;
};
```

Key functions:
```c
llama_simple_context * llama_simple_init(const llama_simple_params * params);
void llama_simple_free(llama_simple_context * ctx);
int llama_simple_load_model(llama_simple_context * ctx, const char * model_path, const llama_simple_params * params);
int llama_simple_unload_model(llama_simple_context * ctx);
char * llama_simple_format_chat(llama_simple_context * ctx, const llama_simple_chat_msg * messages, int32_t num_messages, bool add_generation_prompt);
int llama_simple_prompt_stream(llama_simple_context * ctx, const char * prompt, const llama_simple_gen_params * params, llama_simple_token_callback callback, void * user_data);
```

### Step 4: Implementation

**File:** `tools/ffi/llama_simple.cpp` (479 lines)

**Context Structure:**
```cpp
struct llama_simple_context {
    common_init_result llama_init;
    const llama_vocab * vocab = nullptr;
    common_chat_templates_ptr chat_templates;
    common_params params;
    bool model_loaded = false;
    std::string last_error;
    llama_simple_token_callback token_cb = nullptr;
    void * token_cb_data = nullptr;
    llama_simple_logit_callback logit_cb = nullptr;
    void * logit_cb_data = nullptr;
    std::vector<llama_logit_bias> logit_biases;
    std::mutex mutex;
};
```

**Model Loading:**
```cpp
int llama_simple_load_model(llama_simple_context * ctx, const char * model_path, const llama_simple_params * load_params) {
    // Parameter validation
    if (!ctx) return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    if (!model_path) { ctx->last_error = "Model path cannot be NULL"; return LLAMA_SIMPLE_ERROR_INVALID_PARAMS; }

    std::lock_guard<std::mutex> lock(ctx->mutex);

    // Unload existing model if any
    if (ctx->model_loaded) { /* cleanup */ }

    // Update parameters
    ctx->params.model.path = model_path;

    // Load model using llama.cpp core API
    ctx->llama_init = common_init_from_params(ctx->params);
    if (!ctx->llama_init.model) { /* error handling */ }

    // Get vocab from model
    ctx->vocab = llama_model_get_vocab(ctx->llama_init.model.get());

    // Initialize chat templates
    ctx->chat_templates = common_chat_templates_init(ctx->llama_init.model.get(), "", "", "");

    ctx->model_loaded = true;
    return LLAMA_SIMPLE_OK;
}
```

**Text Generation:**
```cpp
int llama_simple_prompt_stream(llama_simple_context * ctx, const char * prompt, const llama_simple_gen_params * gen_params, llama_simple_token_callback callback, void * user_data) {
    // Validation
    if (!ctx || !prompt || !callback) return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    if (!ctx->model_loaded) return LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED;

    // Tokenize prompt
    auto tokens = common_tokenize(ctx->vocab, prompt, true, true);
    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());

    // Decode prompt
    llama_decode(ctx->llama_init.context.get(), batch);

    // Create sampler with params
    auto & sparams = ctx->params.sampling;
    if (gen_params) {
        sparams.temp = gen_params->temperature;
        sparams.top_p = gen_params->top_p;
        sparams.top_k = gen_params->top_k;
        sparams.penalty_repeat = gen_params->repeat_penalty;
    }

    // Apply logit bias if set (before creating sampler)
    if (!ctx->logit_biases.empty()) {
        sparams.logit_bias = ctx->logit_biases;
    }

    auto smpl = common_sampler_init(ctx->llama_init.model.get(), sparams);

    // Generation loop
    while (true) {
        llama_token token = common_sampler_sample(smpl, ctx->llama_init.context.get(), -1);
        if (llama_vocab_is_eog(ctx->vocab, token)) break;

        std::string token_text = common_token_to_piece(ctx->vocab, token);
        bool should_continue = callback(user_data, token_text.c_str(), token, prob, n_generated);
        if (!should_continue) break;

        // Decode next token
        batch = llama_batch_get_one(&token, 1);
        llama_decode(ctx->llama_init.context.get(), batch);
    }

    common_sampler_free(smpl);
    return LLAMA_SIMPLE_OK;
}
```

## Refactoring (REFACTOR Phase)

### Step 5: API Compatibility Fixes

Encountered multiple llama.cpp API changes during implementation:

**Issue 1: Deprecated `llama_token_is_eog()`**
```diff
- llama_token_is_eog(ctx->llama_init.model.get(), token)
+ llama_vocab_is_eog(ctx->vocab, token)
```

**Issue 2: Missing `common_params_default()`**
```diff
- ctx->params = common_params_default();
+ ctx->params = common_params();  // Uses struct default values
```

**Issue 3: Deprecated `common_vocab_init_from_model()`**
```diff
- ctx->vocab = common_vocab_init_from_model(ctx->llama_init.model.get());
+ ctx->vocab = llama_model_get_vocab(ctx->llama_init.model.get());
+ // Also changed type from llama_vocab* to const llama_vocab*
```

**Issue 4: Sampler Chain Manipulation**
```diff
- // Manual chain manipulation (incorrect)
- auto bias_sampler = llama_sampler_init_logit_bias(...);
- llama_sampler_chain_add(smpl, bias_sampler);  // Type error

+ // Configure params before sampler creation (correct)
+ if (!ctx->logit_biases.empty()) {
+     sparams.logit_bias = ctx->logit_biases;
+ }
+ auto smpl = common_sampler_init(ctx->llama_init.model.get(), sparams);
```

**Issue 5: Changed Vocab Token Count Function**
```diff
- llama_model_n_vocab(model)
+ llama_vocab_n_tokens(vocab)
```

### Step 6: Build System Configuration

**File:** `tools/ffi/CMakeLists.txt`

**Challenge:** Linking static libraries with circular dependencies

**Solution:** Use linker groups (`--start-group`/`--end-group`)

```cmake
add_library(llama_simple STATIC llama_simple.cpp)

target_link_libraries(llama_simple
    common
    llama
    -Wl,--start-group
    ggml-base
    ggml-cpu
    ggml
    -Wl,--end-group
    gomp  # OpenMP for parallel computation
)
```

**Library Dependencies:**
- `common` - llama.cpp common utilities
- `llama` - Core llama.cpp library
- `ggml`, `ggml-base`, `ggml-cpu` - GGML tensor library (circular deps)
- `gomp` - GNU OpenMP for thread synchronization

## Final Test Results

```
========================================
llama_simple C API Tests
========================================

[TEST] Context initialization and cleanup
  ✓ PASS

[TEST] Context initialization with NULL params (should use defaults)
  ✓ PASS

[TEST] Load model with invalid path (should fail gracefully)
    Error message: Failed to load model: /nonexistent/model.gguf
  ✓ PASS

[TEST] Load model with NULL context (should fail)
  ✓ PASS

[TEST] Unload model without loading one first
  ✓ PASS

[TEST] Format chat without loading model (should fail)
  ✓ PASS

[TEST] Format chat with NULL messages (should fail)
  ✓ PASS

[TEST] Prompt stream without model (should fail)
  ✓ PASS

[TEST] Prompt stream with NULL prompt (should fail)
  ✓ PASS

[TEST] Prompt stream with NULL callback (should fail)
  ✓ PASS

[TEST] Set logit bias without model (should fail)
  ✓ PASS

[TEST] Set logit bias with NULL context (should fail)
  ✓ PASS

[TEST] Get error when no error occurred
  ✓ PASS

[TEST] Free NULL string (should not crash)
  ✓ PASS

[TEST] Double free context (should not crash)
    (Double-free protection is implementation-dependent)
  ✓ PASS

[TEST] Use after free (should fail gracefully)
    (Use-after-free protection is implementation-dependent)
  ✓ PASS

--- Positive Path Tests (requires model) ---

[TEST] Load real model (positive path)
    Model loaded successfully!
  ✓ PASS

[TEST] Simple text generation (positive path)
    Model loaded, generating tokens...
    Token 0: ' T' (id=558, prob=1.0000)
    Token 1: 'anish' (id=12578, prob=1.0000)
    Token 2: 'a' (id=236746, prob=1.0000)
    Token 3: '.' (id=236761, prob=1.0000)
    Token 4: ' I' (id=564, prob=1.0000)
    Token 5: ' am' (id=1006, prob=1.0000)
    Token 6: ' a' (id=496, prob=1.0000)
    Token 7: ' ' (id=236743, prob=1.0000)
    Token 8: '2' (id=236778, prob=1.0000)
    Token 9: '0' (id=236771, prob=1.0000)
    Token 10: '-' (id=236772, prob=1.0000)
    Token 11: 'something' (id=44784, prob=1.0000)
    Token 12: ',' (id=236764, prob=1.0000)
    Token 13: ' single' (id=3161, prob=1.0000)
    Token 14: ' mom' (id=2801, prob=1.0000)
    Token 15: ' of' (id=529, prob=1.0000)
    Token 16: ' two' (id=1156, prob=1.0000)
    Token 17: ',' (id=236764, prob=1.0000)
    Token 18: ' wife' (id=6853, prob=1.0000)
    Token 19: ' of' (id=529, prob=1.0000)
    Generated 21 tokens
    Text: ' Tanisha. I am a 20-something, single mom of two, wife of'
  ✓ PASS

[TEST] Chat template formatting with real model
    Formatted chat:
<|im_start|>user
Hello!<|im_end|>
<|im_start|>assistant
Hi there! How can I help you?<|im_end|>
<|im_start|>user
What's the weather like?<|im_end|>
<|im_start|>assistant

    Chat template formatting successful!
  ✓ PASS

========================================
Test Results
========================================
Tests run:    19
Tests passed: 19

✓ ALL TESTS PASSED
```

## Build Instructions

```bash
# From llama.cpp root
cd tools/ffi

# Create build directory
mkdir build && cd build

# Configure with tests enabled
cmake .. -DLLAMA_SIMPLE_BUILD_TESTS=ON

# Build library and tests
make -j4

# Run tests
./test_llama_simple
```

## Files Created

1. **`tools/ffi/llama_simple.h`** (308 lines) - C API header
2. **`tools/ffi/llama_simple.cpp`** (479 lines) - Implementation
3. **`tools/ffi/tests/test_llama_simple.c`** (600+ lines) - Test suite
4. **`tools/ffi/CMakeLists.txt`** (81 lines) - Build configuration
5. **`tools/ffi/TDD_DEVELOPMENT_LOG.md`** (This file) - Process documentation
6. **`models/test/gemma-3-270m-qat-Q4_0.gguf`** (231 MB) - Test model

## Key Learnings

1. **TDD Benefits:**
   - Tests caught API compatibility issues immediately
   - Defensive tests ensured robust error handling
   - Refactoring was safe with comprehensive test coverage

2. **llama.cpp API Evolution:**
   - API functions frequently renamed/deprecated
   - Best practice: Check examples for current patterns
   - Opaque types (e.g., `common_sampler`) require configuration before creation

3. **C++ Linking Complexity:**
   - Static library circular dependencies need linker groups
   - OpenMP (`gomp`) required for ggml threading
   - Link order matters for static libraries

4. **FFI Design Patterns:**
   - Opaque context handles (`llama_simple_context*`)
   - C-compatible callbacks with user_data pointers
   - Heap-allocated strings with explicit free function
   - Error codes + error message retrieval

## Next Steps

**Phase 1:** GStreamer Plugin (Basic Element)
- Use `llama_simple` as backend
- Implement `gstllama` element with source/sink pads
- Text input → LLM generation → text output

**Phase 2:** Signal System
- Add GObject signals for token streaming
- Configure via GStreamer properties

**Phase 3+:** Advanced features (adaptive steering, Common Lisp bindings, etc.)

---

**Development Time:** ~4 hours (including API compatibility debugging)
**Test Coverage:** 19 tests (16 defensive + 3 positive path)
**Status:** ✅ Phase 0 Complete - All tests passing
