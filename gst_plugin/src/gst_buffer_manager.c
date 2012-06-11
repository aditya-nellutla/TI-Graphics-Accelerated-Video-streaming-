/******************************************************************************
*****************************************************************************
 * gstbuffer_manager.c 
 * Responsible for gst buffer pool management -  Adopted base implementation from
 * gst-plugin-bc project -  http://gitorious.org/gst-plugin-bc/gst-plugin-bc 
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



#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* despite what config.h thinks, don't use 64bit mmap...
 */
#ifdef _FILE_OFFSET_BITS
#  undef _FILE_OFFSET_BITS
#endif

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gst_render_bridge.h"

#include<stdio.h>
#include <cmem.h>

#define BCIO_FLUSH                BC_IOWR(5)
#define MAX_FCOUNT 8
GST_DEBUG_CATEGORY_EXTERN (bcsink_debug);
#define GST_CAT_DEFAULT bcsink_debug

static CMEM_AllocParams cmem_params = { CMEM_POOL, CMEM_CACHED, 4096 };

/* round X up to a multiple of Y:
 */
#define CEIL(X,Y)  ((Y) * ( ((X)/(Y)) + (((X)%(Y)==0)?0:1) ))

#define BPP 2

/*
 * GstBufferClassBuffer:
 */

static GstBufferClass *buffer_parent_class = NULL;
extern int fd_bcinit_fifo;

static void
gst_bcbuffer_finalize (GstBufferClassBuffer * buffer)
{
  GstBufferClassBufferPool *pool = buffer->pool;
  gboolean resuscitated;

  GST_LOG_OBJECT (pool->elem, "finalizing buffer %p %d", buffer, buffer->index);


  GST_BCBUFFERPOOL_LOCK (pool);
  if (pool->running) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_READONLY);
    g_async_queue_push (pool->avail_buffers, buffer);
    resuscitated = TRUE;
  } else {
    GST_LOG_OBJECT (pool->elem, "the pool is shutting down");
    resuscitated = FALSE;
  }

  if (resuscitated) {
    GST_LOG_OBJECT (pool->elem, "reviving buffer %p, %d", buffer,
        buffer->index);
    gst_buffer_ref (GST_BUFFER (buffer));
  }

  GST_BCBUFFERPOOL_UNLOCK (pool);

  if (!resuscitated) {
    GST_LOG_OBJECT (pool->elem, "buffer %p not recovered, unmapping", buffer);
    gst_mini_object_unref (GST_MINI_OBJECT (pool));
//    munmap ((void *) GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));

    GST_MINI_OBJECT_CLASS (buffer_parent_class)->
        finalize (GST_MINI_OBJECT (buffer));
  }
}


static void
gst_bcbuffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_bcbuffer_finalize;
}


GType
gst_bcbuffer_get_type (void)
{
  static GType _gst_bcbuffer_type;

  if (G_UNLIKELY (_gst_bcbuffer_type == 0)) {
    static const GTypeInfo bcbuffer_info = {
      sizeof (GstBufferClassBufferClass),
      NULL,
      NULL,
      gst_bcbuffer_class_init,
      NULL,
      NULL,
      sizeof (GstBufferClassBuffer),
      0,
      NULL,
      NULL
    };
    _gst_bcbuffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstBufferClassBuffer", &bcbuffer_info, 0);
  }
  return _gst_bcbuffer_type;
}


static GstBufferClassBuffer *
gst_bcbuffer_new (GstBufferClassBufferPool * pool, int idx, int sz, unsigned long buf_paddr)
{
  GstBufferClassBuffer *ret = NULL;
  ret = (GstBufferClassBuffer *) gst_mini_object_new (GST_TYPE_BCBUFFER);

  if(ret == NULL)
	goto fail;

  ret->pool = GST_BCBUFFERPOOL (gst_mini_object_ref (GST_MINI_OBJECT (pool)));
  ret->index = idx;

  GST_LOG_OBJECT (pool->elem, "creating buffer %u (sz=%d), %p in pool %p", idx,
      sz, ret, pool);
  GST_BUFFER_SIZE (ret) = sz;
  return ret;

fail:
  gst_mini_object_unref (GST_MINI_OBJECT (ret));
  return NULL;
}


