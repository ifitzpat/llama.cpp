/* Basic test for gst-llama plugin registration */

#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char * argv[]) {
    GstElement *        element;
    GstElementFactory * factory;
    int                 ret = 0;

    gst_init(&argc, &argv);

    /* Check if plugin is registered */
    GstRegistry * registry = gst_registry_get();
    GstPlugin *   plugin   = gst_registry_find_plugin(registry, "llama");

    if (!plugin) {
        fprintf(stderr, "ERROR: Plugin 'llama' not found in registry\n");
        ret = 1;
        goto done;
    }

    printf("✓ Plugin 'llama' found in registry\n");
    printf("  Version: %s\n", gst_plugin_get_version(plugin));
    printf("  Description: %s\n", gst_plugin_get_description(plugin));

    gst_object_unref(plugin);

    /* Try to create element */
    element = gst_element_factory_make("llama", "test");
    if (!element) {
        fprintf(stderr, "ERROR: Failed to create 'llama' element\n");
        ret = 1;
        goto done;
    }

    printf("✓ Successfully created 'llama' element\n");

    /* Check element metadata */
    factory = gst_element_get_factory(element);
    printf("  Metadata:\n");
    printf("    Long name: %s\n", gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_LONGNAME));
    printf("    Classification: %s\n", gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS));
    printf("    Description: %s\n", gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_DESCRIPTION));
    printf("    Author: %s\n", gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_AUTHOR));

    /* Check pads */
    GstPad * sinkpad = gst_element_get_static_pad(element, "sink");
    GstPad * srcpad  = gst_element_get_static_pad(element, "src");

    if (!sinkpad || !srcpad) {
        fprintf(stderr, "ERROR: Missing pads (sink=%p, src=%p)\n", sinkpad, srcpad);
        ret = 1;
        goto cleanup_element;
    }

    printf("✓ Element has sink and src pads\n");

    gst_object_unref(sinkpad);
    gst_object_unref(srcpad);

    /* Check properties */
    GParamSpec ** properties;
    guint         n_properties, i;

    properties = g_object_class_list_properties(G_OBJECT_GET_CLASS(element), &n_properties);

    printf("✓ Element has %u properties:\n", n_properties);
    for (i = 0; i < n_properties; i++) {
        printf("    - %s (%s)\n", g_param_spec_get_name(properties[i]), g_type_name(properties[i]->value_type));
    }

    g_free(properties);

cleanup_element:
    gst_object_unref(element);

done:
    gst_deinit();

    if (ret == 0) {
        printf("\n✓ All plugin tests passed!\n");
    } else {
        printf("\n✗ Plugin tests failed\n");
    }

    return ret;
}
