/**
 * llama_simple.h - Simple C API wrapper for llama.cpp
 *
 * Provides a clean C interface for Foreign Function Interface (FFI) usage
 * from languages like Common Lisp, Python, Ruby, etc.
 *
 * Features:
 * - Model loading/unloading
 * - Chat template formatting (Jinja)
 * - Token streaming with callbacks
 * - Logit bias (token weighting)
 * - Pre-sampling hooks
 *
 * Usage:
 *   llama_simple_context * ctx = llama_simple_init(NULL);
 *   llama_simple_load_model(ctx, "/path/to/model.gguf", NULL);
 *   llama_simple_prompt_stream(ctx, "Hello!", NULL, token_callback, NULL);
 *   llama_simple_free(ctx);
 */

#ifndef LLAMA_SIMPLE_H
#define LLAMA_SIMPLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Types
// =============================================================================

/**
 * Opaque handle to llama_simple context.
 * Manages the lifetime of model, context, and associated resources.
 */
typedef struct llama_simple_context llama_simple_context;

/**
 * Callback for token streaming during generation.
 *
 * @param user_data  User-provided data passed through
 * @param token_text Text representation of the token
 * @param token_id   Token ID
 * @param probability Token probability (0.0 to 1.0)
 * @param position   Position in the generated sequence
 * @return true to continue generation, false to stop
 */
typedef bool (*llama_simple_token_callback)(void *       user_data,
                                            const char * token_text,
                                            int          token_id,
                                            float        probability,
                                            int          position);

/**
 * Callback for logit probabilities before sampling.
 * Allows inspection and potential modification of candidate tokens.
 *
 * @param user_data  User-provided data
 * @param position   Current position in sequence
 * @param token_ids  Array of top-k token IDs
 * @param probs      Array of corresponding probabilities
 * @param k          Number of candidates
 */
typedef void (*llama_simple_logit_callback)(void *          user_data,
                                            int             position,
                                            const int32_t * token_ids,
                                            const float *   probs,
                                            int32_t         k);

// =============================================================================
// Error Codes
// =============================================================================

enum llama_simple_error {
    LLAMA_SIMPLE_OK                      = 0,
    LLAMA_SIMPLE_ERROR_INIT_FAILED       = -1,
    LLAMA_SIMPLE_ERROR_MODEL_LOAD_FAILED = -2,
    LLAMA_SIMPLE_ERROR_NO_MODEL_LOADED   = -3,
    LLAMA_SIMPLE_ERROR_INVALID_PARAMS    = -4,
    LLAMA_SIMPLE_ERROR_OUT_OF_MEMORY     = -5,
    LLAMA_SIMPLE_ERROR_GENERATION_FAILED = -6,
};

// =============================================================================
// Parameter Structures
// =============================================================================

/**
 * Initialization and model loading parameters.
 */
struct llama_simple_params {
    int32_t n_ctx;         ///< Context size (0 = model default)
    int32_t n_gpu_layers;  ///< GPU layers (-1 = all, 0 = CPU only)
    int32_t n_threads;     ///< CPU threads (-1 = auto-detect)
    int32_t seed;          ///< RNG seed (-1 = random)
    bool    verbose;       ///< Verbose logging
};

typedef struct llama_simple_params llama_simple_params;

/**
 * Generation parameters for text completion.
 */
struct llama_simple_gen_params {
    int32_t       max_tokens;      ///< Max tokens to generate (-1 = unlimited)
    float         temperature;     ///< Sampling temperature (0.0 = greedy)
    float         top_p;           ///< Top-p (nucleus) sampling
    int32_t       top_k;           ///< Top-k sampling
    float         repeat_penalty;  ///< Repetition penalty (1.0 = no penalty)
    const char ** stop_words;      ///< Null-terminated array of stop strings
    int32_t       num_stop_words;  ///< Number of stop words
};

typedef struct llama_simple_gen_params llama_simple_gen_params;

/**
 * Chat message for template formatting.
 */
struct llama_simple_chat_msg {
    const char * role;     ///< "system", "user", or "assistant"
    const char * content;  ///< Message content
};

typedef struct llama_simple_chat_msg llama_simple_chat_msg;

/**
 * Logit bias entry for token weighting.
 */
struct llama_simple_logit_bias {
    const char * token_str;  ///< Token text (will be tokenized)
    float        bias;       ///< Bias value (+inf to -inf)
};

typedef struct llama_simple_logit_bias llama_simple_logit_bias;

