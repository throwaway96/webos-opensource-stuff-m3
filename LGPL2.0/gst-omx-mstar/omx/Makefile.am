plugin_LTLIBRARIES = libgstomx.la

VP8_C_FILES=gstomxvp8dec.c
VP8_H_FILES=gstomxvp8dec.h

if HAVE_THEORA
THEORA_C_FILES=gstomxtheoradec.c
THEORA_H_FILES=gstomxtheoradec.h
endif

libgstomx_la_SOURCES = \
	gstomx.c \
	mstar_video_meta.c \
	gstomxvideodec.c \
	gstomxvideoenc.c \
	gstomxaudiodec.c \
	gstomxaudioenc.c \
	gstomxmjpegdec.c \
	gstomxmpeg4videodec.c \
	gstomxmpeg2videodec.c \
	gstomxh264dec.c \
	gstomxh263dec.c \
	gstomxwmvdec.c \
    gstomxmvcdec.c \
	$(VP8_C_FILES) \
	$(THEORA_C_FILES) \
	gstomxvp9dec.c \
	gstomxmpeg4videoenc.c \
	gstomxh264enc.c \
	gstomxh263enc.c \
	gstomxaacenc.c \
	gstomxaudiosink.c \
	gstomxanalogaudiosink.c \
	gstomxhdmiaudiosink.c \
	gstomxdivx4dec.c \
	gstomxdivx3dec.c \
	gstomxrvdec.c \
	gstomxavsdec.c \
	gstomxhevcdec.c \
	gstomxvc1dec.c \
	gstomxflvdec.c \
	gstomxmp3dec.c \
	gstomxmp2dec.c \
	gstomxwmadec.c \
	gstomxac3dec.c \
	gstomxaacdec.c \
	gstomxvorbisdec.c \
	gstomxdtsdec.c \
	gstomxadpcmdec.c \
	gstomxamrdec.c \
	gstomxradec.c \
	gstomxflacdec.c \
	gstomxlpcmdec.c

noinst_HEADERS = \
	gstomx.h \
	mstar_video_meta.h \
	mst_tempo.h \
	gstomxvideodec.h \
	gstomxvideoenc.h \
	gstomxaudiodec.h \
	gstomxaudioenc.h \
	gstomxmjpegdec.h \
	gstomxmpeg2videodec.h \
	gstomxmpeg4videodec.h \
	gstomxh264dec.h \
	gstomxh263dec.h \
	gstomxwmvdec.h \
    gstomxmvcdec.h \
	$(VP8_H_FILES) \
	$(THEORA_H_FILES) \
	gstomxvp9dec.h \
	gstomxmpeg4videoenc.h \
	gstomxh264enc.h \
	gstomxh263enc.h \
	gstomxaacenc.h \
	gstomxaudiosink.h \
	gstomxanalogaudiosink.h \
	gstomxhdmiaudiosink.h \
	gstomxdivx4dec.h \
	gstomxdivx3dec.h \
	gstomxrvdec.h \
	gstomxavsdec.h \
	gstomxhevcdec.h \
	gstomxvc1dec.h \
	gstomxflvdec.h \
	gstomxmp3dec.h \
	gstomxmp2dec.h \
	gstomxwmadec.h \
	gstomxac3dec.h \
	gstomxaacdec.h \
	gstomxvorbisdec.h \
	gstomxdtsdec.h \
	gstomxadpcmdec.h \
	gstomxamrdec.h \
	gstomxradec.h \
	gstomxflacdec.h \
	gstomxlpcmdec.h

if !HAVE_EXTERNAL_OMX
OMX_INCLUDEPATH = -I$(abs_srcdir)/openmax
endif

libgstomx_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API=1 \
	$(OMX_INCLUDEPATH) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstomx_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	-lgstvideo-@GST_API_VERSION@ \
	-lmstempo \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstomx_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -L.

EXTRA_DIST = \
	openmax \
	gstomxvp8dec.c \
	gstomxvp8dec.h \
	gstomxtheoradec.c \
	gstomxtheoradec.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstomx -:SHARED libgstomx \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstomx_la_SOURCES) \
	           $(nodist_libgstomx_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstomx_la_CFLAGS) \
	 -:LDFLAGS $(libgstomx_la_LDFLAGS) \
	           $(libgstomx_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-$(GST_API_VERSION)' \
	> $@
