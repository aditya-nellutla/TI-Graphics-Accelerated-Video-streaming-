/******************************************************************************
*****************************************************************************
 * gst_render_bridge.c 
 * Establishes communication with the renderer to display video onto 
 * 3d surface using the IMGBufferClass extension for texture streaming.
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


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gst_render_bridge.h"
#include <stdio.h>
#include "bc_cat.h"
#include <pthread.h>

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/*Used for unreferencing buffers for deferred rendering architecture */
GstBufferClassBuffer *bcbuf_queue[MAX_QUEUE]= {NULL, NULL, NULL};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV("{I420, YV12, NV12, UYVY, YUYV}"))
    );

GST_DEBUG_CATEGORY (bcsink_debug);
#define GST_CAT_DEFAULT bcsink_debug

#define PROP_DEF_DEVICE             "/dev/bccat0"
/* FIFO PIPE for communication b/w sink and Texture streaming API */
char BCSINK_FIFO_NAME[]= "gstbcsink_fifo0";
char BCINIT_FIFO_NAME[]= "gstbcinit_fifo0";
char BCACK_FIFO_NAME[]= "gstbcack_fifo0";
char INSTANCEID_FIFO_NAME[]="gstinstanceid_fifo";

pthread_mutex_t ctrlmutex = PTHREAD_MUTEX_INITIALIZER;
int fd_bcsink_fifo;
int fd_bcinit_fifo;
int fd_bcack_fifo;
extern unsigned long TextureBufsPa[MAX_FCOUNT];
pthread_mutex_t initmutex = PTHREAD_MUTEX_INITIALIZER;
/* Passes the configuration display parameters from cmd line */
static gst_initpacket pack_info;
/* Properties */
enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_QUEUE_SIZE,
  PROP_GL_EXAMPLE,
  PROP_XPOS,
  PROP_YPOS,
  PROP_WIDTH,
  PROP_HEIGHT,
};

/* Signals */
enum
{
  SIG_INIT,
  SIG_RENDER,
  SIG_CLOSE,
  /* add more above */
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* data to be transmitted across pipe */
bc_gstpacket datapacket;


GST_BOILERPLATE (GstBufferClassSink, gst_render_bridge, GstVideoSink,
    GST_TYPE_VIDEO_SINK);


static void gst_render_bridge_dispose (GObject * object);
static void gst_render_bridge_finalize (GstBufferClassSink * bcsink);

/* GObject methods: */
static void gst_render_bridge_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_render_bridge_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


/* GstElement methods: */
static GstStateChangeReturn gst_render_bridge_change_state (GstElement * element,
    GstStateChange transition);

/* GstBaseSink methods: */
#if 0
static GstCaps *gst_render_bridge_get_caps (GstBaseSink * bsink);
#endif
static gboolean gst_render_bridge_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstFlowReturn gst_render_bridge_buffer_alloc (GstBaseSink * bsink,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static GstFlowReturn gst_render_bridge_show_frame (GstBaseSink * bsink,
    GstBuffer * buf);


static void
gst_render_bridge_base_init (gpointer g_class)
{
  GstBufferClassSinkClass *gstbcsink_class = GST_BCSINK_CLASS (g_class);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (gstbcsink_class);

  GST_DEBUG_CATEGORY_INIT (bcsink_debug, "bcsink", 0, "BC sink element");

  gst_element_class_set_details_simple (gstelement_class, 
      "BufferClass Render Bridge API",
      "Sink/Video",
      "A video sink utilizing the IMG texture streaming extension",
      "Aditya Nellutla <aditya.n@ti.com>,");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
}


static void
gst_render_bridge_class_init (GstBufferClassSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseSinkClass *basesink_class;

  GST_DEBUG ("ENTER");

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  basesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->dispose = gst_render_bridge_dispose;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_render_bridge_finalize;
  gobject_class->set_property = gst_render_bridge_set_property;
  gobject_class->get_property = gst_render_bridge_get_property;

  element_class->change_state = gst_render_bridge_change_state;

  /**
   * GstBufferClassSink:device
   *
   * The path to bc_cat device file
   */
  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device", "Device location",
          PROP_DEF_DEVICE, G_PARAM_READWRITE));

