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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <gst/gst.h>
#include <gst/audio/audio.h>

//#include <math.h>
#include <stdio.h>

#include "gstomxaudiosink.h"

#include "OMX_Index.h"
#include "OMX_Other.h"
#include "OMX_AudioExt.h"
#include "OMX_IndexExt.h"
// enable tempo function in sink
//#define MS_TEMPO_SINK
//#define MS_TEMPO_NEW_PTS
#define MAX_STRING_LEN 1024
#ifdef MS_TEMPO_SINK
#define TEMPO_BUF_SIZE 624*1024
#define TEMPO_PROCESS_SIZE_MAX 32*1024  // this is to avoid long tempo process time, which would cause ring buffer empty and thus render get mute samples
#include "mst_tempo.h"
#endif

GST_DEBUG_CATEGORY_STATIC (gst_omx_audio_sink_debug_category);
#define GST_CAT_DEFAULT gst_omx_audio_sink_debug_category

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_audio_sink_debug_category, "omxaudiosink", \
      0, "debug category for gst-omx audio sink base class");

#define DEFAULT_PROP_MUTE            FALSE
#define DEFAULT_PROP_VOLUME          1.0
#define DEFAULT_APP_TYPE             "default_app_type"
#define DEFAULT_PROP_NODELAY         FALSE

#define DEFAULT_NODELAY_MODE         2
#define DEFAULT_NODELAY_SKIP         450
#define DEFAULT_NODELAY_FAST         300
#define DEFAULT_NODELAY_RECOVER      150
#define DEFAULT_NODELAY_FAST_FREQ    10250
#define DEFAULT_LIPSYNC_OFFSET       0

#define NODELAY_ASINK_DROP_FRAME_NUMBER         5
#define CORRECT_TOLERANCE ((2*90 * GST_MSECOND) / GST_USECOND)  // consider positive and nagative varience, so must be multiplied by 2

#define DEFAULT_RTC_LATENCY       87    /* gst ringbuffer (50ms) + driver mixer (40ms) + DMA reader (70ms) */

#define VOLUME_MAX_DOUBLE       10.0
#define OUT_CHANNELS(num_channels) ((num_channels) > 4 ? 12: (num_channels) > 2 ? 4: (num_channels))

// customized mixer setting
#define DEFAULT_PROP_MIXER       TRUE
#define DEFAULT_PROP_SERVER_TRICK       FALSE

#define DEFAULT_PROP_RINGBUFFER_DELAY_NORMAL ((200 * GST_MSECOND) / GST_USECOND)        // normal delay
#define DEFAULT_PROP_RINGBUFFER_DELAY_LOW       ((30 * GST_MSECOND) / GST_USECOND)      // low delay mode

enum
{
  PROP_0,
  PROP_MUTE,                    // set      // bool     // mute/unmute
  PROP_VOLUME,
  PROP_INDEX,
  PROP_RESOURCE_INFO,           // set  // GstStructure // set audio output port    // audio-port:  0, 1
  PROP_MIXER,                   // set  // bool // set mixer mode    // use pcm mix
  PROP_SERVERSIDE_TRICKPLAY,    // set  // bool // set server's trick play mode //
  PROP_APP_TYPE,                // set // string    // set app type // "RTC" for Miracast app type
  PROP_DECODED_SIZE,            // get  // unsigned long long   // size of total audio decoded ES data
  PROP_UNDECODED_SIZE,          // get  // unsigned long long   // bytes of undecoded ES size
  PROP_CURRENT_PTS,             // get  // unsigned long long   // get rendering timing audio position
  PROP_LIPSYNC_OFFSET,
  PROP_NO_DELAY,                // set   // bool         // enable/disable no delay mode // miracast always set this value for low delay mode
  PROP_NO_DELAY_MODE,           // set   // unsigned integer     // miracast always set this value to 2 for hybrid mode
  PROP_NO_DELAY_SKIP,           // set   // unsigned integer // set buffering threshold of skippping // Miracast always set this value to 450 for dropping the data in the audio buffer when exceeds 450ms data in the buffer
  PROP_NO_DELAY_FAST,           // set   // junsigned integer    // set buffering threshold of Fast      // Miracast always set this value to 300 for fast play mode when exceeds 300ms in the buffer
  PROP_NO_DELAY_RECOVER,        // set   // unsigned integer     // set buffering threshold of normal state      // Miracast always set this value to 150 for recovering playing mode from "fast" or "skip" mode, under the 150ms in the audio buffer disable fast or skip mode
  PROP_NO_DELAY_FAST_FREQ,      // set   // unsigned integer     // set freq of Fast State       // Miracast always set this value to 47250 for changing playrate
  PROP_NO_SEAMLESS,             // set   // bool         // seamless audio change         // for HbbTV
  PROP_DISABLE_LOST_STATE,
  PROP_FADE,
  PROP_FADE_VOLUME
};

enum
{
  /* FILL ME */
  AUDIO_UNDERRUN,
  LAST_SIGNAL
};

enum
{
  FORMAT_TYPE_AAC,
  FORMAT_TYPE_MPEG,
  FORMAT_TYPE_VORBIS,
  FORMAT_TYPE_AC3,
  FORMAT_TYPE_EAC3,
  FORMAT_TYPE_WMA,
  FORMAT_TYPE_DTS,
  FORMAT_TYPE_AMR,
  FORMAT_TYPE_LPCM,
  FORMAT_TYPE_RA,
  FORMAT_TYPE_FLAC,
  FORMAT_TYPE_ADPCM,
};


static guint gst_omx_audio_sink_signal[LAST_SIGNAL] = { 0 };

#define gst_omx_audio_sink_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXAudioSink, gst_omx_audio_sink,
    GST_TYPE_AUDIO_SINK, G_IMPLEMENT_INTERFACE (GST_TYPE_STREAM_VOLUME, NULL);
    DEBUG_INIT);

#define transform_3_4(type) \
static inline void \
transform_3_4_##type (gpointer psrc, gpointer pdst, guint len) \
{ \
  g##type *src = (g##type *) psrc; \
  g##type *dst = (g##type *) pdst; \
  for (; len > 0; len--) { \
    dst[0] = src[0]; \
    dst[1] = src[1]; \
    dst[2] = src[2]; \
    dst[3] = 0; \
    src += 3; \
    dst += 4; \
  } \
}

#define transform_5_8(type) \
static inline void \
transform_5_8_##type (gpointer psrc, gpointer pdst, guint len) \
{ \
  g##type *src = (g##type *) psrc; \
  g##type *dst = (g##type *) pdst; \
  for (; len > 0; len--) { \
    dst[0] = src[0]; \
    dst[1] = src[1]; \
    dst[2] = src[2]; \
    dst[3] = src[3]; \
    dst[4] = src[4]; \
    dst[5] = 0; \
    dst[6] = 0; \
    dst[7] = 0; \
    src += 5; \
    dst += 8; \
  } \
}

#define transform_6_8(type) \
static inline void \
transform_6_8_##type (gpointer psrc, gpointer pdst, guint len) \
{ \
  g##type *src = (g##type *) psrc; \
  g##type *dst = (g##type *) pdst; \
  for (; len > 0; len--) { \
    dst[0] = src[0]; \
    dst[1] = src[1]; \
    dst[2] = src[2]; \
    dst[3] = src[3]; \
    dst[4] = src[4]; \
    dst[5] = src[5]; \
    dst[6] = 0; \
    dst[7] = 0; \
    src += 6; \
    dst += 8; \
  } \
}

#define transform_7_8(type) \
static inline void \
transform_7_8_##type (gpointer psrc, gpointer pdst, guint len) \
{ \
  g##type *src = (g##type *) psrc; \
  g##type *dst = (g##type *) pdst; \
  for (; len > 0; len--) { \
    dst[0] = src[0]; \
    dst[1] = src[1]; \
    dst[2] = src[2]; \
    dst[3] = src[3]; \
    dst[4] = src[4]; \
    dst[5] = src[5]; \
    dst[6] = src[6]; \
    dst[7] = 0; \
    src += 7; \
    dst += 8; \
  } \
}

transform_3_4 (int16);
transform_5_8 (int16);
transform_6_8 (int16);
transform_7_8 (int16);

transform_3_4 (int32);
transform_5_8 (int32);
transform_6_8 (int32);
transform_7_8 (int32);

static void inline
transform (guint in_chan, guint width, gpointer psrc, gpointer pdst, guint len)
{
  guint out_chan = OUT_CHANNELS (in_chan);
  if (width == 16) {
    switch (out_chan) {
      case 4:
        if (in_chan == 3) {
          transform_3_4_int16 (psrc, pdst, len);
        } else {
          g_assert (FALSE);
        }
        break;

      case 8:
        switch (in_chan) {
          case 5:
            transform_5_8_int16 (psrc, pdst, len);
            break;
          case 6:
            transform_6_8_int16 (psrc, pdst, len);
            break;
          case 7:
            transform_7_8_int16 (psrc, pdst, len);
            break;
          default:
            g_assert (FALSE);
            break;
        }
        break;

      default:
        g_assert (FALSE);
    }
  } else if (width == 32) {
    switch (out_chan) {
      case 4:
        if (in_chan == 3) {
          transform_3_4_int32 (psrc, pdst, len);
        } else {
          g_assert (FALSE);
        }
        break;

      case 8:
        switch (in_chan) {
          case 5:
            transform_5_8_int32 (psrc, pdst, len);
            break;

          case 6:
            transform_6_8_int32 (psrc, pdst, len);
            break;

          case 7:
            transform_7_8_int32 (psrc, pdst, len);
            break;

          default:
            g_assert (FALSE);
            break;
        }
        break;

      default:
        g_assert (FALSE);
    }
  } else {
    g_assert (FALSE);
  }
}

