/* GStreamer llama.cpp Plugin
 * Copyright (C) 2025 llama.cpp contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 */

#ifndef __GST_LLAMA_H__
#define __GST_LLAMA_H__

#include "../ffi/llama_simple.h"

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_LLAMA (gst_llama_get_type())
G_DECLARE_FINAL_TYPE(GstLlama, gst_llama, GST, LLAMA, GstElement)

struct _GstLlama {
    GstElement parent;

    /* Pads */
    GstPad * sinkpad;
    GstPad * srcpad;
    GstPad * ctrlpad; /* Control pad for runtime parameter adjustment */

    /* Properties */
    gchar *  model_path;
    gint     n_ctx;
    gint     n_gpu_layers;
    gint     n_threads;
    gfloat   temperature;
    gfloat   top_p;
    gint     top_k;
    gfloat   repeat_penalty;
    gint     max_tokens;
    gboolean stream_tokens;
    gint     seed;
    gint     generation_timeout; /* Timeout in seconds (0 = no timeout) */

    /* State */
    gboolean model_loaded;
    GMutex   lock;
    GCond    cond;

    /* llama.cpp context */
    llama_simple_context * llama_ctx;

    /* Generation state */
    gboolean generating;
    gboolean eos_received;
    gboolean generation_aborted;    /* Flag for timeout/error aborts */
    gint64   generation_start_time; /* Start time for timeout tracking */
};

G_END_DECLS

#endif /* __GST_LLAMA_H__ */
