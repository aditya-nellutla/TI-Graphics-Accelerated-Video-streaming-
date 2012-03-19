/******************************************************************************
*****************************************************************************
 * gst_render_bridge.h 
 * Render bridge header 
 *
 * Adopted base implementation from gst-plugin-bc project
 * http://gitorious.org/gst-plugin-bc/gst-plugin-bc 
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of Texas Instruments Incorporated nor the names of
 *   its contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact: aditya.n@ti.com
 ****************************************************************************/


#ifndef __GST_BC_SINK_H__
#define __GST_BC_SINK_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>

#include "gst_buffer_manager.h"
#include "bc_cat.h"

G_BEGIN_DECLS

#define GST_TYPE_BCSINK \
  (gst_render_bridge_get_type())
#define GST_BCSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BCSINK,GstBufferClassSink))
#define GST_BCSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BCSINK,GstBufferClassSinkClass))
#define GST_IS_BCSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BCSINK))
#define GST_IS_BCSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BCSINK))
typedef struct _GstBufferClassSink GstBufferClassSink;
typedef struct _GstBufferClassSinkClass GstBufferClassSinkClass;


#define PROP_DEF_QUEUE_SIZE 2
#define GST_BC_MIN_BUFFERS  2
#define GST_BC_MAX_BUFFERS 12

/**
 * GstBufferClassSink:
 *
 * Opaque data structure.
 */
struct _GstBufferClassSink
{
  GstVideoSink parent;

  /*< private > */
  gchar *videodev;
  guint32 num_buffers;

  GstBufferClassBufferPool *pool;

  int fd;
};
typedef struct gstpacket
{
	GstBufferClassBuffer *buf;
	int index;
}bc_gstpacket;

typedef struct gst_initpacket
{
	unsigned long phyaddr;
	bc_buf_params_t params;
}gst_initpacket;


struct _GstBufferClassSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_render_bridge_get_type (void);

G_END_DECLS
#endif /* __GST_BC_SINK_H__ */