#if 0                           // for lpcm skip frame
static void
update_timestamps (GstOMXAudioSink * self, GstBuffer * buf)
{
  GstMapInfo minfo;
  GstClockTime duration;

  gst_buffer_map (buf, &minfo, GST_MAP_READ);

  if (!GST_CLOCK_TIME_IS_VALID (self->firstTimeStamp)
      && (GST_BUFFER_TIMESTAMP_IS_VALID (buf))) {

    self->firstTimeStamp = GST_BUFFER_TIMESTAMP (buf);
    self->prvTimeStamp = self->firstTimeStamp + gst_util_uint64_scale (minfo.size / 4, GST_SECOND, self->rate); //samples = minfo.size/4
    gst_buffer_unmap (buf, &minfo);
    return;
  }

  duration = gst_util_uint64_scale (minfo.size / 4, GST_SECOND, self->rate);
  if (GST_CLOCK_TIME_IS_VALID (self->prvTimeStamp)) {
    GST_BUFFER_TIMESTAMP (buf) = self->prvTimeStamp;
  }
  self->prvTimeStamp += duration;

  gst_buffer_unmap (buf, &minfo);
}
#endif
#if (MIRACAST_SINK_ADD_BUFFER_LIST==1)
static void
check_video_position (GstOMXAudioSink * self, GstBuffer * buf)
{
#if 0
  GstElement *el, *el1;
  GstIterator *pIt2;
  GValue data2 = { 0, };
  gboolean done2;
  GstClockTime vsinkposition = GST_CLOCK_TIME_NONE;

  el = el1 = GST_ELEMENT_PARENT (self);
  while (el) {
    el1 = el;
    el = GST_ELEMENT_PARENT (el1);
  }

  pIt2 = gst_bin_iterate_elements ((GstBin *) el1);

  done2 = FALSE;
  while (!done2) {
    switch (gst_iterator_next (pIt2, &data2)) {
      case GST_ITERATOR_OK:
      {
        GstElement *child2 = g_value_get_object (&data2);

        if (g_str_has_prefix (GST_ELEMENT_NAME (child2), "gstmvsink")) {
          guint64 uPts = 0;

          g_object_get (G_OBJECT (child2), "current-pts", &uPts, NULL);
          vsinkposition = uPts;

          GST_DEBUG_OBJECT (self,
              "video position=%" GST_TIME_FORMAT " audio pts=%" GST_TIME_FORMAT
              "", GST_TIME_ARGS (vsinkposition),
              GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

          done2 = TRUE;
        }

        g_value_reset (&data2);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (pIt2);
        break;
      default:
      case GST_ITERATOR_DONE:
        done2 = TRUE;
        break;
    }
  }

  g_value_unset (&data2);
  gst_iterator_free (pIt2);
#endif
}

static void
fast_play_start (GstOMXAudioSink * self)
{
  if (self->rtc_fast_play == FALSE) {
    gst_omx_component_set_config (self->comp, OMX_IndexConfigRenderFast,
        &(self->nodelay_fast_freq));
    GST_LOG_OBJECT (self, "start fast play");
    self->rtc_fast_play = TRUE;
  }
}

static void
fast_play_stop (GstOMXAudioSink * self)
{
  if (self->rtc_fast_play == TRUE) {
    guint32 syncRate = 10000;
    gst_omx_component_set_config (self->comp, OMX_IndexConfigRenderFast,
        &(syncRate));
    GST_LOG_OBJECT (self, "stop fast play");
    self->rtc_fast_play = FALSE;
  }
}

static void
drop_pcm (GstOMXAudioSink * self)
{
  guint len;

  len = g_slist_length (self->rtc_list);

  GST_DEBUG_OBJECT (self, "too many PCM data %d, drop", len);

  while (len > 0) {
    GstBuffer *tmp;

    tmp = self->rtc_list->data;
    self->rtc_list = g_slist_remove (self->rtc_list, tmp);
    gst_buffer_unref (tmp);
    len = g_slist_length (self->rtc_list);
  }
}

static void
gst_omx_audio_sink_rtc_thread_func (GstOMXAudioSink * self)
{
  guint len;
  GstBuffer *buf;

  g_mutex_lock (&self->rtc_thread_mutex);
  len = g_slist_length (self->rtc_list);

  if (len == 0) {
    g_mutex_unlock (&self->rtc_thread_mutex);
    g_usleep (5000);
    return;
  }

  buf = self->rtc_list->data;
  self->rtc_list = g_slist_remove (self->rtc_list, buf);
  g_mutex_unlock (&self->rtc_thread_mutex);

  self->base_sink_chain_func (GST_BASE_SINK_PAD (self), (GstObject *) self,
      buf);
}

static void
gst_omx_audio_sink_open_rtc_thread (GstOMXAudioSink * self)
{
  GstTask *task;

  g_mutex_init (&self->rtc_thread_mutex);
  g_rec_mutex_init (&self->rtc_rec_lock);

  GST_DEBUG_OBJECT (self, "start rtc task");

  GST_OBJECT_LOCK (self);
  task = self->rtc_task;
  task =
      gst_task_new ((GstTaskFunction) gst_omx_audio_sink_rtc_thread_func, self,
      NULL);

  gst_task_set_lock (task, &(self->rtc_rec_lock));
//    gst_task_set_enter_callback (task, pad_enter_thread, pad, NULL);
//    gst_task_set_leave_callback (task, pad_leave_thread, pad, NULL);
  GST_INFO_OBJECT (self, "created rtc task %p", task);
  self->rtc_task = task;
  gst_object_ref (task);
  /* release lock to post the message */
  GST_OBJECT_UNLOCK (self);

//    do_stream_status (pad, GST_STREAM_STATUS_TYPE_CREATE, NULL, task);

  gst_object_unref (task);

  GST_OBJECT_LOCK (self);
  /* nobody else is supposed to have changed the pad now */
  if (self->rtc_task != task) {
    GST_OBJECT_UNLOCK (self);
    GST_ERROR_OBJECT (self, "why different ?!");
    return;
  }

  if (!gst_task_set_state (task, GST_TASK_STARTED)) {
    GST_ERROR_OBJECT (self, "can not start rtc task");
  }
  GST_OBJECT_UNLOCK (self);

  self->rtc_drop_at_start = TRUE;
  self->rtc_drop_count = 0;
  self->rtc_fast_play = FALSE;

  GST_ERROR_OBJECT (self, "rtc task open ok");
}

static void
gst_omx_audio_sink_close_rtc_thread (GstOMXAudioSink * self)
{
  GstTask *task;

  GST_DEBUG_OBJECT (self, "stop rtc task");

  GST_OBJECT_LOCK (self);
  task = self->rtc_task;
  self->rtc_task = NULL;
  if (!gst_task_set_state (task, GST_TASK_STOPPED)) {

  }
  GST_OBJECT_UNLOCK (self);

//  GST_PAD_STREAM_LOCK (pad);
//  GST_PAD_STREAM_UNLOCK (pad);

  if (!gst_task_join (task)) {

  }

  gst_object_unref (task);

  g_mutex_clear (&self->rtc_thread_mutex);
  g_rec_mutex_clear (&self->rtc_rec_lock);

  if (self->rtc_list) {
    GstBuffer *tmp;
    guint len;

    len = g_slist_length (self->rtc_list);
    while (len) {
      g_print ("%s: flush PCM data %d\r\n", __FUNCTION__, len);

      tmp = self->rtc_list->data;
      self->rtc_list = g_slist_remove (self->rtc_list, tmp);
      gst_buffer_unref (tmp);
      len = g_slist_length (self->rtc_list);
    }
    self->rtc_list = NULL;
  }
}

static GstFlowReturn
gst_omx_audio_sink_sink_rtc_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (parent);
  guint len, latency;

  if (self->rtc_drop_at_start == TRUE) {
    self->rtc_drop_count++;

    if (self->rtc_drop_count == 1) {    /* for unlock preroll faster */
      self->base_sink_chain_func (GST_BASE_SINK_PAD (self), (GstObject *) self,
          buf);
      return GST_FLOW_OK;
    }

    if (self->rtc_drop_count >= NODELAY_ASINK_DROP_FRAME_NUMBER) {      /* drop first garbage data */
      self->rtc_drop_at_start = FALSE;
    } else {
      gst_buffer_unref (buf);
      return GST_FLOW_OK;
    }
  }

  g_mutex_lock (&self->rtc_thread_mutex);

  self->rtc_list = g_slist_append (self->rtc_list, buf);
  len = g_slist_length (self->rtc_list);
  latency = (len * 21) + DEFAULT_RTC_LATENCY;

  GST_LOG_OBJECT (self,
      "PCM data list %u (%u ms), recover: %u ms, fast: %u ms, skip: %u ms", len,
      latency, self->nodelay_recover, self->nodelay_fast, self->nodelay_skip);

  check_video_position (self, buf);

  if (latency <= self->nodelay_recover) {
    fast_play_stop (self);
  } else if ((latency > self->nodelay_fast) && (latency <= self->nodelay_skip)) {
    fast_play_start (self);
  } else if (latency > self->nodelay_skip) {
    drop_pcm (self);
  }

  g_mutex_unlock (&self->rtc_thread_mutex);

  return ret;
}
#endif

#if (STORE_MODE_PROTECTING_CODE_USED_G_TIMEOUT==1)
static gboolean
gst_omx_audio_sink_protecting_func (gpointer data)
{
  GstOMXAudioSink *self = (GstOMXAudioSink *) data;
  GstClockTime timer;

  g_mutex_lock (&self->protecting_g_timeout_lock);
  timer = self->protecting_g_timeout_pcm_coming_Time;
  g_mutex_unlock (&self->protecting_g_timeout_lock);

  GST_LOG_OBJECT (self, "timer=%" GST_TIME_FORMAT " %s",
      GST_TIME_ARGS (timer), gst_element_state_get_name (GST_STATE (self)));

  if ((GST_CLOCK_TIME_IS_VALID (timer))
      && (GST_STATE (self) == GST_STATE_PLAYING)) {
    GstClockTime timer2;

    timer2 = gst_clock_get_time (GST_ELEMENT_CLOCK (self));

    if (GST_CLOCK_DIFF (timer, timer2) >= 10 * GST_SECOND) {
      /* use query to check is there mstar audio decoder element at the front */
      GstQuery *query;
      GstStructure *structure;
      gboolean res;

      structure = gst_structure_new ("Have_EOS",
          "receive_eos", G_TYPE_BOOLEAN, FALSE, NULL);

      query = gst_query_new_custom (GST_QUERY_CUSTOM, structure);
      res = gst_pad_peer_query (GST_BASE_SINK_PAD (self), query);
      if (res == TRUE) {
        gboolean receive_eos = FALSE;

        gst_structure_get_boolean (structure, "receive_eos", &receive_eos);
        if (receive_eos == TRUE) {
          GstMessage *message;
          guint32 eos_seqnum = 1234;

          /* ok, now we can post the message */
          GST_DEBUG_OBJECT (self, "Now posting fake EOS");

          message = gst_message_new_eos (GST_OBJECT_CAST (self));
          gst_message_set_seqnum (message, eos_seqnum);
          gst_element_post_message (GST_ELEMENT_CAST (self), message);
        } else {
          g_mutex_lock (&self->protecting_g_timeout_lock);
          self->protecting_g_timeout_pcm_coming_Time =
              gst_clock_get_time (GST_ELEMENT_CLOCK (self));
          g_mutex_unlock (&self->protecting_g_timeout_lock);
        }
      } else {
        g_mutex_lock (&self->protecting_g_timeout_lock);
        self->protecting_g_timeout_pcm_coming_Time =
            gst_clock_get_time (GST_ELEMENT_CLOCK (self));
        g_mutex_unlock (&self->protecting_g_timeout_lock);
      }

      gst_query_unref (query);
    }
  }

  return TRUE;
}

static void
gst_omx_audio_sink_add_protecting_func (GstOMXAudioSink * self)
{
  g_mutex_init (&self->protecting_g_timeout_lock);
  self->protecting_g_timeout_pcm_coming_Time = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (self, "start add protecting function");

  self->protecting_g_timeout_func =
      g_timeout_add (1000, (GSourceFunc) gst_omx_audio_sink_protecting_func,
      (gpointer) self);
  if (self->protecting_g_timeout_func == 0) {
    GST_ERROR_OBJECT (self, "add protecting function fail !!");
    return;
  }

  GST_DEBUG_OBJECT (self, "add protecting function ok, id %u",
      self->protecting_g_timeout_func);
}

static void
gst_omx_audio_sink_remove_protecting_func (GstOMXAudioSink * self)
{
  gboolean ret;
  GST_DEBUG_OBJECT (self, "remove protecting function");

  if (self->protecting_g_timeout_func) {
    ret = g_source_remove (self->protecting_g_timeout_func);
    if (ret == FALSE) {
      GST_ERROR_OBJECT (self, "remove protecting function fail");
    }
    self->protecting_g_timeout_func = 0;
  }

  g_mutex_clear (&self->protecting_g_timeout_lock);

  GST_DEBUG_OBJECT (self, "remove protecting function ok");
}
#endif

#ifdef MS_TEMPO_SINK
#ifdef MS_TEMPO_NEW_PTS
static void
mst_tempo_reset_pts_pool (GstOMXAudioSink * parent)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (parent);
  gint32 i;
  self->tempo_pts_index = 0;
  for (i = 0; i < (MSTEMPO_PTS_POOL_SIZE + 1); i++) {
    self->tempo_pts_pool[i] = -1;
    self->tempo_pcmsize_pool[i] = 0;
  }

  self->tempo_pts_pool[i] = self->tempo_pts;
}

static GstFlowReturn
mst_tempo_push_pts (GstOMXAudioSink * parent, GstBuffer * buf, gint32 pcmsize)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (parent);

  if (self->tempo_pts_index > (MSTEMPO_PTS_POOL_SIZE - 1)) {
    GST_DEBUG_OBJECT (self, "MSTEMPO error pts index=%d too large!!",
        self->tempo_pts_index);
    return -1;
  }

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buf))
      && GST_BUFFER_PTS (buf) != -1) {
    self->tempo_pts_pool[self->tempo_pts_index] = GST_BUFFER_PTS (buf);
    self->tempo_pcmsize_pool[self->tempo_pts_index] = pcmsize;
    //if(self->tempo_pts_index > 0)
    //   self->tempo_pcmsize_pool[self->tempo_pts_index] += self->tempo_pcmsize_pool[self->tempo_pts_index - 1]; // accmu pre size
    self->tempo_pts_index++;
  } else {
    self->tempo_pcmsize_pool[self->tempo_pts_index] += pcmsize;
  }

  return ret;

}

// pcmsize is after tempo
static GstFlowReturn
mst_tempo_pop_pts (GstOMXAudioSink * parent, gint32 pcmsize)
{

  GstFlowReturn ret = GST_FLOW_OK;
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (parent);
  GstBaseSink *bsink_self = GST_BASE_SINK (parent);
  //GstMapInfo tempo_minfo;
  GstClockTime tempo_duration;
  guint32 bytes_per_sample;     // bpf
  gint32 pts_index, i;

  gint32 stream_pcmsize = pcmsize * bsink_self->segment.applied_rate;
  pts_index = 0;
  while (pts_index <= self->tempo_pts_index) {
    tempo_duration = stream_pcmsize;
    stream_pcmsize -= self->tempo_pcmsize_pool[pts_index];
    if (stream_pcmsize < 0) {   //if( self->tempo_pts_pcmsize[pts_index] > stream_pcmsize) {
      self->tempo_pcmsize_pool[pts_index] -= tempo_duration;
      goto _cal_next_pts;       //break;
    } else if (stream_pcmsize == 0) {
      if (self->tempo_pts_pool[pts_index + 1] == -1) {  // if next pts is empty
        self->tempo_pcmsize_pool[pts_index] = 0;
        goto _cal_next_pts;     //break;
      } else {                  // // if next pts is valid
        pts_index++;
        self->tempo_pts_index--;
        goto _shift_to_first;   // break;
      }                         // end if(self->tempo_pts_pool[pts_index+1] == -1)
    }                           // end else if(stream_pcmsize == 0){
    pts_index++;
    self->tempo_pts_index--;
  }
  self->tempo_pts_index = 0;    // no valid pts
  pts_index = 0;
_cal_next_pts:
  /* cal start pts of next buf */
  bytes_per_sample = 2 * self->width / 8;       //@ here default channel is 2, if channel is diff to 2, need to modify it
  tempo_duration /= bytes_per_sample;
  tempo_duration *= 1000;
  tempo_duration /= self->rate;
  tempo_duration *= GST_MSECOND;
  self->tempo_pts_pool[pts_index] += tempo_duration;

_shift_to_first:
  /* shift to first  */
  for (i = 0; i < (MSTEMPO_PTS_POOL_SIZE - pts_index); i++) {
    self->tempo_pts_pool[i] = self->tempo_pts_pool[pts_index + i];
    self->tempo_pcmsize_pool[i] = self->tempo_pcmsize_pool[pts_index + i];
  }

//_reset_remain_data:
  /* reset remain buf data */
  for (i = (self->tempo_pts_index + 1); i < (MSTEMPO_PTS_POOL_SIZE + 1); i++) {
    self->tempo_pts_pool[i] = -1;
    self->tempo_pcmsize_pool[i] = 0;
  }
  // cal next buf tempo pts;
  self->tempo_pts =
      (self->tempo_pts_pool[0] -
      bsink_self->segment.start) / bsink_self->segment.applied_rate +
      bsink_self->segment.start;;

  return ret;

}
#endif
#endif // END MSTEMPO

static GstFlowReturn
gst_omx_audio_sink_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (parent);
  GstBuffer *buf_out = buf;
  GstBaseSink *bsink_self = GST_BASE_SINK (parent);
#ifdef MS_TEMPO_SINK
  gboolean bLastRender = FALSE;
#endif

#if 0
  GstMapInfo minfo;
  GstClockTime timestamp, duration;

  timestamp = GST_BUFFER_TIMESTAMP (buf);
  duration = GST_BUFFER_DURATION (buf);

  gst_buffer_map (buf, &minfo, GST_MAP_READ);

  GST_DEBUG_OBJECT (self,
      "audio sink chain playrate=%f OUTPUT PTS=%" GST_TIME_FORMAT " DUR=%"
      GST_TIME_FORMAT " Size=%d maxsize=%d",
      self->playrate GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration),
      minfo.size, minfo.maxsize);

  gst_buffer_unmap (buf, &minfo);
#endif
#if (PATCH_AUDIO_UNDERRUN_FOR_ICS==1)
  if (g_str_equal (self->app_type, "mheg-ics")) {
    self->asink_gap_no_duration = FALSE;
  }
