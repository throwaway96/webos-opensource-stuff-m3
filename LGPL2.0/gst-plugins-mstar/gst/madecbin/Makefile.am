plugin_LTLIBRARIES = libgstmadecbin.la

# sources used to compile this plug-in
libgstmadecbin_la_SOURCES = \
	plugin.c \
	gstmadecbin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstmadecbin_la_CFLAGS = $(GST_CFLAGS) $(MSILOMX_CFLAGS)
libgstmadecbin_la_LIBADD = $(GST_LIBS) $(MSILOMX_LIBS) -lgstvideo-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@
libgstmadecbin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmadecbin_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstmadecbin.h
