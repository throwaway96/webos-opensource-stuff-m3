/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2013, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2014, Sebastian Dröge <sebastian@centricular.com>
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
#include "gstomxaudiodec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_audio_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_audio_dec_debug_category

#define DEFAULT_APP_TYPE        "default_app_type"

//#define NODELAY_ADEC_DROP_FRAME_NUMBER         150
#define NODELAY_ADEC_DROP_FRAME_NUMBER         1


/* prototypes */
static void gst_omx_audio_dec_finalize (GObject * object);

static GstStateChangeReturn
gst_omx_audio_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_omx_audio_dec_open (GstAudioDecoder * decoder);
static gboolean gst_omx_audio_dec_close (GstAudioDecoder * decoder);
static gboolean gst_omx_audio_dec_start (GstAudioDecoder * decoder);
static gboolean gst_omx_audio_dec_stop (GstAudioDecoder * decoder);
static gboolean gst_omx_audio_dec_set_format (GstAudioDecoder * decoder,
    GstCaps * caps);
static void gst_omx_audio_dec_flush (GstAudioDecoder * decoder, gboolean hard);
static GstFlowReturn gst_omx_audio_dec_handle_frame (GstAudioDecoder * decoder,
    GstBuffer * buffer);
static GstFlowReturn gst_omx_audio_dec_drain (GstOMXAudioDec * self);
static gboolean gst_omx_audio_dec_sink_event (GstAudioDecoder * dec,
    GstEvent * event);
static GstFlowReturn gst_omx_audio_dec_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static gboolean gst_omx_audio_dec_sink_event2 (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_omx_audio_dec_pre_push (GstAudioDecoder * dec,
    GstBuffer ** buffer);
static gboolean gst_omx_audio_dec_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
#if (MIRACAST_ADD_BUFFER_LIST==1)
static void gst_omx_audio_dec_rtc_thread_func (GstOMXAudioDec * self);
static void gst_omx_audio_dec_open_rtc_thread (GstOMXAudioDec * self);
static void gst_omx_audio_dec_close_rtc_thread (GstOMXAudioDec * self);
static GstFlowReturn gst_omx_audio_dec_sink_rtc_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
#endif

enum
{
  PROP_0,
  PROP_RESOURCE_INFO,
  PROP_SILENT,
  PROP_DECODED_SIZE,
  PROP_UNDECODED_SIZE,
  PROP_CURRENT_PTS,
  PROP_WINDOW_ID,
  PROP_APP_TYPE,
  PROP_URI,
  PROP_SERVERSIDE_TRICKPLAY,
  PROP_DTS_SEAMLESS,
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

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_audio_dec_debug_category, "omxaudiodec", 0, \
      "debug category for gst-omx audio decoder base class");


G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXAudioDec, gst_omx_audio_dec,
    GST_TYPE_AUDIO_DECODER, DEBUG_INIT);


static void
parent_check (GstOMXAudioDec * self)
{
  GstElement *el, *el1;

  el = el1 = GST_ELEMENT_PARENT (self);
  while (el) {
    GST_DEBUG_OBJECT (self, "el name: %s", GST_ELEMENT_NAME (el));

    el1 = el;
    el = GST_ELEMENT_PARENT (el1);
  }

  self->parent_check = TRUE;
}

#if (MIRACAST_ADD_BUFFER_LIST==1)
static void
gst_omx_audio_dec_rtc_thread_func (GstOMXAudioDec * self)
{
  guint len;
  GstBuffer *buf;

  g_mutex_lock (&self->rtc_thread_mutex);
  len = g_slist_length (self->rtc_list);

  if (self->is_custom_player == TRUE) {
    if (self->start_decode == FALSE) {
      if (len >= 45) {
        self->start_decode = TRUE;
      } else {
        g_mutex_unlock (&self->rtc_thread_mutex);
        return;
      }
    }
  }

  if (len == 0) {
    g_mutex_unlock (&self->rtc_thread_mutex);
    if (self->is_custom_player == FALSE) {
      g_usleep (5000);
    }
    return;
  }

  buf = self->rtc_list->data;
  self->rtc_list = g_slist_remove (self->rtc_list, buf);
  g_mutex_unlock (&self->rtc_thread_mutex);

  self->base_sink_chain_func (GST_AUDIO_DECODER_SINK_PAD (self),
      (GstObject *) self, buf);
}

static void
gst_omx_audio_dec_open_rtc_thread (GstOMXAudioDec * self)
{
  GstTask *task;

  g_mutex_init (&self->rtc_thread_mutex);
  g_rec_mutex_init (&self->rtc_rec_lock);

  GST_DEBUG_OBJECT (self, "start rtc task");

  GST_OBJECT_LOCK (self);
  task = self->rtc_task;
  task =
      gst_task_new ((GstTaskFunction) gst_omx_audio_dec_rtc_thread_func, self,
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

  GST_ERROR_OBJECT (self, "rtc task open ok");
}

static void
gst_omx_audio_dec_close_rtc_thread (GstOMXAudioDec * self)
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
      g_print ("%s: flush ES data %d\r\n", __FUNCTION__, len);

      tmp = self->rtc_list->data;
      self->rtc_list = g_slist_remove (self->rtc_list, tmp);
      gst_buffer_unref (tmp);
      len = g_slist_length (self->rtc_list);
    }
    self->rtc_list = NULL;
  }
}

static GstFlowReturn
gst_omx_audio_dec_sink_rtc_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (parent);

  g_mutex_lock (&self->rtc_thread_mutex);

  if (self->is_custom_player == TRUE) {
    guint len;

    len = g_slist_length (self->rtc_list);
    while (len >= 50) {
      g_mutex_unlock (&self->rtc_thread_mutex);

      g_usleep (20000);

      g_mutex_lock (&self->rtc_thread_mutex);

      len = g_slist_length (self->rtc_list);
    }
  }

  self->rtc_list = g_slist_append (self->rtc_list, buf);

  g_mutex_unlock (&self->rtc_thread_mutex);

  return ret;
}
#endif

static void
gst_omx_audio_dec_init (GstOMXAudioDec * self)
{
  GstOMXAudioDecClass *klass = GST_OMX_AUDIO_DEC_GET_CLASS (self);

  self->base_sink_chain_func =
      GST_PAD_CHAINFUNC (GST_AUDIO_DECODER_SINK_PAD (self));
  gst_pad_set_chain_function (GST_AUDIO_DECODER_SINK_PAD (self),
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_sink_chain));

  self->base_sink_event_func =
      GST_PAD_EVENTFUNC (GST_AUDIO_DECODER_SINK_PAD (self));
  gst_pad_set_event_function (GST_AUDIO_DECODER_SINK_PAD (self),
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_sink_event2));

  self->src_query_func = GST_PAD_QUERYFUNC (GST_AUDIO_DECODER_SRC_PAD (self));
  gst_pad_set_query_function (GST_AUDIO_DECODER_SRC_PAD (self),
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_src_query));

  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_drainable (GST_AUDIO_DECODER (self), TRUE);

  g_mutex_init (&self->drain_lock);
  g_cond_init (&self->drain_cond);

  self->index = 1;              // default decode by decoder 0
  self->app_type = g_strdup (DEFAULT_APP_TYPE);

  klass->flush = NULL;

  /* RTC used start */