#endif
#if (STORE_MODE_PROTECTING_CODE_USED_G_TIMEOUT==1)
  if (self->protecting_g_timeout_func) {
    g_mutex_lock (&self->protecting_g_timeout_lock);
    self->protecting_g_timeout_pcm_coming_Time =
        gst_clock_get_time (GST_ELEMENT_CLOCK (self));
    g_mutex_unlock (&self->protecting_g_timeout_lock);
  }
#endif


#ifdef MS_TEMPO_SINK
  GST_DEBUG_OBJECT (self,
      "gstomxaudiosinkchain applied_rate=%f OUTPUT PTS=%" GST_TIME_FORMAT
      " DUR=%" GST_TIME_FORMAT " offset=%lld len=%lld samplerate=%d width=%d",
      bsink_self->segment.applied_rate,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_OFFSET (buf),
      GST_BUFFER_OFFSET_END (buf) - GST_BUFFER_OFFSET (buf), self->rate,
      self->width);
  {
    GstMapInfo tempo_minfo;
    GstClockTime tempo_duration;
    guint32 bytes_per_sample;   // bpf
    gint32 status = 0;
    gint32 tempo_framesize = 0;
    gint32 tempo_insize = 0;
    gint32 tempo_outsize = 0;
    gint32 tempo_totaloutsize = 0;
    gint32 tempo_totalinsize = 0;
    gint32 tempo_copySize = 0;
    gint8 *tempo_pOut;
    gint8 *tempo_pIn;
    gint32 copySize = 0;
    gint32 tempo_QQcopySize = 0;

    // currently tempo only works for FF2x and FF1/2
    if (bsink_self->segment.applied_rate == 2
        || bsink_self->segment.applied_rate == 0.5) {

#ifndef MS_TEMPO_NEW_PTS
      static GstClockTime tempo_start_pts;

      if (self->tempo_pts == GST_CLOCK_TIME_NONE) {
        if (G_LIKELY (GST_BUFFER_TIMESTAMP (buf) > bsink_self->segment.start)) {
          tempo_start_pts =
              (GST_BUFFER_TIMESTAMP (buf) -
              bsink_self->segment.start) / bsink_self->segment.applied_rate +
              bsink_self->segment.start;
        } else {
          gint64 temp_pts =
              ((gint64) GST_BUFFER_TIMESTAMP (buf) -
              (gint64) bsink_self->segment.start) /
              bsink_self->segment.applied_rate +
              (gint64) bsink_self->segment.start;
          if (temp_pts >= 0) {
            tempo_start_pts = (guint64) temp_pts;
          } else {
            tempo_start_pts = 0;
          }
        }
        GST_DEBUG_OBJECT (self,
            "tempo_start_pts=%" GST_TIME_FORMAT " GST_BUFFER_TIMESTAMP(buf)=%"
            GST_TIME_FORMAT " segment.start=%" GST_TIME_FORMAT,
            GST_TIME_ARGS (tempo_start_pts),
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
            GST_TIME_ARGS (bsink_self->segment.start));
      }
#endif

      /* get buf inf */
      /*
         if ( !GST_CLOCK_TIME_IS_VALID (self->tempo_pts) ) {
         if ( GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buf)) ) {
         self->tempo_pts = GST_BUFFER_PTS (buf);
         }
         else {
         self->tempo_pts = GST_CLOCK_TIME_NONE;
         }
         }
       */
      //GST_DEBUG_OBJECT (self, "1 output pts \n");
      //GST_BUFFER_PTS (buf) = self->tempo_pts;
      gst_buffer_map (buf, &tempo_minfo, GST_MAP_READ);

      if (self->tempo_is_preroll == TRUE)       //This is preroll for X2 because in X2 we have to reduce data to half, we need some more preroll for avoiding audio cutting
      {
        if (self->tempo_preroll_index + tempo_minfo.size >=
            self->tempo_preroll_length) {
          //backup the last buffer, and re-create buf to an accumulated one
          GstBuffer *bbuf = NULL;
          GstMapInfo btempo_minfo;
          bbuf = gst_buffer_copy (buf);
          gst_buffer_map (bbuf, &btempo_minfo, GST_MAP_READ);
          gst_buffer_unmap (buf, &tempo_minfo);
          gst_mini_object_unref (GST_MINI_OBJECT_CAST (buf));

          buf =
              gst_buffer_new_allocate (NULL,
              self->tempo_preroll_index + btempo_minfo.size, NULL);
          gst_buffer_map (buf, &tempo_minfo, GST_MAP_READ);
          memcpy (tempo_minfo.data, self->tempo_preroll,
              self->tempo_preroll_index);
          memcpy (tempo_minfo.data + self->tempo_preroll_index,
              btempo_minfo.data, btempo_minfo.size);

          self->tempo_is_preroll = FALSE;
          self->tempo_preroll_index = 0;
          GST_DEBUG_OBJECT (self, "tempo accumulate done");
          gst_buffer_unmap (bbuf, &btempo_minfo);
          gst_mini_object_unref (GST_MINI_OBJECT_CAST (bbuf));
          g_free (self->tempo_preroll);
        } else {
          memcpy (self->tempo_preroll + self->tempo_preroll_index,
              tempo_minfo.data, tempo_minfo.size);
          self->tempo_preroll_index += tempo_minfo.size;

          GST_DEBUG_OBJECT (self, "tempo accumulate = %d", tempo_minfo.size);
          gst_buffer_unmap (buf, &tempo_minfo);
          gst_mini_object_unref (GST_MINI_OBJECT_CAST (buf));
          return ret;
        }
      }


      GST_DEBUG_OBJECT (self, "tempo in output pts copySize=%d\n",
          tempo_minfo.size);
      copySize = tempo_minfo.size;
      /* do tempo */
      tempo_copySize = (gint32) copySize;
#ifdef MS_TEMPO_NEW_PTS
      mst_tempo_push_pts (self, buf, tempo_minfo.size);
#endif
      tempo_totalinsize = 0;
      tempo_totaloutsize = 0;
      if (self->tempo_trick_f != bsink_self->segment.applied_rate) {
        int intFs = self->rate;
        if (self->rate == 11024)        // patch for mantis 819083, file name:  MVI_0080.avi
          intFs = 11025;
        self->tempo_trick_f = bsink_self->segment.applied_rate;
        mst_tempo_reset (self->tempo_handle, self->tempo_trick_f, intFs, (int *) &tempo_framesize);     // self->rate is sample rate
        self->tempo_trick_f = bsink_self->segment.applied_rate;
        self->tempo_framesize = tempo_framesize;
        GST_DEBUG_OBJECT (self, "tempo output pts fs=%x \n",
            self->tempo_framesize);
      }

      if (self->have_audio_decoder == 1) {
        if (self->channels > 2) // multi-channel to stereo
        {
          gint32 i;
          gint32 *tempo_pIn32 = (gint32 *) tempo_minfo.data;
          gint32 *tempo_pOut32 = (gint32 *) self->tempo_inbuf;
          tempo_copySize = copySize / self->channels * 2;
          for (i = 0; i < (copySize / self->channels / sizeof (gint16)); i++) {
            tempo_pOut32[i] = tempo_pIn32[i * self->channels / sizeof (gint16)];
          }
        } else {
          memcpy (self->tempo_inbuf, tempo_minfo.data, copySize);
        }
      } else if (self->have_audio_decoder == 0) {
        if (self->channels != 2 || self->width != 16
            || self->endianness != G_LITTLE_ENDIAN) {
          guint32 i, j;
          guint16 u16Temp;
          guint8 *pu8SrcBuf, *pu8DstBuf;
          guint8 u8HighByte, u8LowByte, u8SkipByte = 0;

          pu8SrcBuf = (guint8 *) tempo_minfo.data;
          pu8DstBuf = (guint8 *) self->tempo_inbuf;

          if (self->width == 24 || self->width == 32) {
            u8SkipByte = (self->width - 16) / 8;
          }

          for (i = 0, j = 0; i < copySize;) {
            if (self->width == 8) {
              u16Temp = ((guint16) (pu8SrcBuf[i++] - 128) << 8);
              // 8 bits -> 16 bits
              u8HighByte = (u16Temp >> 8);
              u8LowByte = (u16Temp & 0xFF);
            } else {
              if (self->endianness == G_LITTLE_ENDIAN) {
                //Little Endian
                i += u8SkipByte;
                u8LowByte = pu8SrcBuf[i++];
                u8HighByte = pu8SrcBuf[i++];
              } else {
                // Big Endian
                u8HighByte = pu8SrcBuf[i++];
                u8LowByte = pu8SrcBuf[i++];
                i += u8SkipByte;
              }
            }

            //Uptdae samples to output buffer
            pu8DstBuf[j++] = u8LowByte;
            pu8DstBuf[j++] = u8HighByte;

            if (self->channels == 1) {
              pu8DstBuf[j++] = u8LowByte;
              pu8DstBuf[j++] = u8HighByte;
            }
          }
          tempo_copySize = j;
        } else {
          memcpy (self->tempo_inbuf, tempo_minfo.data, copySize);
        }
      }
      tempo_QQcopySize = tempo_copySize;
      gst_buffer_unmap (buf, &tempo_minfo);
      gst_buffer_unref (buf);

    RE_RENDER:
      // to avoid too much time spent in tempo process, limit the max process size
      if (tempo_QQcopySize - tempo_totalinsize > TEMPO_PROCESS_SIZE_MAX) {
        tempo_copySize = tempo_totalinsize + TEMPO_PROCESS_SIZE_MAX;
      } else {
        tempo_copySize = tempo_QQcopySize;
        bLastRender = TRUE;
      }

      while (tempo_totalinsize < tempo_copySize) {
        tempo_insize = self->tempo_framesize << 2;
        //GST_DEBUG_OBJECT (self, "output pts tempo_copySize - tempo_totalinsize=%d\n",tempo_copySize - tempo_totalinsize);
        if ((tempo_copySize - tempo_totalinsize) < (self->tempo_framesize << 2)) {
          tempo_insize = (tempo_copySize - tempo_totalinsize);
        }
        tempo_pIn = (((gint8 *) self->tempo_inbuf + tempo_totalinsize));
        tempo_pOut = (((gint8 *) self->tempo_outbuf + tempo_totaloutsize));
        status =
            (gint32) mst_tempo_process (self->tempo_handle, (short *) tempo_pIn,
            tempo_insize >> 2, (short *) tempo_pOut, (int *) &tempo_outsize);
        //GST_DEBUG_OBJECT (self, "output pts tempo_insize=%d tempo_outsize=%d totalinsize=%d totaloutsize=%d\n",tempo_insize,tempo_outsize <<2,tempo_totalinsize,tempo_totaloutsize);
        status = status;        // to avoid warning
        tempo_totalinsize += tempo_insize;
        tempo_totaloutsize += tempo_outsize << 2;
      }
      copySize = (gint32) tempo_totaloutsize;   // tempo output bytes count by 16 bits width

//        gst_buffer_unmap (buf, &tempo_minfo);
//        gst_buffer_unref (buf);
      if (tempo_totaloutsize == 0)
        return ret;

      /* output to new gst buf */
#if 1
      tempo_totaloutsize = copySize;
      if (self->have_audio_decoder == 1 && self->channels > 2)  // multi-channel
      {
        tempo_totaloutsize = copySize * self->channels / 2;
      } else if (self->have_audio_decoder == 0) {
        tempo_totaloutsize = copySize * self->channels * self->width / 2 / 16;
      }

      buf_out = gst_buffer_new_allocate (NULL, tempo_totaloutsize, NULL);
      if (buf_out == NULL) {
        GST_DEBUG_OBJECT (self, "output pts tempo acquire buffer error!!");
        return GST_OMX_ACQUIRE_BUFFER_ERROR;
      }
      gst_buffer_map (buf_out, &tempo_minfo, GST_MAP_READ);

      if (self->have_audio_decoder == 1) {
        if (self->channels > 2) // stereo to multi-channel
        {
          memcpy (self->tempo_inbuf, self->tempo_outbuf, copySize);
          memset (tempo_minfo.data, 0, tempo_totaloutsize);

          gint32 i;
          gint32 *tempo_pIn32 = (gint32 *) tempo_minfo.data;
          gint32 *tempo_pOut32 = (gint32 *) self->tempo_inbuf;
          copySize = tempo_totaloutsize;
          for (i = 0; i < (copySize / self->channels / sizeof (gint16)); i++) {
            tempo_pOut32[i * self->channels / sizeof (gint16)] = tempo_pIn32[i];
          }
        } else {
          memcpy (tempo_minfo.data, self->tempo_outbuf, copySize);
        }
      } else if (self->have_audio_decoder == 0) {
        if (self->channels != 2 || self->width != 16
            || self->endianness != G_LITTLE_ENDIAN) {
          guint32 i, j;
          guint16 u16Temp;
          guint8 *pu8SrcBuf, *pu8DstBuf;
          guint8 u8HighByte, u8LowByte, u8SkipByte = 0;

          memset (tempo_minfo.data, 0, tempo_totaloutsize);

          pu8SrcBuf = (guint8 *) self->tempo_outbuf;
          pu8DstBuf = (guint8 *) tempo_minfo.data;

          if (self->width == 24 || self->width == 32) {
            u8SkipByte = (self->width - 16) / 8;
          }

          for (i = 0, j = 0; i < copySize;) {
            u8LowByte = pu8SrcBuf[i++];
            u8HighByte = pu8SrcBuf[i++];

            if (self->width == 8) {
              // 16 bits -> 8 bits
              u16Temp = ((guint16) u8HighByte << 8) | ((guint16) u8LowByte);
              pu8DstBuf[j++] = (u16Temp >> 8) + 128;
            } else {
              if (self->endianness == G_LITTLE_ENDIAN) {
                //Little Endian
                j += u8SkipByte;
                pu8DstBuf[j++] = u8LowByte;
                pu8DstBuf[j++] = u8HighByte;
              } else {
                // Big Endian
                pu8DstBuf[j++] = u8HighByte;
                pu8DstBuf[j++] = u8LowByte;
                j += u8SkipByte;
              }
            }

            if (self->channels == 1) {
              // skip next sample
              i += 2;
            }
          }
          copySize = j;
        } else {
          memcpy (tempo_minfo.data, self->tempo_outbuf, copySize);
        }
      }

      /* pts and duration update */
#ifdef MS_TEMPO_NEW_PTS
      mst_tempo_pop_pts (self, 0);      // update self->tempo_pts for init pts
#else
      if (self->tempo_pts == GST_CLOCK_TIME_NONE) {
        self->tempo_pts = tempo_start_pts;
        GST_DEBUG_OBJECT (self, "tempo_pts=%" GST_TIME_FORMAT,
            GST_TIME_ARGS (self->tempo_pts));
      }
#endif

      GST_BUFFER_PTS (buf_out) = self->tempo_pts;
      // prepare next pts
      {
        //        divisor
        guint64 s64divisor = 0;
        tempo_duration = copySize;
        bytes_per_sample = self->channels * self->width / 8;    //@ here default channel is 2, if channel is diff to 2, need to modify it
        tempo_duration *= 1000;
        tempo_duration *= GST_MSECOND;

        s64divisor = bytes_per_sample * self->rate;
        tempo_duration /= s64divisor;

        GST_BUFFER_DURATION (buf_out) = tempo_duration;
        GST_DEBUG_OBJECT (self,
            "tempo out output pts %" GST_TIME_FORMAT " dura %" GST_TIME_FORMAT
            " inSize=%d outSize=%d tempo_pts_index =%d",
            GST_TIME_ARGS (GST_BUFFER_PTS (buf_out)),
            GST_TIME_ARGS (GST_BUFFER_DURATION (buf_out)), tempo_totalinsize,
            tempo_minfo.size, self->tempo_pts_index);
#ifdef MS_TEMPO_NEW_PTS
        mst_tempo_pop_pts (self, copySize);
#else
        self->tempo_pts += tempo_duration;
#endif
#endif // end output to new gst buf
      }
      /* free buf minfo */
      gst_buffer_unmap (buf_out, &tempo_minfo);
      tempo_totaloutsize = 0;
    }                           // if (self->playrate == 2

  }                             // tempo end