  /**
   * GstBufferClassSink:device
   *
   * Provides the display configuration parameters.
   */
  g_object_class_install_property (gobject_class, PROP_XPOS,
      g_param_spec_float ("x-pos",
          "Display config parameters",
          "Specifies normalized x-cordinate for the video"
          "on the display", -1, 1, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_YPOS,
      g_param_spec_float ("y-pos",
          "Display config parameters",
          "Specifies normalized y-cordinate for the video"
          "on the display", -1, 1, 1, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_float ("width",
          "Display config parameters",
          "Specifies the width for the video"
          "on the display", 0, 2, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_HEIGHT,
      g_param_spec_float ("height",
          "Display config parameters",
          "Specifies the height for the video"
          "on the display", 0, 2, 0, G_PARAM_READWRITE));

  /**
   * GstBufferClassSink:queue-size
   *
   * Number of buffers to be enqueued in the driver in streaming mode
   */
  g_object_class_install_property (gobject_class, PROP_QUEUE_SIZE,
      g_param_spec_uint ("queue-size", "Queue size",
          "Number of buffers to be enqueued in the driver in streaming mode",
          GST_BC_MIN_BUFFERS, GST_BC_MAX_BUFFERS, PROP_DEF_QUEUE_SIZE,
          G_PARAM_READWRITE));

  /**
   * GstBufferClassSink:gl-example
   *
   * Controls which example 3d code to use if application does not register
   * it's own render callback
   */
  g_object_class_install_property (gobject_class, PROP_GL_EXAMPLE,
      g_param_spec_uint ("gl-example",
          "Whether to use OpenGLES 1.x or 2.x example",
          "Controls which example 3d code to use if application does not register "
          "it's own render callback", 1, 2, 1, G_PARAM_READWRITE));


  /**
   * GstBufferClassSink::init:
   * @bcsink: the #GstBufferClassSink
   * @buffercount:  the number of buffers used
   *
   * Will be emitted after buffers are allocated, to give the application
   * an opportunity to bind the surfaces
   */
  signals[SIG_INIT] =
      g_signal_new ("init", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST, 0,
      NULL, NULL, gst_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  /**
   * GstBufferClassSink::render:
   * @bcsink: the #GstBufferClassSink
   * @bufferindex:  the index of the buffer to render
   *
   * Will be emitted when a new buffer is ready to be rendered
   */
  signals[SIG_RENDER] =
      g_signal_new ("render", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST, 0,
      NULL, NULL, gst_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  /**
   * GstBufferClassSink::close:
   * @bcsink: the #GstBufferClassSink
   *
   * Will be emitted when the device is closed
   */
  signals[SIG_CLOSE] =
      g_signal_new ("close", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST, 0,
      NULL, NULL, gst_marshal_VOID__VOID, G_TYPE_NONE, 0);

#if 0
  basesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_render_bridge_get_caps);
#endif
  basesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_render_bridge_set_caps);
  basesink_class->buffer_alloc = GST_DEBUG_FUNCPTR (gst_render_bridge_buffer_alloc);
  basesink_class->render = GST_DEBUG_FUNCPTR (gst_render_bridge_show_frame);
}

static void
gst_render_bridge_init (GstBufferClassSink * bcsink, GstBufferClassSinkClass * klass)
{
  int fd = -1, n;
  int deviceid = -1;
  FILE *fd_instance;
  GST_DEBUG_OBJECT (bcsink, "ENTER");

  /* default property values: */
  bcsink->num_buffers = PROP_DEF_QUEUE_SIZE;
  
  fd_instance = fopen( INSTANCEID_FIFO_NAME, "w");
  
  /* Create Named pipe for the ith instance of dev node. */
  if((fd = open("/dev/bccat0", O_RDWR|O_NDELAY)) != -1)
  {
        close(fd);
	deviceid = 0;
	BCSINK_FIFO_NAME[strlen(BCSINK_FIFO_NAME)-1]='0';
	BCINIT_FIFO_NAME[strlen(BCINIT_FIFO_NAME)-1]='0';
	BCACK_FIFO_NAME[strlen(BCACK_FIFO_NAME)-1]='0';
	fprintf(fd_instance,"%d", deviceid); 
  }
  else
  if((fd = open("/dev/bccat1", O_RDWR|O_NDELAY)) != -1)
  {
        close(fd);
	deviceid = 1;
	BCSINK_FIFO_NAME[strlen(BCSINK_FIFO_NAME)-1]='1';
	BCINIT_FIFO_NAME[strlen(BCINIT_FIFO_NAME)-1]='1';
	BCACK_FIFO_NAME[strlen(BCACK_FIFO_NAME)-1]='1';
	fprintf(fd_instance,"%d", deviceid); 
  }
  else
  if((fd = open("/dev/bccat2", O_RDWR|O_NDELAY)) != -1)
  {
        close(fd);
	deviceid = 2;
	BCSINK_FIFO_NAME[strlen(BCSINK_FIFO_NAME)-1]='2';
	BCINIT_FIFO_NAME[strlen(BCINIT_FIFO_NAME)-1]='2';
	BCACK_FIFO_NAME[strlen(BCACK_FIFO_NAME)-1]='2';
	fprintf(fd_instance,"%d", deviceid); 
  }
  else
  if((fd = open("/dev/bccat3", O_RDWR|O_NDELAY)) != -1)
  {
        close(fd);
	deviceid = 3;
	BCSINK_FIFO_NAME[strlen(BCSINK_FIFO_NAME)-1]='3';
	BCINIT_FIFO_NAME[strlen(BCINIT_FIFO_NAME)-1]='3';
	BCACK_FIFO_NAME[strlen(BCACK_FIFO_NAME)-1]='3';
	fprintf(fd_instance,"%d", deviceid); 
  }
  
  fclose(fd_instance);

  /* All devices are busy - quit */
  if(deviceid == -1)
  {
	printf("All devices are busy, can't process the request ...\n");
	exit(0);	
  }
  
  fd_bcinit_fifo = open( BCINIT_FIFO_NAME, O_WRONLY );
  if(fd_bcinit_fifo < 0)
  {
	printf (" Failed to open bcinit_fifo FIFO - fd: %d\n", fd_bcinit_fifo);
	exit(0);
  }

  fd_bcsink_fifo = open( BCSINK_FIFO_NAME, O_WRONLY );
  if(fd_bcsink_fifo < 0)
  {
	printf (" Failed to open bcsink_fifo FIFO - fd: %d\n", fd_bcsink_fifo);
	exit(0);
  }

  /* Set it as non blocking as we expect some delay in buffer unreferencing
     to account for SGX deferred rendering architecture */
  fd_bcack_fifo = open( BCACK_FIFO_NAME, O_RDONLY | O_NONBLOCK );
  if(fd_bcack_fifo < 0)
  {
	printf (" Failed to open bciack_fifo FIFO - fd: %d\n", fd_bcack_fifo);
	exit(0);
  }
	/*Initialize packet dat with default values*/
	pack_info.xpos =0;
	pack_info.ypos =0;
	pack_info.width =0;
	pack_info.height =0;

}


static void
gst_render_bridge_dispose (GObject * object)
{
  GST_DEBUG_OBJECT (object, "ENTER");
  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_render_bridge_finalize (GstBufferClassSink * bcsink)
{
  GST_DEBUG_OBJECT (bcsink, "ENTER");
  if (G_LIKELY (bcsink->pool)) {
    gst_mini_object_unref (GST_MINI_OBJECT (bcsink->pool));
    bcsink->pool = NULL;
  }
  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (bcsink));
}

static void
gst_render_bridge_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstBufferClassSink *bcsink = GST_BCSINK (object);
  int temp;
  switch (prop_id) {
    case PROP_DEVICE:
     // g_free (bcsink->videodev);
     // bcsink->videodev = g_value_dup_string (value);
      break;

    case PROP_QUEUE_SIZE:
      bcsink->num_buffers = g_value_get_uint (value);
      break;

    case PROP_XPOS:
	pack_info.xpos = g_value_get_float (value);
	break;

    case PROP_YPOS:
	pack_info.ypos = g_value_get_float (value);
	break;

    case PROP_WIDTH:
	pack_info.width = g_value_get_float (value);
	break;

    case PROP_HEIGHT:
	pack_info.height = g_value_get_float (value);
	break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
gst_render_bridge_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstBufferClassSink *bcsink = GST_BCSINK (object);
  switch (prop_id) {
    case PROP_DEVICE:{
      //g_value_set_string (value, bcsink->videodev);
      break;
    }
    case PROP_QUEUE_SIZE:{
      g_value_set_uint (value, bcsink->num_buffers);
      break;
    }
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}


static GstStateChangeReturn
gst_render_bridge_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstBufferClassSink *bcsink = GST_BCSINK (element);

  GST_DEBUG_OBJECT (bcsink, "%d -> %d",
      GST_STATE_TRANSITION_CURRENT (transition),
      GST_STATE_TRANSITION_NEXT (transition));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      break;
    }
    default:{
      break;
    }
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      /* TODO stop streaming */
      break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:{
      g_signal_emit (bcsink, signals[SIG_CLOSE], 0);
      if (bcsink->pool) {
        gst_buffer_manager_dispose (bcsink->pool);
        bcsink->pool = NULL;
      }
      break;
    }
    default:{
      break;
    }
  }

  GST_DEBUG_OBJECT (bcsink, "end");

  return ret;
}

static gboolean
gst_render_bridge_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstBufferClassSink *bcsink = GST_BCSINK (bsink);