// =============================================================================
// Lifecycle Functions
// =============================================================================

/**
 * Initialize a new llama_simple context.
 *
 * @param params Initial parameters (can be NULL for defaults)
 * @return Opaque context handle, or NULL on failure
 */
llama_simple_context * llama_simple_init(const llama_simple_params * params);

/**
 * Free the context and all associated resources.
 * Safe to call with NULL.
 *
 * @param ctx Context handle
 */
void llama_simple_free(llama_simple_context * ctx);

// =============================================================================
// Model Management
// =============================================================================

/**
 * Load a GGUF model file.
 *
 * @param ctx Context handle
 * @param model_path Path to GGUF file
 * @param params Model-specific parameters (can be NULL to use context defaults)
 * @return Error code (LLAMA_SIMPLE_OK on success)
 */
int llama_simple_load_model(llama_simple_context * ctx, const char * model_path, const llama_simple_params * params);

/**
 * Unload the current model.
 *
 * @param ctx Context handle
 * @return Error code (LLAMA_SIMPLE_OK on success)
 */
int llama_simple_unload_model(llama_simple_context * ctx);

// =============================================================================
// Chat Template Formatting
// =============================================================================

/**
 * Format a chat prompt using the model's chat template (Jinja).
 *
 * @param ctx Context handle
 * @param messages Array of chat messages
 * @param num_messages Number of messages
 * @param add_generation_prompt Add assistant prefix for generation
 * @return Formatted prompt (caller must free with llama_simple_free_string)
 *         Returns NULL on error
 */
char * llama_simple_format_chat(llama_simple_context *        ctx,
                                const llama_simple_chat_msg * messages,
                                int32_t                       num_messages,
                                bool                          add_generation_prompt);

// =============================================================================
// Text Generation
// =============================================================================

/**
 * Generate a complete response (blocking, returns JSON).
 *
 * @param ctx Context handle
 * @param prompt Input prompt (plain text or chat-formatted)
 * @param params Generation parameters (can be NULL for defaults)
 * @return JSON response string (caller must free with llama_simple_free_string)
 *         Returns NULL on error
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
char * llama_simple_prompt_json(llama_simple_context *          ctx,
                                const char *                    prompt,
                                const llama_simple_gen_params * params);

/**
 * Generate a streaming response (callback for each token).
 *
 * @param ctx Context handle
 * @param prompt Input prompt (plain text or chat-formatted)
 * @param params Generation parameters (can be NULL for defaults)
 * @param callback Function called for each token
 * @param user_data User data passed to callback
 * @return Error code (LLAMA_SIMPLE_OK on success)
 */
int llama_simple_prompt_stream(llama_simple_context *          ctx,
                               const char *                    prompt,
                               const llama_simple_gen_params * params,
                               llama_simple_token_callback     callback,
                               void *                          user_data);

// =============================================================================
// Advanced Features
// =============================================================================

/**
 * Set logit bias for token weighting.
 * Biases can boost or suppress specific tokens during generation.
 *
 * @param ctx Context handle
 * @param biases Array of bias entries
 * @param num_biases Number of bias entries
 * @return Error code (LLAMA_SIMPLE_OK on success)
 */
int llama_simple_set_logit_bias(llama_simple_context * ctx, const llama_simple_logit_bias * biases, int32_t num_biases);

/**
 * Set callback for logit probabilities before sampling.
 * Allows inspection of candidate tokens and their probabilities.
 *
 * @param ctx Context handle
 * @param callback Logit callback function (NULL to unset)
 * @param user_data User data passed to callback
 * @return Error code (LLAMA_SIMPLE_OK on success)
 */
int llama_simple_set_logit_callback(llama_simple_context * ctx, llama_simple_logit_callback callback, void * user_data);

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Get the last error message.
 *
 * @param ctx Context handle
 * @return Error message string (do not free, may be NULL or empty)
 */
const char * llama_simple_get_error(llama_simple_context * ctx);

/**
 * Get model information as JSON.
 *
 * @param ctx Context handle
 * @return JSON string (caller must free with llama_simple_free_string)
 *         Returns NULL on error
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
char * llama_simple_get_model_info(llama_simple_context * ctx);

/**
 * Free a string returned by this API.
 * Safe to call with NULL.
 *
 * @param str String to free
 */
void llama_simple_free_string(char * str);

#ifdef __cplusplus
}
#endif

#endif  // LLAMA_SIMPLE_H
