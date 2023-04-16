/***
 * MStreamer
 *
 * $Id: //MPL/MStreamer/element/masink/source/masink.h#4 $
 * $Header: //MPL/MStreamer/element/masink/source/masink.h#4 $
 * $Date: 2010/08/03 $
 * $DateTime: 2010/08/03 19:22:49 $
 * $Change: 303291 $
 * $File: //MPL/MStreamer/element/masink/source/masink.h $
 * $Revision: #4 $
 * $Author: MPL $
 *
 */

////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2014 MStar Semiconductor, Inc.
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
/// file    masink.h
/// @brief  mstar video sink header file
/// @author MStar Semiconductor Inc.
///////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef __GST_MASINK_H__
#define __GST_MASINK_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>

#include "msil/msil_audio.h"
#include "msil.h"

G_BEGIN_DECLS

#define GST_TYPE_MASINK            (gst_masink_get_type())
#define GST_MASINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_MASINK, GstMasink))
#define GST_MASINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_MASINK, GstMasinkClass))
#define GST_IS_MASINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_MASINK))
#define GST_IS_MASINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_MASINK))
#define GST_MASINK_CAST(obj)       ((GstMasink *) (obj))


typedef struct _GstMasink GstMasink;
typedef struct _GstMasinkClass GstMasinkClass;

#define GST_MASINK_GET_LOCK(obj)    (GST_MASINK_CAST (obj)->masink_lock)
#define GST_MASINK_LOCK(obj)	    (g_mutex_lock (GST_MASINK_GET_LOCK (obj)))
#define GST_MASINK_UNLOCK(obj)	    (g_mutex_unlock (GST_MASINK_GET_LOCK (obj)))

struct _GstMasink
{
    GstAudioSink sink;

  /* dSound buffer size */
    guint buffer_size;

  /* current volume setup by mixer interface */
    guint volume;

    guint64   WriteFrame;

    GstCaps *cached_caps;

    GstAudioRingBufferSpec *spec; //MUSTFIX

    gpointer SinkHandle;

    gboolean SPDIF_non_pcm;
    gboolean HDMI_non_pcm;

    gboolean mute;

    gint     DecId;
    gint     aType;

    guint32 pcm3Level;

    GMutex* masink_lock;
    guint32 resetflag;
    guint32 dropCnt;

    gchar*         app_type;

    GstPadEventFunction  sink_event_func;

    gboolean disable_lost_state;

};

struct _GstMasinkClass
{
    GstAudioSinkClass parent_class;

};

GType gst_masink_get_type (void);

G_END_DECLS

#endif /* __GST_MASINK_H__ */