#if (MIRACAST_ADD_BUFFER_LIST==1)
  self->rtc_list = NULL;
  self->rtc_task = NULL;
#endif
  /* RTC used end */

  self->parent_check = FALSE;
  self->is_custom_player = FALSE;
  self->is_eos_received = FALSE;
  self->dts_seamless = FALSE;
  self->serverside_trickplay = FALSE;
  self->output_frame = -1;
}

static GstFlowReturn
gst_omx_audio_dec_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (parent);

  if (self->rate != 1.0) {
    gst_buffer_unref (buf);
    return ret;
  }
#if 0
  GstMapInfo minfo;
  GstClockTime timestamp, duration;

  timestamp = GST_BUFFER_TIMESTAMP (buf);
  duration = GST_BUFFER_DURATION (buf);

  gst_buffer_map (buf, &minfo, GST_MAP_READ);

  GST_DEBUG_OBJECT (self,
      "INPUT ES TS=%" GST_TIME_FORMAT " DUR=%" GST_TIME_FORMAT
      " Size=%d maxsize=%d", GST_TIME_ARGS (timestamp),
      GST_TIME_ARGS (duration), minfo.size, minfo.maxsize);

  gst_buffer_unmap (buf, &minfo);
#endif

#if (MIRACAST_ADD_BUFFER_LIST==1)
  if (g_str_equal (self->app_type, "RTC")) {
    if (self->rtc_drop_at_start == TRUE) {
      self->rtc_drop_count++;
      if (self->rtc_drop_count >= NODELAY_ADEC_DROP_FRAME_NUMBER) {     /* drop first garbage data */
        self->rtc_drop_at_start = FALSE;
      } else {
        gst_buffer_unref (buf);
        return GST_FLOW_OK;
      }
    }

    ret = gst_omx_audio_dec_sink_rtc_chain (pad, parent, buf);
  } else if (self->is_custom_player == TRUE) {
    ret = gst_omx_audio_dec_sink_rtc_chain (pad, parent, buf);
  } else {
    ret = self->base_sink_chain_func (pad, parent, buf);
  }
#else
  ret = self->base_sink_chain_func (pad, parent, buf);
#endif

  return ret;
}

static gboolean
gst_omx_audio_dec_sink_event2 (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret;
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (parent);

//    GST_DEBUG_OBJECT (self, "#### %s: %s ####", __FUNCTION__, GST_EVENT_TYPE_NAME (event));

  ret = self->base_sink_event_func (pad, parent, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      if (self->rate != 1.0) {
        GST_OBJECT_LOCK (self);
//                gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
        gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self),
            gst_event_new_gap (0, GST_CLOCK_TIME_NONE));
        GST_OBJECT_UNLOCK (self);
      }
      self->seeking = FALSE;
    }
      break;
    case GST_EVENT_FLUSH_START:
    {
      g_mutex_lock (&self->drain_lock);
      if (self->draining) {
        GST_DEBUG_OBJECT (self, "Draining aborted by seeking");
        self->draining = FALSE;
        g_cond_broadcast (&self->drain_cond);
      }
      self->seeking = TRUE;
      g_mutex_unlock (&self->drain_lock);
    }
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_omx_audio_dec_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret;
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (parent);

  GST_DEBUG_OBJECT (self, "#### gst_omx_audio_dec_src_query %s ####",
      GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
    {
      GstStructure *structure = NULL;

      structure = (GstStructure *) gst_query_get_structure (query);
      if (structure) {
        const gchar *name;

        name = gst_structure_get_name (structure);
        if (name) {
#if (STORE_MODE_PROTECTING_CODE_ADEC==1)
          if (g_str_equal (name, "Have_EOS")) {
            GValue eos_received = { 0 };

            g_value_init (&eos_received, G_TYPE_BOOLEAN);
            g_value_set_boolean (&eos_received, self->is_eos_received);
            gst_structure_set_value (structure, "receive_eos", &eos_received);

            return TRUE;
          }
#endif
        }
      }

      break;
    }

    default:
      break;
  }

  ret = self->src_query_func (pad, parent, query);

  return ret;
}

