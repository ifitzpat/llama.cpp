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
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("text/plain, charset=utf-8"));

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
    PROP_LAST
};

/* Default property values */
#define DEFAULT_N_CTX          2048
#define DEFAULT_N_GPU_LAYERS   0
#define DEFAULT_N_THREADS      -1
#define DEFAULT_TEMPERATURE    0.7f
#define DEFAULT_TOP_P          0.9f
#define DEFAULT_TOP_K          40
#define DEFAULT_REPEAT_PENALTY 1.1f
#define DEFAULT_MAX_TOKENS     512
#define DEFAULT_STREAM_TOKENS  TRUE
#define DEFAULT_SEED           -1

/* Signals */
enum {
    SIGNAL_TOKEN_GENERATED,
    SIGNAL_GENERATION_STARTED,
    SIGNAL_GENERATION_COMPLETE,
    SIGNAL_MODEL_LOADED,
    SIGNAL_MODEL_UNLOADED,
    LAST_SIGNAL
};

static guint gst_llama_signals[LAST_SIGNAL] = { 0 };

/* GObject boilerplate */
#define gst_llama_parent_class parent_class
G_DEFINE_TYPE(GstLlama, gst_llama, GST_TYPE_ELEMENT);

/* Forward declarations */
static void                 gst_llama_set_property(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void                 gst_llama_get_property(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void                 gst_llama_finalize(GObject * object);
static GstStateChangeReturn gst_llama_change_state(GstElement * element, GstStateChange transition);
static GstFlowReturn        gst_llama_chain(GstPad * pad, GstObject * parent, GstBuffer * buf);
static gboolean             gst_llama_sink_event(GstPad * pad, GstObject * parent, GstEvent * event);
static GstPad *             gst_llama_request_new_pad(GstElement * element, GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void                 gst_llama_release_pad(GstElement * element, GstPad * pad);
static GstFlowReturn        gst_llama_ctrl_chain(GstPad * pad, GstObject * parent, GstBuffer * buf);

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

    element_class->change_state     = gst_llama_change_state;
    element_class->request_new_pad  = gst_llama_request_new_pad;
    element_class->release_pad      = gst_llama_release_pad;

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
        g_signal_new("token-generated", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 4,
                     G_TYPE_STRING,  /* token */
                     G_TYPE_INT,     /* token_id */
                     G_TYPE_FLOAT,   /* probability */
                     G_TYPE_INT);    /* position */

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
                     G_TYPE_NONE, 3,
                     G_TYPE_STRING,  /* full_text */
                     G_TYPE_INT,     /* num_tokens */
                     G_TYPE_STRING); /* stop_reason */

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
    gst_llama_signals[SIGNAL_MODEL_UNLOADED] =
        g_signal_new("model-unloaded", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE,
                     0);

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
    self->model_path     = NULL;
    self->n_ctx          = DEFAULT_N_CTX;
    self->n_gpu_layers   = DEFAULT_N_GPU_LAYERS;
    self->n_threads      = DEFAULT_N_THREADS;
    self->temperature    = DEFAULT_TEMPERATURE;
    self->top_p          = DEFAULT_TOP_P;
    self->top_k          = DEFAULT_TOP_K;
    self->repeat_penalty = DEFAULT_REPEAT_PENALTY;
    self->max_tokens     = DEFAULT_MAX_TOKENS;
    self->stream_tokens  = DEFAULT_STREAM_TOKENS;
    self->seed           = DEFAULT_SEED;

    /* Initialize state */
    self->model_loaded = FALSE;
    self->generating   = FALSE;
    self->eos_received = FALSE;
    self->llama_ctx    = NULL;

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
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }

    g_mutex_unlock(&self->lock);
}

