plugin_LTLIBRARIES = libgstmvsink.la

# sources used to compile this plug-in
libgstmvsink_la_SOURCES = gstmvsink.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstmvsink_la_CFLAGS = $(GST_CFLAGS) $(MSILOMX_CFLAGS) -DGST_MS_OMX
libgstmvsink_la_LIBADD = $(GST_LIBS) $(MSILOMX_LIBS) -lgstvideo-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@
libgstmvsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmvsink_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstmvsink.h