#else
  GST_DEBUG_OBJECT (self,
      "gstomxaudiosinkchain OUTPUT PTS=%" GST_TIME_FORMAT " DUR=%"
      GST_TIME_FORMAT " offset=%lld len=%lld samplerate=%d width=%d",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_OFFSET (buf),
      GST_BUFFER_OFFSET_END (buf) - GST_BUFFER_OFFSET (buf), self->rate,
      self->width);
  if (bsink_self->segment.rate != 1.0) {
    gst_buffer_unref (buf);
    return ret;
  }
#endif

  {
    guint32 paused = OMX_TRUE;
    gst_omx_component_get_parameter (self->comp, OMX_IndexArenderPause,
        &paused);

    if (paused == OMX_TRUE) {
      paused = OMX_FALSE;
      GST_OBJECT_LOCK (self);
      gst_omx_component_set_parameter (self->comp, OMX_IndexArenderPause,
          &paused);
      GST_OBJECT_UNLOCK (self);
    }
  }

#if (MIRACAST_SINK_ADD_BUFFER_LIST==1)
  if (g_str_equal (self->app_type, "RTC")) {
    ret = gst_omx_audio_sink_sink_rtc_chain (pad, parent, buf_out);
  } else {
    ret = self->base_sink_chain_func (pad, parent, buf_out);

#ifdef MS_TEMPO_SINK
    if (bsink_self->segment.applied_rate == 2
        || bsink_self->segment.applied_rate == 0.5) {
      if (bLastRender == FALSE) {
        goto RE_RENDER;
      }
    }
#endif
  }
#else
  ret = self->base_sink_chain_func (pad, parent, buf_out);
#endif

  return ret;
}

static void
gst_omx_audio_sink_mute_set (GstOMXAudioSink * self, gboolean mute)
{
  if (self->comp) {
    OMX_ERRORTYPE err;
    gboolean bMute = mute;

    err =
        gst_omx_component_set_config (self->comp, OMX_IndexConfigAudioMute,
        &bMute);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to set mute to %d: %s (0x%08x)",
          bMute, gst_omx_error_to_string (err), err);
    }
  }
  self->mute = mute;
}

static void
gst_omx_audio_sink_volume_set (GstOMXAudioSink * self, gdouble volume)
{
  if (self->comp) {
    OMX_ERRORTYPE err;
    OMX_AUDIO_CONFIG_VOLUMETYPE param;
    GST_OMX_INIT_STRUCT (&param);
    param.nPortIndex = self->in_port->index;
    param.bLinear = OMX_TRUE;
    param.sVolume.nValue = volume * 100;
    err = gst_omx_component_set_config (self->comp,
        OMX_IndexConfigAudioVolume, &param);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to set volume to %d: %s (0x%08x)",
          (gint) param.sVolume.nValue, gst_omx_error_to_string (err), err);
    }
  }
  self->volume = volume;
}

static gboolean
gst_omx_audio_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (bsink);

  GST_DEBUG_OBJECT (self, "#### sink_sink_event %s ####",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *prevcaps;
      GstStructure *structure;
      const gchar *vendor;
      gboolean mstar;

      gst_event_parse_caps (event, &prevcaps);
      structure = gst_caps_get_structure (prevcaps, 0);
      vendor = gst_structure_get_string (structure, "vendor");
      mstar = vendor && !g_strcmp0 (vendor, "mstar");
      GST_INFO_OBJECT (self, "set is_support_decoder to %d", mstar);

      if (mstar) {
        if (self->have_audio_decoder != mstar) {
          self->have_audio_decoder = mstar;
          gst_omx_component_set_config (self->comp,
              OMX_IndexConfigHaveAudioDecoder, &mstar);
        }
      }
    }
      break;

    case GST_EVENT_FLUSH_START:
#if (PATCH_AUDIO_UNDERRUN_FOR_ICS==1)
      if (g_str_equal (self->app_type, "mheg-ics")) {
        GST_DEBUG_OBJECT (self, "GST_EVENT_FLUSH_START: clear eos flag");
        self->asink_is_eos_received = FALSE;
      }
#endif
      if (self->disable_lost_state == TRUE) {
        gst_event_unref (event);
        return TRUE;
      }
      break;

    case GST_EVENT_FLUSH_STOP:
      if (self->disable_lost_state == TRUE) {
        gst_event_unref (event);
        return TRUE;
      }
      break;

    case GST_EVENT_SEGMENT:
    {
      float param;

      if (self->disable_lost_state == TRUE) {
        self->disable_lost_state = FALSE;
        gst_event_unref (event);
        return TRUE;
      }

      GST_OBJECT_LOCK (bsink);
#if 0
      /* use query to check is there mstar audio decoder element at the front */
      {
        GstQuery *query;
        GstStructure *structure;
        gboolean res;
        gboolean MstarDecoder = FALSE;

        structure = gst_structure_new ("WhoAreYou",
            "AreYouMstarSink", G_TYPE_BOOLEAN, TRUE,
            "AreYouMstarDecoder", G_TYPE_BOOLEAN, FALSE,
            "format", G_TYPE_STRING, "123", NULL);

        query = gst_query_new_custom (GST_QUERY_CUSTOM, structure);
        res = gst_pad_peer_query (bsink->sinkpad, query);
        if (res == TRUE) {
          gst_structure_get_boolean (structure, "AreYouMstarDecoder",
              &MstarDecoder);
        }

        if (self->have_audio_decoder != MstarDecoder) {
          self->have_audio_decoder = MstarDecoder;
          gst_omx_component_set_config (self->comp,
              OMX_IndexConfigHaveAudioDecoder, &MstarDecoder);
        }

        gst_query_unref (query);
      }
      /* use query to check is there mstar audio decoder element at the front */
#endif

      gst_event_copy_segment (event, &bsink->segment);
      param = bsink->segment.rate;
      self->playrate = param;
//            gst_omx_component_set_parameter (self->comp, OMX_IndexParamRate, &param);

      self->current_pts = bsink->segment.start;

      GST_LOG_OBJECT (self,
          "set current pts base %" GST_TIME_FORMAT
          " self->playrate=%f applied_rate=%f\n",
          GST_TIME_ARGS (self->current_pts), bsink->segment.rate,
          bsink->segment.applied_rate);
#if 0
      if ((bsink->segment.rate <= 2) && (bsink->segment.rate > 1))      //1~2X
      {
#ifdef MS_TEMPO_SINK
        if (bsink->segment.rate == 2) {
          self->tempo_is_preroll = TRUE;
          self->tempo_preroll_index = 0;
          self->tempo_preroll_length = 800 * (self->width / 8) * (self->rate / 1000);   //ms
          self->tempo_preroll = g_malloc (self->tempo_preroll_length);

#ifdef MS_TEMPO_NEW_PTS
          self->tempo_pts = bsink->segment.start;
          mst_tempo_reset_pts_pool (self);
#else
          self->tempo_pts = GST_CLOCK_TIME_NONE;
#endif
          if (GST_CLOCK_TIME_IS_VALID (bsink->segment.stop)) {
            bsink->segment.stop =
                bsink->segment.start + ((bsink->segment.stop -
                    bsink->segment.start) / 2);
          }
          bsink->segment.applied_rate = 2;
          bsink->segment.rate = 1;
          GST_DEBUG_OBJECT (self,
              "Tempo Sink new segement play_rate=%f applied_rate=%f OUTPUT PTS seg.start=%"
              GST_TIME_FORMAT " seg.stop=%" GST_TIME_FORMAT
              "==============================", bsink->segment.rate,
              bsink->segment.applied_rate, GST_TIME_ARGS (bsink->segment.start),
              GST_TIME_ARGS (bsink->segment.stop));
        }
#endif
        bsink->have_newsegment = TRUE;
        GST_OBJECT_UNLOCK (bsink);
        return TRUE;
      } else if (bsink->segment.rate > 0.0 && bsink->segment.rate <= 2.0)       // 0x ~ 1.0x
      {
#ifdef MS_TEMPO_SINK
        if (bsink->segment.rate == 0.5) {
#ifdef MS_TEMPO_NEW_PTS
          self->tempo_pts = bsink->segment.start;
          mst_tempo_reset_pts_pool (self);
#else
          self->tempo_pts = GST_CLOCK_TIME_NONE;
#endif
          if (GST_CLOCK_TIME_IS_VALID (bsink->segment.stop)) {
            bsink->segment.stop =
                bsink->segment.start + ((bsink->segment.stop -
                    bsink->segment.start) * 2);
          }
          bsink->segment.applied_rate = 0.5;
          bsink->segment.rate = 1;
          GST_DEBUG_OBJECT (self,
              "Tempo Sink new segement playrate=%f applied_rate=%f OUTPUT PTS seg.start=%"
              GST_TIME_FORMAT " seg.stop=%" GST_TIME_FORMAT
              "==============================", bsink->segment.rate,
              bsink->segment.applied_rate, GST_TIME_ARGS (bsink->segment.start),
              GST_TIME_ARGS (bsink->segment.stop));
        }
#endif
        bsink->have_newsegment = TRUE;
        GST_OBJECT_UNLOCK (bsink);
        return TRUE;
      } else if (bsink->segment.rate < 0.0 || bsink->segment.rate > 2.0)        // drop audio ES, no need to output
      {
        GST_OBJECT_UNLOCK (bsink);
        return GST_BASE_SINK_CLASS (parent_class)->event (bsink,
            gst_event_new_eos ());
      }
#else
      // drop audio ES, no need to output, do not support trick mode in M2
      if (bsink->segment.rate != 1) {
        GST_OBJECT_UNLOCK (bsink);
//                return GST_BASE_SINK_CLASS (parent_class)->event (bsink, gst_event_new_eos ());
        return GST_BASE_SINK_CLASS (parent_class)->event (bsink,
            gst_event_new_gap (0, GST_CLOCK_TIME_NONE));
      } else {
        bsink->have_newsegment = TRUE;
        GST_OBJECT_UNLOCK (bsink);
        return TRUE;
      }

#endif
    }
      break;

    case GST_EVENT_GAP:
    {
      GstClockTime timestamp, duration;
      gst_event_parse_gap (event, &timestamp, &duration);

      GST_DEBUG_OBJECT (self,
          "GAP timestamp %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT "",
          GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration));

      if (self->have_audio_decoder == 1 && self->channels > 2)  // AAC , AC3 ,DTS
      {
        gboolean bNonPcmGap = TRUE;
        self->bNonPcmGap = bNonPcmGap;
        gst_omx_component_set_config (self->comp, OMX_IndexConfigNonPcmGap,
            &bNonPcmGap);
      }
#if (PATCH_AUDIO_UNDERRUN_FOR_ICS==1)
      if (g_str_equal (self->app_type, "mheg-ics")) {
        if (!GST_CLOCK_TIME_IS_VALID (duration)) {      //FF >=4X and backforward
          self->asink_gap_no_duration = TRUE;
        }
      }
#endif
#ifdef MS_TEMPO_SINK
      if (bsink->segment.rate > 0.0
          && (bsink->segment.applied_rate == 2.0
              || bsink->segment.applied_rate == 0.5)) {

        GST_LOG_OBJECT (self,
            "Tempo: (Original GAP) start %" GST_TIME_FORMAT " duration %"
            GST_TIME_FORMAT " rate=%f applied_rate=%f\n",
            GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration),
            bsink->segment.rate, bsink->segment.applied_rate);

        if (GST_CLOCK_TIME_IS_VALID (timestamp)
            && GST_CLOCK_TIME_IS_VALID (bsink->segment.start)) {
          if (timestamp - bsink->segment.start > 0) {
            timestamp =
                (timestamp -
                bsink->segment.start) / bsink->segment.applied_rate +
                bsink->segment.start;
          } else {
            timestamp = bsink->segment.start;
          }
        }

        if (GST_CLOCK_TIME_IS_VALID (duration)) {
          duration = duration / bsink->segment.applied_rate;
          self->tempo_pts += duration;
        }

        gst_event_unref (event);
        event = gst_event_new_gap (timestamp, duration);

        GST_LOG_OBJECT (self,
            "Tempo: (New GAP) start %" GST_TIME_FORMAT " duration %"
            GST_TIME_FORMAT " rate=%f applied_rate=%f\n",
            GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration),
            bsink->segment.rate, bsink->segment.applied_rate);
      }