/*
 * GstBufferClassBufferPool:
 */
static GstMiniObjectClass *buffer_pool_parent_class = NULL;


static void
gst_buffer_manager_finalize (GstBufferClassBufferPool * pool)
{
  g_mutex_free (pool->lock);
  pool->lock = NULL;

  if (pool->avail_buffers) {
    g_async_queue_unref (pool->avail_buffers);
    pool->avail_buffers = NULL;
  }

  if (pool->buffers) {
    g_free (pool->buffers);
    pool->buffers = NULL;
  }

  gst_caps_unref (pool->caps);
  pool->caps = NULL;

  GST_MINI_OBJECT_CLASS (buffer_pool_parent_class)->finalize (GST_MINI_OBJECT
      (pool));
}


static void
gst_buffer_manager_init (GstBufferClassBufferPool * pool, gpointer g_class)
{
  pool->lock = g_mutex_new ();
  pool->running = FALSE;
}


static void
gst_buffer_manager_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  buffer_pool_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_buffer_manager_finalize;
}


GType
gst_buffer_manager_get_type (void)
{
  static GType _gst_buffer_manager_type;

  if (G_UNLIKELY (_gst_buffer_manager_type == 0)) {
    static const GTypeInfo buffer_manager_info = {
      sizeof (GstBufferClassBufferPoolClass),
      NULL,
      NULL,
      gst_buffer_manager_class_init,
      NULL,
      NULL,
      sizeof (GstBufferClassBufferPool),
      0,
      (GInstanceInitFunc) gst_buffer_manager_init,
      NULL
    };
    _gst_buffer_manager_type = g_type_register_static (GST_TYPE_MINI_OBJECT,
        "GstBufferClassBufferPool", &buffer_manager_info, 0);
  }
  return _gst_buffer_manager_type;
}

unsigned long TextureBufsPa[MAX_FCOUNT];
/*
 * Construct new bufferpool and allocate buffers from driver
 *
 * @elem      the parent element that owns this buffer
 * @fd        the file descriptor of the device file
 * @count     the requested number of buffers in the pool
 * @caps      the requested buffer caps
 * @return the bufferpool or <code>NULL</code> if error
 */
