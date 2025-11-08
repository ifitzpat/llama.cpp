/**
 * llama_simple.cpp - Full implementation of simple C API wrapper
 *
 * Integrates with llama.cpp core for actual model loading and generation.
 */

#include "llama_simple.h"

// llama.cpp headers
#include "chat.h"
#include "common.h"
#include "llama.h"
#include "sampling.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Context structure
struct llama_simple_context {
    // llama.cpp objects
    common_init_result        llama_init;
    const llama_vocab *       vocab = nullptr;
    common_chat_templates_ptr chat_templates;

    // Parameters
    common_params params;

    // State
    bool        model_loaded = false;
    std::string last_error;

    // Callbacks
    llama_simple_token_callback token_cb      = nullptr;
    void *                      token_cb_data = nullptr;
    llama_simple_logit_callback logit_cb      = nullptr;
    void *                      logit_cb_data = nullptr;

    // Logit bias
    std::vector<llama_logit_bias> logit_biases;

    // Thread safety
    std::mutex mutex;
};

extern "C" {

// =============================================================================
// Lifecycle Functions
// =============================================================================

llama_simple_context * llama_simple_init(const llama_simple_params * init_params) {
    auto ctx = new llama_simple_context();

    // Set default parameters (common_params has default values in struct definition)
    ctx->params = common_params();

    if (init_params) {
        if (init_params->n_ctx > 0) {
            ctx->params.n_ctx = init_params->n_ctx;
        }
        ctx->params.n_gpu_layers = init_params->n_gpu_layers;
        if (init_params->n_threads > 0) {
            ctx->params.cpuparams.n_threads       = init_params->n_threads;
            ctx->params.cpuparams_batch.n_threads = init_params->n_threads;
        }
        if (init_params->seed >= 0) {
            ctx->params.sampling.seed = init_params->seed;
        }
        ctx->params.verbosity = init_params->verbose ? 1 : 0;
    }

    // Initialize llama backend
    llama_backend_init();

    return ctx;
}

void llama_simple_free(llama_simple_context * ctx) {
    if (!ctx) {
        return;
    }

    // Unload model if loaded
    if (ctx->model_loaded) {
        llama_simple_unload_model(ctx);
    }

    llama_backend_free();

    delete ctx;
}

// =============================================================================
// Model Management
// =============================================================================

int llama_simple_load_model(llama_simple_context *      ctx,
                            const char *                model_path,
                            const llama_simple_params * load_params) {
    if (!ctx) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }
    if (!model_path) {
        ctx->last_error = "Model path cannot be NULL";
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);

    // Unload existing model if any
    if (ctx->model_loaded) {
        ctx->llama_init = common_init_result();
        ctx->vocab      = nullptr;
        ctx->chat_templates.reset();
        ctx->model_loaded = false;
    }

    // Update parameters
    ctx->params.model.path = model_path;

    if (load_params) {
        if (load_params->n_ctx > 0) {
            ctx->params.n_ctx = load_params->n_ctx;
        }
        if (load_params->n_gpu_layers >= 0) {
            ctx->params.n_gpu_layers = load_params->n_gpu_layers;
        }
        if (load_params->n_threads > 0) {
            ctx->params.cpuparams.n_threads       = load_params->n_threads;
            ctx->params.cpuparams_batch.n_threads = load_params->n_threads;
        }
    }

    // Load model
    try {
        ctx->llama_init = common_init_from_params(ctx->params);

        if (!ctx->llama_init.model) {
            ctx->last_error = "Failed to load model: " + std::string(model_path);
            return LLAMA_SIMPLE_ERROR_MODEL_LOAD_FAILED;
        }

        // Get vocab from model
        ctx->vocab = llama_model_get_vocab(ctx->llama_init.model.get());

        // Initialize chat templates
        ctx->chat_templates = common_chat_templates_init(ctx->llama_init.model.get(), "", "", "");

        ctx->model_loaded = true;
        ctx->last_error.clear();

        return LLAMA_SIMPLE_OK;

    } catch (const std::exception & e) {
        ctx->last_error = std::string("Exception loading model: ") + e.what();
        return LLAMA_SIMPLE_ERROR_MODEL_LOAD_FAILED;
    }
}

int llama_simple_unload_model(llama_simple_context * ctx) {
    if (!ctx) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);

    if (!ctx->model_loaded) {
        return LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED;
    }

    // Release resources
    ctx->llama_init = common_init_result();
    ctx->vocab      = nullptr;
    ctx->chat_templates.reset();
    ctx->model_loaded = false;
    ctx->last_error.clear();

    return LLAMA_SIMPLE_OK;
}

// =============================================================================
// Chat Template Formatting
// =============================================================================

char * llama_simple_format_chat(llama_simple_context *        ctx,
                                const llama_simple_chat_msg * messages,
                                int32_t                       num_messages,
                                bool                          add_generation_prompt) {
    if (!ctx || !messages || num_messages <= 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);

    if (!ctx->model_loaded || !ctx->chat_templates) {
        return nullptr;
    }

    try {
        // Convert C messages to C++ format
        std::vector<common_chat_msg> cpp_messages;
        for (int32_t i = 0; i < num_messages; i++) {
            common_chat_msg msg;
            msg.role    = messages[i].role;
            msg.content = messages[i].content;
            cpp_messages.push_back(msg);
        }

        // Apply chat template
        common_chat_templates_inputs inputs;
        inputs.messages              = cpp_messages;
        inputs.add_generation_prompt = add_generation_prompt;
        inputs.use_jinja             = true;

        common_chat_params result = common_chat_templates_apply(ctx->chat_templates.get(), inputs);

        // Return as heap-allocated C string
        return strdup(result.prompt.c_str());

    } catch (const std::exception & e) {
        ctx->last_error = std::string("Error formatting chat: ") + e.what();
        return nullptr;
    }
}