#endif
    }
      break;

    case GST_EVENT_EOS:
    {
      if (self->have_audio_decoder == 1 && self->channels > 2 && self->bNonPcmGap != TRUE)      // AAC , AC3 ,DTS
      {
        gboolean bNonPcmGap = TRUE;
        self->bNonPcmGap = bNonPcmGap;
        gst_omx_component_set_config (self->comp, OMX_IndexConfigNonPcmGap,
            &bNonPcmGap);
      }
#if (PATCH_AUDIO_UNDERRUN_FOR_ICS==1)
      if (g_str_equal (self->app_type, "mheg-ics")) {
        GST_DEBUG_OBJECT (self, "set eos flag");
        self->asink_is_eos_received = TRUE;
      }
#endif
    }
      break;
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
}

static gboolean
gst_omx_audio_sink_open (GstAudioSink * audiosink)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  GstOMXAudioSinkClass *klass = GST_OMX_AUDIO_SINK_GET_CLASS (self);
  gint port_index;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (self, "Opening audio sink");

  self->comp =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->cdata.core_name,
      klass->cdata.component_name, klass->cdata.component_role,
      klass->cdata.hacks);

  if (!self->comp)
    return FALSE;

  if (gst_omx_component_get_state (self->comp,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  port_index = klass->cdata.in_port_index;

  if (port_index == -1) {
    OMX_PORT_PARAM_TYPE param;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->comp, OMX_IndexParamAudioInit,
        &param);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Couldn't get port information: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* Fallback */
      port_index = 0;
    } else {
      GST_DEBUG_OBJECT (self, "Detected %u ports, starting at %u",
          (guint) param.nPorts, (guint) param.nStartPortNumber);
      port_index = param.nStartPortNumber + 0;
    }
  }
  self->in_port = gst_omx_component_add_port (self->comp, port_index);

  port_index = klass->cdata.out_port_index;

  if (port_index == -1) {
    OMX_PORT_PARAM_TYPE param;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->comp, OMX_IndexParamAudioInit,
        &param);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Couldn't get port information: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* Fallback */
      port_index = 0;
    } else {
      GST_DEBUG_OBJECT (self, "Detected %u ports, starting at %u",
          (guint) param.nPorts, (guint) param.nStartPortNumber);
      port_index = param.nStartPortNumber + 1;
    }
  }
  self->out_port = gst_omx_component_add_port (self->comp, port_index);

  if (!self->in_port || !self->out_port)
    return FALSE;

  err = gst_omx_port_set_enabled (self->in_port, FALSE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to enable port: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  err = gst_omx_port_set_enabled (self->out_port, FALSE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to enable port: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  err = gst_omx_port_wait_enabled (self->in_port, 1 * GST_SECOND);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "port not enabled: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Opened audio sink");

  return TRUE;
}

static gboolean
gst_omx_audio_sink_close (GstAudioSink * audiosink)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Closing audio sink");

  state = gst_omx_component_get_state (self->comp, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->comp, OMX_StateIdle);
      gst_omx_component_get_state (self->comp, 5 * GST_SECOND);
    }
    gst_omx_component_set_state (self->comp, OMX_StateLoaded);
    gst_omx_port_deallocate_buffers (self->in_port);
    if (state > OMX_StateLoaded)
      gst_omx_component_get_state (self->comp, 5 * GST_SECOND);
  }

  self->in_port = NULL;
  self->out_port = NULL;
  if (self->comp)
    gst_omx_component_free (self->comp);
  self->comp = NULL;

  GST_DEBUG_OBJECT (self, "Closed audio sink");

  return TRUE;
}

static gboolean
gst_omx_audio_sink_parse_spec (GstOMXAudioSink * self,
    GstAudioRingBufferSpec * spec)
{
  self->iec61937 = FALSE;
  self->endianness = GST_AUDIO_INFO_ENDIANNESS (&spec->info);
  self->rate = GST_AUDIO_INFO_RATE (&spec->info);
  self->channels = GST_AUDIO_INFO_CHANNELS (&spec->info);
  self->width = GST_AUDIO_INFO_WIDTH (&spec->info);
  self->is_signed = GST_AUDIO_INFO_IS_SIGNED (&spec->info);
  self->is_float = GST_AUDIO_INFO_IS_FLOAT (&spec->info);

  switch (spec->type) {
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_RAW:
    {
      guint out_channels = OUT_CHANNELS (self->channels);

      self->samples = spec->segsize / self->channels / (self->width >> 3);
      if (self->channels == out_channels) {
        self->buffer_size = spec->segsize;
      } else {
        self->buffer_size = (spec->segsize / self->channels) * out_channels;
      }
      break;
    }
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_AC3:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_EAC3:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_DTS:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_MPEG:
      self->iec61937 = TRUE;
      self->endianness = G_LITTLE_ENDIAN;
      self->channels = 2;
      self->width = 16;
      self->is_signed = TRUE;
      self->is_float = FALSE;
      self->buffer_size = spec->segsize;
      break;
    default:
      return FALSE;
  }

  return TRUE;
}

static inline void
channel_mapping (GstAudioRingBufferSpec * spec,
    OMX_AUDIO_CHANNELTYPE * eChannelMapping)
{
  gint i, nchan = GST_AUDIO_INFO_CHANNELS (&spec->info);

  for (i = 0; i < nchan; i++) {
    OMX_AUDIO_CHANNELTYPE pos;

    switch (GST_AUDIO_INFO_POSITION (&spec->info, i)) {
      case GST_AUDIO_CHANNEL_POSITION_MONO:
      case GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER:
        pos = OMX_AUDIO_ChannelCF;
        break;
      case GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT:
        pos = OMX_AUDIO_ChannelLF;
        break;
      case GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT:
        pos = OMX_AUDIO_ChannelRF;
        break;
      case GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT:
        pos = OMX_AUDIO_ChannelLS;
        break;
      case GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT:
        pos = OMX_AUDIO_ChannelRS;
        break;
      case GST_AUDIO_CHANNEL_POSITION_LFE1:
        pos = OMX_AUDIO_ChannelLFE;
        break;
      case GST_AUDIO_CHANNEL_POSITION_REAR_CENTER:
        pos = OMX_AUDIO_ChannelCS;
        break;
      case GST_AUDIO_CHANNEL_POSITION_REAR_LEFT:
        pos = OMX_AUDIO_ChannelLR;
        break;
      case GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT:
        pos = OMX_AUDIO_ChannelRR;
        break;
      default:
        pos = OMX_AUDIO_ChannelNone;
        break;
    }
    eChannelMapping[i] = pos;
  }
}

static inline const gchar *
ch2str (OMX_AUDIO_CHANNELTYPE ch)
{
  switch (ch) {
    case OMX_AUDIO_ChannelNone:
      return "OMX_AUDIO_ChannelNone";
    case OMX_AUDIO_ChannelLF:
      return "OMX_AUDIO_ChannelLF";
    case OMX_AUDIO_ChannelRF:
      return "OMX_AUDIO_ChannelRF";
    case OMX_AUDIO_ChannelCF:
      return "OMX_AUDIO_ChannelCF";
    case OMX_AUDIO_ChannelLS:
      return "OMX_AUDIO_ChannelLS";
    case OMX_AUDIO_ChannelRS:
      return "OMX_AUDIO_ChannelRS";
    case OMX_AUDIO_ChannelLFE:
      return "OMX_AUDIO_ChannelLFE";
    case OMX_AUDIO_ChannelCS:
      return "OMX_AUDIO_ChannelCS";
    case OMX_AUDIO_ChannelLR:
      return "OMX_AUDIO_ChannelLR";
    case OMX_AUDIO_ChannelRR:
      return "OMX_AUDIO_ChannelRR";
    default:
      return "Invalid value";
  }
}

static inline gboolean
gst_omx_audio_sink_configure_pcm (GstOMXAudioSink * self,
    GstAudioRingBufferSpec * spec)
{
  OMX_AUDIO_PARAM_PCMMODETYPE param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = self->in_port->index;
  param.nChannels = OUT_CHANNELS (self->channels);
  param.eNumData =
      (self->is_signed ? OMX_NumericalDataSigned : OMX_NumericalDataUnsigned);
  param.eEndian =
      ((self->endianness ==
          G_LITTLE_ENDIAN) ? OMX_EndianLittle : OMX_EndianBig);
  param.bInterleaved = OMX_TRUE;
  param.nBitPerSample = self->width;

  param.nSamplingRate = self->rate;

  if (self->is_float) {
    /* This is cherrypicked from xbmc but it doesn't seems to be valid on my RPI.
     * https://github.com/xbmc/xbmc/blob/master/xbmc/cores/AudioEngine/Sinks/AESinkPi.cpp
     */
    param.ePCMMode = (OMX_AUDIO_PCMMODETYPE) 0x8000;
  } else {
    param.ePCMMode = OMX_AUDIO_PCMModeLinear;
  }

  if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_RAW) {
    channel_mapping (spec, &param.eChannelMapping[0]);
  }

  GST_DEBUG_OBJECT (self, "Setting PCM parameters");
  GST_DEBUG_OBJECT (self, "  nChannels: %u", (guint) param.nChannels);
  GST_DEBUG_OBJECT (self, "  eNumData: %s",
      (param.eNumData == OMX_NumericalDataSigned ? "signed" : "unsigned"));
  GST_DEBUG_OBJECT (self, "  eEndian: %s",
      (param.eEndian == OMX_EndianLittle ? "little endian" : "big endian"));
  GST_DEBUG_OBJECT (self, "  bInterleaved: %d", param.bInterleaved);
  GST_DEBUG_OBJECT (self, "  nBitPerSample: %u", (guint) param.nBitPerSample);
  GST_DEBUG_OBJECT (self, "  nSamplingRate: %u", (guint) param.nSamplingRate);
  GST_DEBUG_OBJECT (self, "  ePCMMode: %04x", param.ePCMMode);
  GST_DEBUG_OBJECT (self,
      "  eChannelMapping: {%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s}",
      ch2str (param.eChannelMapping[0]), ch2str (param.eChannelMapping[1]),
      ch2str (param.eChannelMapping[2]), ch2str (param.eChannelMapping[3]),
      ch2str (param.eChannelMapping[4]), ch2str (param.eChannelMapping[5]),
      ch2str (param.eChannelMapping[6]), ch2str (param.eChannelMapping[7]),
      ch2str (param.eChannelMapping[8]), ch2str (param.eChannelMapping[9]),
      ch2str (param.eChannelMapping[10]), ch2str (param.eChannelMapping[11]));

  err =
      gst_omx_component_set_parameter (self->comp, OMX_IndexParamAudioPcm,
      &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set PCM parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_audio_sink_prepare (GstAudioSink * audiosink,
    GstAudioRingBufferSpec * spec)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  GstAudioBaseSink *bsink = GST_AUDIO_BASE_SINK (audiosink);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_ERRORTYPE err;

  if (!gst_omx_audio_sink_parse_spec (self, spec))
    goto spec_parse;


  {
    GstCaps *prevcaps;
    GstStructure *structure;
    const gchar *vendor;
    gboolean mstar;

    prevcaps = spec->caps;
    structure = gst_caps_get_structure (prevcaps, 0);

    vendor = gst_structure_get_string (structure, "vendor");
    mstar = vendor && !g_strcmp0 (vendor, "mstar");
    GST_INFO_OBJECT (self, "set is_support_decoder to %d", mstar);

    if (self->have_audio_decoder != mstar) {
      self->have_audio_decoder = mstar;
      gst_omx_component_set_config (self->comp, OMX_IndexConfigHaveAudioDecoder,
          &mstar);
    }
  }


#if 1
  // set parameter
  gst_omx_component_set_parameter (self->comp, OMX_IndexConfigAudioPortIndex, &self->index);    // this is to indicate mixer path
  gst_omx_component_set_parameter (self->comp, OMX_IndexMstarAppType,
      self->app_type);
  gst_omx_component_set_parameter (self->comp, OMX_IndexMstarLowDelay,
      &self->bLowDelay);
#endif

  gst_omx_port_get_port_definition (self->in_port, &port_def);

  port_def.nBufferSize = self->buffer_size;
  /* Only allocate a min number of buffers for transfers from our ringbuffer to
   * the hw ringbuffer as we want to keep our small */
  port_def.nBufferCountActual = MAX (port_def.nBufferCountMin, 2);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingPCM;

  GST_DEBUG_OBJECT (self, "Updating outport port definition");
  GST_DEBUG_OBJECT (self, "  nBufferSize: %u", (guint) port_def.nBufferSize);
  GST_DEBUG_OBJECT (self, "  nBufferCountActual: %u", (guint)
      port_def.nBufferCountActual);
  GST_DEBUG_OBJECT (self, "  audio.eEncoding: 0x%08x",
      port_def.format.audio.eEncoding);

  err = gst_omx_port_update_port_definition (self->in_port, &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to configure port: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto configuration;
  }

  if (!gst_omx_audio_sink_configure_pcm (self, spec)) {
    goto configuration;
  }

  err = gst_omx_component_set_state (self->comp, OMX_StateIdle);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set state idle: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  err = gst_omx_port_set_enabled (self->in_port, TRUE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to enable port: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  GST_DEBUG_OBJECT (self, "Allocate buffers");
  err = gst_omx_port_allocate_buffers (self->in_port);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed on buffer allocation: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  err = gst_omx_port_wait_enabled (self->in_port, 5 * GST_SECOND);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "port not enabled: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  err = gst_omx_port_mark_reconfigured (self->in_port);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Couln't mark port as reconfigured: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  err = gst_omx_component_set_state (self->comp, OMX_StatePause);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set state paused: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  if (gst_omx_component_get_state (self->comp,
          GST_CLOCK_TIME_NONE) != OMX_StatePause)
    goto activation;

  /* Configure some parameters */
//    self->mute = 0;
  GST_OBJECT_LOCK (self);
  gst_omx_audio_sink_mute_set (self, self->mute);
  gst_omx_audio_sink_volume_set (self, self->volume);
  GST_OBJECT_UNLOCK (self);

