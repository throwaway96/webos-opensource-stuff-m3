/***
 * MStreamer
 *
 * $Id: //MPL/MStreamer/element/mvsink/source/mvsink.h#4 $
 * $Header: //MPL/MStreamer/element/mvsink/source/mvsink.h#4 $
 * $Date: 2010/08/03 $
 * $DateTime: 2010/08/03 19:22:49 $
 * $Change: 303291 $
 * $File: //MPL/MStreamer/element/mvsink/source/mvsink.h $
 * $Revision: #4 $
 * $Author: MPL $
 *
 */

////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2014-2015 MStar Semiconductor, Inc.
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

///////////////////////////////////////////////////////////////////////////////////////////////////
///
/// file    mvsink.h
/// @brief  mstar video sink header file
/// @author MStar Semiconductor Inc.
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __MVSINK_H__
#define __MVSINK_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>
#include "msil.h"
#if VSYNC_BRIDGE
#include "msil/MsFrmFormat.h"
#include "msil/vsync_bridge.h"
#endif

G_BEGIN_DECLS

#define GST_TYPE_MVSINK \
  (gst_mvsink_get_type())
#define GST_MVSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MVSINK,GSTMVSink))
#define GST_MVSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MVSINK,GSTMVSinkClass))
#define GST_IS_FBDEVSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MVSINK))
#define GST_IS_FBDEVSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MVSINK))

typedef enum {
  GST_MVSINK_OPEN      = (GST_ELEMENT_FLAG_LAST << 0),

  GST_MVSINK_FLAG_LAST = (GST_ELEMENT_FLAG_LAST << 2),
} GSTMVSinkFlags;

typedef struct _GSTMVSink GSTMVSink;
typedef struct _GSTMVSinkClass GSTMVSinkClass;

#define MAX_FRAME_BUFF_SIZE 8

enum
{
  STREAM_AUDIO = 0,
  STREAM_VIDEO,
  STREAM_TEXT,
  STREAM_LAST
};

struct _GSTMVSink {
    GstVideoSink videosink;

    int fps_n, fps_d;

    unsigned char u8AspectRate;
    unsigned char bProgressive;
    GstClock *clock;
    gint64 max_lateness;

    gint32 tvmode;
    gint32 plane;
    GArray *rectangle;
    guint64 current_pts;
    gboolean flush_repeat_frame;
    guint64 inter_frame_delay;
    gint32 slow_mode_rate;
    gint32 step_frame;
    guint32 content_framerate;
    gboolean mute;
    gboolean XCInit;
    gboolean bThumbnaiMode;
    gboolean b2Stream3D;
    guint window_id;
    gboolean seamlessPlay;
    gboolean flushing;
    gchar *app_type;
    GstState state;
    guint32 disp_frame_count;
    pthread_t free_thread;
    gboolean stop_thread;
    GstBuffer *frame_buf[MAX_FRAME_BUFF_SIZE];
    guint frame_buf_w_index;
    guint frame_buf_r_index;
    MZ_MVOP_InputSel enInputSel;
    MZ_WINDOW_TYPE stDispWin;
    MZ_MVOP_CFG stMVopCfg;
    MZ_MVOP_Module mvop_module;
    GstPadEventFunction sink_event_func;
    gdouble playrate;
    gboolean firstFrame;
    gboolean firstsync;
    guint uiConstantDelay;
    guint uiLowDelay;
    guint iInterleaving;
    guint the3DLayout;
    guint par_width;
    guint par_height;
    MS_HDRInfo hdr_info;
    gulong renderDelay;
    guint frame_width;
    guint frame_height;
    gboolean interlace;
    gboolean is_support_decoder;
    guint asf3DType;
    gboolean bSetAsf3DType;
    gint32 consecutive_drop_count;
    guint32 mvop_framerate;
    GstClockTime average_delay;
};

struct _GSTMVSinkClass {
  GstVideoSinkClass parent_class;
    GstBuffer *(*convert_frame)(GSTMVSink *playsink, GstCaps *caps);
    GstBuffer *(*get_last_sample)(GSTMVSink *playsink, GstCaps *caps);
};

GstBuffer *gst_mvsink_convert_frame(GSTMVSink *playsink, GstCaps *caps);
GstBuffer *gst_mvsink_get_last_sample(GSTMVSink *playsink, GstCaps *caps);

GType gst_mvsink_get_type(void);

G_END_DECLS

#endif /* __MVSINK_H__ */