// =============================================================================
// Text Generation
// =============================================================================

char * llama_simple_prompt_json(llama_simple_context *          ctx,
                                const char *                    prompt,
                                const llama_simple_gen_params * params) {
    // TODO: Implement full JSON response generation
    (void) ctx;
    (void) prompt;
    (void) params;
    return nullptr;
}

int llama_simple_prompt_stream(llama_simple_context *          ctx,
                               const char *                    prompt,
                               const llama_simple_gen_params * gen_params,
                               llama_simple_token_callback     callback,
                               void *                          user_data) {
    if (!ctx) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }
    if (!prompt) {
        ctx->last_error = "Prompt cannot be NULL";
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }
    if (!callback) {
        ctx->last_error = "Callback cannot be NULL";
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);

    if (!ctx->model_loaded) {
        ctx->last_error = "No model loaded";
        return LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED;
    }

    try {
        // Tokenize prompt
        auto tokens = common_tokenize(ctx->vocab, prompt, true, true);

        if (tokens.empty()) {
            ctx->last_error = "Failed to tokenize prompt";
            return LLAMA_SIMPLE_ERROR_GENERATION_FAILED;
        }

        // Create batch
        llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());

        // Decode prompt
        if (llama_decode(ctx->llama_init.context.get(), batch) != 0) {
            ctx->last_error = "Failed to decode prompt";
            return LLAMA_SIMPLE_ERROR_GENERATION_FAILED;
        }

        // Create sampler
        auto & sparams = ctx->params.sampling;

        if (gen_params) {
            sparams.temp           = gen_params->temperature;
            sparams.top_p          = gen_params->top_p;
            sparams.top_k          = gen_params->top_k;
            sparams.penalty_repeat = gen_params->repeat_penalty;
        }

        // Apply logit bias if set (must be done before creating sampler)
        if (!ctx->logit_biases.empty()) {
            sparams.logit_bias = ctx->logit_biases;
        }

        auto smpl = common_sampler_init(ctx->llama_init.model.get(), sparams);

        if (!smpl) {
            ctx->last_error = "Failed to create sampler";
            return LLAMA_SIMPLE_ERROR_GENERATION_FAILED;
        }

        // Generation loop
        int n_generated = 0;
        int max_tokens  = gen_params ? gen_params->max_tokens : -1;

        while (true) {
            // Call logit callback if set
            if (ctx->logit_cb) {
                // Get logits and extract top-k
                // (Simplified - full implementation would extract actual top-k)
                ctx->logit_cb(ctx->logit_cb_data, n_generated, nullptr, nullptr, 0);
            }

            // Sample next token
            llama_token token = common_sampler_sample(smpl, ctx->llama_init.context.get(), -1);

            // Check for EOS
            if (llama_vocab_is_eog(ctx->vocab, token)) {
                callback(user_data, "", token, 0.0f, n_generated);
                break;
            }

            // Decode token to text
            std::string token_text = common_token_to_piece(ctx->vocab, token);

            // Get probability (approximate)
            float prob = 1.0f;  // Simplified

            // Call user callback
            bool should_continue = callback(user_data, token_text.c_str(), token, prob, n_generated);

            if (!should_continue) {
                break;
            }

            // Check max tokens
            n_generated++;
            if (max_tokens > 0 && n_generated >= max_tokens) {
                callback(user_data, "", token, 0.0f, n_generated);
                break;
            }

            // Decode next token
            batch = llama_batch_get_one(&token, 1);
            if (llama_decode(ctx->llama_init.context.get(), batch) != 0) {
                ctx->last_error = "Failed to decode during generation";
                common_sampler_free(smpl);
                return LLAMA_SIMPLE_ERROR_GENERATION_FAILED;
            }
        }

        common_sampler_free(smpl);
        return LLAMA_SIMPLE_OK;

    } catch (const std::exception & e) {
        ctx->last_error = std::string("Generation error: ") + e.what();
        return LLAMA_SIMPLE_ERROR_GENERATION_FAILED;
    }
}

// =============================================================================
// Advanced Features
// =============================================================================

int llama_simple_set_logit_bias(llama_simple_context *          ctx,
                                const llama_simple_logit_bias * biases,
                                int32_t                         num_biases) {
    if (!ctx) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);

    if (!ctx->model_loaded) {
        return LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED;
    }

    try {
        ctx->logit_biases.clear();

        for (int32_t i = 0; i < num_biases; i++) {
            // Tokenize the string
            auto tokens = common_tokenize(ctx->vocab, biases[i].token_str, false, false);

            for (auto token_id : tokens) {
                llama_logit_bias bias;
                bias.token = token_id;
                bias.bias  = biases[i].bias;
                ctx->logit_biases.push_back(bias);
            }
        }

        return LLAMA_SIMPLE_OK;

    } catch (const std::exception & e) {
        ctx->last_error = std::string("Error setting logit bias: ") + e.what();
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }
}

int llama_simple_set_logit_callback(llama_simple_context *      ctx,
                                    llama_simple_logit_callback callback,
                                    void *                      user_data) {
    if (!ctx) {
        return LLAMA_SIMPLE_ERROR_INVALID_PARAMS;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);

    ctx->logit_cb      = callback;
    ctx->logit_cb_data = user_data;

    return LLAMA_SIMPLE_OK;
}

// =============================================================================
// Utility Functions
// =============================================================================

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

    std::lock_guard<std::mutex> lock(ctx->mutex);

    if (!ctx->model_loaded) {
        return nullptr;
    }

    // TODO: Implement JSON model info
    return nullptr;
}

void llama_simple_free_string(char * str) {
    free(str);
}

}  // extern "C"