static void
gst_omx_audio_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (object);
  GST_DEBUG_OBJECT (self, "set prop %d", prop_id);


  switch (prop_id) {
    case PROP_RESOURCE_INFO:
    {
      const GstStructure *s = gst_value_get_structure (value);

      if (gst_structure_has_field (s, "audio-port")) {
        if (gst_structure_get_int (s, "audio-port", &self->index)) {
          guint32 param;

          GST_DEBUG_OBJECT (self, "ADEC ID %d", self->index);

          GST_OBJECT_LOCK (self);
          param = self->index;
          if (self->dec != NULL) {
            gst_omx_component_set_parameter (self->dec,
                OMX_IndexConfigAudioPortIndex, &param);
          }
          GST_OBJECT_UNLOCK (self);
        }
      }
      break;
    }

    case PROP_APP_TYPE:
    {
      GST_OBJECT_LOCK (self);
      g_free (self->app_type);
      self->app_type = g_value_dup_string (value);
      /* setting NULL restores the default device */
      if (self->app_type == NULL) {
        self->app_type = g_strdup (DEFAULT_APP_TYPE);
      }

      if (self->dec != NULL) {
        gst_omx_component_set_parameter (self->dec, OMX_IndexMstarAppType,
            self->app_type);
      }

      GST_DEBUG_OBJECT (self, "app_type=%s", self->app_type);
      GST_OBJECT_UNLOCK (self);

#if (MIRACAST_ADD_BUFFER_LIST==1)
      if (g_str_equal (self->app_type, "RTC")) {
        gst_omx_audio_dec_open_rtc_thread (self);
        self->output_frame = -1;
      }
#endif
#if 0
      if (g_str_equal (self->app_type, "chrome")) {
        self->is_custom_player = TRUE;
      }
#endif
      break;
    }

    case PROP_DTS_SEAMLESS:
    {
      gboolean bDtsSeamless = g_value_get_boolean (value);
      self->dts_seamless = bDtsSeamless;

      if (self->dec != NULL) {
        GstEvent *flush_start, *flush_stop;
        flush_start = gst_event_new_flush_start ();
        flush_stop = gst_event_new_flush_stop (FALSE);
        GST_DEBUG_OBJECT (self, "Set seamless mode and flush");
        gst_pad_push_event (GST_AUDIO_DECODER (object)->srcpad, flush_start);
        gst_omx_component_set_parameter (self->dec, OMX_IndexMstarDtsSeamless,
            &bDtsSeamless);
        gst_pad_push_event (GST_AUDIO_DECODER (object)->srcpad, flush_stop);
      }

      break;
    }
    case PROP_SERVERSIDE_TRICKPLAY:
      self->serverside_trickplay = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_omx_audio_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (object);
  GST_DEBUG_OBJECT (self, "get prop %d", prop_id);

  switch (prop_id) {
    case PROP_DECODED_SIZE:
    {
      OMX_U64 u64Param = 0;
      gst_omx_component_get_parameter (self->dec, OMX_IndexMstarDecodedSize,
          &u64Param);
      g_value_set_uint64 (value, u64Param);
      break;
    }
    case PROP_UNDECODED_SIZE:
    {
      OMX_U64 u64Param = 0;
      gst_omx_component_get_parameter (self->dec, OMX_IndexMstarUnDecodedeSize,
          &u64Param);
      g_value_set_uint64 (value, u64Param);
      break;
    }

    case PROP_0:
    case PROP_RESOURCE_INFO:
    case PROP_SILENT:
    case PROP_WINDOW_ID:
    case PROP_APP_TYPE:
    case PROP_URI:
    case PROP_SERVERSIDE_TRICKPLAY:
      break;

    case PROP_CURRENT_PTS:     // get  // unsigned long long   // get rendering timing audio position
      GST_OBJECT_LOCK (self);
      g_value_set_uint64 (value, self->current_pts);
      GST_OBJECT_UNLOCK (self);

      GST_DEBUG_OBJECT (self, "self->current_pts=%llu", self->current_pts);

      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_omx_audio_dec_pre_push (GstAudioDecoder * dec, GstBuffer ** buffer)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (dec);
  GstBuffer *buf = *buffer;
  GstMapInfo minfo;
  GstClockTime duration;
  guint32 bytes_per_sample;

  //GST_DEBUG_OBJECT (self, "dec_pre_push prate=%f OUTPUT PTS=%" GST_TIME_FORMAT " DUR=%" GST_TIME_FORMAT "",self->rate,
  //          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  if (0)                        // (self->rate == 0.5 || self->rate == 2) move to sink
  {
    if (!GST_CLOCK_TIME_IS_VALID (self->pts)) {
      if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buf))) {
        self->pts = GST_BUFFER_PTS (buf);
      } else {
        self->pts = 0;
      }
    }

    GST_BUFFER_PTS (buf) = self->pts;

    gst_buffer_map (buf, &minfo, GST_MAP_READ);

    duration = minfo.size;

    //because decoder output is 48000
    if (self->sample_rate == 96000) {
      self->sample_rate = 48000;
    }

    bytes_per_sample = 2 * self->bits_per_sample / 8;   //@ here default channel is 2, if channel is diff to 2, need to modify it
    duration /= bytes_per_sample;
    duration *= 1000;
    duration /= self->sample_rate;
    duration *= GST_MSECOND;

    GST_BUFFER_DURATION (buf) = duration;

    GST_DEBUG_OBJECT (self,
        "0.5x output pts %" GST_TIME_FORMAT " dura %" GST_TIME_FORMAT
        " Size %d", GST_TIME_ARGS (GST_BUFFER_PTS (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), minfo.size);

    self->pts += duration;

    gst_buffer_unmap (buf, &minfo);
  }

  self->current_pts = GST_BUFFER_PTS (buf);

  return GST_FLOW_OK;
}

static gboolean
gst_omx_audio_dec_open (GstAudioDecoder * decoder)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (decoder);
  GstOMXAudioDecClass *klass = GST_OMX_AUDIO_DEC_GET_CLASS (self);
  gint in_port_index, out_port_index;
  guint32 param;

  GST_DEBUG_OBJECT (self, "Opening decoder");

  self->dec =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->cdata.core_name,
      klass->cdata.component_name, klass->cdata.component_role,
      klass->cdata.hacks);
  self->started = FALSE;
  self->seeking = FALSE;

  if (!self->dec)
    return FALSE;

  if (gst_omx_component_get_state (self->dec,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  param = self->index;
  gst_omx_component_set_parameter (self->dec, OMX_IndexConfigAudioPortIndex,
      &param);

  in_port_index = klass->cdata.in_port_index;
  out_port_index = klass->cdata.out_port_index;

  if (in_port_index == -1 || out_port_index == -1) {
    OMX_PORT_PARAM_TYPE param;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->dec, OMX_IndexParamAudioInit,
        &param);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Couldn't get port information: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* Fallback */
      in_port_index = 0;
      out_port_index = 1;
    } else {
      GST_DEBUG_OBJECT (self, "Detected %u ports, starting at %u",
          (guint) param.nPorts, (guint) param.nStartPortNumber);
      in_port_index = param.nStartPortNumber + 0;
      out_port_index = param.nStartPortNumber + 1;
    }
  }
  self->dec_in_port = gst_omx_component_add_port (self->dec, in_port_index);
  self->dec_out_port = gst_omx_component_add_port (self->dec, out_port_index);

  if (!self->dec_in_port || !self->dec_out_port)
    return FALSE;

  gst_omx_component_set_parameter (self->dec, OMX_IndexConfigAudioPortIndex,
      &self->index);
  gst_omx_component_set_parameter (self->dec, OMX_IndexMstarAppType,
      self->app_type);
  gst_omx_component_set_parameter (self->dec, OMX_IndexMstarDtsSeamless,
      &self->dts_seamless);

  GST_DEBUG_OBJECT (self, "Opened decoder");

  if (self->parent_check == FALSE) {
    parent_check (self);
  }

  /* patch for web youtube no sound issue, */
  /* if parent is custom-player, set audio clock to master */
  if (self->is_custom_player == TRUE) {
    self->start_decode = FALSE;
    gst_omx_audio_dec_open_rtc_thread (self);
  }

  return TRUE;
}

static gboolean
gst_omx_audio_dec_shutdown (GstOMXAudioDec * self)
{
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Shutting down decoder");

  state = gst_omx_component_get_state (self->dec, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->dec, OMX_StateIdle);
      gst_omx_component_get_state (self->dec, 5 * GST_SECOND);
    }
    gst_omx_component_set_state (self->dec, OMX_StateLoaded);
    gst_omx_port_deallocate_buffers (self->dec_in_port);
    gst_omx_port_deallocate_buffers (self->dec_out_port);
    if (state > OMX_StateLoaded)
      gst_omx_component_get_state (self->dec, 5 * GST_SECOND);
  }

  return TRUE;
}

static gboolean
gst_omx_audio_dec_close (GstAudioDecoder * decoder)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Closing decoder");

  if (!gst_omx_audio_dec_shutdown (self))
    return FALSE;

  self->dec_in_port = NULL;
  self->dec_out_port = NULL;
  if (self->dec)
    gst_omx_component_free (self->dec);
  self->dec = NULL;

  self->started = FALSE;

  GST_DEBUG_OBJECT (self, "Closed decoder");

  return TRUE;
}