#if defined (USE_OMX_TARGET_RPI)
  {
    GstOMXAudioSinkClass *klass = GST_OMX_AUDIO_SINK_GET_CLASS (self);
    OMX_ERRORTYPE err;
    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE param;

    if (klass->destination
        && g_strlen (klass->destination) < sizeof (param.sName)) {
      GST_DEBUG_OBJECT (self, "Setting destination: %s", klass->destination);
      GST_OMX_INIT_STRUCT (&param);
      g_strcpy ((char *) param.sName, klass->destination);
      err = gst_omx_component_set_config (self->comp,
          OMX_IndexConfigBrcmAudioDestination, &param);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self,
            "Failed to configuring destination: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto activation;
      }
    }
  }
#endif

#if 0
  // set parameter
  gst_omx_component_set_parameter (self->comp, OMX_IndexConfigAudioPortIndex, &self->index);    // this is to indicate mixer path
  gst_omx_component_set_parameter (self->comp, OMX_IndexMstarAppType,
      self->app_type);
  gst_omx_component_set_parameter (self->comp, OMX_IndexMstarLowDelay,
      &self->bLowDelay);
#endif

  // This patch is for multi-audio track, to recover the curent segdone after switching
  if (gst_omx_component_get_state (self->comp,
          GST_CLOCK_TIME_NONE) == OMX_StatePause) {
    g_atomic_int_set (&bsink->ringbuffer->segdone, self->ringbuffer_segdone);
  }

  return TRUE;

/* ERRORS */
spec_parse:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS, (NULL),
        ("Error parsing spec"));
    return FALSE;
  }

configuration:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS, (NULL),
        ("Configuration failed"));
    return FALSE;
  }
activation:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS, (NULL),
        ("Component activation failed"));
    return FALSE;
  }
}

static gboolean
gst_omx_audio_sink_unprepare (GstAudioSink * audiosink)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  OMX_ERRORTYPE err;

  if (gst_omx_component_get_state (self->comp, 0) == OMX_StateIdle)
    return TRUE;

  err = gst_omx_port_set_flushing (self->in_port, 5 * GST_SECOND, TRUE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set port flushing: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto failed;
  }

  err = gst_omx_component_set_state (self->comp, OMX_StateIdle);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set state idle: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto failed;
  }

  err = gst_omx_port_set_enabled (self->in_port, FALSE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set port disabled: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto failed;
  }

  err = gst_omx_port_wait_buffers_released (self->in_port, 5 * GST_SECOND);
  if (err != OMX_ErrorNone) {
    goto failed;
  }

  err = gst_omx_port_deallocate_buffers (self->in_port);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Couldn't deallocate buffers: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto failed;
  }

  err = gst_omx_port_wait_enabled (self->in_port, 5 * GST_SECOND);
  if (err != OMX_ErrorNone) {
    goto failed;
  }

  err = gst_omx_port_set_flushing (self->in_port, 5 * GST_SECOND, FALSE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set port not flushing: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto failed;
  }

  if (self->bNonPcmGap == TRUE) {
    gboolean bNonPcmGap = FALSE;
    self->bNonPcmGap = bNonPcmGap;
    gst_omx_component_set_config (self->comp, OMX_IndexConfigNonPcmGap,
        &bNonPcmGap);
  }

  gst_omx_component_get_state (self->comp, GST_CLOCK_TIME_NONE);

  return TRUE;

  /* ERRORS */
failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->comp),
            gst_omx_component_get_last_error (self->comp)));
    return FALSE;
  }
}

static GstOMXBuffer *
gst_omx_audio_sink_acquire_buffer (GstOMXAudioSink * self)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXPort *port = self->in_port;
  OMX_ERRORTYPE err, err2;
  GstOMXBuffer *buf = NULL;

  // This patch is for multi-audio track, to change IL to PLAYING after setcaps was done.
  // (gst-omx is always keeping in PLAYING)
  if (gst_omx_component_get_state (self->comp,
          GST_CLOCK_TIME_NONE) == OMX_StatePause) {
    err2 = gst_omx_component_set_state (self->comp, OMX_StateExecuting);
    if (err2 != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to set state executing: %s (0x%08x)",
          gst_omx_error_to_string (err2), err2);
    }
  }

  while (!buf) {
    acq_ret = gst_omx_port_acquire_buffer (port, &buf);
    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_ERROR) {
      goto component_error;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      GST_DEBUG_OBJECT (self, "Flushing...");
      goto flushing;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      GST_DEBUG_OBJECT (self, "Reconfigure...");
      /* Reallocate all buffers */
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self, "Failed to set port disabled: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self, "Couldn't deallocate buffers: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }

      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }

      err = gst_omx_port_allocate_buffers (port);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }
      continue;
    }
  }

  return buf;

/* ERRORS */
component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->comp),
            gst_omx_component_get_last_error (self->comp)));
    return NULL;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure input port"));
    return NULL;
  }
flushing:
  {
    return NULL;
  }
}

#if (PATCH_AUDIO_UNDERRUN_FOR_ICS==1)
static gint
gst_omx_audio_sink_get_ringbuffer_level_for_ics_underrun (GstAudioBaseSink *
    sink)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (sink);
  //g_print("==gst_audio_base_sink_get_ringbuf_level ()==\r\n");
  static guint64 pre_sample = 0;
  guint64 sample = 0;
  gint writeseg, segdone, sps;
  static gint pre_diff = 0;
  gint diff;

  /* assume we can append to the previous sample */
  sample = sink->next_sample;

  /* no previous sample, try to insert at position 0 */
  if (sample == -1) {
    GST_LOG_OBJECT (self, "Set sample to 0");
    sample = 0;
  }

  if (pre_sample != sample) {
    pre_sample = sample;

    sps = sink->ringbuffer->samples_per_seg;
    GST_LOG_OBJECT (self, " ===> sps %d", sps);
    /* figure out the segment and the offset inside the segment where
     * the sample should be written. */
    writeseg = sample / sps;

    /* get the currently processed segment */
    segdone = g_atomic_int_get (&sink->ringbuffer->segdone)
        - sink->ringbuffer->segbase;

    /* see how far away it is from the write segment */
    diff = writeseg - segdone;
    pre_diff = diff;
    GST_LOG_OBJECT (self, "diff %d = writeseg %d, segdone %d", diff, writeseg,
        sink->ringbuffer->segdone);
    return diff;
  } else {
    return pre_diff;
  }
}
#endif //PATCH_AUDIO_UNDERRUN_FOR_ICS
static gint
gst_omx_audio_sink_write (GstAudioSink * audiosink, gpointer data, guint length)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);

  GstOMXBuffer *buf;
  OMX_ERRORTYPE err;
#if (PATCH_AUDIO_UNDERRUN_FOR_ICS==1)
  gint diff = 0;
  GstAudioBaseSink *basesink = GST_AUDIO_BASE_SINK (self);
  GstClockTime cur_time, audio_under_run_timediff;
#endif

  GST_LOG_OBJECT (self, "received audio samples buffer of %u bytes", length);

  GST_OMX_AUDIO_SINK_LOCK (self);

#if (PATCH_AUDIO_UNDERRUN_FOR_ICS==1)
  if (g_str_equal (self->app_type, "mheg-ics")) //for ics certification : ICS003 Audio Mute Issue
  {
    if (self->asink_is_eos_received == FALSE
        && self->asink_gap_no_duration == FALSE) {
      diff =
          gst_omx_audio_sink_get_ringbuffer_level_for_ics_underrun (basesink);

      if (!GST_CLOCK_TIME_IS_VALID (self->audio_under_run_basetime)) {
        GST_DEBUG_OBJECT (self,
            "Invalid reassign: self->audio_under_run_basetime %"
            GST_TIME_FORMAT, GST_TIME_ARGS (self->audio_under_run_basetime));
        self->audio_under_run_basetime =
            gst_clock_get_time (GST_ELEMENT_CLOCK (self));
      }

      cur_time = gst_clock_get_time (GST_ELEMENT_CLOCK (self));
      audio_under_run_timediff = cur_time - self->audio_under_run_basetime;
      GST_DEBUG_OBJECT (self,
          "basetime %" GST_TIME_FORMAT "cur_time = %" GST_TIME_FORMAT
          " timediff %lld, diff %d",
          GST_TIME_ARGS (self->audio_under_run_basetime),
          GST_TIME_ARGS (cur_time), audio_under_run_timediff, diff);
      if ((diff < 5) && (audio_under_run_timediff >= 2 * GST_SECOND)) {
        GST_ERROR_OBJECT (self,
            "Signal -AUDIO_UNDERRUN --- diff %d, audio_under_run_basetime %lld",
            diff, audio_under_run_timediff);
        g_signal_emit (self, gst_omx_audio_sink_signal[AUDIO_UNDERRUN], 0, 0);
        self->audio_under_run_basetime = cur_time;
        GST_ERROR_OBJECT (self,
            "assigned self->audio_under_run_basetime = %" GST_TIME_FORMAT,
            GST_TIME_ARGS (self->audio_under_run_basetime));
      }
    }
  }
#endif //PATCH_AUDIO_UNDERRUN_FOR_ICS

  if (!(buf = gst_omx_audio_sink_acquire_buffer (self))) {
    goto beach;
  }

  if (buf->omx_buf->nAllocLen == length) {
    memcpy (buf->omx_buf->pBuffer + buf->omx_buf->nOffset, data, length);
  } else {
    if (0)                      //(self->channels != 2)  //We don't need transform because we donly have 2 channels pcm output, Frank.Lu
    {
      transform (self->channels, self->width, data,
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset, self->samples);
    } else {
      memcpy (buf->omx_buf->pBuffer + buf->omx_buf->nOffset, data, length);
    }
  }
  //buf->omx_buf->nFilledLen = buf->omx_buf->nAllocLen; //it seems wrong. I changed to below line. Frank.Lu
  buf->omx_buf->nFilledLen = length;

  err = gst_omx_port_release_buffer (self->in_port, buf);
  if (err != OMX_ErrorNone)
    goto release_error;

beach:

  GST_OMX_AUDIO_SINK_UNLOCK (self);

//    GST_LOG_OBJECT (self, "writed audio samples buffer of %u bytes", length);

  if (GST_CLOCK_TIME_IS_VALID (self->current_pts)) {
    guint64 tmp;
    tmp = 10 * GST_MSECOND;
    self->current_pts += tmp;
  }

  GST_LOG_OBJECT (self, "write done");
//    GST_LOG_OBJECT (self, "current pts %" GST_TIME_FORMAT "", GST_TIME_ARGS (self->current_pts));

  return length;

  /* ERRORS */
release_error:
  {
    GST_OMX_AUDIO_SINK_UNLOCK (self);
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase input buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    return 0;
  }
}

static guint
gst_omx_audio_sink_delay (GstAudioSink * audiosink)
{
#if defined (USE_OMX_TARGET_RPI)
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  OMX_PARAM_U32TYPE param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = self->in_port->index;
  param.nU32 = 0;
  err = gst_omx_component_get_config (self->comp,
      OMX_IndexConfigAudioRenderingLatency, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to get rendering latency: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    param.nU32 = 0;
  }

  GST_DEBUG_OBJECT (self, "reported delay %u samples", (guint) param.nU32);
  return param.nU32;
#else
#if 1                           //This seems neccessay but original design of this part always returns 0, Frank.Lu
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  OMX_TIME_CONFIG_RENDERINGDELAYTYPE param;
  OMX_ERRORTYPE err;

  err =
      gst_omx_component_get_config (self->comp,
      OMX_IndexConfigTimeRenderingDelay, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to get rendering latency: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }
  //printf("param.nRenderingDelay=%d", param.nRenderingDelay);
  return param.nRenderingDelay;
#else //original code
  return 0;
#endif
#endif
}

static void
gst_omx_audio_sink_reset (GstAudioSink * audiosink)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  GstAudioBaseSink *bsink = GST_AUDIO_BASE_SINK (audiosink);
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Flushing sink");

  self->ringbuffer_segdone = g_atomic_int_get (&bsink->ringbuffer->segdone);

  gst_omx_port_set_flushing (self->in_port, 5 * GST_SECOND, TRUE);

  GST_OBJECT_LOCK (self);

  if ((self->comp != NULL) && GST_CLOCK_TIME_IS_VALID (self->ease_start_time)) {
    self->ease_pause_time = gst_clock_get_time (GST_ELEMENT_CLOCK (self));
    gst_omx_component_get_config (self->comp, OMX_IndexConfigFade,
        &self->current_ease_volume);
    GST_DEBUG_OBJECT (self,
        "ease_pause_time %" GST_TIME_FORMAT ", current volume (%d)",
        GST_TIME_ARGS (self->ease_pause_time), self->current_ease_volume);
    // Set to pause ease voluem
    OMX_AUDIO_PARAM_EASE EaseStruct;
    EaseStruct.strEaseType = g_malloc (MAX_STRING_LEN);
    g_strlcpy (EaseStruct.strEaseType, self->ease_type, MAX_STRING_LEN);
    EaseStruct.u32DurationInMs = 10;
    EaseStruct.u32TargetVolume = self->current_ease_volume;
    if (self->comp != NULL) {
      gst_omx_component_set_config (self->comp, OMX_IndexConfigFade,
          &EaseStruct);
    }
    g_free (EaseStruct.strEaseType);
  }

  GST_OBJECT_UNLOCK (self);

  GST_OMX_AUDIO_SINK_LOCK (self);
  if ((state = gst_omx_component_get_state (self->comp, 0)) > OMX_StatePause) {
    gst_omx_component_set_state (self->comp, OMX_StatePause);
    gst_omx_component_get_state (self->comp, GST_CLOCK_TIME_NONE);
  }

  gst_omx_component_set_state (self->comp, state);
  gst_omx_component_get_state (self->comp, GST_CLOCK_TIME_NONE);

  gst_omx_port_set_flushing (self->in_port, 5 * GST_SECOND, FALSE);

  // can't drop data after playing to pause
  //self->rtc_drop_at_start = TRUE;
  //self->rtc_drop_count = 0;

  if (self->bNonPcmGap == TRUE) {
    gboolean bNonPcmGap = FALSE;
    self->bNonPcmGap = bNonPcmGap;
    gst_omx_component_set_config (self->comp, OMX_IndexConfigNonPcmGap,
        &bNonPcmGap);
  }

  GST_OMX_AUDIO_SINK_UNLOCK (self);
}

