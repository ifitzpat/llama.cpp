/* GStreamer llama.cpp Plugin
 * Copyright (C) 2025 llama.cpp contributors
 */

#ifdef HAVE_CONFIG_H
#    include "config.h"
#endif

#include "gstllama.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC(gst_llama_debug);
#define GST_CAT_DEFAULT gst_llama_debug

/* Pad templates */
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("text/plain, charset=utf-8"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("text/plain, charset=utf-8"));

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

    element_class->change_state = gst_llama_change_state;

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

    /* Add pad templates */
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

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

    result = llama_simple_prompt_stream(self->llama_ctx, prompt, &gen_params, token_callback, self);

    if (result != LLAMA_SIMPLE_OK) {
        const char * error = llama_simple_get_error(self->llama_ctx);
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Generation failed: %s", error), (NULL));
        ret = GST_FLOW_ERROR;
    } else {
        GST_INFO_OBJECT(self, "Generation completed successfully");
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