static void
gst_omx_audio_dec_finalize (GObject * object)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (object);

#if (MIRACAST_ADD_BUFFER_LIST==1)
  if ((g_str_equal (self->app_type, "RTC")) || (self->is_custom_player == TRUE)) {
    gst_omx_audio_dec_close_rtc_thread (self);
  }
#endif

  g_free (self->app_type);

  g_mutex_clear (&self->drain_lock);
  g_cond_clear (&self->drain_cond);

  G_OBJECT_CLASS (gst_omx_audio_dec_parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_omx_audio_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstOMXAudioDec *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstState srcState = (transition >> 3) & 0x7;
  GstState dstState = transition & 0x7;

  g_return_val_if_fail (GST_IS_OMX_AUDIO_DEC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_OMX_AUDIO_DEC (element);
  GST_DEBUG_OBJECT (self, "%s ==> %s", gst_element_state_get_name (srcState),
      gst_element_state_get_name (dstState));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->downstream_flow_ret = GST_FLOW_OK;
      self->draining = FALSE;
      self->started = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->dec_in_port)
        gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
      if (self->dec_out_port)
        gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

      g_mutex_lock (&self->drain_lock);
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      g_mutex_unlock (&self->drain_lock);
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret =
      GST_ELEMENT_CLASS (gst_omx_audio_dec_parent_class)->change_state
      (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:

      /* send GAP again, because playing to paused still need check preroll done at sink */
      if (self->rate != 1.0) {
        //GST_OBJECT_LOCK (self);
        gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self),
            gst_event_new_gap (0, GST_CLOCK_TIME_NONE));
        //GST_OBJECT_UNLOCK (self);
      }

      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->downstream_flow_ret = GST_FLOW_FLUSHING;
      self->started = FALSE;

      if (!gst_omx_audio_dec_shutdown (self))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_omx_audio_dec_loop (GstOMXAudioDec * self)
{
  GstOMXPort *port = self->dec_out_port;
  GstOMXBuffer *buf = NULL;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;
  OMX_ERRORTYPE err;

  acq_return = gst_omx_port_acquire_buffer (port, &buf);
  if (acq_return == GST_OMX_ACQUIRE_BUFFER_ERROR) {
    goto component_error;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
    goto flushing;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_EOS) {
    goto eos;
  }

  if (!gst_pad_has_current_caps (GST_AUDIO_DECODER_SRC_PAD (self)) ||
      acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    OMX_AUDIO_PARAM_PCMMODETYPE pcm_param;
    GstAudioChannelPosition omx_position[OMX_AUDIO_MAXCHANNELS];
    gint i;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    /* Reallocate all buffers */
    if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE
        && gst_omx_port_is_enabled (port)) {
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;
    }

    /* Just update caps */
    GST_AUDIO_DECODER_STREAM_LOCK (self);

    gst_omx_port_get_port_definition (port, &port_def);
    g_assert (port_def.format.audio.eEncoding == OMX_AUDIO_CodingPCM);

    GST_OMX_INIT_STRUCT (&pcm_param);
    //pcm_param.nPortIndex = self->dec_in_port->index; //mark, Frank.Lu
    pcm_param.nPortIndex = self->dec_out_port->index;   //Frank.Lu
    err =
        gst_omx_component_get_parameter (self->dec, OMX_IndexParamAudioPcm,
        &pcm_param);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to get PCM parameters: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      goto caps_failed;
    }

    g_assert (pcm_param.ePCMMode == OMX_AUDIO_PCMModeLinear);
    g_assert (pcm_param.bInterleaved == OMX_TRUE);

    gst_audio_info_init (&self->info);

    for (i = 0; i < pcm_param.nChannels; i++) {
      switch (pcm_param.eChannelMapping[i]) {
        case OMX_AUDIO_ChannelLF:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
          break;
        case OMX_AUDIO_ChannelRF:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
          break;
        case OMX_AUDIO_ChannelCF:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
          break;
        case OMX_AUDIO_ChannelLS:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
          break;
        case OMX_AUDIO_ChannelRS:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
          break;
        case OMX_AUDIO_ChannelLFE:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_LFE1;
          break;
        case OMX_AUDIO_ChannelCS:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
          break;
        case OMX_AUDIO_ChannelLR:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
          break;
        case OMX_AUDIO_ChannelRR:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
          break;
        case OMX_AUDIO_ChannelNone:
        default:
          /* This will break the outer loop too as the
           * i == pcm_param.nChannels afterwards */
          for (i = 0; i < pcm_param.nChannels; i++)
            omx_position[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
          break;
      }
    }
    if (pcm_param.nChannels == 1
        && omx_position[0] == GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER)
      omx_position[0] = GST_AUDIO_CHANNEL_POSITION_MONO;

    memcpy (self->position, omx_position, sizeof (omx_position));
    gst_audio_channel_positions_to_valid_order (self->position,
        pcm_param.nChannels);
    self->needs_reorder =
        (memcmp (self->position, omx_position,
            sizeof (GstAudioChannelPosition) * pcm_param.nChannels) != 0);
    if (self->needs_reorder)
      gst_audio_get_channel_reorder_map (pcm_param.nChannels, self->position,
          omx_position, self->reorder_map);

    gst_audio_info_set_format (&self->info,
        gst_audio_format_build_integer (pcm_param.eNumData ==
            OMX_NumericalDataSigned,
            pcm_param.eEndian ==
            OMX_EndianLittle ? G_LITTLE_ENDIAN : G_BIG_ENDIAN,
            pcm_param.nBitPerSample, pcm_param.nBitPerSample),
        pcm_param.nSamplingRate, pcm_param.nChannels, self->position);

    GST_DEBUG_OBJECT (self,
        "Setting output state: format %s, rate %u, channels %u",
        gst_audio_format_to_string (self->info.finfo->format),
        (guint) pcm_param.nSamplingRate, (guint) pcm_param.nChannels);

    if (!gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (self),
            &self->info)
        || !gst_audio_decoder_negotiate (GST_AUDIO_DECODER (self))) {
      if (buf)
        gst_omx_port_release_buffer (port, buf);
      goto caps_failed;
    }


    /* Add vendor field */
    {
      GstCaps *prevcaps;
      prevcaps = gst_pad_get_current_caps (GST_AUDIO_DECODER_SRC_PAD (self));
      prevcaps = gst_caps_make_writable (prevcaps);
      gst_caps_set_simple (prevcaps, "vendor", G_TYPE_STRING, "mstar", NULL);
      gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (self), prevcaps);
      gst_caps_unref (prevcaps);
    }


    self->sample_rate = (guint) pcm_param.nSamplingRate;
    self->bits_per_sample = (guint) pcm_param.nBitPerSample;

    GST_AUDIO_DECODER_STREAM_UNLOCK (self);

    if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_allocate_buffers (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_populate (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;
    }

    /* Now get a buffer */
    if (acq_return != GST_OMX_ACQUIRE_BUFFER_OK) {
      return;
    }
  }

  g_assert (acq_return == GST_OMX_ACQUIRE_BUFFER_OK);
  /*
     if (!buf) {
     g_assert ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER));
     GST_AUDIO_DECODER_STREAM_LOCK (self);
     goto eos;
     }
   */

  /* This prevents a deadlock between the srcpad stream
   * lock and the audiocodec stream lock, if ::reset()
   * is called at the wrong time
   */
  if (gst_omx_port_is_flushing (port)) {
    GST_DEBUG_OBJECT (self, "Flushing");
    gst_omx_port_release_buffer (port, buf);
    goto flushing;
  }

  GST_DEBUG_OBJECT (self, "Handling buffer: 0x%08x %" G_GUINT64_FORMAT,
      (guint) buf->omx_buf->nFlags, (guint64) buf->omx_buf->nTimeStamp);

  GST_AUDIO_DECODER_STREAM_LOCK (self);

  if (buf->omx_buf->nFilledLen > 0) {
    GstBuffer *outbuf;
    gint nframes, spf;
    GstMapInfo minfo;
    GstOMXAudioDecClass *klass = GST_OMX_AUDIO_DEC_GET_CLASS (self);

    GST_DEBUG_OBJECT (self, "Handling output data");

    if (buf->omx_buf->nFilledLen % self->info.bpf != 0) {
      gst_omx_port_release_buffer (port, buf);
      goto invalid_buffer;
    }

    outbuf =
        gst_audio_decoder_allocate_output_buffer (GST_AUDIO_DECODER (self),
        buf->omx_buf->nFilledLen);

    gst_buffer_map (outbuf, &minfo, GST_MAP_WRITE);
    if (self->needs_reorder) {
      gint i, n_samples, c, n_channels;
      gint *reorder_map = self->reorder_map;
      gint16 *dest, *source;

      dest = (gint16 *) minfo.data;
      source = (gint16 *) (buf->omx_buf->pBuffer + buf->omx_buf->nOffset);
      n_samples = buf->omx_buf->nFilledLen / self->info.bpf;
      n_channels = self->info.channels;

      for (i = 0; i < n_samples; i++) {
        for (c = 0; c < n_channels; c++) {
          dest[i * n_channels + reorder_map[c]] = source[i * n_channels + c];
        }
      }
    } else {
      memcpy (minfo.data, buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);
    }
    gst_buffer_unmap (outbuf, &minfo);

    nframes = 1;
    spf = klass->get_samples_per_frame (self, self->dec_out_port);
    if (spf != -1) {
      nframes = buf->omx_buf->nFilledLen / self->info.bpf;
      if (nframes % spf != 0)
        GST_WARNING_OBJECT (self, "Output buffer does not contain an integer "
            "number of input frames (frames: %d, spf: %d)", nframes, spf);
      nframes = (nframes + spf - 1) / spf;
    }

    GST_BUFFER_TIMESTAMP (outbuf) =
        gst_util_uint64_scale (buf->omx_buf->nTimeStamp, GST_SECOND,
        OMX_TICKS_PER_SECOND);
    if (buf->omx_buf->nTickCount != 0)
      GST_BUFFER_DURATION (outbuf) =
          gst_util_uint64_scale (buf->omx_buf->nTickCount, GST_SECOND,
          OMX_TICKS_PER_SECOND);

    nframes = (self->output_frame == -1) ? nframes : self->output_frame;

    flow_ret =
        gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (self), outbuf,
        nframes);
  }

  GST_DEBUG_OBJECT (self, "Read frame from component");

  GST_DEBUG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));

  if (buf) {
    err = gst_omx_port_release_buffer (port, buf);
    if (err != OMX_ErrorNone)
      goto release_error;
  }

  self->downstream_flow_ret = flow_ret;

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  GST_AUDIO_DECODER_STREAM_UNLOCK (self);

  return;

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->dec),
            gst_omx_component_get_last_error (self->dec)));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }

flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_FLUSHING;
    self->started = FALSE;
    return;
  }

eos:
  {
    g_mutex_lock (&self->drain_lock);
    if (self->draining) {
      GST_DEBUG_OBJECT (self, "Drained");
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      flow_ret = GST_FLOW_OK;
      gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    } else {
      GST_DEBUG_OBJECT (self, "Component signalled EOS");
      flow_ret = GST_FLOW_EOS;
    }
    g_mutex_unlock (&self->drain_lock);

    GST_AUDIO_DECODER_STREAM_LOCK (self);
    self->downstream_flow_ret = flow_ret;

    /* Here we fallback and pause the task for the EOS case */
    if (flow_ret != GST_FLOW_OK)
      goto flow_error;

    GST_AUDIO_DECODER_STREAM_UNLOCK (self);

    return;
  }

flow_error:
  {
    if (flow_ret == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
      self->started = FALSE;
    } else if (flow_ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED,
          ("Internal data stream error."), ("stream stopped, reason %s",
              gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
      self->started = FALSE;
    } else if (flow_ret == GST_FLOW_FLUSHING) {
      GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
      gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
      self->started = FALSE;
    }
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }

reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }

invalid_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Invalid sized input buffer"));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }

caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    return;
  }
release_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase output buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }
}

static gboolean
gst_omx_audio_dec_start (GstAudioDecoder * decoder)
{
  GstOMXAudioDec *self;

  self = GST_OMX_AUDIO_DEC (decoder);

  self->last_upstream_ts = GST_CLOCK_TIME_NONE;
  self->downstream_eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;

  self->rtc_drop_at_start = TRUE;
  self->rtc_drop_count = 0;

  self->current_pts = 0;

  return TRUE;
}

