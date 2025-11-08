/* GStreamer llama.cpp Signal Example
 * Demonstrates how to connect to and handle signals from the llama element
 */

#include <gst/gst.h>
#include <stdio.h>

/* Signal callback functions */

static void on_token_generated(GstElement * element,
                                const gchar * token,
                                gint          token_id,
                                gfloat        probability,
                                gint          position,
                                gpointer      user_data) {
    g_print("[Token %d] '%s' (prob=%.4f)\n", position, token, probability);
}

static void on_generation_started(GstElement * element, const gchar * prompt, gpointer user_data) {
    g_print("\n=== Generation Started ===\n");
    g_print("Prompt: %s\n", prompt);
    g_print("==========================\n\n");
}

static void on_generation_complete(GstElement * element,
                                    const gchar * full_text,
                                    gint          num_tokens,
                                    const gchar * stop_reason,
                                    gpointer      user_data) {
    g_print("\n=== Generation Complete ===\n");
    g_print("Tokens generated: %d\n", num_tokens);
    g_print("Stop reason: %s\n", stop_reason);
    g_print("===========================\n");
}

static void on_model_loaded(GstElement * element, const gchar * model_path, gpointer user_data) {
    g_print("\n[Model Loaded] %s\n\n", model_path);
}

static void on_model_unloaded(GstElement * element, gpointer user_data) {
    g_print("\n[Model Unloaded]\n\n");
}

/* Main pipeline setup */

int main(int argc, char * argv[]) {
    GstElement *  pipeline;
    GstElement *  filesrc;
    GstElement *  llama;
    GstElement *  filesink;
    GstBus *      bus;
    GstMessage *  msg;
    const gchar * model_path;
    const gchar * input_file;
    const gchar * output_file;

    /* Check arguments */
    if (argc != 4) {
        g_print("Usage: %s <model.gguf> <input.txt> <output.txt>\n", argv[0]);
        return 1;
    }

    model_path  = argv[1];
    input_file  = argv[2];
    output_file = argv[3];

    /* Initialize GStreamer */
    gst_init(&argc, &argv);

    /* Create pipeline elements */
    pipeline = gst_pipeline_new("signal-example");
    filesrc  = gst_element_factory_make("filesrc", "source");
    llama    = gst_element_factory_make("llama", "generator");
    filesink = gst_element_factory_make("filesink", "sink");

    if (!pipeline || !filesrc || !llama || !filesink) {
        g_printerr("Failed to create elements. Make sure gstllama plugin is installed.\n");
        return 1;
    }

    /* Configure elements */
    g_object_set(filesrc, "location", input_file, NULL);
    g_object_set(llama, "model", model_path, "temperature", 0.7, "max-tokens", 100, "stream-tokens", TRUE, NULL);
    g_object_set(filesink, "location", output_file, NULL);

    /* Connect signals */
    g_print("Connecting to llama element signals...\n");

    g_signal_connect(llama, "token-generated", G_CALLBACK(on_token_generated), NULL);
    g_signal_connect(llama, "generation-started", G_CALLBACK(on_generation_started), NULL);
    g_signal_connect(llama, "generation-complete", G_CALLBACK(on_generation_complete), NULL);
    g_signal_connect(llama, "model-loaded", G_CALLBACK(on_model_loaded), NULL);
    g_signal_connect(llama, "model-unloaded", G_CALLBACK(on_model_unloaded), NULL);

    /* Build pipeline */
    gst_bin_add_many(GST_BIN(pipeline), filesrc, llama, filesink, NULL);
    if (!gst_element_link_many(filesrc, llama, filesink, NULL)) {
        g_printerr("Elements could not be linked.\n");
        gst_object_unref(pipeline);
        return 1;
    }

    /* Start playing */
    g_print("Starting pipeline...\n");
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /* Wait until error or EOS */
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Handle message */
    if (msg != NULL) {
        GError *      err;
        gchar *       debug_info;
        GstMessageType type = GST_MESSAGE_TYPE(msg);

        switch (type) {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error: %s\n", err->message);
                g_printerr("Debug: %s\n", debug_info ? debug_info : "none");
                g_error_free(err);
                g_free(debug_info);
                break;
            case GST_MESSAGE_EOS:
                g_print("\nEnd-of-stream reached.\n");
                break;
            default:
                /* Should not reach here */
                g_printerr("Unexpected message type: %d\n", type);
                break;
        }
        gst_message_unref(msg);
    }

    /* Cleanup */
    g_print("Cleaning up...\n");
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
