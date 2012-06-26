/******************************************************************************
*****************************************************************************
 * gst_buffer_manager.h 
 * Header file for gstbuffer_manager - Adopted base implementation from gst-plugin-bc project
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


#ifndef __GST_BC_BUFFERPOOL_H__
#define __GST_BC_BUFFERPOOL_H__

#include <gst/gst.h>
#include "bc_cat.h"

G_BEGIN_DECLS

typedef struct _GstBufferClassBuffer GstBufferClassBuffer;
typedef struct _GstBufferClassBufferClass GstBufferClassBufferClass;

typedef struct _GstBufferClassBufferPool GstBufferClassBufferPool;
typedef struct _GstBufferClassBufferPoolClass GstBufferClassBufferPoolClass;

/*
 * GstBufferClassBuffer:
 */


#define GST_TYPE_BCBUFFER \
  (gst_bcbuffer_get_type())
#define GST_BCBUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BCBUFFER,GstBufferClassBuffer))
#define GST_IS_BCBUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BCBUFFER))


/**
 * GstBufferClassBuffer:
 *
 * Opaque data structure.
 */
struct _GstBufferClassBuffer
{
  GstBuffer parent;

  /*< private > */
  GstBufferClassBufferPool *pool;
  gint index;

};

struct _GstBufferClassBufferClass
{
  GstBufferClass parent_class;
};

GType gst_bcbuffer_get_type (void);
void gst_bcbuffer_flush (GstBufferClassBuffer * buffer);


/*
 * GstBufferClassBufferPool:
 */

#define GST_TYPE_BCBUFFERPOOL \
  (gst_buffer_manager_get_type())
#define GST_BCBUFFERPOOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BCBUFFERPOOL,GstBufferClassBufferPool))
#define GST_IS_BCBUFFERPOOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BCBUFFERPOOL))



/**
 * GstBufferClassBuffer:
 *
 * Opaque data structure.
 */
struct _GstBufferClassBufferPool
{
  GstMiniObject parent;

  /*< private > */
  GstElement *elem;
  GstCaps *caps;
  GMutex *lock;
  gboolean running;
  int fd;
  guint32 num_buffers;
  GstBufferClassBuffer **buffers;
  GAsyncQueue *avail_buffers;   /* pool of available buffers */

};

typedef struct gst_initpacket
{
	unsigned long phyaddr;
	bc_buf_params_t params;
	float xpos;
	float  ypos;
	float width;
	float height;
}gst_initpacket;

struct _GstBufferClassBufferPoolClass
{
  GstMiniObjectClass parent_class;
};

GType gst_buffer_manager_get_type (void);

GstBufferClassBufferPool *gst_buffer_manager_new (GstElement * elem, gst_initpacket pack_info, int count, GstCaps * caps);
void gst_buffer_manager_dispose (GstBufferClassBufferPool *pool);
GstCaps *gst_buffer_manager_get_caps (GstBufferClassBufferPool * pool);
GstBufferClassBuffer *gst_buffer_manager_get (GstBufferClassBufferPool * pool);

#define GST_BCBUFFERPOOL_LOCK(pool)     g_mutex_lock ((pool)->lock)
#define GST_BCBUFFERPOOL_UNLOCK(pool)   g_mutex_unlock ((pool)->lock)


G_END_DECLS
#endif /* __GST_BC_BUFFERPOOL_H__ */