static gboolean
gst_omx_audio_dec_stop (GstAudioDecoder * decoder)
{
  GstOMXAudioDec *self;

  self = GST_OMX_AUDIO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Stopping decoder");

  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

  gst_pad_stop_task (GST_AUDIO_DECODER_SRC_PAD (decoder));

  if (gst_omx_component_get_state (self->dec, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->dec, OMX_StateIdle);

  self->downstream_flow_ret = GST_FLOW_FLUSHING;
  self->started = FALSE;
  self->downstream_eos = FALSE;

  g_mutex_lock (&self->drain_lock);
  self->draining = FALSE;
  g_cond_broadcast (&self->drain_cond);
  g_mutex_unlock (&self->drain_lock);

  gst_omx_component_get_state (self->dec, 5 * GST_SECOND);

  gst_buffer_replace (&self->codec_data, NULL);

  GST_DEBUG_OBJECT (self, "Stopped decoder");

  return TRUE;
}

static gboolean
gst_omx_audio_dec_set_format (GstAudioDecoder * decoder, GstCaps * caps)
{
  GstOMXAudioDec *self;
  GstOMXAudioDecClass *klass;
  GstStructure *s;
  const GValue *codec_data;
  gboolean is_format_change = FALSE;
  gboolean needs_disable = FALSE;
  gint rate;

  self = GST_OMX_AUDIO_DEC (decoder);
  klass = GST_OMX_AUDIO_DEC_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, caps);

  self->pts = GST_CLOCK_TIME_NONE;
  /* Check if the caps change is a real format change or if only irrelevant
   * parts of the caps have changed or nothing at all.
   */
  if (klass->is_format_change) {
    is_format_change = klass->is_format_change (self, self->dec_in_port, caps);
  }

  needs_disable =
      gst_omx_component_get_state (self->dec,
      GST_CLOCK_TIME_NONE) != OMX_StateLoaded;
  /* If the component is not in Loaded state and a real format change happens
   * we have to disable the port and re-allocate all buffers. If no real
   * format change happened we can just exit here.
   */

  if (needs_disable && !is_format_change) {
    GST_DEBUG_OBJECT (self,
        "Already running and caps did not change the format");
    return TRUE;
  }

  if (needs_disable && is_format_change) {
    GstOMXPort *out_port = self->dec_out_port;

    GST_DEBUG_OBJECT (self, "Need to disable and drain decoder");

    gst_omx_audio_dec_drain (self);
    gst_omx_audio_dec_flush (decoder, FALSE);
    gst_omx_port_set_flushing (out_port, 5 * GST_SECOND, TRUE);

    if (klass->cdata.hacks & GST_OMX_HACK_NO_COMPONENT_RECONFIGURE) {
      GST_AUDIO_DECODER_STREAM_UNLOCK (self);
      gst_omx_audio_dec_stop (GST_AUDIO_DECODER (self));
      gst_omx_audio_dec_close (GST_AUDIO_DECODER (self));
      GST_AUDIO_DECODER_STREAM_LOCK (self);

      if (!gst_omx_audio_dec_open (GST_AUDIO_DECODER (self))) {
        return FALSE;
        needs_disable = FALSE;
      }
    } else {
      if (gst_omx_port_set_enabled (self->dec_in_port, FALSE) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_set_enabled (out_port, FALSE) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_buffers_released (self->dec_in_port,
              5 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_buffers_released (out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_deallocate_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_deallocate_buffers (self->dec_out_port) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->dec_in_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (out_port, 1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
    }

    GST_DEBUG_OBJECT (self, "Decoder drained and disabled");
  }

  s = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (s, "rate", &rate);
  self->sample_rate = rate;

  if (klass->set_format) {
    if (!klass->set_format (self, self->dec_in_port, caps)) {
      GST_ERROR_OBJECT (self, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (self, "Updating outport port definition");
  if (gst_omx_port_update_port_definition (self->dec_out_port,
          NULL) != OMX_ErrorNone)
    return FALSE;

  /* Get codec data from caps */
  gst_buffer_replace (&self->codec_data, NULL);
  codec_data = gst_structure_get_value (s, "codec_data");

  if (codec_data) {
    /* Vorbis and some other codecs have multiple buffers in
     * the stream-header field */
    self->codec_data = gst_value_get_buffer (codec_data);

    if (self->codec_data)
      gst_buffer_ref (self->codec_data);
  }

  GST_DEBUG_OBJECT (self, "Enabling component");

  if (needs_disable) {
    if (gst_omx_port_set_enabled (self->dec_in_port, TRUE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_allocate_buffers (self->dec_in_port) != OMX_ErrorNone)
      return FALSE;

    if ((klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      if (gst_omx_port_set_enabled (self->dec_out_port, TRUE) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_allocate_buffers (self->dec_out_port) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->dec_out_port,
              5 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_port_wait_enabled (self->dec_in_port,
            5 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_port_mark_reconfigured (self->dec_in_port) != OMX_ErrorNone)
      return FALSE;

  } else {
    if (!(klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      /* Disable output port */
      if (gst_omx_port_set_enabled (self->dec_out_port, FALSE) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->dec_out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_component_set_state (self->dec,
              OMX_StateIdle) != OMX_ErrorNone)
        return FALSE;

      /* Need to allocate buffers to reach Idle state */
      if (gst_omx_port_allocate_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;
    } else {
      if (gst_omx_component_set_state (self->dec,
              OMX_StateIdle) != OMX_ErrorNone)
        return FALSE;

      /* Need to allocate buffers to reach Idle state */
      if (gst_omx_port_allocate_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_allocate_buffers (self->dec_out_port) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_component_get_state (self->dec,
            GST_CLOCK_TIME_NONE) != OMX_StateIdle)
      return FALSE;

    if (gst_omx_component_set_state (self->dec,
            OMX_StateExecuting) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_component_get_state (self->dec,
            GST_CLOCK_TIME_NONE) != OMX_StateExecuting)
      return FALSE;
  }

  /* Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, FALSE);

  if (gst_omx_component_get_last_error (self->dec) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Component in error state: %s (0x%08x)",
        gst_omx_component_get_last_error_string (self->dec),
        gst_omx_component_get_last_error (self->dec));
    return FALSE;
  }

  /* Start the srcpad loop again */
  GST_DEBUG_OBJECT (self, "Starting task again");
  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_AUDIO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_audio_dec_loop, decoder, NULL);

  return TRUE;
}

static void
gst_omx_audio_dec_flush (GstAudioDecoder * decoder, gboolean hard)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (decoder);
  GstOMXAudioDecClass *klass = GST_OMX_AUDIO_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Resetting decoder");

  if (self->is_custom_player == TRUE) {
    g_mutex_lock (&self->rtc_thread_mutex);
    self->start_decode = FALSE;
    guint len;
    len = g_slist_length (self->rtc_list);
    while (len > 0) {
      GstBuffer *tmp;
      tmp = self->rtc_list->data;
      self->rtc_list = g_slist_remove (self->rtc_list, tmp);
      gst_buffer_unref (tmp);
      len = g_slist_length (self->rtc_list);
    }
    g_mutex_unlock (&self->rtc_thread_mutex);
  }

  if (klass->flush) {
    klass->flush (self, hard);
  }

  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

  /* Wait until the srcpad loop is finished */
  GST_AUDIO_DECODER_STREAM_UNLOCK (self);
  GST_PAD_STREAM_LOCK (GST_AUDIO_DECODER_SRC_PAD (self));
  GST_PAD_STREAM_UNLOCK (GST_AUDIO_DECODER_SRC_PAD (self));
  GST_AUDIO_DECODER_STREAM_LOCK (self);

  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_populate (self->dec_out_port);

  self->pts = GST_CLOCK_TIME_NONE;
  self->is_eos_received = FALSE;

  /* Start the srcpad loop again */
  self->last_upstream_ts = GST_CLOCK_TIME_NONE;
  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_AUDIO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_audio_dec_loop, decoder, NULL);
}

static GstFlowReturn
gst_omx_audio_dec_handle_frame (GstAudioDecoder * decoder, GstBuffer * inbuf)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXAudioDec *self;
  GstOMXPort *port;
  GstOMXBuffer *buf;
  GstBuffer *codec_data = NULL;
  guint offset = 0;
  GstClockTime timestamp, duration, upstream_ts;
  OMX_ERRORTYPE err;
  GstMapInfo minfo;

  self = GST_OMX_AUDIO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Handling frame");

  /* Make sure to keep a reference to the input here,
   * it can be unreffed from the other thread if
   * finish_frame() is called */
  if (inbuf)
    gst_buffer_ref (inbuf);

  if (self->downstream_flow_ret != GST_FLOW_OK) {
    if (inbuf) {
      gst_buffer_unref (inbuf);
    } else {
      // Drain function will let omx-component into EOS state.
      // We do this function even when downstream flow error.
      // Otherwise, the EOS state between gst and omx will dismatch.
      gst_omx_audio_dec_drain (self);
    }
    return self->downstream_flow_ret;
  }

  if (inbuf == NULL)
    return gst_omx_audio_dec_drain (self);

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  duration = GST_BUFFER_DURATION (inbuf);

  port = self->dec_in_port;

  gst_buffer_map (inbuf, &minfo, GST_MAP_READ);

  while (offset < minfo.size) {
    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    acq_ret = gst_omx_port_acquire_buffer (port, &buf);

    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_ERROR) {
      GST_AUDIO_DECODER_STREAM_LOCK (self);
      goto component_error;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      GST_AUDIO_DECODER_STREAM_LOCK (self);
      goto flushing;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      /* Reallocate all buffers */
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_allocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      /* Now get a new buffer and fill it */
      GST_AUDIO_DECODER_STREAM_LOCK (self);
      continue;
    }
    GST_AUDIO_DECODER_STREAM_LOCK (self);

    g_assert (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

    if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <= 0) {
      gst_omx_port_release_buffer (port, buf);
      goto full_buffer;
    }

    if (self->downstream_flow_ret != GST_FLOW_OK) {
      gst_omx_port_release_buffer (port, buf);
      goto flow_error;
    }

    if (self->codec_data) {
      GST_DEBUG_OBJECT (self, "Passing codec data to the component");

      codec_data = self->codec_data;

      if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <
          gst_buffer_get_size (codec_data)) {
        gst_omx_port_release_buffer (port, buf);
        goto too_large_codec_data;
      }

      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
      buf->omx_buf->nFilledLen = gst_buffer_get_size (codec_data);
      gst_buffer_extract (codec_data, 0,
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);

      if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
        buf->omx_buf->nTimeStamp =
            gst_util_uint64_scale (timestamp, OMX_TICKS_PER_SECOND, GST_SECOND);
      } else {
        buf->omx_buf->nTimeStamp = -1;
      }
      buf->omx_buf->nTickCount = 0;

      self->started = TRUE;
      err = gst_omx_port_release_buffer (port, buf);
      gst_buffer_replace (&self->codec_data, NULL);
      if (err != OMX_ErrorNone)
        goto release_error;
      /* Acquire new buffer for the actual frame */
      continue;
    }

    /* Now handle the frame */
    GST_DEBUG_OBJECT (self, "Passing frame offset %d to the component", offset);

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    buf->omx_buf->nFilledLen =
        MIN (minfo.size - offset,
        buf->omx_buf->nAllocLen - buf->omx_buf->nOffset);
    gst_buffer_extract (inbuf, offset,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);

    if (timestamp != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTimeStamp =
          gst_util_uint64_scale (timestamp, OMX_TICKS_PER_SECOND, GST_SECOND);
      upstream_ts = timestamp;
    } else {
      buf->omx_buf->nTimeStamp = -1;
    }

    if (duration != GST_CLOCK_TIME_NONE && offset == 0) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (duration, OMX_TICKS_PER_SECOND, GST_SECOND);
      upstream_ts = upstream_ts + duration;
    } else {
      buf->omx_buf->nTickCount = 0;
    }

    if (offset == 0)
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;

    /* TODO: Set flags
     *   - OMX_BUFFERFLAG_DECODEONLY for buffers that are outside
     *     the segment
     */

    offset += buf->omx_buf->nFilledLen;

    if (offset == minfo.size)
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    self->started = TRUE;
    err = gst_omx_port_release_buffer (port, buf);
    if (err != OMX_ErrorNone)
      goto release_error;

    //LG request: If difference with previous timestamp is larger than 100 seconds, don't trust this timestamp.
    if (GST_CLOCK_TIME_IS_VALID (self->last_upstream_ts)
        && GST_BUFFER_TIMESTAMP_IS_VALID (inbuf)) {
      if (GST_CLOCK_DIFF (self->last_upstream_ts,
              GST_BUFFER_TIMESTAMP (inbuf)) > 100 * GST_SECOND) {
        GST_WARNING_OBJECT (self,
            "Abnormal ts detected: cur timestamp %" GST_TIME_FORMAT
            " last timestamp = %" GST_TIME_FORMAT " time diff = %"
            GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)),
            GST_TIME_ARGS (self->last_upstream_ts),
            GST_TIME_ARGS (GST_CLOCK_DIFF (self->last_upstream_ts,
                    GST_BUFFER_TIMESTAMP (inbuf))));
        GST_BUFFER_TIMESTAMP (inbuf) = GST_CLOCK_TIME_NONE;
      }
    }
    //update last ts after checking abnormal ts and release buffer
    self->last_upstream_ts = upstream_ts;

  }
  gst_buffer_unmap (inbuf, &minfo);

  GST_DEBUG_OBJECT (self, "Passed frame to component");
  if (inbuf)
    gst_buffer_unref (inbuf);

  return self->downstream_flow_ret;

full_buffer:
  {
    gst_buffer_unmap (inbuf, &minfo);
    if (inbuf)
      gst_buffer_unref (inbuf);

    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Got OpenMAX buffer with no free space (%p, %u/%u)", buf,
            (guint) buf->omx_buf->nOffset, (guint) buf->omx_buf->nAllocLen));
    return GST_FLOW_ERROR;
  }

flow_error:
  {
    gst_buffer_unmap (inbuf, &minfo);
    if (inbuf)
      gst_buffer_unref (inbuf);

    return self->downstream_flow_ret;
  }

too_large_codec_data:
  {
    gst_buffer_unmap (inbuf, &minfo);
    if (inbuf)
      gst_buffer_unref (inbuf);

    GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
        ("codec_data larger than supported by OpenMAX port "
            "(%" G_GSIZE_FORMAT " > %u)", gst_buffer_get_size (codec_data),
            (guint) self->dec_in_port->port_def.nBufferSize));
    return GST_FLOW_ERROR;
  }

component_error:
  {
    gst_buffer_unmap (inbuf, &minfo);
    if (inbuf)
      gst_buffer_unref (inbuf);

    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->dec),
            gst_omx_component_get_last_error (self->dec)));
    return GST_FLOW_ERROR;
  }