/* Cleanup */
static void gst_llama_finalize(GObject * object) {
    GstLlama * self = GST_LLAMA(object);

    g_free(self->model_path);

    if (self->llama_ctx) {
        llama_simple_free(self->llama_ctx);
        self->llama_ctx = NULL;
    }

    g_mutex_clear(&self->lock);
    g_cond_clear(&self->cond);

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
                llama_simple_params params = { 0 };
                params.n_ctx               = self->n_ctx;
                params.n_gpu_layers        = self->n_gpu_layers;
                params.n_threads           = self->n_threads;
                params.seed                = self->seed;
                params.verbose             = FALSE;

                self->llama_ctx = llama_simple_init(&params);
                if (!self->llama_ctx) {
                    g_mutex_unlock(&self->lock);
                    GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ, ("Failed to initialize llama context"), (NULL));
                    return GST_STATE_CHANGE_FAILURE;
                }

                int result = llama_simple_load_model(self->llama_ctx, self->model_path, NULL);
                if (result != LLAMA_SIMPLE_OK) {
                    const char * error = llama_simple_get_error(self->llama_ctx);
                    g_mutex_unlock(&self->lock);
                    GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ, ("Failed to load model: %s", error), (NULL));
                    llama_simple_free(self->llama_ctx);
                    self->llama_ctx = NULL;
                    return GST_STATE_CHANGE_FAILURE;
                }

                self->model_loaded = TRUE;
                GST_INFO_OBJECT(self, "Model loaded: %s", self->model_path);

                /* Emit model-loaded signal */
                g_signal_emit(self, gst_llama_signals[SIGNAL_MODEL_LOADED], 0, self->model_path);
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
            if (self->llama_ctx && self->model_loaded) {
                llama_simple_unload_model(self->llama_ctx);
                self->model_loaded = FALSE;
                GST_INFO_OBJECT(self, "Model unloaded");

                /* Emit model-unloaded signal */
                g_signal_emit(self, gst_llama_signals[SIGNAL_MODEL_UNLOADED], 0);
            }
            g_mutex_unlock(&self->lock);
            break;

        case GST_STATE_CHANGE_READY_TO_NULL:
            /* Cleanup llama context */
            g_mutex_lock(&self->lock);
            if (self->llama_ctx) {
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
    GstFlowReturn ret    = GST_FLOW_OK;
    gchar *       prompt = NULL;
    int           result;

    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(self, "Failed to map input buffer");
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    /* Extract prompt from buffer */
    prompt = g_strndup((const gchar *) map.data, map.size);
    gst_buffer_unmap(buf, &map);
    gst_buffer_unref(buf);

    GST_DEBUG_OBJECT(self, "Received prompt: %s", prompt);

    g_mutex_lock(&self->lock);

    if (!self->model_loaded) {
        g_mutex_unlock(&self->lock);
        GST_ELEMENT_ERROR(self, CORE, FAILED, ("No model loaded"), (NULL));
        g_free(prompt);
        return GST_FLOW_ERROR;
    }

    self->generating = TRUE;

    /* Configure generation parameters */
    llama_simple_gen_params gen_params = { 0 };
    gen_params.max_tokens              = self->max_tokens;
    gen_params.temperature             = self->temperature;
    gen_params.top_p                   = self->top_p;
    gen_params.top_k                   = self->top_k;
    gen_params.repeat_penalty          = self->repeat_penalty;
    gen_params.stop_words              = NULL;
    gen_params.num_stop_words          = 0;

    /* Generate text */
    GST_INFO_OBJECT(self, "Starting generation (max_tokens=%d, temp=%.2f)", gen_params.max_tokens,
                    gen_params.temperature);

    /* Emit generation-started signal */
    g_signal_emit(self, gst_llama_signals[SIGNAL_GENERATION_STARTED], 0, prompt);

    result = llama_simple_prompt_stream(self->llama_ctx, prompt, &gen_params, token_callback, self);

    if (result != LLAMA_SIMPLE_OK) {
        const char * error = llama_simple_get_error(self->llama_ctx);
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Generation failed: %s", error), (NULL));
        ret = GST_FLOW_ERROR;
    } else {
        GST_INFO_OBJECT(self, "Generation completed successfully");
        /* Emit generation-complete signal */
        /* Note: full_text and num_tokens not currently tracked, using placeholders */
        g_signal_emit(self, gst_llama_signals[SIGNAL_GENERATION_COMPLETE], 0, "", gen_params.max_tokens, "completed");
    }

    self->generating = FALSE;
    g_mutex_unlock(&self->lock);

    g_free(prompt);

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
static GstPad * gst_llama_request_new_pad(GstElement * element, GstPadTemplate * templ, const gchar * name, const GstCaps * caps) {
    GstLlama * self = GST_LLAMA(element);
    GstPad *   pad  = NULL;

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

        GST_INFO_OBJECT(self, "Control pad created");

        g_mutex_unlock(&self->lock);
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
    if (json_object_has_member(obj, "temperature")) {
        self->temperature = json_object_get_double_member(obj, "temperature");
        GST_INFO_OBJECT(self, "Updated temperature: %.2f", self->temperature);
    }

    if (json_object_has_member(obj, "top_p")) {
        self->top_p = json_object_get_double_member(obj, "top_p");
        GST_INFO_OBJECT(self, "Updated top_p: %.2f", self->top_p);
    }

    if (json_object_has_member(obj, "top_k")) {
        self->top_k = json_object_get_int_member(obj, "top_k");
        GST_INFO_OBJECT(self, "Updated top_k: %d", self->top_k);
    }

    if (json_object_has_member(obj, "max_tokens")) {
        self->max_tokens = json_object_get_int_member(obj, "max_tokens");
        GST_INFO_OBJECT(self, "Updated max_tokens: %d", self->max_tokens);
    }

    if (json_object_has_member(obj, "repeat_penalty")) {
        self->repeat_penalty = json_object_get_double_member(obj, "repeat_penalty");
        GST_INFO_OBJECT(self, "Updated repeat_penalty: %.2f", self->repeat_penalty);
    }

    if (json_object_has_member(obj, "seed")) {
        self->seed = json_object_get_int_member(obj, "seed");
        GST_INFO_OBJECT(self, "Updated seed: %d", self->seed);
    }
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

    JsonNode *  root = json_parser_get_root(parser);
    JsonObject * obj  = json_node_get_object(root);

    if (!json_object_has_member(obj, "command")) {
        GST_ERROR_OBJECT(self, "Control message missing 'command' field");
        ret = GST_FLOW_ERROR;
        goto cleanup;
    }

    const gchar * command = json_object_get_string_member(obj, "command");

    g_mutex_lock(&self->lock);

    if (g_str_equal(command, "set_params")) {
        gst_llama_handle_set_params(self, obj);
    } else {
        GST_WARNING_OBJECT(self, "Unknown control command: %s", command);
    }

    g_mutex_unlock(&self->lock);

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