static GstBuffer *
gst_omx_audio_sink_payload (GstAudioBaseSink * audiobasesink, GstBuffer * buf)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiobasesink);

  if (self->iec61937) {
    GstBuffer *out;
    gint framesize;
    GstMapInfo iinfo, oinfo;
    GstAudioRingBufferSpec *spec = &audiobasesink->ringbuffer->spec;

    framesize = gst_audio_iec61937_frame_size (spec);
    if (framesize <= 0)
      return NULL;

    out = gst_buffer_new_and_alloc (framesize);

    gst_buffer_map (buf, &iinfo, GST_MAP_READ);
    gst_buffer_map (out, &oinfo, GST_MAP_WRITE);

    if (!gst_audio_iec61937_payload (iinfo.data, iinfo.size,
            oinfo.data, oinfo.size, spec, G_BIG_ENDIAN)) {
      gst_buffer_unref (out);
      return NULL;
    }

    gst_buffer_unmap (buf, &iinfo);
    gst_buffer_unmap (out, &oinfo);

    gst_buffer_copy_into (out, buf, GST_BUFFER_COPY_METADATA, 0, -1);
    return out;
  }

  return gst_buffer_ref (buf);
}

static gboolean
gst_omx_audio_sink_accept_caps (GstOMXAudioSink * self, GstCaps * caps)
{
  GstPad *pad = GST_BASE_SINK (self)->sinkpad;
  GstCaps *pad_caps;
  GstStructure *st;
  gboolean ret = FALSE;
  GstAudioRingBufferSpec spec = { 0 };

  pad_caps = gst_pad_query_caps (pad, caps);
  if (!pad_caps || gst_caps_is_empty (pad_caps)) {
    if (pad_caps)
      gst_caps_unref (pad_caps);
    ret = FALSE;
    goto done;
  }
  gst_caps_unref (pad_caps);

  /* If we've not got fixed caps, creating a stream might fail, so let's just
   * return from here with default acceptcaps behaviour */
  if (!gst_caps_is_fixed (caps))
    goto done;

  /* parse helper expects this set, so avoid nasty warning
   * will be set properly later on anyway  */
  spec.latency_time = GST_SECOND;
  if (!gst_audio_ring_buffer_parse_caps (&spec, caps))
    goto done;

  /* Make sure input is framed (one frame per buffer) and can be payloaded */
  switch (spec.type) {
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_AC3:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_EAC3:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_DTS:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_MPEG:
    {
      gboolean framed = FALSE, parsed = FALSE;
      st = gst_caps_get_structure (caps, 0);

      gst_structure_get_boolean (st, "framed", &framed);
      gst_structure_get_boolean (st, "parsed", &parsed);
      if ((!framed && !parsed) || gst_audio_iec61937_frame_size (&spec) <= 0)
        goto done;
    }
    default:{
    }
  }
  ret = TRUE;

done:
  gst_caps_replace (&spec.caps, NULL);
  return ret;
}

static gboolean
gst_omx_audio_sink_query (GstBaseSink * basesink, GstQuery * query)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (basesink);
  gboolean ret;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;

      gst_query_parse_accept_caps (query, &caps);
      ret = gst_omx_audio_sink_accept_caps (self, caps);
      gst_query_set_accept_caps_result (query, ret);
      ret = TRUE;
      break;
    }
    default:
      ret = GST_BASE_SINK_CLASS (parent_class)->query (basesink, query);
      break;
  }

  return ret;
}

static void
gst_omx_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_MUTE:
    {
      gboolean mute = g_value_get_boolean (value);
      GST_OBJECT_LOCK (self);
      if (self->mute != mute) {
        gst_omx_audio_sink_mute_set (self, mute);
      }
      GST_OBJECT_UNLOCK (self);
      break;
    }
    case PROP_VOLUME:
    {
      gdouble volume = g_value_get_double (value);
      GST_OBJECT_LOCK (self);
      if (volume != self->volume) {
        gst_omx_audio_sink_volume_set (self, volume);
      }
      GST_OBJECT_UNLOCK (self);
      break;
    }

    case PROP_RESOURCE_INFO:
    {
      const GstStructure *s = gst_value_get_structure (value);

      if (gst_structure_has_field (s, "audio-port")) {
        if (gst_structure_get_int (s, "audio-port", &self->index)) {
          guint32 param;
          GST_OBJECT_LOCK (self);
          param = self->index;
          if (self->comp != NULL)
            gst_omx_component_set_parameter (self->comp, OMX_IndexConfigAudioPortIndex, &param);        // this is to indicate mixer path
          GST_OBJECT_UNLOCK (self);
          GST_DEBUG_OBJECT (self, "index=%d", self->index);
        }
      }
      break;
    }
    case PROP_APP_TYPE:        // set // string    // set app type // "RTC" for Miracast app type
    {
      GST_OBJECT_LOCK (self);
      g_free (self->app_type);
      self->app_type = g_value_dup_string (value);
      /* setting NULL restores the default device */
      if (self->app_type == NULL) {
        self->app_type = g_strdup (DEFAULT_APP_TYPE);
      }

      if (g_str_equal (self->app_type, "RTC"))  // miracast
      {
        self->bLowDelay = TRUE;
      }
      GST_OBJECT_UNLOCK (self);

      if (self->bLowDelay == TRUE) {
        GST_AUDIO_BASE_SINK (self)->buffer_time =
            DEFAULT_PROP_RINGBUFFER_DELAY_LOW;

//            gst_base_sink_set_max_lateness (GST_BASE_SINK (self), -1);
        gst_base_sink_set_sync (GST_BASE_SINK (self), FALSE);
      } else {
        GST_AUDIO_BASE_SINK (self)->buffer_time =
            DEFAULT_PROP_RINGBUFFER_DELAY_NORMAL;
        gst_base_sink_set_sync (GST_BASE_SINK (self), TRUE);
      }

      if (self->comp != NULL) {
        gst_omx_component_set_parameter (self->comp, OMX_IndexMstarAppType,
            self->app_type);
      }
#if (MIRACAST_SINK_ADD_BUFFER_LIST==1)
      if (g_str_equal (self->app_type, "RTC")) {
        gst_omx_audio_sink_open_rtc_thread (self);
      }
#endif

      GST_DEBUG_OBJECT (self, "app_type=%s", self->app_type);
    }
      break;

    case PROP_NO_DELAY:        // set   // bool         // enable/disable no delay mode // miracast always set this value for low delay mode
    {
      gboolean bLowDelay = g_value_get_boolean (value);
      GST_OBJECT_LOCK (self);

      if (self->bLowDelay != bLowDelay) {
        if (self->comp != NULL) {
          gst_omx_component_set_parameter (self->comp, OMX_IndexMstarLowDelay,
              &self->bLowDelay);
        }
        self->bLowDelay = bLowDelay;
      }
      GST_OBJECT_UNLOCK (self);

      // as for low delay mode, reduce ring buffer size from 400 to 70ms
      if (self->bLowDelay == TRUE) {
        GST_AUDIO_BASE_SINK (self)->buffer_time =
            DEFAULT_PROP_RINGBUFFER_DELAY_LOW;

//            gst_base_sink_set_max_lateness (GST_BASE_SINK (self), -1);
        gst_base_sink_set_sync (GST_BASE_SINK (self), FALSE);
      } else {
        GST_AUDIO_BASE_SINK (self)->buffer_time =
            DEFAULT_PROP_RINGBUFFER_DELAY_NORMAL;
        gst_base_sink_set_sync (GST_BASE_SINK (self), TRUE);
      }

    }
      break;

    case PROP_NO_DELAY_MODE:   // set   // unsigned integer     // miracast always set this value to 2 for hybrid mode
    {
      guint nodelay_mode = g_value_get_uint (value);

      self->nodelay_mode = nodelay_mode;

      GST_DEBUG_OBJECT (self, "nodelay_mode=%u", self->nodelay_mode);

      break;
    }

    case PROP_NO_DELAY_SKIP:   // set   // unsigned integer // set buffering threshold of skippping // Miracast always set this value to 450 for dropping the data in the audio buffer when exceeds 450ms data in the buffer
    {
      guint nodelay_skip = g_value_get_uint (value);

      self->nodelay_skip = nodelay_skip;

      GST_DEBUG_OBJECT (self, "nodelay_skip=%u ms", self->nodelay_skip);

      break;
    }

    case PROP_NO_DELAY_FAST:   // set   // junsigned integer    // set buffering threshold of Fast      // Miracast always set this value to 300 for fast play mode when exceeds 300ms in the buffer
    {
      guint nodelay_fast = g_value_get_uint (value);

      self->nodelay_fast = nodelay_fast;

      GST_DEBUG_OBJECT (self, "nodelay_fast=%u ms", self->nodelay_fast);

      break;
    }

    case PROP_NO_DELAY_RECOVER:        // set   // unsigned integer     // set buffering threshold of normal state      // Miracast always set this value to 150 for recovering playing mode from "fast" or "skip" mode, under the 150ms in the audio buffer disable fast or skip mode
    {
      guint nodelay_recover = g_value_get_uint (value);

      self->nodelay_recover = nodelay_recover;

      GST_DEBUG_OBJECT (self, "nodelay_recover=%u ms", self->nodelay_recover);

      break;
    }

    case PROP_NO_DELAY_FAST_FREQ:      // set   // unsigned integer     // set freq of Fast State       // Miracast always set this value to 47250 for changing playrate
    {
      guint nodelay_fast_freq = g_value_get_uint (value);

      self->nodelay_fast_freq = (guint32) nodelay_fast_freq;

      GST_DEBUG_OBJECT (self, "nodelay_fast_freq=%u", self->nodelay_fast_freq);

      break;
    }

    case PROP_LIPSYNC_OFFSET:
    {
      guint lipsync_offset = g_value_get_uint (value);

      self->lipsync_offset = lipsync_offset;

      //gst_base_sink_set_render_delay  (GST_BASE_SINK (self), self->lipsync_offset * GST_MSECOND);

      GST_DEBUG_OBJECT (self, "lipsync_offset=%u ms", self->lipsync_offset);

      break;
    }

    case PROP_DISABLE_LOST_STATE:{
      if (GST_STATE (self) == GST_STATE_PLAYING) {
        self->disable_lost_state = g_value_get_boolean (value);
        GST_DEBUG_OBJECT (self, "disable_lost_state=%d",
            self->disable_lost_state);
      } else {
        GST_DEBUG_OBJECT (self, "not playing state, ignore disable_lost_state");
      }
      break;
    }

    case PROP_FADE:
    {

      const gchar *ease_type = NULL;
      self->ease_target_volume = 100;
      self->ease_duration = 0;

      const GstStructure *s = gst_value_get_structure (value);
      if (gst_structure_has_field (s, "target-volume")) {
        gst_structure_get_int (s, "target-volume", &self->ease_target_volume);
      }

      if (gst_structure_has_field (s, "duration-in-ms")) {
        gst_structure_get_int (s, "duration-in-ms", &self->ease_duration);
      }

      if (gst_structure_has_field (s, "fade")) {
        ease_type = gst_structure_get_string (s, "fade");
      }

      GST_DEBUG_OBJECT (self, "ease_type %s", ease_type);
      GST_DEBUG_OBJECT (self, "ease duration %d", self->ease_duration);
      GST_DEBUG_OBJECT (self, "ease volume %d", self->ease_target_volume);

      if (self->ease_type != NULL) {
        g_free (self->ease_type);
        self->ease_type = NULL;
      }
      self->ease_type = g_strdup (ease_type);

      OMX_AUDIO_PARAM_EASE EaseStruct;
      EaseStruct.strEaseType = g_strdup (ease_type);
      EaseStruct.u32DurationInMs = self->ease_duration;
      EaseStruct.u32TargetVolume = self->ease_target_volume;

      GST_OBJECT_LOCK (self);
      if (self->comp != NULL) {
        self->ease_start_time = gst_clock_get_time (GST_ELEMENT_CLOCK (self));
        gst_omx_component_set_config (self->comp, OMX_IndexConfigFade,
            &EaseStruct);

        GST_DEBUG_OBJECT (self, "ease_start_time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (self->ease_start_time));
      }
      GST_OBJECT_UNLOCK (self);
      g_free (EaseStruct.strEaseType);

      break;
    }

    case PROP_MIXER:           // set  // bool // set mixer mode    // use pcm mix
    case PROP_SERVERSIDE_TRICKPLAY:    // set  // bool // set server's trick play mode //
    case PROP_DECODED_SIZE:    // get  // unsigned long long   // size of total audio decoded ES data
    case PROP_UNDECODED_SIZE:  // get  // unsigned long long   // bytes of undecoded ES size
    case PROP_CURRENT_PTS:     // get  // unsigned long long   // get rendering timing audio position
    case PROP_NO_SEAMLESS:     // set   // bool         // seamless audio change         // for HbbTV
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_MUTE:
      GST_OBJECT_LOCK (self);
      g_value_set_boolean (value, self->mute);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_VOLUME:
      GST_OBJECT_LOCK (self);
      g_value_set_double (value, self->volume);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_CURRENT_PTS:     // get  // unsigned long long   // get rendering timing audio position
      GST_OBJECT_LOCK (self);
      g_value_set_uint64 (value, self->current_pts);
      GST_OBJECT_UNLOCK (self);

      GST_DEBUG_OBJECT (self, "self->current_pts=%llu", self->current_pts);

      break;
    case PROP_LIPSYNC_OFFSET:
      GST_OBJECT_LOCK (self);
      g_value_set_uint (value, self->lipsync_offset);
      GST_OBJECT_UNLOCK (self);
      break;

    case PROP_FADE_VOLUME:
    {
      gint volume;
      GST_OBJECT_LOCK (self);
      if (self->comp != NULL) {
        gst_omx_component_get_config (self->comp, OMX_IndexConfigFade, &volume);
      }
      GST_OBJECT_UNLOCK (self);

      g_value_set_int (value, volume);
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_omx_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (element);
  OMX_ERRORTYPE err;
  GstState srcState = (transition >> 3) & 0x7;
  GstState dstState = transition & 0x7;

  GST_DEBUG_OBJECT (self, "%s ==> %s", gst_element_state_get_name (srcState),
      gst_element_state_get_name (dstState));

  // Mstar Customized Patch Begin, FIXME
  // avoid pause to play, cutting sound issue, because need to info component
  // know now into pause state and do not flush data in DMA reader.
  if (transition == GST_STATE_CHANGE_PLAYING_TO_PAUSED) {
    guint32 param;
    GST_OBJECT_LOCK (self);
    param = 1;
    gst_omx_component_set_parameter (self->comp, OMX_IndexArenderPause, &param);
    GST_OBJECT_UNLOCK (self);
  }

  /*
     if (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING) {
     guint32 param;
     GST_OBJECT_LOCK (self);
     param = 0;
     gst_omx_component_set_parameter (self->comp, OMX_IndexArenderPause, &param);
     GST_OBJECT_UNLOCK (self);
     }
   */
  // Mstar Customized Patch End

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      GST_DEBUG_OBJECT (self, "going to PLAYING state");
      err = gst_omx_component_set_state (self->comp, OMX_StateExecuting);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self, "Failed to set state executing: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        return GST_STATE_CHANGE_FAILURE;
      }

      if (gst_omx_component_get_state (self->comp,
              GST_CLOCK_TIME_NONE) != OMX_StateExecuting) {
        return GST_STATE_CHANGE_FAILURE;
      }
#if (PATCH_AUDIO_UNDERRUN_FOR_ICS==1)
      if (g_str_equal (self->app_type, "mheg-ics")) {
        //reset basetime
        self->audio_under_run_basetime =
            gst_clock_get_time (GST_ELEMENT_CLOCK (self));
      }
#endif
      if ((self->comp != NULL)
          && GST_CLOCK_TIME_IS_VALID (self->ease_start_time)
          && GST_CLOCK_TIME_IS_VALID (self->ease_pause_time)) {
        // Set to pause ease voluem
        OMX_AUDIO_PARAM_EASE EaseStruct;
        GstClockTimeDiff ClockDiff;
        EaseStruct.strEaseType = g_malloc (MAX_STRING_LEN);
        g_strlcpy (EaseStruct.strEaseType, self->ease_type, MAX_STRING_LEN);
        EaseStruct.u32DurationInMs = 10;
        EaseStruct.u32TargetVolume = self->current_ease_volume;

        GST_OBJECT_LOCK (self);
        if (self->comp != NULL) {
          gst_omx_component_set_config (self->comp, OMX_IndexConfigFade,
              &EaseStruct);
        }
        GST_OBJECT_UNLOCK (self);


        // set to real target ease volume
        ClockDiff =
            GST_CLOCK_DIFF (self->ease_start_time, self->ease_pause_time);
        if (self->ease_duration * GST_MSECOND > ClockDiff) {
          EaseStruct.u32DurationInMs =
              (self->ease_duration * GST_MSECOND - ClockDiff) / GST_MSECOND;
        } else {
          EaseStruct.u32DurationInMs = 10;
        }

        EaseStruct.u32TargetVolume = self->ease_target_volume;
        GST_OBJECT_LOCK (self);
        if (self->comp != NULL) {
          GST_DEBUG_OBJECT (self, "Easing from %d to %d in %lu ms",
              self->current_ease_volume, self->ease_target_volume,
              EaseStruct.u32DurationInMs);
          gst_omx_component_set_config (self->comp, OMX_IndexConfigFade,
              &EaseStruct);
        }
        GST_OBJECT_UNLOCK (self);

        g_free (EaseStruct.strEaseType);
      }

      GST_DEBUG_OBJECT (self, "in PLAYING state");
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    {
      GST_DEBUG_OBJECT (self, "going to PAUSED state");
      err = gst_omx_component_set_state (self->comp, OMX_StatePause);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self, "Failed to set state paused: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        return GST_STATE_CHANGE_FAILURE;
      }

      if (gst_omx_component_get_state (self->comp,
              GST_CLOCK_TIME_NONE) != OMX_StatePause) {
        return GST_STATE_CHANGE_FAILURE;
      }
      GST_DEBUG_OBJECT (self, "in PAUSED state");
      break;
    }
    default:
      break;
  }

  return ret;
}