flushing:
  {
    gst_buffer_unmap (inbuf, &minfo);
    if (inbuf)
      gst_buffer_unref (inbuf);

    GST_DEBUG_OBJECT (self, "Flushing -- returning FLUSHING");
    return GST_FLOW_FLUSHING;
  }
reconfigure_error:
  {
    gst_buffer_unmap (inbuf, &minfo);
    if (inbuf)
      gst_buffer_unref (inbuf);

    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure input port"));
    return GST_FLOW_ERROR;
  }
release_error:
  {
    gst_buffer_unmap (inbuf, &minfo);
    if (inbuf)
      gst_buffer_unref (inbuf);

    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase input buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));

    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_omx_audio_dec_drain (GstOMXAudioDec * self)
{
  GstOMXAudioDecClass *klass;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (self, "Draining component");

  klass = GST_OMX_AUDIO_DEC_GET_CLASS (self);

  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Component not started yet");
    return GST_FLOW_OK;
  }
  self->started = FALSE;

  if ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER)) {
    GST_WARNING_OBJECT (self, "Component does not support empty EOS buffers");
    return GST_FLOW_OK;
  }

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_AUDIO_DECODER_STREAM_UNLOCK (self);

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->dec_in_port, &buf);
  if (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    GST_AUDIO_DECODER_STREAM_LOCK (self);
    GST_ERROR_OBJECT (self, "Failed to acquire buffer for draining: %d",
        acq_ret);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&self->drain_lock);
  self->draining = TRUE;
  buf->omx_buf->nFilledLen = 0;

  if (GST_CLOCK_TIME_IS_VALID (self->last_upstream_ts)) {
    buf->omx_buf->nTimeStamp =
        gst_util_uint64_scale (self->last_upstream_ts, OMX_TICKS_PER_SECOND,
        GST_SECOND);
  } else {
    buf->omx_buf->nTimeStamp = -1;
  }

  buf->omx_buf->nTickCount = 0;
  buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
  err = gst_omx_port_release_buffer (self->dec_in_port, buf);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to drain component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    g_mutex_unlock (&self->drain_lock);
    GST_AUDIO_DECODER_STREAM_LOCK (self);
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (self, "Waiting until component is drained");

  if (self->seeking) {
    GST_DEBUG_OBJECT (self, "Draining aborted by seeking");
    self->draining = FALSE;
  } else if (G_UNLIKELY (self->dec->hacks & GST_OMX_HACK_DRAIN_MAY_NOT_RETURN)) {
    gint64 wait_until = g_get_monotonic_time () + G_TIME_SPAN_SECOND / 2;

    if (!g_cond_wait_until (&self->drain_cond, &self->drain_lock, wait_until))
      GST_WARNING_OBJECT (self, "Drain timed out");
    else
      GST_DEBUG_OBJECT (self, "Drained component");

  } else {
    g_cond_wait (&self->drain_cond, &self->drain_lock);
    GST_DEBUG_OBJECT (self, "Drained component");
  }

  g_mutex_unlock (&self->drain_lock);
  GST_AUDIO_DECODER_STREAM_LOCK (self);

  self->started = FALSE;

  return GST_FLOW_OK;
}

