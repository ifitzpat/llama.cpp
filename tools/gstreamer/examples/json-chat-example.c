/* GStreamer llama.cpp JSON Chat Example
 * Demonstrates JSON chat message format with per-request parameters
 */

#include <gst/gst.h>
#include <stdio.h>
#include <string.h>

/* Example JSON chat request (OpenAI-compatible format) */
static const char * json_chat_request =
    "{"
    "  \"messages\": ["
    "    {\"role\": \"system\", \"content\": \"You are a helpful assistant.\"},"
    "    {\"role\": \"user\", \"content\": \"What is the capital of France?\"}"
    "  ],"
    "  \"temperature\": 0.7,"
    "  \"max_tokens\": 100,"
    "  \"top_p\": 0.95"
    "}";

int main(int argc, char * argv[]) {
    GstElement * pipeline;
    GstElement * appsrc;
    GstElement * llama;
    GstElement * filesink;
    GstBus *     bus;
    GstMessage * msg;
    GstBuffer *  buffer;

    if (argc != 3) {
        g_print("Usage: %s <model.gguf> <output.txt>\n", argv[0]);
        g_print("\nThis example sends a JSON chat request to the llama element.\n");
        g_print("The request includes system and user messages, plus per-request parameters.\n");
        return 1;
    }

    /* Initialize GStreamer */
    gst_init(&argc, &argv);

    /* Create pipeline elements */
    pipeline = gst_pipeline_new("json-chat-example");
    appsrc   = gst_element_factory_make("appsrc", "source");
    llama    = gst_element_factory_make("llama", "generator");
    filesink = gst_element_factory_make("filesink", "sink");

    if (!pipeline || !appsrc || !llama || !filesink) {
        g_printerr("Failed to create elements\n");
        return 1;
    }

    /* Configure appsrc for JSON data */
    GstCaps * caps = gst_caps_new_simple("application/json", NULL, NULL);
    g_object_set(appsrc, "caps", caps, "format", GST_FORMAT_TIME, NULL);
    gst_caps_unref(caps);

    /* Configure llama element (default parameters, will be overridden by JSON) */
    g_object_set(llama, "model", argv[1], "stream-tokens", FALSE, NULL);

    /* Configure output */
    g_object_set(filesink, "location", argv[2], NULL);

    /* Add elements to pipeline */
    gst_bin_add_many(GST_BIN(pipeline), appsrc, llama, filesink, NULL);

    /* Link elements */
    if (!gst_element_link(appsrc, llama)) {
        g_printerr("Failed to link appsrc to llama\n");
        return 1;
    }
    if (!gst_element_link(llama, filesink)) {
        g_printerr("Failed to link llama to filesink\n");
        return 1;
    }

    g_print("========================================\n");
    g_print("JSON Chat Example\n");
    g_print("========================================\n");
    g_print("Model: %s\n", argv[1]);
    g_print("Output: %s\n", argv[2]);
    g_print("\nJSON Request:\n%s\n", json_chat_request);
    g_print("========================================\n\n");

    /* Start playing */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /* Create and push buffer with JSON chat request */
    buffer = gst_buffer_new_allocate(NULL, strlen(json_chat_request), NULL);
    gst_buffer_fill(buffer, 0, json_chat_request, strlen(json_chat_request));

    GstFlowReturn flow_ret;
    g_signal_emit_by_name(appsrc, "push-buffer", buffer, &flow_ret);
    gst_buffer_unref(buffer);

    if (flow_ret != GST_FLOW_OK) {
        g_printerr("Failed to push JSON buffer\n");
        return 1;
    }

    g_print("JSON request sent, waiting for generation...\n");

    /* Signal end-of-stream */
    g_signal_emit_by_name(appsrc, "end-of-stream", &flow_ret);

    /* Wait for EOS or error */
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

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
                g_print("\n========================================\n");
                g_print("Generation complete!\n");
                g_print("Output saved to: %s\n", argv[2]);
                g_print("========================================\n");
                break;
            default:
                break;
        }
        gst_message_unref(msg);
    }

    /* Cleanup */
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
