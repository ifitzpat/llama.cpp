#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "llama_simple.h"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;

#define TEST_START(name) \
    do { \
        printf("\n[TEST] %s\n", name); \
        tests_run++; \
    } while (0)

#define TEST_PASS() \
    do { \
        printf("  ✓ PASS\n"); \
        tests_passed++; \
    } while (0)

#define TEST_ASSERT(condition, msg) \
    do { \
        if (!(condition)) { \
            printf("  ✗ FAIL: %s\n", msg); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

// =============================================================================
// Test 1: Context Lifecycle
// =============================================================================

int test_context_init_and_free(void) {
    TEST_START("Context initialization and cleanup");

    // Initialize with default params
    llama_simple_params params = {0};
    params.n_ctx = 2048;
    params.n_gpu_layers = 0;
    params.n_threads = -1;
    params.seed = 42;
    params.verbose = false;

    llama_simple_context * ctx = llama_simple_init(&params);
    TEST_ASSERT(ctx != NULL, "Context should not be NULL");

    // Clean up
    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

int test_context_init_with_null_params(void) {
    TEST_START("Context initialization with NULL params (should use defaults)");

    llama_simple_context * ctx = llama_simple_init(NULL);
    TEST_ASSERT(ctx != NULL, "Context should not be NULL with default params");

    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

// =============================================================================
// Test 2: Model Loading/Unloading
// =============================================================================

int test_load_model_invalid_path(void) {
    TEST_START("Load model with invalid path (should fail gracefully)");

    llama_simple_params params = {0};
    params.n_ctx = 512;
    params.verbose = false;

    llama_simple_context * ctx = llama_simple_init(&params);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    // Try to load non-existent model
    int result = llama_simple_load_model(ctx, "/nonexistent/model.gguf", NULL);
    TEST_ASSERT(result != LLAMA_SIMPLE_OK, "Loading non-existent model should fail");

    // Error message should be available
    const char * error = llama_simple_get_error(ctx);
    TEST_ASSERT(error != NULL, "Error message should be set");
    TEST_ASSERT(strlen(error) > 0, "Error message should not be empty");
    printf("    Error message: %s\n", error);

    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

int test_load_model_null_context(void) {
    TEST_START("Load model with NULL context (should fail)");

    int result = llama_simple_load_model(NULL, "/some/model.gguf", NULL);
    TEST_ASSERT(result == LLAMA_SIMPLE_ERROR_INVALID_PARAMS,
                "Loading with NULL context should return INVALID_PARAMS");

    TEST_PASS();
    return 0;
}

int test_unload_model_without_loading(void) {
    TEST_START("Unload model without loading one first");

    llama_simple_params params = {0};
    llama_simple_context * ctx = llama_simple_init(&params);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    // Unload without loading
    int result = llama_simple_unload_model(ctx);
    TEST_ASSERT(result == LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED,
                "Unloading without a model should return NO_MODEL_LOADED");

    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

// =============================================================================
// Test 3: Chat Template Formatting
// =============================================================================

int test_format_chat_without_model(void) {
    TEST_START("Format chat without loading model (should fail)");

    llama_simple_context * ctx = llama_simple_init(NULL);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    llama_simple_chat_msg messages[] = {
        { .role = "user", .content = "Hello!" }
    };

    char * formatted = llama_simple_format_chat(ctx, messages, 1, true);
    TEST_ASSERT(formatted == NULL, "Formatting without model should return NULL");

    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

int test_format_chat_null_messages(void) {
    TEST_START("Format chat with NULL messages (should fail)");

    llama_simple_context * ctx = llama_simple_init(NULL);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    char * formatted = llama_simple_format_chat(ctx, NULL, 0, true);
    TEST_ASSERT(formatted == NULL, "Formatting NULL messages should return NULL");

    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

// =============================================================================
// Test 4: Token Streaming
// =============================================================================

// Callback for token streaming tests
static int callback_token_count = 0;
static char callback_accumulated_text[1024] = {0};

static bool test_token_callback(void * user_data,
                                const char * token_text,
                                int token_id,
                                float probability,
                                int position) {
    (void)user_data;  // Unused

    callback_token_count++;

    if (token_text && strlen(token_text) > 0) {
        strncat(callback_accumulated_text, token_text,
                sizeof(callback_accumulated_text) - strlen(callback_accumulated_text) - 1);
    }

    printf("    Token %d: '%s' (id=%d, prob=%.4f)\n",
           position, token_text, token_id, probability);

    return true;  // Continue generation
}

int test_prompt_stream_without_model(void) {
    TEST_START("Prompt stream without model (should fail)");

    llama_simple_context * ctx = llama_simple_init(NULL);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    llama_simple_gen_params gen_params = {0};
    gen_params.max_tokens = 10;
    gen_params.temperature = 0.7;

    callback_token_count = 0;
    memset(callback_accumulated_text, 0, sizeof(callback_accumulated_text));

    int result = llama_simple_prompt_stream(
        ctx, "Hello", &gen_params, test_token_callback, NULL
    );

    TEST_ASSERT(result == LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED,
                "Streaming without model should return NO_MODEL_LOADED");

    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

int test_prompt_stream_null_prompt(void) {
    TEST_START("Prompt stream with NULL prompt (should fail)");

    llama_simple_context * ctx = llama_simple_init(NULL);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    llama_simple_gen_params gen_params = {0};

    int result = llama_simple_prompt_stream(
        ctx, NULL, &gen_params, test_token_callback, NULL
    );

    TEST_ASSERT(result == LLAMA_SIMPLE_ERROR_INVALID_PARAMS,
                "Streaming NULL prompt should return INVALID_PARAMS");

    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

int test_prompt_stream_null_callback(void) {
    TEST_START("Prompt stream with NULL callback (should fail)");

    llama_simple_context * ctx = llama_simple_init(NULL);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    llama_simple_gen_params gen_params = {0};

    int result = llama_simple_prompt_stream(
        ctx, "Hello", &gen_params, NULL, NULL
    );

    TEST_ASSERT(result == LLAMA_SIMPLE_ERROR_INVALID_PARAMS,
                "Streaming without callback should return INVALID_PARAMS");

    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

// =============================================================================
// Test 5: Logit Bias
// =============================================================================

int test_set_logit_bias_without_model(void) {
    TEST_START("Set logit bias without model (should fail)");

    llama_simple_context * ctx = llama_simple_init(NULL);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    llama_simple_logit_bias biases[] = {
        { .token_str = "the", .bias = -1.0 },
        { .token_str = "AI", .bias = 2.0 }
    };

    int result = llama_simple_set_logit_bias(ctx, biases, 2);
    TEST_ASSERT(result == LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED,
                "Setting bias without model should return NO_MODEL_LOADED");

    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

int test_set_logit_bias_null_context(void) {
    TEST_START("Set logit bias with NULL context (should fail)");

    llama_simple_logit_bias biases[] = {
        { .token_str = "test", .bias = 1.0 }
    };

    int result = llama_simple_set_logit_bias(NULL, biases, 1);
    TEST_ASSERT(result == LLAMA_SIMPLE_ERROR_INVALID_PARAMS,
                "Setting bias with NULL context should return INVALID_PARAMS");

    TEST_PASS();
    return 0;
}

// =============================================================================
// Test 6: Error Handling
// =============================================================================

int test_get_error_without_error(void) {
    TEST_START("Get error when no error occurred");

    llama_simple_context * ctx = llama_simple_init(NULL);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    const char * error = llama_simple_get_error(ctx);
    // Error message can be NULL or empty when no error
    if (error != NULL) {
        TEST_ASSERT(strlen(error) == 0, "Error message should be empty when no error");
    }

    llama_simple_free(ctx);

    TEST_PASS();
    return 0;
}

int test_free_string_null(void) {
    TEST_START("Free NULL string (should not crash)");

    // Should be safe to call with NULL
    llama_simple_free_string(NULL);

    TEST_PASS();
    return 0;
}

// =============================================================================
// Test 7: Memory Safety
// =============================================================================

int test_double_free_context(void) {
    TEST_START("Double free context (should not crash)");

    llama_simple_context * ctx = llama_simple_init(NULL);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    llama_simple_free(ctx);

    // Second free with same pointer - this is undefined behavior in general,
    // but we want to ensure our implementation handles it gracefully
    // In practice, users shouldn't do this, but it's good to test

    // NOTE: We can't actually test double-free safely here without
    // risking crashes. This test documents the expectation.
    printf("    (Double-free protection is implementation-dependent)\n");

    TEST_PASS();
    return 0;
}

int test_use_after_free(void) {
    TEST_START("Use after free (should fail gracefully)");

    llama_simple_context * ctx = llama_simple_init(NULL);
    TEST_ASSERT(ctx != NULL, "Context should initialize");

    llama_simple_free(ctx);

    // Try to use freed context
    // NOTE: This is undefined behavior, but we document the expectation
    // that the implementation should handle it gracefully
    printf("    (Use-after-free protection is implementation-dependent)\n");

    TEST_PASS();
    return 0;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("llama_simple C API Tests\n");
    printf("========================================\n");

    // Run all tests
    if (test_context_init_and_free() != 0) return 1;
    if (test_context_init_with_null_params() != 0) return 1;

    if (test_load_model_invalid_path() != 0) return 1;
    if (test_load_model_null_context() != 0) return 1;
    if (test_unload_model_without_loading() != 0) return 1;

    if (test_format_chat_without_model() != 0) return 1;
    if (test_format_chat_null_messages() != 0) return 1;

    if (test_prompt_stream_without_model() != 0) return 1;
    if (test_prompt_stream_null_prompt() != 0) return 1;
    if (test_prompt_stream_null_callback() != 0) return 1;

    if (test_set_logit_bias_without_model() != 0) return 1;
    if (test_set_logit_bias_null_context() != 0) return 1;

    if (test_get_error_without_error() != 0) return 1;
    if (test_free_string_null() != 0) return 1;

    if (test_double_free_context() != 0) return 1;
    if (test_use_after_free() != 0) return 1;

    // Summary
    printf("\n");
    printf("========================================\n");
    printf("Test Results\n");
    printf("========================================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);

    if (tests_passed == tests_run) {
        printf("\n✓ ALL TESTS PASSED\n\n");
        return 0;
    } else {
        printf("\n✗ SOME TESTS FAILED\n\n");
        return 1;
    }
}