static void
gst_omx_audio_sink_finalize (GObject * object)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (object);

#if (MIRACAST_SINK_ADD_BUFFER_LIST==1)
  if (g_str_equal (self->app_type, "RTC")) {
    gst_omx_audio_sink_close_rtc_thread (self);
  }
#endif

#if (STORE_MODE_PROTECTING_CODE_USED_G_TIMEOUT==1)
  if (self->protecting_g_timeout_func) {
    gst_omx_audio_sink_remove_protecting_func (self);
  }
#endif

  g_free (self->app_type);
  g_mutex_clear (&self->lock);

#ifdef MS_TEMPO_SINK
  if (self->tempo_inbuf != NULL) {
    g_free (self->tempo_inbuf);
  }
  if (self->tempo_outbuf != NULL) {
    g_free (self->tempo_outbuf);
  }
  if (self->tempo_handle != NULL) {
    g_free (self->tempo_handle);
  }
#endif

  if (self->ease_type != NULL) {
    g_free (self->ease_type);
    self->ease_type = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_omx_audio_sink_class_init (GstOMXAudioSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);
  GstAudioBaseSinkClass *baudiosink_class = GST_AUDIO_BASE_SINK_CLASS (klass);
  GstAudioSinkClass *audiosink_class = GST_AUDIO_SINK_CLASS (klass);

  gobject_class->set_property = gst_omx_audio_sink_set_property;
  gobject_class->get_property = gst_omx_audio_sink_get_property;
  gobject_class->finalize = gst_omx_audio_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "mute channel",
          DEFAULT_PROP_MUTE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "volume factor, 1.0=100%",
          0.0, VOLUME_MAX_DOUBLE, DEFAULT_PROP_VOLUME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RESOURCE_INFO,
      g_param_spec_boxed ("resource-info", "Resource information",
          "Hold various information for managing resource",
          GST_TYPE_STRUCTURE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject_class, PROP_NO_DELAY,
      g_param_spec_boolean ("nodelay", "no-delay", "app no delay",
          DEFAULT_PROP_NODELAY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DECODED_SIZE,
      g_param_spec_uint64 ("decoded-size",
          "decoded-size",
          "Decoded Data Length",
          0, G_MAXUINT64, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_UNDECODED_SIZE,
      g_param_spec_uint64 ("undecoded-size",
          "undecoded-size",
          "UnDecoded Data Length",
          0, G_MAXUINT64, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_CURRENT_PTS,
      g_param_spec_uint64 ("current-pts",
          "current-pts",
          "Get current PTS",
          0, G_MAXUINT64, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_APP_TYPE,
      g_param_spec_string ("app-type",
          "app-type",
          "Set app type",
          DEFAULT_APP_TYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_SERVERSIDE_TRICKPLAY,
      g_param_spec_boolean ("serverside-trickplay",
          "server side trick play",
          "server side trick play", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_NO_DELAY_MODE,
      g_param_spec_uint ("nodelay-mode",
          "nodelay-mode",
          "nodelay Mode (0: CONT, 1: SKIP, 2: HYBRID)",
          0, 2, DEFAULT_NODELAY_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_NO_DELAY_SKIP,
      g_param_spec_uint ("nodelay-skip",
          "nodelay-skip",
          "set buffering Threshold of Skipping",
          0, G_MAXUINT32, DEFAULT_NODELAY_SKIP,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_NO_DELAY_FAST,
      g_param_spec_uint ("nodelay-fast",
          "nodelay-fast",
          "set buffering Threshold of Fast",
          0, G_MAXUINT32, DEFAULT_NODELAY_FAST,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_NO_DELAY_RECOVER,
      g_param_spec_uint ("nodelay-recover",
          "nodelay-recover",
          "set buffering Threshold of Normal State",
          0, G_MAXUINT32, DEFAULT_NODELAY_RECOVER,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_NO_DELAY_FAST_FREQ,
      g_param_spec_uint ("nodelay-fast-freq",
          "nodelay-fast-freq",
          "set frequency of Fast State (unit : Hz)",
          0, G_MAXUINT32, DEFAULT_NODELAY_FAST_FREQ,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_LIPSYNC_OFFSET,
      g_param_spec_uint ("lipsync-offset",
          "lipsync-offset",
          "property works in audio sink and video sink element.",
          0, G_MAXUINT32, DEFAULT_LIPSYNC_OFFSET,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_DISABLE_LOST_STATE,
      g_param_spec_boolean ("disable-lost-state",
          "disable-lost-state",
          "Disable seek event if TRUE", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_FADE,
      g_param_spec_boxed ("fade",
          "fade",
          "set fade structure",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FADE_VOLUME, g_param_spec_int ("fade-volume",    // name
          "Fade Volume",        // nick name
          "get the fade volume",        // description
          0, 100, 0,            // minimum, maximum, default
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_omx_audio_sink_signal[AUDIO_UNDERRUN] =
      g_signal_new ("audio-underrun", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_audio_sink_change_state);

  basesink_class->query = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_query);

  baudiosink_class->payload = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_payload);

  audiosink_class->open = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_open);
  audiosink_class->close = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_close);
  audiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_prepare);
  audiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_unprepare);
  audiosink_class->write = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_write);
  audiosink_class->delay = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_delay);
  audiosink_class->reset = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_reset);

  basesink_class->event = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_event);

  klass->cdata.type = GST_OMX_COMPONENT_TYPE_SINK;
}

static void
gst_omx_audio_sink_init (GstOMXAudioSink * self)
{
#ifdef MS_TEMPO_SINK
  gint32 instanceRequireBytes;
  gint32 input_size;
  gint32 tempo_rate = 1;
  gint8 *instanceSram = NULL;
#endif


  self->base_sink_chain_func = GST_PAD_CHAINFUNC (GST_BASE_SINK_PAD (self));
  gst_pad_set_chain_function (GST_BASE_SINK_PAD (self),
      GST_DEBUG_FUNCPTR (gst_omx_audio_sink_sink_chain));

  g_mutex_init (&self->lock);

  self->mute = DEFAULT_PROP_MUTE;
  self->volume = DEFAULT_PROP_VOLUME;

  /* If there is no mstar audio decoder at the front, set to FALSE */
  self->have_audio_decoder = FALSE;

  /*Customized info init */
  self->index = 0;              // default output via mixer path 0
  self->bLowDelay = FALSE;
  self->app_type = g_strdup (DEFAULT_APP_TYPE);
  self->nodelay_mode = DEFAULT_NODELAY_MODE;
  self->nodelay_skip = DEFAULT_NODELAY_SKIP;
  self->nodelay_fast = DEFAULT_NODELAY_FAST;
  self->nodelay_recover = DEFAULT_NODELAY_RECOVER;
  self->nodelay_fast_freq = DEFAULT_NODELAY_FAST_FREQ;
  self->lipsync_offset = DEFAULT_LIPSYNC_OFFSET;

  self->prvTimeStamp = GST_CLOCK_TIME_NONE;
  self->firstTimeStamp = GST_CLOCK_TIME_NONE;

  self->ease_type = NULL;
  self->ease_start_time = GST_CLOCK_TIME_NONE;
  self->ease_pause_time = GST_CLOCK_TIME_NONE;

  self->disable_lost_state = FALSE;

#if (PATCH_AUDIO_UNDERRUN_FOR_ICS==1)
  self->audio_under_run_basetime =
      gst_clock_get_time (GST_ELEMENT_CLOCK (self));
  self->asink_is_eos_received = FALSE;
  self->asink_gap_no_duration = FALSE;
#endif
  /* For the Raspberry PI there's a big hw buffer and 400 ms seems a good
   * size for our ringbuffer. OpenSL ES Sink also allocates a buffer of 400 ms
   * in Android so I guess that this should be a sane value for OpenMax in
   * general. */
  GST_AUDIO_BASE_SINK (self)->buffer_time =
      DEFAULT_PROP_RINGBUFFER_DELAY_NORMAL;
  gst_audio_base_sink_set_provide_clock (GST_AUDIO_BASE_SINK (self), FALSE);
  gst_audio_base_sink_set_slave_method (GST_AUDIO_BASE_SINK (self), GST_AUDIO_BASE_SINK_SLAVE_SKEW);    // default: GST_AUDIO_BASE_SINK_SLAVE_SKEW
  gst_audio_base_sink_set_drift_tolerance (GST_AUDIO_BASE_SINK (self),
      CORRECT_TOLERANCE);

  /* RTC used start */
#if (MIRACAST_SINK_ADD_BUFFER_LIST==1)
  self->rtc_list = NULL;
  self->rtc_task = NULL;
#endif
  /* RTC used end */

  self->current_pts = GST_CLOCK_TIME_NONE;


#ifdef MS_TEMPO_SINK
  instanceRequireBytes = mst_tempo_get_sram_size (48000);
  self->tempo_inbuf = g_malloc (TEMPO_BUF_SIZE);
  self->tempo_outbuf = g_malloc (TEMPO_BUF_SIZE);
  instanceSram = (gint8 *) g_malloc (instanceRequireBytes);
  if (self->tempo_inbuf == NULL || instanceSram == NULL
      || self->tempo_outbuf == NULL) {
    // MS_OMX_Port_Destructor(pOMXComponent);
    // MS_OMX_BaseComponent_Destructor(hComponent);
    //ret = OMX_ErrorInsufficientResources;
    //MS_OSAL_Log(MS_LOG_ERROR, "OMX_ErrorInsufficientResources, Line:%d", __LINE__);
    GST_DEBUG_OBJECT (self, "tempo OMX_ErrorInsufficientResources\n");

    if (instanceSram != NULL) {
      g_free (instanceSram);
      instanceSram = NULL;
    }
    return;
  }
  memset (self->tempo_inbuf, 0, 1024 * 312);
  memset (self->tempo_outbuf, 0, 1024 * 312);

  self->tempo_handle =
      mst_tempo_init ((unsigned char *) instanceSram, (int) 48000,
      (int *) &input_size, (int) tempo_rate);
  self->tempo_framesize = input_size;
  self->tempo_pts = GST_CLOCK_TIME_NONE;
  GST_DEBUG_OBJECT (self, "Gstomxaudiosink tempo init !!!!!!!!!!!!!!");
#endif

#if (STORE_MODE_PROTECTING_CODE_USED_G_TIMEOUT==1)
  self->protecting_g_timeout_func = 0;
  self->protecting_g_timeout_pcm_coming_Time = GST_CLOCK_TIME_NONE;
  gst_omx_audio_sink_add_protecting_func (self);
#endif

}
