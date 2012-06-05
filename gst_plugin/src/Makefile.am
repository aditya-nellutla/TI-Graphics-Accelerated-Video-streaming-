
-include rules.make
plugin_LTLIBRARIES = libgstbc.la

AM_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
AM_LIBS = $(GST_BASE_LIBS)

noinst_HEADERS = \
	gst_buffer_manager.h  \
	gst_render_bridge.h

libgstbc_la_SOURCES = \
	gst_buffer_manager.c \
	gst_render_bridge.c  \
	gstsink_plugin.c

GSDK_KM_DIR = $(GSDK_ROOT)/GFX_Linux_KM
CMEM_LIB     ?= $(CMEM_DIR)/lib/cmem.a470MV
GST_PLUGIN_BASE = $(GSTREAMER_SRC_DIR)/gst-plugins-base-0.10.30/

# check order of CFLAGS and LIBS, shouldn't the order be the other way around
# (like in AM_CFLAGS)?
libgstbc_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	-DLINUX \
	-I$(GSDK_KM_DIR)/include4 \
	-I$(CMEM_DIR)/include \
	-I$(GSDK_KM_DIR)/services4/include \
	-I$(GSDK_KM_DIR)/services4/3rdparty/bufferclass_ti \
	-I$(GSDK_ROOT)/include/OGLES \
	-I$(GSDK_ROOT)/include/OGLES2 \
	-I$(top_srcdir)/khronos \
	-I$(top_srcdir)/module \
	-I$(GST_PLUGIN_BASE)/gst-libs/

libgstbc_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
        -L$(TGTFS_PATH)/lib  -lgstvideo-0.10 \
        -L$(GSDK_ROOT)/gfx_rel_es5.x  -lGLESv2 -lEGL	

AM_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(libgstbc_la_CFLAGS) $(libgstbc_la_LIBADD)
libgstbc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstbc_la_LDFLAGS += -Wl,$(CMEM_LIB) 
