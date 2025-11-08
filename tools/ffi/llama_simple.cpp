/**
 * llama_simple.cpp - Implementation of simple C API wrapper
 *
 * This is a minimal stub implementation for TDD.
 * Tests should fail until we implement the actual functionality.
 */

#include "llama_simple.h"

#include <cstring>
#include <string>

// Stub implementation - just enough to compile
struct llama_simple_context {
    std::string last_error;
    bool        model_loaded = false;
};

extern "C" {

// Lifecycle
llama_simple_context * llama_simple_init(const llama_simple_params * params) {
    (void) params;  // Unused for now
    return new llama_simple_context();
}

void llama_simple_free(llama_simple_context * ctx) {
    delete ctx;
}

// Model management
int llama_simple_load_model(llama_simple_context * ctx, const char * model_path, const llama_simple_params * params) {
    if (!ctx) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }
    if (!model_path) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    (void) params;  // Unused for now

    // Stub: Always fail for now
    ctx->last_error = "Model loading not implemented yet";
    return LLAMA_SIMPLE_ERROR_MODEL_LOAD_FAILED;
}

int llama_simple_unload_model(llama_simple_context * ctx) {
    if (!ctx) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    if (!ctx->model_loaded) {
        return LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED;
    }

    // Stub: Mark as unloaded
    ctx->model_loaded = false;
    return LLAMA_SIMPLE_OK;
}

// Chat formatting
char * llama_simple_format_chat(llama_simple_context *        ctx,
                                const llama_simple_chat_msg * messages,
                                int32_t                       num_messages,
                                bool                          add_generation_prompt) {
    if (!ctx || !messages || num_messages <= 0) {
        return nullptr;
    }

    (void) add_generation_prompt;  // Unused for now

    // Stub: Return NULL (not implemented)
    return nullptr;
}

// Generation
char * llama_simple_prompt_json(llama_simple_context *          ctx,
                                const char *                    prompt,
                                const llama_simple_gen_params * params) {
    if (!ctx || !prompt) {
        return nullptr;
    }

    (void) params;  // Unused for now

    // Stub: Return NULL (not implemented)
    return nullptr;
}

int llama_simple_prompt_stream(llama_simple_context *          ctx,
                               const char *                    prompt,
                               const llama_simple_gen_params * params,
                               llama_simple_token_callback     callback,
                               void *                          user_data) {
    if (!ctx) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }
    if (!prompt) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }
    if (!callback) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    if (!ctx->model_loaded) {
        return LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED;
    }

    (void) params;     // Unused for now
    (void) user_data;  // Unused for now

    // Stub: Return error (not implemented)
    return LLAMA_SIMPLE_ERROR_GENERATION_FAILED;
}

// Advanced features
int llama_simple_set_logit_bias(llama_simple_context *          ctx,
                                const llama_simple_logit_bias * biases,
                                int32_t                         num_biases) {
    if (!ctx) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    if (!ctx->model_loaded) {
        return LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED;
    }

    (void) biases;      // Unused for now
    (void) num_biases;  // Unused for now

    // Stub: Return OK (not actually doing anything)
    return LLAMA_SIMPLE_OK;
}

int llama_simple_set_logit_callback(llama_simple_context *      ctx,
                                    llama_simple_logit_callback callback,
                                    void *                      user_data) {
    if (!ctx) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    (void) callback;   // Unused for now
    (void) user_data;  // Unused for now

    // Stub: Return OK (not actually doing anything)
    return LLAMA_SIMPLE_OK;
}

// Utility
const char * llama_simple_get_error(llama_simple_context * ctx) {
    if (!ctx) {
        return nullptr;
    }

    if (ctx->last_error.empty()) {
        return "";
    }

    return ctx->last_error.c_str();
}

char * llama_simple_get_model_info(llama_simple_context * ctx) {
    if (!ctx) {
        return nullptr;
    }

    // Stub: Return NULL (not implemented)
    return nullptr;
}

void llama_simple_free_string(char * str) {
    free(str);
}

}  // extern "C"
