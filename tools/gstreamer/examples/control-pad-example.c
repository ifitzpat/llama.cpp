/* GStreamer llama.cpp Control Pad Example
 * Demonstrates runtime parameter adjustment via control pad
 */

#include <gst/gst.h>
#include <stdio.h>
#include <string.h>

/* Simple example showing how to send control messages */

int main(int argc, char * argv[]) {
    GstElement * pipeline;
    GstElement * filesrc;
    GstElement * llama;
    GstElement * filesink;
    GstElement * ctrlsrc;
    GstPad *     ctrl_pad;
    GstBus *     bus;
    GstMessage * msg;

    if (argc != 4) {
        g_print("Usage: %s <model.gguf> <input.txt> <output.txt>\n", argv[0]);
        return 1;
    }

    /* Initialize GStreamer */
    gst_init(&argc, &argv);

    /* Create pipeline elements */
    pipeline = gst_pipeline_new("control-example");
    filesrc  = gst_element_factory_make("filesrc", "source");
    llama    = gst_element_factory_make("llama", "generator");
    filesink = gst_element_factory_make("filesink", "sink");
    ctrlsrc  = gst_element_factory_make("appsrc", "ctrlsrc");

    if (!pipeline || !filesrc || !llama || !filesink || !ctrlsrc) {
        g_printerr("Failed to create elements\n");
        return 1;
    }

    /* Configure elements */
    g_object_set(filesrc, "location", argv[2], NULL);
    g_object_set(llama, "model", argv[1], "temperature", 0.7, "max-tokens", 100, NULL);
    g_object_set(filesink, "location", argv[3], NULL);

    /* Configure control source */
    g_object_set(ctrlsrc, "caps", gst_caps_from_string("application/x-llama-control"), "format", GST_FORMAT_TIME, NULL);

    /* Add elements to pipeline */
    gst_bin_add_many(GST_BIN(pipeline), filesrc, llama, filesink, ctrlsrc, NULL);

    /* Link main data path */
    if (!gst_element_link(filesrc, llama)) {
        g_printerr("Failed to link filesrc to llama\n");
        return 1;
    }
    if (!gst_element_link(llama, filesink)) {
        g_printerr("Failed to link llama to filesink\n");
        return 1;
    }

    /* Request control pad and link it */
    ctrl_pad = gst_element_request_pad_simple(llama, "ctrl");
    if (!ctrl_pad) {
        g_printerr("Failed to request control pad\n");
        return 1;
    }

    GstPad * ctrlsrc_pad = gst_element_get_static_pad(ctrlsrc, "src");
    if (gst_pad_link(ctrlsrc_pad, ctrl_pad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link control pad\n");
        return 1;
    }
    gst_object_unref(ctrlsrc_pad);

    g_print("Control pad created and linked\n");

    /* Start playing */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /* Send a control message to adjust temperature */
    const gchar * ctrl_msg = "{\"command\": \"set_params\", \"temperature\": 1.2, \"max_tokens\": 50}";

    GstBuffer * ctrl_buffer = gst_buffer_new_allocate(NULL, strlen(ctrl_msg), NULL);
    gst_buffer_fill(ctrl_buffer, 0, ctrl_msg, strlen(ctrl_msg));

    GstFlowReturn flow_ret;
    g_signal_emit_by_name(ctrlsrc, "push-buffer", ctrl_buffer, &flow_ret);
    gst_buffer_unref(ctrl_buffer);

    if (flow_ret != GST_FLOW_OK) {
        g_printerr("Failed to push control message\n");
    } else {
        g_print("Control message sent: %s\n", ctrl_msg);
    }

    /* Signal end-of-stream on control pad */
    g_signal_emit_by_name(ctrlsrc, "end-of-stream", &flow_ret);

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
                g_print("End-of-stream reached\n");
                break;
            default:
                break;
        }
        gst_message_unref(msg);
    }

    /* Cleanup */
    gst_element_release_request_pad(llama, ctrl_pad);
    gst_object_unref(ctrl_pad);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