  g_return_val_if_fail (caps, FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  if (G_UNLIKELY (bcsink->pool)) {
    GstCaps *current_caps = gst_buffer_manager_get_caps (bcsink->pool);

    GST_DEBUG_OBJECT (bcsink, "already have caps: %" GST_PTR_FORMAT,
        current_caps);
    if (gst_caps_is_equal (current_caps, caps)) {
      GST_DEBUG_OBJECT (bcsink, "they are equal!");
      gst_caps_unref (current_caps);
      return TRUE;
    }

    gst_caps_unref (current_caps);
    GST_DEBUG_OBJECT (bcsink, "new caps are different: %" GST_PTR_FORMAT, caps);

    // TODO
    GST_DEBUG_OBJECT (bsink, "reallocating buffers not implemented yet");
    g_return_val_if_fail (0, FALSE);
  }

  GST_DEBUG_OBJECT (bcsink,
      "constructing bufferpool with caps: %" GST_PTR_FORMAT, caps);

  bcsink->pool =
      gst_buffer_manager_new (GST_ELEMENT (bcsink), pack_info,
      bcsink->num_buffers, caps);
  if (!bcsink->pool) {
	return FALSE;
}
  if (bcsink->num_buffers != bcsink->pool->num_buffers) {
    GST_DEBUG_OBJECT (bcsink, "asked for %d buffers, got %d instead",
        bcsink->num_buffers, bcsink->pool->num_buffers);
    bcsink->num_buffers = bcsink->pool->num_buffers;
    g_object_notify (G_OBJECT (bcsink), "queue-size");
  }

  g_signal_emit (bcsink, signals[SIG_INIT], 0, bcsink->num_buffers);

  return TRUE;
}


/** buffer alloc function to implement pad_alloc for upstream element */
static GstFlowReturn
gst_render_bridge_buffer_alloc (GstBaseSink * bsink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstBufferClassSink *bcsink = GST_BCSINK (bsink);

  if (G_UNLIKELY (!bcsink->pool)) {
    /* it's possible caps haven't been set yet: */
    gst_render_bridge_set_caps (bsink, caps);
    if (!bcsink->pool)
      return GST_FLOW_ERROR;
  }

  *buf = GST_BUFFER (gst_buffer_manager_get (bcsink->pool));

  if (G_LIKELY (buf)) {
    GST_DEBUG_OBJECT (bcsink, "allocated buffer: %p", *buf);
    return GST_FLOW_OK;
  }

  GST_DEBUG_OBJECT (bcsink, "failed to allocate buffer");
  return GST_FLOW_ERROR;
}



/** called after A/V sync to render frame */
static GstFlowReturn
gst_render_bridge_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{
  GstBufferClassSink *bcsink = GST_BCSINK (bsink);
  GstBufferClassBuffer *bcbuf;
  GstBufferClassBuffer *bcbuf_rec;
  GstBuffer *newbuf = NULL;
  int n;
  static int queue_counter=0;

  GST_DEBUG_OBJECT (bcsink, "render buffer: %p", buf);

  if (G_UNLIKELY (!GST_IS_BCBUFFER (buf))) {
    GstFlowReturn ret;

    GST_DEBUG_OBJECT (bcsink, "slow-path.. I got a %s so I need to memcpy",
        g_type_name (G_OBJECT_TYPE (buf)));
    ret = gst_render_bridge_buffer_alloc (bsink,
        GST_BUFFER_OFFSET (buf), GST_BUFFER_SIZE (buf), GST_BUFFER_CAPS (buf),
        &newbuf);

    if (GST_FLOW_OK != ret) {
      GST_DEBUG_OBJECT (bcsink,
          "dropping frame!  Consider increasing 'queue-size' property!");
      return GST_FLOW_OK;
    }

    memcpy (GST_BUFFER_DATA (newbuf),
        GST_BUFFER_DATA (buf),
        MIN (GST_BUFFER_SIZE (newbuf), GST_BUFFER_SIZE (buf)));

    GST_DEBUG_OBJECT (bcsink, "render copied buffer: %p", newbuf);

    buf = newbuf;
  }

  bcbuf = GST_BCBUFFER (buf);

  /* cause buffer to be flushed before rendering */
  gst_bcbuffer_flush (bcbuf);

  //g_signal_emit (bcsink, signals[SIG_RENDER], 0, bcbuf->index);

  gst_buffer_ref(bcbuf);
/*****************************************************************
******************************************************************/

/* Populate the packet data to be communicated accross pipes */	
  datapacket.buf = bcbuf;
  datapacket.index = bcbuf->index;
  
  n = write(fd_bcsink_fifo, &datapacket, sizeof(bc_gstpacket));


  if(n != sizeof(bc_gstpacket))
  {
	printf("Error in writing to queue\n");
  }

  n = read(fd_bcack_fifo, &bcbuf_rec, sizeof(GstBufferClassBuffer *));

  /* To account for the delay in unreferencing the buffer from the renderer read is non blocking */
  if(n == sizeof(GstBufferClassBuffer *))
  {
	gst_buffer_unref(bcbuf_rec);
  }

/*****************************************************************
******************************************************************/

  /* note: it would be nice to know when the driver is done with the buffer..
   * but for now we don't keep an extra ref
   */

  if (newbuf) {
    gst_buffer_unref (newbuf);
  }

  return GST_FLOW_OK;
}
