////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2008-2015 MStar Semiconductor, Inc.
// All rights reserved.
//
// Unless otherwise stipulated in writing, any and all information contained
// herein regardless in any format shall remain the sole proprietary of
// MStar Semiconductor Inc. and be kept in strict confidence
// ("MStar Confidential Information") by the recipient.
// Any unauthorized act including without limitation unauthorized disclosure,
// copying, use, reproduction, sale, distribution, modification, disassembling,
// reverse engineering and compiling of the contents of MStar Confidential
// Information is unlawful and strictly prohibited. MStar hereby reserves the
// rights to any and all damages, losses, costs and expenses resulting therefrom.
//
////////////////////////////////////////////////////////////////////////////////

#ifndef __GST_MADECBIN_H__
#define __GST_MADECBIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_MADEC_BIN                        (gst_madec_bin_get_type())
#define GST_MADEC_BIN(obj)                           (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_MADEC_BIN, Gstmadecbin))
#define GST_MADEC_BIN_CLASS(klass)            (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_MADEC_BIN, GstmadecbinClass))
#define GST_MADEC_BIN_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_MADEC_BIN, GstmadecbinClass))
#define GST_IS_MADEC_BIN(obj)                 (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_MADEC_BIN))
#define GST_IS_MADEC_BIN_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_MADEC_BIN))

typedef struct _Gstmadecbin Gstmadecbin;
typedef struct _GstmadecbinClass GstmadecbinClass;



struct _Gstmadecbin
{
    GstBin 	 bin;

    GstElement *decoder;          /* this holds the decoder object */
    GstPad *gpad_sink;
    GstPad *gpad_src;

    gboolean     silent;

    gint index;
    gint audio_format;
    gboolean bDtsSeamless;
};

struct _GstmadecbinClass
{
    GstBinClass parent_class;

};

/// codec type enumerator
typedef enum MADECBIN_CODECTYPE {
    // unsupported codec type

    E_MADECBIN_CODEC_TYPE_NONE = 0,
    E_MADECBIN_CODEC_TYPE_MPEG2,
    E_MADECBIN_CODEC_TYPE_MP3,
    E_MADECBIN_CODEC_TYPE_AAC,
    E_MADECBIN_CODEC_TYPE_AC3,
    E_MADECBIN_CODEC_TYPE_DTS,
    E_MADECBIN_CODEC_TYPE_AMR,
    E_MADECBIN_CODEC_TYPE_RA,
    E_MADECBIN_CODEC_TYPE_VORBIS,
    E_MADECBIN_CODEC_TYPE_ADPCM,
    E_MADECBIN_CODEC_TYPE_WMA,
    E_MADECBIN_CODEC_TYPE_FLAC,
    E_MADECBIN_CODEC_TYPE_LPCM,
    E_MADECBIN_CODEC_TYPE_NUM

} MADECBIN_CODECTYPE;

GType gst_madec_bin_get_type (void);

G_END_DECLS

#endif /* __GST_MADECSINK_H__ */

