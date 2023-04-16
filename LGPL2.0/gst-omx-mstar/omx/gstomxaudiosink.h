/*
 * Copyright (C) 2014, Fluendo, S.A.
 * Copyright (C) 2014, Metrological Media Innovations B.V.
 *   Author: Josep Torra <josep@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_OMX_AUDIO_SINK_H__
#define __GST_OMX_AUDIO_SINK_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstomx.h"

G_BEGIN_DECLS

#define MIRACAST_SINK_ADD_BUFFER_LIST                       1
#define STORE_MODE_PROTECTING_CODE_USED_G_TIMEOUT           1
//for ics certification : ICS003 Audio Mute Issue, currently only for app-type "mheg-ics"
#define PATCH_AUDIO_UNDERRUN_FOR_ICS                        1

#define GST_TYPE_OMX_AUDIO_SINK \
  (gst_omx_audio_sink_get_type())
#define GST_OMX_AUDIO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OMX_AUDIO_SINK,GstOMXAudioSink))
#define GST_OMX_AUDIO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OMX_AUDIO_SINK,GstOMXAudioSinkClass))
#define GST_OMX_AUDIO_SINK_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_OMX_AUDIO_SINK,GstOMXAudioSinkClass))
#define GST_IS_OMX_AUDIO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OMX_AUDIO_SINK))
#define GST_IS_OMX_AUDIO_SINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OMX_AUDIO_SINK))
#define GST_OMX_AUDIO_SINK_CAST(obj)      ((GstOMXAudioSink *) (obj))

#define GST_OMX_AUDIO_SINK_GET_LOCK(obj)	(&GST_OMX_AUDIO_SINK_CAST (obj)->lock)
#define GST_OMX_AUDIO_SINK_LOCK(obj)	    (g_mutex_lock (GST_OMX_AUDIO_SINK_GET_LOCK (obj)))
#define GST_OMX_AUDIO_SINK_UNLOCK(obj)    (g_mutex_unlock (GST_OMX_AUDIO_SINK_GET_LOCK (obj)))

#define PASSTHROUGH_CAPS \
    "audio/x-ac3, framed = (boolean) true;" \
    "audio/x-eac3, framed = (boolean) true; " \
    "audio/x-dts, framed = (boolean) true, " \
      "block-size = (int) { 512, 1024, 2048 }; " \
    "audio/mpeg, mpegversion = (int) 1, " \
      "mpegaudioversion = (int) [ 1, 2 ], parsed = (boolean) true;"

typedef struct _GstOMXAudioSink GstOMXAudioSink;
typedef struct _GstOMXAudioSinkClass GstOMXAudioSinkClass;

#define MSTEMPO_PTS_POOL_SIZE 20

struct _GstOMXAudioSink
{
  GstAudioSink parent;

  /* < protected > */
  GstOMXComponent *comp;
  GstOMXPort *in_port, *out_port;
  
  gboolean mute;
  gdouble volume;

  gboolean iec61937;
  guint endianness;
  guint rate;
  guint channels;
  guint width;
  gboolean is_signed;
  gboolean is_float;

  guint buffer_size;
  guint samples;

  GMutex lock;

  /* If there is no mstar audio decoder at the front, set to FALSE */
  gboolean have_audio_decoder;

  // customized info
  gint index;
  gchar *app_type;
  gboolean bLowDelay;
  gboolean bNonPcmGap;

  GstPadChainFunction base_sink_chain_func;

  /* RTC used start */
#if (MIRACAST_SINK_ADD_BUFFER_LIST==1)
  GSList*     rtc_list;
  GstTask*    rtc_task;
  GRecMutex   rtc_rec_lock;
  GMutex      rtc_thread_mutex;
  gboolean    rtc_drop_at_start;
  guint       rtc_drop_count;
  gboolean    rtc_fast_play;
#endif
  /* RTC used end */

  guint nodelay_mode;
  guint nodelay_skip;
  guint nodelay_fast;
  guint nodelay_recover;
  guint32 nodelay_fast_freq;

  guint64 current_pts;

  gint ease_target_volume;
  gchar *ease_type;
  gint ease_duration;

  GstClockTime ease_start_time;
  GstClockTime ease_pause_time;

  gint current_ease_volume;

  guint lipsync_offset;
  gint ringbuffer_segdone;

  GstClockTime    firstTimeStamp;
  GstClockTime    prvTimeStamp;
  float           playrate;
#if 1 //MS_TEMPO
  guint64 tempo_pts;
  gchar *tempo_inbuf;
  gchar *tempo_outbuf;
  gint *tempo_handle;
  gint tempo_framesize;
  float  tempo_trick_f;
  GstClockTime tempo_pts_pool[MSTEMPO_PTS_POOL_SIZE+1]; // quene previous pts
  gint         tempo_pcmsize_pool[MSTEMPO_PTS_POOL_SIZE+1];        
  gint         tempo_pts_index;   
  //for preroll
  gboolean tempo_is_preroll;
  gint8*  tempo_preroll;
  guint32 tempo_preroll_index;
  guint32 tempo_preroll_length;
#endif

#if (STORE_MODE_PROTECTING_CODE_USED_G_TIMEOUT==1)
  guint           protecting_g_timeout_func;
  GMutex          protecting_g_timeout_lock;
  GstClockTime    protecting_g_timeout_pcm_coming_Time;
#endif

  gboolean disable_lost_state;

#if (PATCH_AUDIO_UNDERRUN_FOR_ICS==1)
  GstClockTime audio_under_run_basetime;
  gboolean asink_is_eos_received;
  gboolean asink_gap_no_duration;
#endif
};

struct _GstOMXAudioSinkClass
{
  GstAudioSinkClass parent_class;

  GstOMXClassData cdata;
  const gchar * destination;
};

GType gst_omx_audio_sink_get_type (void);

G_END_DECLS

#endif /* __GST_OMX_AUDIO_SINK_H__ */

