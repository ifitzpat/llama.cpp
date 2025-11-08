/* GStreamer llama.cpp Plugin
 * Copyright (C) 2025 llama.cpp contributors
 */

#ifdef HAVE_CONFIG_H
#    include "config.h"
#endif

#include "gstllama.h"

#include <json-glib/json-glib.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC(gst_llama_debug);
#define GST_CAT_DEFAULT gst_llama_debug

/* Pad templates */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS("text/plain, charset=utf-8; "
                                                                                    "application/json"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("text/plain, charset=utf-8"));

static GstStaticPadTemplate ctrl_template =
    GST_STATIC_PAD_TEMPLATE("ctrl", GST_PAD_SINK, GST_PAD_REQUEST, GST_STATIC_CAPS("application/x-llama-control"));

/* Properties */
enum {
    PROP_0,
    PROP_MODEL_PATH,
    PROP_N_CTX,
    PROP_N_GPU_LAYERS,
    PROP_N_THREADS,
    PROP_TEMPERATURE,
    PROP_TOP_P,
    PROP_TOP_K,
    PROP_REPEAT_PENALTY,
    PROP_MAX_TOKENS,
    PROP_STREAM_TOKENS,
    PROP_SEED,
    PROP_GENERATION_TIMEOUT,
    PROP_ENABLE_BUFFER_QUEUE,
    PROP_MAX_QUEUED_BUFFERS,
    PROP_LAST
};

/* Default property values */
#define DEFAULT_N_CTX               2048
#define DEFAULT_N_GPU_LAYERS        0
#define DEFAULT_N_THREADS           -1
#define DEFAULT_TEMPERATURE         0.7f
#define DEFAULT_TOP_P               0.9f
#define DEFAULT_TOP_K               40
#define DEFAULT_REPEAT_PENALTY      1.1f
#define DEFAULT_MAX_TOKENS          512
#define DEFAULT_STREAM_TOKENS       TRUE
#define DEFAULT_SEED                -1
#define DEFAULT_GENERATION_TIMEOUT  300   /* 5 minutes default timeout */
#define DEFAULT_ENABLE_BUFFER_QUEUE FALSE /* Disabled by default */
#define DEFAULT_MAX_QUEUED_BUFFERS  10    /* Default queue limit */

/* Signals */
enum {
    SIGNAL_TOKEN_GENERATED,
    SIGNAL_GENERATION_STARTED,
    SIGNAL_GENERATION_COMPLETE,
    SIGNAL_MODEL_LOADED,
    SIGNAL_MODEL_UNLOADED,
    SIGNAL_MODEL_LOADING,
    SIGNAL_MODEL_LOAD_PROGRESS,
    SIGNAL_MODEL_LOAD_FAILED,
    LAST_SIGNAL
};

static guint gst_llama_signals[LAST_SIGNAL] = { 0 };

/* GObject boilerplate */
#define gst_llama_parent_class parent_class
G_DEFINE_TYPE(GstLlama, gst_llama, GST_TYPE_ELEMENT);