GstBufferClassBufferPool *
gst_buffer_manager_new (GstElement * elem, int fd, int count, GstCaps * caps)
{
  GstBufferClassBufferPool *pool = NULL;
  GstVideoFormat format;
  gint width, height;
  void          *vidStreamBufVa;
  unsigned long vidStreamBufPa;
  int n, i;
  gst_initpacket pack_info;
  if (gst_video_format_parse_caps(caps, &format, &width, &height)) {
    bc_buf_params_t param;
  
/****************************************************************************/
/****************************************************************************/
/****************************************************************************/
    CMEM_init();

    vidStreamBufVa = CMEM_alloc((width*height*BPP*MAX_FCOUNT), &cmem_params);
    if (!vidStreamBufVa)
    {
        printf ("CMEM_alloc for Video Stream buffer returned NULL \n");
        return NULL; 
    }

    vidStreamBufPa = CMEM_getPhys(vidStreamBufVa);
    for (i = 0; i < count; i++)
    {
        TextureBufsPa[i] = vidStreamBufPa + (width*height*BPP*i);
    }
/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

    param.count = count;
    param.width = width;
    param.height = height;
    param.fourcc = gst_video_format_to_fourcc (format);

    param.type = BC_MEMORY_USERPTR;

    pack_info.params = param;
    pack_info.phyaddr = vidStreamBufPa;

    n = write(fd_bcinit_fifo, &pack_info, sizeof(gst_initpacket));

   if(n != sizeof(gst_initpacket))
   {
	printf("Error in writing to queue\n");
   }
	
  /* We no longer need this pipe */
   close(fd_bcinit_fifo);

    /* construct bufferpool */
    pool = (GstBufferClassBufferPool *)
        gst_mini_object_new (GST_TYPE_BCBUFFERPOOL);

//TODO: Remove fd from pool -not required any more.
    pool->fd = -1;
    pool->elem = elem;
    pool->num_buffers = param.count;


    GST_DEBUG_OBJECT (pool->elem, "orig caps: %" GST_PTR_FORMAT, caps);
    GST_DEBUG_OBJECT (pool->elem, "requested %d buffers, got %d buffers", count,
        param.count);
    
    pool->caps = caps;

    /* and allocate buffers:
     */
    pool->num_buffers = param.count;
    pool->buffers = g_new0 (GstBufferClassBuffer *, param.count);
    pool->avail_buffers = g_async_queue_new_full (
        (GDestroyNotify) gst_mini_object_unref);

    for (i = 0; i < param.count; i++) {
     // TODO: Find correct size here
	GstBufferClassBuffer *buf = gst_bcbuffer_new (pool, i, param.width*param.height*BPP, TextureBufsPa[i]);
	GST_BUFFER_DATA (buf) = (vidStreamBufVa +  param.width*param.height*BPP*i);
	GST_BUFFER_SIZE (buf) = param.width*param.height*BPP;

      if (G_UNLIKELY (!buf)) {
        GST_WARNING_OBJECT (pool->elem, "Buffer %d allocation failed", i);
        goto fail;
      }
      gst_buffer_set_caps (GST_BUFFER (buf), caps);
      pool->buffers[i] = buf;
      g_async_queue_push (pool->avail_buffers, buf);
    }

    return pool;
  } else {
    GST_WARNING_OBJECT (elem, "failed to parse caps: %" GST_PTR_FORMAT, caps);
  }

fail:
  if (pool) {
    gst_mini_object_unref (GST_MINI_OBJECT (pool));
  }
  return NULL;
}

/**
 * Stop and dispose of this pool object.
 */
void
gst_buffer_manager_dispose (GstBufferClassBufferPool * pool)
{
  GstBufferClassBuffer *buf;

  g_return_if_fail (pool);

  pool->running = FALSE;

  while ((buf = g_async_queue_try_pop (pool->avail_buffers)) != NULL) {
    gst_buffer_unref (GST_BUFFER (buf));
  }

  gst_mini_object_unref (GST_MINI_OBJECT (pool));

  GST_DEBUG ("end");
}

/**
 * Get the current caps of the pool, they should be unref'd when done
 *
 * @pool   the "this" object
 */
GstCaps *
gst_buffer_manager_get_caps (GstBufferClassBufferPool * pool)
{
  return gst_caps_ref (pool->caps);
}

/**
 * Get an available buffer in the pool
 *
 * @pool   the "this" object
 */
GstBufferClassBuffer *
gst_buffer_manager_get (GstBufferClassBufferPool * pool)
{
  GstBufferClassBuffer *buf = g_async_queue_pop (pool->avail_buffers);

  if (buf) {
    GST_BUFFER_FLAG_UNSET (buf, 0xffffffff);
  }

  pool->running = TRUE;

  return buf;
}

/**
 * cause buffer to be flushed before rendering
 */
void
gst_bcbuffer_flush (GstBufferClassBuffer * buffer)
{
  GstBufferClassBufferPool *pool = buffer->pool;
  BCIO_package param;

  param.input = buffer->index;

  if (ioctl (pool->fd, BCIO_FLUSH, &param) < 0) {
    GST_WARNING_OBJECT (pool->elem, "Failed BCIO_FLUSH: %s",
        g_strerror (errno));
  }
}