static gboolean
gst_omx_audio_dec_sink_event (GstAudioDecoder * dec, GstEvent * event)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (dec);
  //  GstOMXAudioDecClass *klass = GST_OMX_AUDIO_DEC_GET_CLASS (self);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      float param;
      GstSegment seg;
      GST_OBJECT_LOCK (dec);
      gst_event_copy_segment (event, &seg);
      self->rate = seg.rate;
      param = seg.rate;
      gst_omx_component_set_parameter (self->dec, OMX_IndexParamRate, &param);
      GST_OBJECT_UNLOCK (dec);
    }
      break;
    case GST_EVENT_EOS:
      self->is_eos_received = TRUE;
      break;
    default:
      break;
  }

  return GST_AUDIO_DECODER_CLASS (gst_omx_audio_dec_parent_class)->
      sink_event (dec, event);
}

static void
gst_omx_audio_dec_class_init (GstOMXAudioDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *audio_decoder_class = GST_AUDIO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_omx_audio_dec_finalize;
  gobject_class->set_property = gst_omx_audio_dec_set_property;
  gobject_class->get_property = gst_omx_audio_dec_get_property;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_change_state);

  audio_decoder_class->open = GST_DEBUG_FUNCPTR (gst_omx_audio_dec_open);
  audio_decoder_class->close = GST_DEBUG_FUNCPTR (gst_omx_audio_dec_close);
  audio_decoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_audio_dec_start);
  audio_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_audio_dec_stop);
  audio_decoder_class->flush = GST_DEBUG_FUNCPTR (gst_omx_audio_dec_flush);
  audio_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_set_format);
  audio_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_handle_frame);
  audio_decoder_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_sink_event);

  audio_decoder_class->pre_push =
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_pre_push);

  g_object_class_install_property (gobject_class,
      PROP_RESOURCE_INFO,
      g_param_spec_boxed ("resource-info",
          "Resource information",
          "Hold various information for managing resource",
          GST_TYPE_STRUCTURE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject_class,
      PROP_SILENT,
      g_param_spec_boolean ("silent",
          "Silent", "Produce verbose output ?", FALSE, G_PARAM_READWRITE));

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

/*
    g_object_class_install_property (gobject_class,
                                     PROP_URI,
                                     g_param_spec_string ("uri",
                                                          "URI",
                                                          "URI of the media to play",
                                                          DEFAULT_URI_PATH,
                                                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
*/
  g_object_class_install_property (gobject_class,
      PROP_SERVERSIDE_TRICKPLAY,
      g_param_spec_boolean ("serverside-trickplay",
          "server side trick play",
          "server side trick play", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_DTS_SEAMLESS,
      g_param_spec_boolean ("dts-seamless",
          "dts seamless", "dts seamless", FALSE, G_PARAM_READWRITE));


  klass->cdata.type = GST_OMX_COMPONENT_TYPE_FILTER;
  klass->cdata.default_src_template_caps =
      "audio/x-raw, "
      "rate = (int) [ 1, MAX ], "
      "channels = (int) [ 1, " G_STRINGIFY (OMX_AUDIO_MAXCHANNELS) " ], "
      "format = (string) " GST_AUDIO_FORMATS_ALL;
}