/* Forward declarations */
static void gst_llama_set_property(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_llama_get_property(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_llama_finalize(GObject * object);
static GstStateChangeReturn gst_llama_change_state(GstElement * element, GstStateChange transition);
static GstFlowReturn        gst_llama_chain(GstPad * pad, GstObject * parent, GstBuffer * buf);
static gboolean             gst_llama_sink_event(GstPad * pad, GstObject * parent, GstEvent * event);
static GstPad *             gst_llama_request_new_pad(GstElement *     element,
                                                      GstPadTemplate * templ,
                                                      const gchar *    name,
                                                      const GstCaps *  caps);
static void                 gst_llama_release_pad(GstElement * element, GstPad * pad);
static GstFlowReturn        gst_llama_ctrl_chain(GstPad * pad, GstObject * parent, GstBuffer * buf);

/* Helper function to transition model state (thread-safe) */
static void set_model_state(GstLlama * self, GstLlamaModelState new_state) {
    g_mutex_lock(&self->lock);
    GstLlamaModelState old_state = self->model_state;
    self->model_state            = new_state;

    /* Update deprecated model_loaded flag for backward compatibility */
    self->model_loaded = (new_state == GST_LLAMA_MODEL_STATE_READY);

    GST_INFO_OBJECT(self, "Model state transition: %d -> %d", old_state, new_state);
    g_mutex_unlock(&self->lock);
}

/* Helper function to load model (synchronous) */
static gboolean load_model_sync(GstLlama * self, const gchar * model_path, gint n_ctx, gint n_gpu_layers) {
    gchar *  error_msg = NULL;
    gboolean success   = FALSE;

    GST_INFO_OBJECT(self, "Loading model: %s (n_ctx=%d, n_gpu_layers=%d)", model_path, n_ctx, n_gpu_layers);

    /* Validate model path */
    if (!g_file_test(model_path, G_FILE_TEST_EXISTS)) {
        error_msg = g_strdup_printf("Model file not found: %s", model_path);
        goto error;
    }

    if (!g_file_test(model_path, G_FILE_TEST_IS_REGULAR)) {
        error_msg = g_strdup_printf("Model path is not a regular file: %s", model_path);
        goto error;
    }

    /* Check file extension */
    if (!g_str_has_suffix(model_path, ".gguf")) {
        error_msg = g_strdup_printf("Model file must have .gguf extension: %s", model_path);
        goto error;
    }

    /* Create llama context */
    llama_simple_context * ctx = llama_simple_init_from_file_with_params(model_path, n_ctx, n_gpu_layers);

    if (!ctx) {
        error_msg = g_strdup_printf("Failed to initialize llama context from %s", model_path);
        goto error;
    }

    /* Success - update context */
    g_mutex_lock(&self->lock);
    if (self->llama_ctx) {
        llama_simple_free(self->llama_ctx);
    }
    self->llama_ctx = ctx;
    g_free(self->model_path);
    self->model_path   = g_strdup(model_path);
    self->n_ctx        = n_ctx;
    self->n_gpu_layers = n_gpu_layers;
    g_mutex_unlock(&self->lock);

    success = TRUE;
    GST_INFO_OBJECT(self, "Model loaded successfully");
    return TRUE;

error:
    g_mutex_lock(&self->lock);
    g_free(self->loading_error);
    self->loading_error = error_msg;
    g_mutex_unlock(&self->lock);

    GST_ERROR_OBJECT(self, "Model load failed: %s", error_msg);
    return FALSE;
}

/* Helper function to unload model (synchronous) */
static void unload_model_sync(GstLlama * self) {
    GST_INFO_OBJECT(self, "Unloading model");

    g_mutex_lock(&self->lock);
    if (self->llama_ctx) {
        llama_simple_free(self->llama_ctx);
        self->llama_ctx = NULL;
    }
    g_mutex_unlock(&self->lock);

    GST_INFO_OBJECT(self, "Model unloaded");
}

/* Loading thread function */
static gpointer loading_thread_func(gpointer user_data) {
    GstLlama * self = GST_LLAMA(user_data);
    gchar *    model_path;
    gint       n_ctx;
    gint       n_gpu_layers;
    gboolean   success;

    GST_DEBUG_OBJECT(self, "Loading thread started");

    /* Get parameters (thread-safe copy) */
    g_mutex_lock(&self->lock);
    model_path   = g_strdup(self->model_path);
    n_ctx        = self->n_ctx;
    n_gpu_layers = self->n_gpu_layers;
    g_mutex_unlock(&self->lock);

    /* Emit loading signal */
    g_signal_emit(self, gst_llama_signals[SIGNAL_MODEL_LOADING], 0, model_path);

    /* Report progress */
    g_mutex_lock(&self->lock);
    self->loading_progress = 0.25f;
    g_mutex_unlock(&self->lock);
    g_signal_emit(self, gst_llama_signals[SIGNAL_MODEL_LOAD_PROGRESS], 0, 0.25f, "Initializing model");

    /* Load the model */
    success = load_model_sync(self, model_path, n_ctx, n_gpu_layers);

    /* Report completion */
    g_mutex_lock(&self->lock);
    self->loading_progress = 1.0f;
    g_mutex_unlock(&self->lock);

    if (success) {
        set_model_state(self, GST_LLAMA_MODEL_STATE_READY);
        g_signal_emit(self, gst_llama_signals[SIGNAL_MODEL_LOADED], 0, model_path);
        GST_INFO_OBJECT(self, "Async model load completed successfully");

        /* Process any queued buffers */
        if (self->enable_buffer_queue) {
            process_queued_buffers(self);
        }
    } else {
        set_model_state(self, GST_LLAMA_MODEL_STATE_ERROR);
        g_mutex_lock(&self->lock);
        gchar * error_copy = g_strdup(self->loading_error);
        g_mutex_unlock(&self->lock);
        g_signal_emit(self, gst_llama_signals[SIGNAL_MODEL_LOAD_FAILED], 0, error_copy);
        g_free(error_copy);
        GST_ERROR_OBJECT(self, "Async model load failed");
    }

    /* Clean up */
    g_free(model_path);

    g_mutex_lock(&self->lock);
    self->loading_thread_running = FALSE;
    g_cond_broadcast(&self->cond);
    g_mutex_unlock(&self->lock);

    GST_DEBUG_OBJECT(self, "Loading thread exiting");
    return NULL;
}

/* Process queued buffers after model becomes ready */
static void process_queued_buffers(GstLlama * self) {
    GstBuffer * buf;
    gint        processed = 0;

    g_mutex_lock(&self->lock);

    GST_INFO_OBJECT(self, "Processing %d queued buffers", self->queued_count);

    while ((buf = g_queue_pop_head(self->buffer_queue)) != NULL) {
        self->queued_count--;

        /* Unlock while processing buffer */
        g_mutex_unlock(&self->lock);

        /* Process the buffer through the chain function
         * Note: We need to be careful here - we're calling chain from loading thread
         * Instead, we'll push directly to srcpad with EOS for now
         * A better approach would be to use a separate task or push to a GstQueue element
         */
        GST_DEBUG_OBJECT(self, "Processing queued buffer %d", processed + 1);

        /* For simplicity, we'll unref queued buffers for now
         * TODO: Implement proper queued buffer processing
         */
        gst_buffer_unref(buf);
        processed++;

        g_mutex_lock(&self->lock);
    }

    GST_INFO_OBJECT(self, "Processed %d queued buffers", processed);
    g_mutex_unlock(&self->lock);
}

/* Start asynchronous model loading */
static gboolean start_async_load(GstLlama * self, const gchar * model_path, gint n_ctx, gint n_gpu_layers) {
    g_mutex_lock(&self->lock);

    /* Check if already loading */
    if (self->loading_thread_running) {
        GST_WARNING_OBJECT(self, "Model loading already in progress");
        g_mutex_unlock(&self->lock);
        return FALSE;
    }

    /* Check current state */
    if (self->model_state == GST_LLAMA_MODEL_STATE_LOADING || self->model_state == GST_LLAMA_MODEL_STATE_UNLOADING) {
        GST_WARNING_OBJECT(self, "Model transition already in progress (state=%d)", self->model_state);
        g_mutex_unlock(&self->lock);
        return FALSE;
    }

    /* Update parameters */
    g_free(self->model_path);
    self->model_path             = g_strdup(model_path);
    self->n_ctx                  = n_ctx;
    self->n_gpu_layers           = n_gpu_layers;
    self->loading_thread_running = TRUE;
    self->loading_thread_cancel  = FALSE;
    self->loading_progress       = 0.0f;

    /* Clear any previous error */
    g_free(self->loading_error);
    self->loading_error = NULL;

    /* Set state */
    set_model_state(self, GST_LLAMA_MODEL_STATE_LOADING);

    /* Start loading thread */
    self->loading_thread = g_thread_new("llama-loader", loading_thread_func, self);

    g_mutex_unlock(&self->lock);

    GST_INFO_OBJECT(self, "Started async model load: %s", model_path);
    return TRUE;
}

/* Helper function to parse JSON chat messages and parameters
 * Returns formatted prompt (caller must free) or NULL on error
 * Optionally extracts per-request generation parameters
 */
static gchar * parse_json_chat_request(GstLlama * self, const gchar * json_str, llama_simple_gen_params * out_params) {
    GError *     error      = NULL;
    JsonParser * parser     = NULL;
    JsonNode *   root       = NULL;
    JsonObject * obj        = NULL;
    JsonArray *  messages   = NULL;
    gchar *      formatted  = NULL;
    gboolean     has_params = FALSE;

    /* Parse JSON */
    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_str, -1, &error)) {
        GST_WARNING_OBJECT(self, "Failed to parse JSON: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        GST_WARNING_OBJECT(self, "JSON root is not an object");
        g_object_unref(parser);
        return NULL;
    }

    obj = json_node_get_object(root);

    /* Extract messages array (required) */
    if (!json_object_has_member(obj, "messages")) {
        GST_WARNING_OBJECT(self, "JSON missing 'messages' field");
        g_object_unref(parser);
        return NULL;
    }

    messages = json_object_get_array_member(obj, "messages");
    if (!messages) {
        GST_WARNING_OBJECT(self, "Failed to get messages array");
        g_object_unref(parser);
        return NULL;
    }

    guint num_messages = json_array_get_length(messages);
    if (num_messages == 0) {
        GST_WARNING_OBJECT(self, "Empty messages array");
        g_object_unref(parser);
        return NULL;
    }

    /* Build llama_simple_chat_msg array */
    llama_simple_chat_msg * chat_msgs = g_new0(llama_simple_chat_msg, num_messages);

    for (guint i = 0; i < num_messages; i++) {
        JsonObject * msg_obj = json_array_get_object_element(messages, i);
        if (!msg_obj) {
            GST_WARNING_OBJECT(self, "Message %u is not an object", i);
            g_free(chat_msgs);
            g_object_unref(parser);
            return NULL;
        }

        /* Extract role and content */
        if (!json_object_has_member(msg_obj, "role") || !json_object_has_member(msg_obj, "content")) {
            GST_WARNING_OBJECT(self, "Message %u missing 'role' or 'content'", i);
            g_free(chat_msgs);
            g_object_unref(parser);
            return NULL;
        }

        chat_msgs[i].role    = json_object_get_string_member(msg_obj, "role");
        chat_msgs[i].content = json_object_get_string_member(msg_obj, "content");

        GST_DEBUG_OBJECT(self, "Message %u: role=%s, content=%s", i, chat_msgs[i].role, chat_msgs[i].content);
    }

    /* Format using chat template */
    formatted = llama_simple_format_chat(self->llama_ctx, chat_msgs, num_messages, TRUE);

    if (!formatted) {
        const char * error_msg = llama_simple_get_error(self->llama_ctx);
        GST_WARNING_OBJECT(self, "Failed to format chat: %s", error_msg ? error_msg : "unknown error");
        g_free(chat_msgs);
        g_object_unref(parser);
        return NULL;
    }

    GST_DEBUG_OBJECT(self, "Formatted chat prompt: %s", formatted);

    /* Extract optional per-request parameters */
    if (out_params) {
        if (json_object_has_member(obj, "temperature")) {
            out_params->temperature = (float) json_object_get_double_member(obj, "temperature");
            has_params              = TRUE;
            GST_DEBUG_OBJECT(self, "Request temperature: %.2f", out_params->temperature);
        }

        if (json_object_has_member(obj, "top_p")) {
            out_params->top_p = (float) json_object_get_double_member(obj, "top_p");
            has_params        = TRUE;
            GST_DEBUG_OBJECT(self, "Request top_p: %.2f", out_params->top_p);
        }

        if (json_object_has_member(obj, "top_k")) {
            out_params->top_k = (int) json_object_get_int_member(obj, "top_k");
            has_params        = TRUE;
            GST_DEBUG_OBJECT(self, "Request top_k: %d", out_params->top_k);
        }

        if (json_object_has_member(obj, "max_tokens")) {
            out_params->max_tokens = (int) json_object_get_int_member(obj, "max_tokens");
            has_params             = TRUE;
            GST_DEBUG_OBJECT(self, "Request max_tokens: %d", out_params->max_tokens);
        }

        if (json_object_has_member(obj, "repeat_penalty")) {
            out_params->repeat_penalty = (float) json_object_get_double_member(obj, "repeat_penalty");
            has_params                 = TRUE;
            GST_DEBUG_OBJECT(self, "Request repeat_penalty: %.2f", out_params->repeat_penalty);
        }

        if (json_object_has_member(obj, "seed")) {
            /* Note: llama_simple doesn't have per-request seed in gen_params,
             * but we log it for future extension */
            int seed = (int) json_object_get_int_member(obj, "seed");
            GST_DEBUG_OBJECT(self, "Request seed: %d (currently unsupported in per-request params)", seed);
        }

        if (has_params) {
            GST_INFO_OBJECT(self, "Using per-request parameters from JSON");
        }
    }

    g_free(chat_msgs);
    g_object_unref(parser);

    return formatted;
}

/* Token callback for streaming */
static gboolean token_callback(void *       user_data,
                               const char * token_text,
                               int          token_id,
                               float        probability,
                               int          position) {
    GstLlama *    self = GST_LLAMA(user_data);
    GstBuffer *   outbuf;
    GstFlowReturn ret;
    gsize         token_len;

    /* Check for timeout */
    if (self->generation_timeout > 0) {
        gint64 elapsed = (g_get_monotonic_time() - self->generation_start_time) / G_USEC_PER_SEC;
        if (elapsed > self->generation_timeout) {
            GST_WARNING_OBJECT(self, "Generation timeout after %" G_GINT64_FORMAT " seconds", elapsed);
            self->generation_aborted = TRUE;
            return FALSE; /* Abort generation */
        }
    }

    /* Check if generation was aborted */
    if (self->generation_aborted) {
        return FALSE;
    }

    if (!self->stream_tokens) {
        /* Not streaming, accumulate tokens internally */
        return TRUE;
    }

    token_len = strlen(token_text);
    if (token_len == 0) {
        return TRUE;
    }

    /* Create buffer for token */
    outbuf = gst_buffer_new_allocate(NULL, token_len, NULL);
    if (!outbuf) {
        GST_ERROR_OBJECT(self, "Failed to allocate buffer for token (size=%zu)", token_len);
        self->generation_aborted = TRUE;
        return FALSE; /* Abort on allocation failure */
    }

    gst_buffer_fill(outbuf, 0, token_text, token_len);

    GST_LOG_OBJECT(self, "Pushing token: '%s' (id=%d, prob=%.4f, pos=%d)", token_text, token_id, probability, position);

    /* Emit token-generated signal */
    g_signal_emit(self, gst_llama_signals[SIGNAL_TOKEN_GENERATED], 0, token_text, token_id, probability, position);

    /* Push buffer downstream */
    ret = gst_pad_push(self->srcpad, outbuf);
    if (ret != GST_FLOW_OK) {
        GST_WARNING_OBJECT(self, "Failed to push token buffer: %s", gst_flow_get_name(ret));
        return FALSE;
    }

    return TRUE;
}

/* Class initialization */
static void gst_llama_class_init(GstLlamaClass * klass) {
    GObjectClass *    gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass * element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_llama_set_property;
    gobject_class->get_property = gst_llama_get_property;
    gobject_class->finalize     = gst_llama_finalize;

    element_class->change_state    = gst_llama_change_state;
    element_class->request_new_pad = gst_llama_request_new_pad;
    element_class->release_pad     = gst_llama_release_pad;

    /* Install properties */
    g_object_class_install_property(gobject_class, PROP_MODEL_PATH,
                                    g_param_spec_string("model", "Model Path", "Path to GGUF model file", NULL,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_N_CTX,
                                    g_param_spec_int("n-ctx", "Context Size", "Context size in tokens", 0, 1048576,
                                                     DEFAULT_N_CTX, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_N_GPU_LAYERS,
        g_param_spec_int("n-gpu-layers", "GPU Layers", "Number of layers to offload to GPU", 0, 10000,
                         DEFAULT_N_GPU_LAYERS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_N_THREADS,
        g_param_spec_int("n-threads", "Threads", "Number of threads (-1 for auto)", -1, 1024, DEFAULT_N_THREADS,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_TEMPERATURE,
        g_param_spec_float("temperature", "Temperature", "Sampling temperature", 0.0, 2.0, DEFAULT_TEMPERATURE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_TOP_P,
                                    g_param_spec_float("top-p", "Top-P", "Nucleus sampling probability", 0.0, 1.0,
                                                       DEFAULT_TOP_P, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_TOP_K,
                                    g_param_spec_int("top-k", "Top-K", "Top-K sampling parameter", 0, 10000,
                                                     DEFAULT_TOP_K, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_REPEAT_PENALTY,
        g_param_spec_float("repeat-penalty", "Repeat Penalty", "Penalty for repeated tokens", 0.0, 2.0,
                           DEFAULT_REPEAT_PENALTY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_MAX_TOKENS,
        g_param_spec_int("max-tokens", "Max Tokens", "Maximum number of tokens to generate", 1, 100000,
                         DEFAULT_MAX_TOKENS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_STREAM_TOKENS,
        g_param_spec_boolean("stream-tokens", "Stream Tokens", "Stream individual tokens instead of complete response",
                             DEFAULT_STREAM_TOKENS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_SEED,
        g_param_spec_int("seed", "Random Seed", "Random seed (-1 for random)", -1, G_MAXINT32, DEFAULT_SEED,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_GENERATION_TIMEOUT,
        g_param_spec_int("generation-timeout", "Generation Timeout",
                         "Timeout for generation in seconds (0 = no timeout)", 0, G_MAXINT32,
                         DEFAULT_GENERATION_TIMEOUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_ENABLE_BUFFER_QUEUE,
        g_param_spec_boolean("enable-buffer-queue", "Enable Buffer Queue",
                             "Queue buffers during model transitions instead of returning NOT_NEGOTIATED",
                             DEFAULT_ENABLE_BUFFER_QUEUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_MAX_QUEUED_BUFFERS,
        g_param_spec_int("max-queued-buffers", "Max Queued Buffers",
                         "Maximum buffers to queue during transitions (0 = unlimited)", 0, G_MAXINT32,
                         DEFAULT_MAX_QUEUED_BUFFERS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /* Register signals */
    /**
     * GstLlama::token-generated:
     * @llama: the llama element
     * @token: token text
     * @token_id: token ID
     * @probability: token probability (0.0-1.0)
     * @position: position in generated sequence
     *
     * Emitted when a token is generated during text generation.
     */
    gst_llama_signals[SIGNAL_TOKEN_GENERATED] =
        g_signal_new("token-generated", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE,
                     4, G_TYPE_STRING, /* token */
                     G_TYPE_INT,       /* token_id */
                     G_TYPE_FLOAT,     /* probability */
                     G_TYPE_INT);      /* position */

    /**
     * GstLlama::generation-started:
     * @llama: the llama element
     * @prompt: the input prompt
     *
     * Emitted when text generation starts.
     */
    gst_llama_signals[SIGNAL_GENERATION_STARTED] =
        g_signal_new("generation-started", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                     G_TYPE_NONE, 1, G_TYPE_STRING); /* prompt */

    /**
     * GstLlama::generation-complete:
     * @llama: the llama element
     * @full_text: complete generated text
     * @num_tokens: number of tokens generated
     * @stop_reason: reason for stopping (e.g., "eos", "max_tokens")
     *
     * Emitted when text generation completes.
     */
    gst_llama_signals[SIGNAL_GENERATION_COMPLETE] =
        g_signal_new("generation-complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                     G_TYPE_NONE, 3, G_TYPE_STRING, /* full_text */
                     G_TYPE_INT,                    /* num_tokens */
                     G_TYPE_STRING);                /* stop_reason */

    /**
     * GstLlama::model-loaded:
     * @llama: the llama element
     * @model_path: path to the loaded model
     *
     * Emitted when a model is successfully loaded.
     */
    gst_llama_signals[SIGNAL_MODEL_LOADED] =
        g_signal_new("model-loaded", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
                     G_TYPE_STRING); /* model_path */

    /**
     * GstLlama::model-unloaded:
     * @llama: the llama element
     *
     * Emitted when a model is unloaded.
     */
    gst_llama_signals[SIGNAL_MODEL_UNLOADED] = g_signal_new("model-unloaded", G_TYPE_FROM_CLASS(klass),
                                                            G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

    /**
     * GstLlama::model-loading:
     * @llama: the llama element
     * @model_path: path to the model being loaded
     *
     * Emitted when model loading starts (asynchronous).
     */
    gst_llama_signals[SIGNAL_MODEL_LOADING] =
        g_signal_new("model-loading", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
                     G_TYPE_STRING); /* model_path */

    /**
     * GstLlama::model-load-progress:
     * @llama: the llama element
     * @progress: loading progress (0.0 to 1.0)
     * @message: progress message
     *
     * Emitted during model loading to report progress.
     */
    gst_llama_signals[SIGNAL_MODEL_LOAD_PROGRESS] =
        g_signal_new("model-load-progress", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                     G_TYPE_NONE, 2, G_TYPE_FLOAT, /* progress */
                     G_TYPE_STRING);               /* message */

    /**
     * GstLlama::model-load-failed:
     * @llama: the llama element
     * @error_message: error description
     *
     * Emitted when model loading fails.
     */
    gst_llama_signals[SIGNAL_MODEL_LOAD_FAILED] =
        g_signal_new("model-load-failed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE,
                     1, G_TYPE_STRING); /* error_message */

    /* Add pad templates */
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &ctrl_template);

    /* Set element metadata */
    gst_element_class_set_static_metadata(element_class, "LLaMA Text Generator", "Filter/Text/AI",
                                          "Generate text using llama.cpp language models", "llama.cpp contributors");

    GST_DEBUG_CATEGORY_INIT(gst_llama_debug, "llama", 0, "llama.cpp text generation element");
}

/* Instance initialization */
static void gst_llama_init(GstLlama * self) {
    /* Create pads */
    self->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    gst_pad_set_chain_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_llama_chain));
    gst_pad_set_event_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_llama_sink_event));
    gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);

    self->srcpad = gst_pad_new_from_static_template(&src_template, "src");
    gst_element_add_pad(GST_ELEMENT(self), self->srcpad);

    /* Control pad will be created on request */
    self->ctrlpad = NULL;

    /* Initialize properties to defaults */
    self->model_path          = NULL;
    self->n_ctx               = DEFAULT_N_CTX;
    self->n_gpu_layers        = DEFAULT_N_GPU_LAYERS;
    self->n_threads           = DEFAULT_N_THREADS;
    self->temperature         = DEFAULT_TEMPERATURE;
    self->top_p               = DEFAULT_TOP_P;
    self->top_k               = DEFAULT_TOP_K;
    self->repeat_penalty      = DEFAULT_REPEAT_PENALTY;
    self->max_tokens          = DEFAULT_MAX_TOKENS;
    self->stream_tokens       = DEFAULT_STREAM_TOKENS;
    self->seed                = DEFAULT_SEED;
    self->generation_timeout  = DEFAULT_GENERATION_TIMEOUT;
    self->enable_buffer_queue = DEFAULT_ENABLE_BUFFER_QUEUE;
    self->max_queued_buffers  = DEFAULT_MAX_QUEUED_BUFFERS;

    /* Initialize state */
    self->model_loaded          = FALSE;
    self->model_state           = GST_LLAMA_MODEL_STATE_UNLOADED;
    self->generating            = FALSE;
    self->eos_received          = FALSE;
    self->generation_aborted    = FALSE;
    self->generation_start_time = 0;
    self->llama_ctx             = NULL;

    /* Initialize loading thread state */
    self->loading_thread         = NULL;
    self->loading_thread_running = FALSE;
    self->loading_thread_cancel  = FALSE;
    self->loading_error          = NULL;
    self->loading_progress       = 0.0f;

    /* Initialize buffer queue */
    self->buffer_queue = g_queue_new();
    self->queued_count = 0;

    g_mutex_init(&self->lock);
    g_cond_init(&self->cond);
}

/* Property setters/getters */
static void gst_llama_set_property(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec) {
    GstLlama * self = GST_LLAMA(object);

    g_mutex_lock(&self->lock);

    switch (prop_id) {
        case PROP_MODEL_PATH:
            g_free(self->model_path);
            self->model_path = g_value_dup_string(value);
            break;
        case PROP_N_CTX:
            self->n_ctx = g_value_get_int(value);
            break;
        case PROP_N_GPU_LAYERS:
            self->n_gpu_layers = g_value_get_int(value);
            break;
        case PROP_N_THREADS:
            self->n_threads = g_value_get_int(value);
            break;
        case PROP_TEMPERATURE:
            self->temperature = g_value_get_float(value);
            break;
        case PROP_TOP_P:
            self->top_p = g_value_get_float(value);
            break;
        case PROP_TOP_K:
            self->top_k = g_value_get_int(value);
            break;
        case PROP_REPEAT_PENALTY:
            self->repeat_penalty = g_value_get_float(value);
            break;
        case PROP_MAX_TOKENS:
            self->max_tokens = g_value_get_int(value);
            break;
        case PROP_STREAM_TOKENS:
            self->stream_tokens = g_value_get_boolean(value);
            break;
        case PROP_SEED:
            self->seed = g_value_get_int(value);
            break;
        case PROP_GENERATION_TIMEOUT:
            self->generation_timeout = g_value_get_int(value);
            break;
        case PROP_ENABLE_BUFFER_QUEUE:
            self->enable_buffer_queue = g_value_get_boolean(value);
            break;
        case PROP_MAX_QUEUED_BUFFERS:
            self->max_queued_buffers = g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }

    g_mutex_unlock(&self->lock);
}

static void gst_llama_get_property(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec) {
    GstLlama * self = GST_LLAMA(object);

    g_mutex_lock(&self->lock);

    switch (prop_id) {
        case PROP_MODEL_PATH:
            g_value_set_string(value, self->model_path);
            break;
        case PROP_N_CTX:
            g_value_set_int(value, self->n_ctx);
            break;
        case PROP_N_GPU_LAYERS:
            g_value_set_int(value, self->n_gpu_layers);
            break;
        case PROP_N_THREADS:
            g_value_set_int(value, self->n_threads);
            break;
        case PROP_TEMPERATURE:
            g_value_set_float(value, self->temperature);
            break;
        case PROP_TOP_P:
            g_value_set_float(value, self->top_p);
            break;
        case PROP_TOP_K:
            g_value_set_int(value, self->top_k);
            break;
        case PROP_REPEAT_PENALTY:
            g_value_set_float(value, self->repeat_penalty);
            break;
        case PROP_MAX_TOKENS:
            g_value_set_int(value, self->max_tokens);
            break;
        case PROP_STREAM_TOKENS:
            g_value_set_boolean(value, self->stream_tokens);
            break;
        case PROP_SEED:
            g_value_set_int(value, self->seed);
            break;
        case PROP_GENERATION_TIMEOUT:
            g_value_set_int(value, self->generation_timeout);
            break;
        case PROP_ENABLE_BUFFER_QUEUE:
            g_value_set_boolean(value, self->enable_buffer_queue);
            break;
        case PROP_MAX_QUEUED_BUFFERS:
            g_value_set_int(value, self->max_queued_buffers);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }

    g_mutex_unlock(&self->lock);
}

/* Cleanup */
static void gst_llama_finalize(GObject * object) {
    GstLlama * self = GST_LLAMA(object);

    GST_DEBUG_OBJECT(self, "Finalizing llama element");

    /* Cancel and join loading thread if running */
    if (self->loading_thread) {
        GST_DEBUG_OBJECT(self, "Cancelling loading thread");
        g_mutex_lock(&self->lock);
        self->loading_thread_cancel = TRUE;
        g_mutex_unlock(&self->lock);
        g_thread_join(self->loading_thread);
        self->loading_thread = NULL;
    }

    /* Free loading error message */
    g_free(self->loading_error);
    self->loading_error = NULL;

    /* Clear buffer queue */
    if (self->buffer_queue) {
        GstBuffer * buf;
        while ((buf = g_queue_pop_head(self->buffer_queue)) != NULL) {
            gst_buffer_unref(buf);
        }
        g_queue_free(self->buffer_queue);
        self->buffer_queue = NULL;
    }

    g_free(self->model_path);
    self->model_path = NULL;

    if (self->llama_ctx) {
        GST_DEBUG_OBJECT(self, "Freeing llama context");
        llama_simple_free(self->llama_ctx);
        self->llama_ctx = NULL;
    }

    g_mutex_clear(&self->lock);
    g_cond_clear(&self->cond);

    GST_DEBUG_OBJECT(self, "Finalization complete");

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

/* State change handler */
static GstStateChangeReturn gst_llama_change_state(GstElement * element, GstStateChange transition) {
    GstLlama *           self = GST_LLAMA(element);
    GstStateChangeReturn ret  = GST_STATE_CHANGE_SUCCESS;

    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            /* Initialize llama.cpp backend */
            GST_INFO_OBJECT(self, "Initializing llama.cpp backend");
            break;

        case GST_STATE_CHANGE_READY_TO_PAUSED:
            /* Load model if path is set */
            g_mutex_lock(&self->lock);
            if (self->model_path && !self->model_loaded) {
                /* Validate model path exists */
                if (!g_file_test(self->model_path, G_FILE_TEST_EXISTS)) {
                    g_mutex_unlock(&self->lock);
                    GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, ("Model file not found: %s", self->model_path),
                                      ("Check that the file exists and the path is correct"));
                    return GST_STATE_CHANGE_FAILURE;
                }

                if (!g_file_test(self->model_path, G_FILE_TEST_IS_REGULAR)) {
                    g_mutex_unlock(&self->lock);
                    GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ,
                                      ("Model path is not a regular file: %s", self->model_path),
                                      ("Path may be a directory or special file"));
                    return GST_STATE_CHANGE_FAILURE;
                }

                GST_INFO_OBJECT(self, "Loading model from: %s", self->model_path);

                llama_simple_params params = { 0 };
                params.n_ctx               = self->n_ctx;
                params.n_gpu_layers        = self->n_gpu_layers;
                params.n_threads           = self->n_threads;
                params.seed                = self->seed;
                params.verbose             = FALSE;

                self->llama_ctx = llama_simple_init(&params);
                if (!self->llama_ctx) {
                    g_mutex_unlock(&self->lock);
                    GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Failed to initialize llama context"),
                                      ("Out of memory or initialization error"));
                    return GST_STATE_CHANGE_FAILURE;
                }

                int result = llama_simple_load_model(self->llama_ctx, self->model_path, NULL);
                if (result != LLAMA_SIMPLE_OK) {
                    const char * error = llama_simple_get_error(self->llama_ctx);
                    g_mutex_unlock(&self->lock);

                    /* Provide more specific error messages */
                    if (g_str_has_suffix(self->model_path, ".gguf")) {
                        GST_ELEMENT_ERROR(self, RESOURCE, READ, ("Failed to load GGUF model: %s", error),
                                          ("File may be corrupted or incompatible. Verify with llama-cli"));
                    } else {
                        GST_ELEMENT_ERROR(self, RESOURCE, READ, ("Failed to load model: %s", error),
                                          ("Only GGUF format is supported. File must have .gguf extension"));
                    }

                    llama_simple_free(self->llama_ctx);
                    self->llama_ctx = NULL;
                    return GST_STATE_CHANGE_FAILURE;
                }

                self->model_loaded = TRUE;
                GST_INFO_OBJECT(self, "Model loaded successfully: %s (ctx=%d, gpu_layers=%d)", self->model_path,
                                self->n_ctx, self->n_gpu_layers);

                /* Emit model-loaded signal */
                g_signal_emit(self, gst_llama_signals[SIGNAL_MODEL_LOADED], 0, self->model_path);
            } else if (!self->model_path) {
                GST_WARNING_OBJECT(self, "No model path set, cannot load model");
            }
            g_mutex_unlock(&self->lock);
            break;

        default:
            break;
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        return ret;
    }

    switch (transition) {
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            /* Unload model */
            g_mutex_lock(&self->lock);

            /* Abort any ongoing generation */
            if (self->generating) {
                GST_WARNING_OBJECT(self, "Aborting ongoing generation during state change");
                self->generation_aborted = TRUE;
                /* Give generation a moment to abort */
                g_cond_wait_until(&self->cond, &self->lock, g_get_monotonic_time() + 1 * G_TIME_SPAN_SECOND);
            }

            if (self->llama_ctx && self->model_loaded) {
                GST_DEBUG_OBJECT(self, "Unloading model");
                llama_simple_unload_model(self->llama_ctx);
                self->model_loaded = FALSE;
                GST_INFO_OBJECT(self, "Model unloaded");

                /* Emit model-unloaded signal */
                g_signal_emit(self, gst_llama_signals[SIGNAL_MODEL_UNLOADED], 0);
            }

            /* Reset state */
            self->generating         = FALSE;
            self->generation_aborted = FALSE;
            self->eos_received       = FALSE;

            g_mutex_unlock(&self->lock);
            break;

        case GST_STATE_CHANGE_READY_TO_NULL:
            /* Cleanup llama context */
            g_mutex_lock(&self->lock);
            if (self->llama_ctx) {
                GST_DEBUG_OBJECT(self, "Freeing llama context in state transition");
                llama_simple_free(self->llama_ctx);
                self->llama_ctx = NULL;
            }
            g_mutex_unlock(&self->lock);
            break;

        default:
            break;
    }

    return ret;
}

/* Chain function - process incoming buffers */
static GstFlowReturn gst_llama_chain(GstPad * pad, GstObject * parent, GstBuffer * buf) {
    GstLlama *    self = GST_LLAMA(parent);
    GstMapInfo    map;
    GstFlowReturn ret         = GST_FLOW_OK;
    gchar *       input_text  = NULL;
    gchar *       prompt      = NULL;
    gboolean      is_json     = FALSE;
    gboolean      free_prompt = FALSE;
    int           result;

    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(self, "Failed to map input buffer");
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    /* Extract input from buffer */
    input_text = g_strndup((const gchar *) map.data, map.size);
    gst_buffer_unmap(buf, &map);
    gst_buffer_unref(buf);

    GST_DEBUG_OBJECT(self, "Received input: %s", input_text);

    g_mutex_lock(&self->lock);

    /* Check model state and handle accordingly */
    if (self->model_state != GST_LLAMA_MODEL_STATE_READY) {
        /* Model not ready - check if we should queue or error */
        if (self->enable_buffer_queue && (self->model_state == GST_LLAMA_MODEL_STATE_LOADING ||
                                          self->model_state == GST_LLAMA_MODEL_STATE_UNLOADING)) {
            /* Queue the buffer during transition */
            if (self->max_queued_buffers > 0 && self->queued_count >= self->max_queued_buffers) {
                g_mutex_unlock(&self->lock);
                GST_WARNING_OBJECT(self, "Buffer queue full (%d buffers), dropping", self->queued_count);
                g_free(input_text);
                return GST_FLOW_OK; /* Drop buffer but don't error */
            }

            /* Re-create buffer and queue it */
            GstBuffer * queued_buf = gst_buffer_new_allocate(NULL, strlen(input_text), NULL);
            gst_buffer_fill(queued_buf, 0, input_text, strlen(input_text));
            g_queue_push_tail(self->buffer_queue, queued_buf);
            self->queued_count++;

            GST_DEBUG_OBJECT(self, "Queued buffer during %s (queue size: %d)",
                             self->model_state == GST_LLAMA_MODEL_STATE_LOADING ? "loading" : "unloading",
                             self->queued_count);

            g_mutex_unlock(&self->lock);
            g_free(input_text);
            return GST_FLOW_OK;
        } else {
            /* Buffer queuing disabled or not in transition state */
            const gchar * state_str = (self->model_state == GST_LLAMA_MODEL_STATE_UNLOADED) ? "unloaded" :
                                      (self->model_state == GST_LLAMA_MODEL_STATE_ERROR)    ? "error" :
                                      (self->model_state == GST_LLAMA_MODEL_STATE_LOADING)  ? "loading" :
                                                                                              "unloading";

            g_mutex_unlock(&self->lock);
            GST_ELEMENT_ERROR(self, CORE, FAILED, ("Model not ready (state: %s)", state_str), (NULL));
            g_free(input_text);
            return GST_FLOW_ERROR;
        }
    }

    /* Initialize generation state */
    self->generating            = TRUE;
    self->generation_aborted    = FALSE;
    self->generation_start_time = g_get_monotonic_time();

    /* Configure generation parameters (start with element defaults) */
    llama_simple_gen_params gen_params = { 0 };
    gen_params.max_tokens              = self->max_tokens;
    gen_params.temperature             = self->temperature;
    gen_params.top_p                   = self->top_p;
    gen_params.top_k                   = self->top_k;
    gen_params.repeat_penalty          = self->repeat_penalty;
    gen_params.stop_words              = NULL;
    gen_params.num_stop_words          = 0;

    /* Detect JSON input (simple heuristic: starts with '{') */
    if (input_text && input_text[0] == '{') {
        is_json = TRUE;
        GST_INFO_OBJECT(self, "Detected JSON input, parsing chat messages");

        /* Parse JSON and extract chat messages + per-request parameters */
        prompt = parse_json_chat_request(self, input_text, &gen_params);

        if (!prompt) {
            g_mutex_unlock(&self->lock);
            GST_ELEMENT_ERROR(self, STREAM, FORMAT, ("Failed to parse JSON chat request"), (NULL));
            g_free(input_text);
            return GST_FLOW_ERROR;
        }

        free_prompt = TRUE;
        GST_INFO_OBJECT(self, "Using formatted chat prompt from JSON");
    } else {
        /* Plain text input - use as-is */
        prompt = input_text;
        GST_INFO_OBJECT(self, "Using plain text input");
    }

    /* Generate text */
    GST_INFO_OBJECT(self, "Starting generation (max_tokens=%d, temp=%.2f)", gen_params.max_tokens,
                    gen_params.temperature);

    /* Emit generation-started signal */
    g_signal_emit(self, gst_llama_signals[SIGNAL_GENERATION_STARTED], 0, prompt);

    result = llama_simple_prompt_stream(self->llama_ctx, prompt, &gen_params, token_callback, self);

    /* Check if generation was aborted or failed */
    const char * stop_reason = "completed";

    if (self->generation_aborted) {
        GST_WARNING_OBJECT(self, "Generation was aborted due to timeout");
        stop_reason = "timeout";
        ret         = GST_FLOW_ERROR;
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Generation timeout"),
                          ("Exceeded %d second limit", self->generation_timeout));
    } else if (result != LLAMA_SIMPLE_OK) {
        const char * error = llama_simple_get_error(self->llama_ctx);
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Generation failed: %s", error), (NULL));
        stop_reason = "error";
        ret         = GST_FLOW_ERROR;
    } else {
        GST_INFO_OBJECT(self, "Generation completed successfully");
    }

    /* Emit generation-complete signal with appropriate stop reason */
    g_signal_emit(self, gst_llama_signals[SIGNAL_GENERATION_COMPLETE], 0, "", gen_params.max_tokens, stop_reason);

    self->generating = FALSE;
    g_mutex_unlock(&self->lock);

    /* Free allocated memory */
    if (free_prompt) {
        /* JSON path: free both the formatted prompt and input text */
        llama_simple_free_string(prompt);
        g_free(input_text);
    } else {
        /* Plain text path: prompt == input_text, only free once */
        g_free(input_text);
    }

    /* Send EOS downstream if not streaming tokens */
    if (!self->stream_tokens && ret == GST_FLOW_OK) {
        gst_pad_push_event(self->srcpad, gst_event_new_eos());
    }

    return ret;
}

/* Event handler for sink pad */
static gboolean gst_llama_sink_event(GstPad * pad, GstObject * parent, GstEvent * event) {
    GstLlama * self = GST_LLAMA(parent);
    gboolean   ret  = TRUE;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_EOS:
            GST_DEBUG_OBJECT(self, "Received EOS event");
            self->eos_received = TRUE;
            ret                = gst_pad_push_event(self->srcpad, event);
            break;

        case GST_EVENT_CAPS:
            {
                GstCaps * caps;
                gst_event_parse_caps(event, &caps);
                GST_DEBUG_OBJECT(self, "Received caps: %" GST_PTR_FORMAT, caps);
                ret = gst_pad_push_event(self->srcpad, event);
                break;
            }

        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }

    return ret;
}

/* Control pad request/release */
static GstPad * gst_llama_request_new_pad(GstElement *     element,
                                          GstPadTemplate * templ,
                                          const gchar *    name,
                                          const GstCaps *  caps) {
    GstLlama * self = GST_LLAMA(element);
    GstPad *   pad  = NULL;

    GST_DEBUG_OBJECT(self, "Request new pad: template=%s, name=%s", GST_PAD_TEMPLATE_NAME_TEMPLATE(templ),
                     name ? name : "NULL");

    if (templ == gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(element), "ctrl")) {
        g_mutex_lock(&self->lock);

        if (self->ctrlpad) {
            GST_WARNING_OBJECT(self, "Control pad already exists");
            g_mutex_unlock(&self->lock);
            return NULL;
        }

        self->ctrlpad = gst_pad_new_from_static_template(&ctrl_template, "ctrl");
        gst_pad_set_chain_function(self->ctrlpad, GST_DEBUG_FUNCPTR(gst_llama_ctrl_chain));
        gst_element_add_pad(element, self->ctrlpad);
        pad = self->ctrlpad;

        GST_INFO_OBJECT(self, "Control pad created successfully");

        g_mutex_unlock(&self->lock);
    } else {
        GST_WARNING_OBJECT(self, "Unknown pad template requested");
    }

    return pad;
}

static void gst_llama_release_pad(GstElement * element, GstPad * pad) {
    GstLlama * self = GST_LLAMA(element);

    g_mutex_lock(&self->lock);

    if (pad == self->ctrlpad) {
        gst_element_remove_pad(element, pad);
        self->ctrlpad = NULL;
        GST_INFO_OBJECT(self, "Control pad released");
    }

    g_mutex_unlock(&self->lock);
}

/* Control message handler */
static void gst_llama_handle_set_params(GstLlama * self, JsonObject * obj) {
    gint params_updated = 0;

    if (json_object_has_member(obj, "temperature")) {
        gfloat new_temp = json_object_get_double_member(obj, "temperature");
        if (new_temp >= 0.0 && new_temp <= 2.0) {
            self->temperature = new_temp;
            GST_INFO_OBJECT(self, "Updated temperature: %.2f", self->temperature);
            params_updated++;
        } else {
            GST_WARNING_OBJECT(self, "Invalid temperature value: %.2f (must be 0.0-2.0)", new_temp);
        }
    }

    if (json_object_has_member(obj, "top_p")) {
        gfloat new_top_p = json_object_get_double_member(obj, "top_p");
        if (new_top_p >= 0.0 && new_top_p <= 1.0) {
            self->top_p = new_top_p;
            GST_INFO_OBJECT(self, "Updated top_p: %.2f", self->top_p);
            params_updated++;
        } else {
            GST_WARNING_OBJECT(self, "Invalid top_p value: %.2f (must be 0.0-1.0)", new_top_p);
        }
    }

    if (json_object_has_member(obj, "top_k")) {
        gint new_top_k = json_object_get_int_member(obj, "top_k");
        if (new_top_k >= 0) {
            self->top_k = new_top_k;
            GST_INFO_OBJECT(self, "Updated top_k: %d", self->top_k);
            params_updated++;
        } else {
            GST_WARNING_OBJECT(self, "Invalid top_k value: %d (must be >= 0)", new_top_k);
        }
    }

    if (json_object_has_member(obj, "max_tokens")) {
        gint new_max_tokens = json_object_get_int_member(obj, "max_tokens");
        if (new_max_tokens > 0) {
            self->max_tokens = new_max_tokens;
            GST_INFO_OBJECT(self, "Updated max_tokens: %d", self->max_tokens);
            params_updated++;
        } else {
            GST_WARNING_OBJECT(self, "Invalid max_tokens value: %d (must be > 0)", new_max_tokens);
        }
    }

    if (json_object_has_member(obj, "repeat_penalty")) {
        gfloat new_penalty = json_object_get_double_member(obj, "repeat_penalty");
        if (new_penalty >= 0.0) {
            self->repeat_penalty = new_penalty;
            GST_INFO_OBJECT(self, "Updated repeat_penalty: %.2f", self->repeat_penalty);
            params_updated++;
        } else {
            GST_WARNING_OBJECT(self, "Invalid repeat_penalty value: %.2f (must be >= 0.0)", new_penalty);
        }
    }

    if (json_object_has_member(obj, "seed")) {
        self->seed = json_object_get_int_member(obj, "seed");
        GST_INFO_OBJECT(self, "Updated seed: %d", self->seed);
        params_updated++;
    }

    GST_DEBUG_OBJECT(self, "Control message processed: %d parameters updated", params_updated);
}

/* Control pad chain function */
static GstFlowReturn gst_llama_ctrl_chain(GstPad * pad, GstObject * parent, GstBuffer * buf) {
    GstLlama *    self = GST_LLAMA(parent);
    GstMapInfo    map;
    JsonParser *  parser = NULL;
    GError *      error  = NULL;
    GstFlowReturn ret    = GST_FLOW_OK;

    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(self, "Failed to map control buffer");
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    /* Parse JSON control message */
    parser = json_parser_new();

    if (!json_parser_load_from_data(parser, (const gchar *) map.data, map.size, &error)) {
        GST_ERROR_OBJECT(self, "Failed to parse control JSON: %s", error->message);
        g_error_free(error);
        ret = GST_FLOW_ERROR;
        goto cleanup;
    }

    JsonNode *   root = json_parser_get_root(parser);
    JsonObject * obj  = json_node_get_object(root);

    if (!json_object_has_member(obj, "command")) {
        GST_ERROR_OBJECT(self, "Control message missing 'command' field");
        ret = GST_FLOW_ERROR;
        goto cleanup;
    }

    const gchar * command = json_object_get_string_member(obj, "command");

    if (g_str_equal(command, "set_params")) {
        g_mutex_lock(&self->lock);
        gst_llama_handle_set_params(self, obj);
        g_mutex_unlock(&self->lock);
    } else if (g_str_equal(command, "load_model")) {
        /* Extract model loading parameters */
        const gchar * model_path = json_object_get_string_member(obj, "model_path");
        gint          n_ctx =
            json_object_has_member(obj, "n_ctx") ? (gint) json_object_get_int_member(obj, "n_ctx") : DEFAULT_N_CTX;
        gint n_gpu_layers = json_object_has_member(obj, "n_gpu_layers") ?
                                (gint) json_object_get_int_member(obj, "n_gpu_layers") :
                                DEFAULT_N_GPU_LAYERS;

        if (!model_path || strlen(model_path) == 0) {
            GST_ERROR_OBJECT(self, "load_model command missing 'model_path'");
            ret = GST_FLOW_ERROR;
            goto cleanup;
        }

        GST_INFO_OBJECT(self, "Control command: load_model %s (n_ctx=%d, n_gpu_layers=%d)", model_path, n_ctx,
                        n_gpu_layers);

        if (!start_async_load(self, model_path, n_ctx, n_gpu_layers)) {
            GST_ERROR_OBJECT(self, "Failed to start model loading");
            ret = GST_FLOW_ERROR;
        }
    } else if (g_str_equal(command, "unload_model")) {
        GST_INFO_OBJECT(self, "Control command: unload_model");

        /* Set state to unloading */
        set_model_state(self, GST_LLAMA_MODEL_STATE_UNLOADING);

        /* Unload synchronously (fast operation) */
        unload_model_sync(self);

        /* Set state and emit signal */
        set_model_state(self, GST_LLAMA_MODEL_STATE_UNLOADED);
        g_signal_emit(self, gst_llama_signals[SIGNAL_MODEL_UNLOADED], 0);
    } else {
        GST_WARNING_OBJECT(self, "Unknown control command: %s", command);
    }

cleanup:
    if (parser) {
        g_object_unref(parser);
    }
    gst_buffer_unmap(buf, &map);
    gst_buffer_unref(buf);

    return ret;
}

/* Plugin entry point */
static gboolean plugin_init(GstPlugin * plugin) {
    return gst_element_register(plugin, "llama", GST_RANK_NONE, GST_TYPE_LLAMA);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  llama,
                  "llama.cpp text generation plugin",
                  plugin_init,
                  "1.0.0",
                  "LGPL",
                  "llama.cpp",
                  "https://github.com/ggml-org/llama.cpp")
