plugin_LTLIBRARIES = libgstmasink.la

# sources used to compile this plug-in
libgstmasink_la_SOURCES = gstmasink.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstmasink_la_CFLAGS = $(GST_CFLAGS) $(MSILOMX_CFLAGS)
libgstmasink_la_LIBADD = $(GST_LIBS) $(MSILOMX_LIBS) -lgstvideo-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@
libgstmasink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmasink_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstmasink.h
