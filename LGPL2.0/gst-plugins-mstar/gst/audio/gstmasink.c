/***
 * MStreamer
 *
 * $Id: //MPL/MStreamer/element/masink/source/gstmsink.c#9 $
 * $Header: //MPL/MStreamer/element/masink/source/gstmasink.c#9 $
 * $Date: 2011/05/03 $
 * $DateTime: 2011/05/03 16:39:17 $
 * $Change: 413858 $
 * $File: //MPL/MStreamer/element/masink/source/gstmasink.c $
 * $Revision: #9 $
 * $Author: THEALE $
 * 
 */

/*
 * this is add for test git tag
 * */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "gstmasink.h"


#define DUMP_FILE        0



// caps
static GstStaticPadTemplate masink_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
#if 0
    GST_STATIC_CAPS ("audio/x-raw"));
#else
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = S16LE , "
        "channels = (int) [1, 2], " "rate = (int) [8000,48000] "));
//                                                                                            "id = (int) [0,3], "
//                                                                                            "atype = (int) [0,3]"));
#endif



enum
{
  PROP_0,
  PROP_VOLUME,
  PROP_SPDIF_NON_PCM,
  PROP_HDMI_NON_PCM,
  PROP_MUTE,
  PROP_APP_TYPE,
  PROP_DISABLE_LOST_STATE
};


#define DEFAULT_APP_TYPE        "default_app_type"

GST_DEBUG_CATEGORY_STATIC (gst_masink_debug);
#define GST_CAT_DEFAULT gst_masink_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_masink_debug, "masink", 0, \
      "debug category for masink");

//GST_BOILERPLATE(GstMasink, gst_masink, GstAudioSink, GST_TYPE_AUDIO_SINK);
//G_DEFINE_TYPE(GstMasink, gst_masink, GST_TYPE_AUDIO_SINK);
G_DEFINE_TYPE_WITH_CODE (GstMasink, gst_masink, GST_TYPE_AUDIO_SINK,
    DEBUG_INIT);


/*
 * This function returns the list of support tracks (inputs, outputs)
 * on this element instance. Elements usually build this list during
 * _init () or when going from NULL to READY.
 */

static void
gst_masink_set_volume (GstMasink * masink)
{
  MZ_ERROR_INFO Ret;

  Ret =
      msil_audio_SetVolume ((MZ_PTR) masink->SinkHandle,
      (MZ_U32) masink->volume);
  if (Ret != MZ_ERROR_NONE) {
    GST_ERROR ("Set volume to %u Fail (%s)", masink->volume,
        msil_audio_ErrorString (Ret));
  } else {
    GST_DEBUG ("Set volume to %u", masink->volume);
  }
}

static void
masink_set_SPDIF_OutputMode (GstMasink * masink)
{
#if 0
  MZ_ERROR_INFO Ret;

  if (masink->SPDIF_non_pcm) {
    if (masink->aType == 3) {
      Ret =
          msil_audio_SetSpdifMode ((MZ_PTR) masink->SinkHandle, (MZ_BOOL) TRUE);
      if (Ret != MZ_ERROR_NONE) {
        GST_ERROR ("Set SPDIF Mode to NON-PCM Fail (%s)",
            msil_audio_ErrorString (Ret));
      } else {
        GST_DEBUG ("Set SPDIF Mode to NON-PCM");
      }
    } else {
      Ret =
          msil_audio_SetSpdifMode ((MZ_PTR) masink->SinkHandle,
          (MZ_BOOL) FALSE);
      if (Ret != MZ_ERROR_NONE) {
        GST_ERROR ("Set SPDIF Mode to PCM Fail (%s)",
            msil_audio_ErrorString (Ret));
      } else {
        GST_DEBUG ("Set SPDIF Mode to PCM");
      }
    }
  } else {
    Ret =
        msil_audio_SetSpdifMode ((MZ_PTR) masink->SinkHandle, (MZ_BOOL) FALSE);
    if (Ret != MZ_ERROR_NONE) {
      GST_ERROR ("Set SPDIF Mode to PCM Fail (%s)",
          msil_audio_ErrorString (Ret));
    } else {
      GST_DEBUG ("Set SPDIF mode to PCM");
    }
  }
#endif
}

static void
masink_set_HDMI_OutputMode (GstMasink * masink)
{
#if 0
  MZ_ERROR_INFO Ret;

  if (masink->HDMI_non_pcm) {
    if (masink->aType == 3) {
      Ret =
          msil_audio_SetHdmiMode ((MZ_PTR) masink->SinkHandle, (MZ_BOOL) TRUE);
      if (Ret != MZ_ERROR_NONE) {
        GST_ERROR ("Set HDMI Mode to NON-PCM Fail (%s)",
            msil_audio_ErrorString (Ret));
      } else {
        GST_DEBUG ("Set HDMI Mode to NON-PCM");
      }
    } else {
      Ret =
          msil_audio_SetHdmiMode ((MZ_PTR) masink->SinkHandle, (MZ_BOOL) FALSE);
      if (Ret != MZ_ERROR_NONE) {
        GST_ERROR ("Set HDMI Mode to PCM Fail (%s)",
            msil_audio_ErrorString (Ret));
      } else {
        GST_DEBUG ("Set HDMI Mode to PCM");
      }
    }
  } else {
    Ret = msil_audio_SetHdmiMode ((MZ_PTR) masink->SinkHandle, (MZ_BOOL) FALSE);
    if (Ret != MZ_ERROR_NONE) {
      GST_ERROR ("Set HDMI Mode to PCM Fail (%s)",
          msil_audio_ErrorString (Ret));
    } else {
      GST_DEBUG ("Set HDMI Mode to PCM");
    }
  }
#endif
}

static void
masink_set_Mute (GstMasink * masink)
{
  MZ_ERROR_INFO Ret;

  Ret =
      msil_audio_SetMute ((MZ_PTR) masink->SinkHandle, (MZ_BOOL) masink->mute);
  if (Ret != MZ_ERROR_NONE) {
    GST_ERROR ("Set mute %s Fail (%s)", (masink->mute == TRUE ? "ON" : "OFF"),
        msil_audio_ErrorString (Ret));
  } else {
    GST_DEBUG ("Set mute %s", (masink->mute == TRUE ? "ON" : "OFF"));
  }
}

static void
gst_masink_finalise (GObject * object)
{
  GstMasink *sink = GST_MASINK (object);
  GstAudioSink parent_class;
  g_mutex_free (sink->masink_lock);

  G_OBJECT_CLASS (gst_masink_parent_class)->finalize (object);
}

static void
gst_masink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstMasink *sink = GST_MASINK (object);

  switch (prop_id) {
    case PROP_VOLUME:
      sink->volume = g_value_get_uint (value);
      gst_masink_set_volume (sink);
      break;

    case PROP_SPDIF_NON_PCM:
      sink->SPDIF_non_pcm = g_value_get_boolean (value);
      masink_set_SPDIF_OutputMode (sink);
      break;

    case PROP_HDMI_NON_PCM:
      sink->HDMI_non_pcm = g_value_get_boolean (value);
      masink_set_HDMI_OutputMode (sink);
      break;

    case PROP_MUTE:
      sink->mute = g_value_get_boolean (value);
      masink_set_Mute (sink);
      break;

    case PROP_APP_TYPE:
      g_free (sink->app_type);
      sink->app_type = g_value_dup_string (value);
      /* setting NULL restores the default device */
      if (sink->app_type == NULL) {
        sink->app_type = g_strdup (DEFAULT_APP_TYPE);
      }
      g_print ("%s: app_type=%s\r\n", __FUNCTION__, sink->app_type);

      if (g_str_equal (sink->app_type, "staticES")) {
        gst_base_sink_set_async_enabled (GST_BASE_SINK (sink), TRUE);
      } else if (g_str_equal (sink->app_type, "SKYPE")) {
        gst_base_sink_set_sync (GST_BASE_SINK (sink), FALSE);
      } else if (g_str_equal (sink->app_type, "RTC")) {
        gst_base_sink_set_max_lateness (GST_BASE_SINK (sink),
            -1 /*360000000000 */ );
        gst_base_sink_set_sync (GST_BASE_SINK (sink), FALSE);   // set freerun // noted by kochien.kuo
      }

      break;

    case PROP_DISABLE_LOST_STATE:{
      sink->disable_lost_state = g_value_get_boolean (value);
      g_print ("%s: disable_lost_state=%d\r\n", __FUNCTION__,
          sink->disable_lost_state);
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_masink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMasink *sink = GST_MASINK (object);

  switch (prop_id) {
    case PROP_VOLUME:
      g_value_set_uint (value, sink->volume);
      break;

    case PROP_SPDIF_NON_PCM:
      g_value_set_boolean (value, sink->SPDIF_non_pcm);
      break;

    case PROP_HDMI_NON_PCM:
      g_value_set_boolean (value, sink->HDMI_non_pcm);
      break;

    case PROP_MUTE:
      g_value_set_boolean (value, sink->mute);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_masink_open (GstAudioSink * asink)
{
  /* create and initialize a Masink object */
  GstMasink *masink = GST_MASINK (asink);

  masink->WriteFrame = 0;
  masink->DecId = 0;
  masink->aType = 0;

  masink->SinkHandle = (gpointer) NULL;

  gst_base_sink_set_max_lateness (GST_BASE_SINK (masink), -1 /*360000000000 */ );       //force to free-run, Frank.Lu
  gst_base_sink_set_sync (GST_BASE_SINK (masink), FALSE);       //force to free-run, Frank.Lu

  return TRUE;
}

static gboolean
gst_masink_prepare (GstAudioSink * asink, GstAudioRingBufferSpec * spec)
{
  GstMasink *masink = GST_MASINK (asink);

  masink->spec = spec;

  if (masink->SinkHandle == NULL) {
    GstStructure *structure = NULL;
    structure = gst_caps_get_structure (masink->spec->caps, 0);
    if (structure) {
      const gchar *mimetype = NULL;
      mimetype = gst_structure_get_name (structure);
      if (mimetype) {
        if (g_str_equal (mimetype, "audio/x-raw")) {
          gint DecId = -1;
          gint aType = -1;
          gst_structure_get_int (structure, "id", &DecId);
          gst_structure_get_int (structure, "atype", &aType);

          GST_INFO ("DecId:[%d]", DecId);
          GST_INFO ("aType:[%d]", aType);
          GST_INFO ("rate:[%d]", masink->spec->info.rate);
          GST_INFO ("channels:[%d]", masink->spec->info.channels);

          if (DecId == -1) {
            masink->DecId = 0;
          } else {
            masink->DecId = DecId;
          }

          if (aType == -1) {
            masink->aType = 0;
          } else {
            masink->aType = aType;
          }

          masink->SinkHandle = (gpointer) msil_audio_AllocAudioSink (DecId);
          if (masink->SinkHandle) {
            //if (!g_str_equal(masink->app_type, "SKYPE"))
            {
              masink->resetflag = TRUE;
              masink->dropCnt = 0;

              msil_audio_dma_reader_open (masink->SinkHandle);
              msil_audio_dma_reader_init (masink->SinkHandle,
                  masink->spec->info.rate);
            }
            /*
               else
               {
               MZ_ERROR_INFO Ret = MZ_ERROR_NONE;
               Ret = msil_audio_SetupExtSynth(masink->SinkHandle, masink->spec->rate);
               if ( Ret != MZ_ERROR_NONE ) {
               GST_ERROR ("Setup Ext Synth to %d Fail (%s)", masink->spec->rate, msil_audio_ErrorString(Ret));
               }
               else {
               GST_DEBUG ("Setup Ext Synth to %d", masink->spec->rate);
               }
               }
             */
            masink->WriteFrame = 0;
            masink->pcm3Level = 0;

            masink_set_SPDIF_OutputMode (masink);
            masink_set_HDMI_OutputMode (masink);
          } else {
            GST_ERROR ("Can not get audio sink handle !!");
            return FALSE;
          }
        } else {
          GST_ERROR ("Mimetype is not audio/x-raw-int !!");
          return FALSE;
        }
      } else {
        GST_ERROR ("Structure have no mimetype ?!");
        return FALSE;
      }
    } else {
      GST_ERROR ("Capability have no structure ?!");
      return FALSE;
    }
  } else {
    GST_WARNING ("Prepare twice ?!");
  }

  return TRUE;
}

static gboolean
gst_masink_unprepare (GstAudioSink * asink)
{
  GstMasink *masink = GST_MASINK (asink);

  if (masink->SinkHandle) {
    MZ_ERROR_INFO Ret = MZ_ERROR_NONE;

    //if (!g_str_equal(masink->app_type, "SKYPE"))
    {
      msil_audio_dma_reader_close (masink->SinkHandle);
    }
    /*
       else
       {
       // sample rate 0 is mean turn off DSP ext synth
       Ret = msil_audio_SetupExtSynth(masink->SinkHandle, 0);
       if ( Ret != MZ_ERROR_NONE ) {
       GST_ERROR ("Setup Ext Synth to %d Fail (%s)", masink->spec->rate, msil_audio_ErrorString(Ret));
       }
       else {
       GST_DEBUG ("Setup Ext Synth to %d", masink->spec->rate);
       }
       }
     */
    Ret =
        msil_audio_SetSpdifMode ((MZ_PTR) masink->SinkHandle, (MZ_BOOL) FALSE);
    if (Ret != MZ_ERROR_NONE) {
      GST_ERROR ("Set SPDIF Mode to PCM Fail (%s)",
          msil_audio_ErrorString (Ret));
    } else {
      GST_DEBUG ("Set SPDIF Mode to PCM");
    }

    Ret = msil_audio_SetHdmiMode ((MZ_PTR) masink->SinkHandle, (MZ_BOOL) FALSE);
    if (Ret != MZ_ERROR_NONE) {
      GST_ERROR ("Set HDMI Mode to PCM Fail (%s)",
          msil_audio_ErrorString (Ret));
    } else {
      GST_DEBUG ("Set HDMI Mode to PCM");
    }

    msil_audio_SetMute ((MZ_PTR) masink->SinkHandle, FALSE);

    if (msil_audio_FreeAudioSink ((MZ_PTR) masink->SinkHandle) == FALSE) {
      GST_ERROR ("Can not free audio sink handle !!");
      return FALSE;
    }

    masink->SinkHandle = NULL;
  } else {
    GST_ERROR ("handle should not be NULL !!");
  }

  return TRUE;
}

static gboolean
gst_masink_close (GstAudioSink * asink)
{
  GstMasink *masink = GST_MASINK (asink);

  /* release Masink object */
  gst_caps_replace (&masink->cached_caps, NULL);

  masink->SinkHandle = (gpointer) NULL;

  return TRUE;
}

static gint
gst_masink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstMasink *masink;
  guint32 u32CopySize = 0;
  MZ_BOOL bRet = FALSE;
  masink = GST_MASINK (asink);

  //if (!g_str_equal(masink->app_type, "SKYPE"))
  {
    GST_MASINK_LOCK (asink);
#if 0
    bRet =
        msil_audio_dma_reader_push ((MZ_PTR) masink->SinkHandle, (MZ_PTR) data,
        (MZ_U32) length, masink->resetflag);

    if (masink->resetflag == TRUE) {
      masink->resetflag = FALSE;
    }
#else
    /* FIXME: dirty patch for Divx issue */
    if (masink->resetflag == TRUE) {
      masink->dropCnt++;
      if (masink->dropCnt == 1) {
        memset (data, 0, length);
        bRet =
            msil_audio_dma_reader_push ((MZ_PTR) masink->SinkHandle,
            (MZ_PTR) data, (MZ_U32) length, 1);
      } else if (masink->dropCnt < 50) {
        memset (data, 0, length);
        bRet =
            msil_audio_dma_reader_push ((MZ_PTR) masink->SinkHandle,
            (MZ_PTR) data, (MZ_U32) length, 0);
      } else {
        masink->dropCnt = 0;
        masink->resetflag = FALSE;
        memset (data, 0, length);
        bRet =
            msil_audio_dma_reader_push ((MZ_PTR) masink->SinkHandle,
            (MZ_PTR) data, (MZ_U32) length, 0);
        msil_audio_SetMute ((MZ_PTR) masink->SinkHandle, FALSE);
      }
    } else {
      bRet =
          msil_audio_dma_reader_push ((MZ_PTR) masink->SinkHandle,
          (MZ_PTR) data, (MZ_U32) length, 0);
    }
#endif

    if (bRet == FALSE) {
      g_print ("Push DMA Reader Fail !!\r\n");
    }

    GST_MASINK_UNLOCK (asink);
  }
  /*
     else
     {
     guint32 pcmBufferLevel = 0;

     bRet = msil_audio_Get_Info((MZ_PTR) masink->SinkHandle, MZ_PCM3_LEVEL, (MZ_PTR) &pcmBufferLevel);
     if (bRet == FALSE) {
     g_print ("get PCM3 level Fail !!\r\n");
     return 0;
     }

     if (pcmBufferLevel < length) {
     masink->WriteFrame = 0;
     }

     GST_MASINK_LOCK (asink);

     bRet = msil_audio_Push2PCM3((MZ_PTR) masink->SinkHandle, (MZ_PTR) data, (MZ_U32) length);
     if (bRet == FALSE) {
     g_print ("push data size %u to PCM3 Fail writeFrame=%llu !!\r\n", length, masink->WriteFrame);
     }

     if (masink->WriteFrame < 10) {
     msil_audio_Get_Info((MZ_PTR) masink->SinkHandle, MZ_PCM3_LEVEL, (MZ_PTR) &masink->pcm3Level);
     g_usleep (9000);
     }
     else {
     guint32 pcm3Level = 0;

     if (msil_audio_Get_Info((MZ_PTR) masink->SinkHandle, MZ_PCM3_LEVEL, (MZ_PTR) &pcm3Level) == FALSE) {
     GST_ERROR ("Can not get PCM3 Level !!");
     }

     if (pcm3Level == 0) {
     GST_INFO ("Pcm buffer is Empty now !!");
     }

     while (pcm3Level > ((guint32) masink->pcm3Level)){
     if ( msil_audio_Get_Info((MZ_PTR) masink->SinkHandle, MZ_PCM3_LEVEL, (MZ_PTR) &pcm3Level) == FALSE )
     {
     g_print ("get PCM3 level Fail !!\r\n");
     }
     g_usleep (100);
     }
     }

     GST_MASINK_UNLOCK (asink);
     }
   */
  u32CopySize = length;
  masink->WriteFrame++;

  return u32CopySize;
}





static guint
gst_masink_delay (GstAudioSink * asink)
{
  GstMasink *masink = GST_MASINK (asink);
  guint nNbSamplesInQueue = 0;
  guint32 u32PCM3Leve = 0;

  if (masink->SinkHandle == NULL) {
    return 0;
  }

  if (!g_str_equal (masink->app_type, "SKYPE")) {
    msil_audio_dma_reader_buffer_level ((MZ_PTR) masink->SinkHandle,
        (MZ_U32 *) & u32PCM3Leve);
    nNbSamplesInQueue = u32PCM3Leve;
  } else {
    if (msil_audio_Get_Info ((MZ_PTR) masink->SinkHandle, MZ_PCM3_LEVEL,
            (MZ_PTR) & u32PCM3Leve) == FALSE) {
      GST_ERROR ("Can not get PCM3 Level !!");
    }

    nNbSamplesInQueue = (guint) (u32PCM3Leve / masink->spec->info.finfo->width);
  }

  GST_LOG ("Total data:[%u] nNbSamplesInQueue:[%u]", u32PCM3Leve,
      nNbSamplesInQueue);

  return nNbSamplesInQueue;
}






static void
gst_masink_reset (GstAudioSink * asink)
{
  MZ_BOOL bRet = FALSE;
  GstMasink *masink = GST_MASINK (asink);

  GST_MASINK_LOCK (asink);

  g_print ("**** gst_masink_reset ****\r\n");

  masink->resetflag = TRUE;
  masink->dropCnt = 0;

  msil_audio_dma_reader_init (masink->SinkHandle, masink->spec->info.rate);

  bRet = msil_audio_ResetPCM3 (masink->SinkHandle);
  if (bRet == FALSE) {
    g_print ("Reset PCM3 level Fail !!\r\n");
  }

  GST_MASINK_UNLOCK (asink);
}



//static gboolean gst_mvsink_sink_event (GstPad* pad, GstObject * parent, GstEvent* event)

static gboolean
gst_masink_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret;
  GstMasink *masink = GST_MASINK (gst_pad_get_parent (pad));
//    GstAudioSinkClass *bclass=GST_AUDIO_SINK_GET_CLASS (masink);

  g_print ("**** %s: %s ****\r\n", __FUNCTION__, GST_EVENT_TYPE_NAME (event));

#if 0
  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    gboolean update = FALSE;
    GstFormat fmt;
    gint64 start, stop, time;
    gdouble rate, arate;
    GstClockTime stime;

    gst_event_parse_new_segment_full (event, &update, &rate, &arate, &fmt,
        &start, &stop, &time);
    g_print ("!!! sink NS rate=%lf, arate=%lf, update=%d, fmt=%s, start=%"
        GST_TIME_FORMAT ", stop=%" GST_TIME_FORMAT ", time=%" GST_TIME_FORMAT
        "\r\n", rate, arate, update, gst_format_get_name (fmt),
        GST_TIME_ARGS (start), GST_TIME_ARGS (stop), GST_TIME_ARGS (time));

    GST_PAD_STREAM_LOCK (pad);

    stime = gst_clock_get_time (GST_ELEMENT_CLOCK (GST_ELEMENT_CAST (masink)));
    gst_element_set_start_time (GST_ELEMENT_CAST (masink), stime);

    gst_element_lost_state (GST_ELEMENT_CAST (masink));

    GST_PAD_STREAM_UNLOCK (pad);
  }
#endif

  if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
    GstCaps *caps;

    gst_event_parse_caps (event, &caps);
    if (TRUE) {
      ret = TRUE;               //bclass->set_caps (GstMasink, caps);
    }

    if (ret) {
      gst_caps_replace (&masink->cached_caps, caps);
    }
  }

  /* patch for seek have pop noise */
  if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_START) {
    if (!g_str_equal (masink->app_type, "SKYPE")) {
      masink->resetflag = TRUE;
      masink->dropCnt = 0;
      msil_audio_dma_reader_command ((MZ_PTR) masink->SinkHandle, FALSE);
      msil_audio_SetMute ((MZ_PTR) masink->SinkHandle, TRUE);
    }
  }

  if (masink->disable_lost_state) {
    if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_START) {
//            g_print ("%s: disable lost state is true\r\n", __FUNCTION__, masink->disable_lost_state);
      g_print ("%s: disable lost state is true\r\n", __FUNCTION__);
      gst_event_unref (event);
      gst_object_unref (masink);
      return TRUE;
    }

    if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
//            g_print ("%s: disable lost state is true\r\n", __FUNCTION__, masink->disable_lost_state);
      g_print ("%s: disable lost state is true\r\n", __FUNCTION__);
      gst_event_unref (event);
      gst_object_unref (masink);
      return TRUE;
    }
#if 1                           // kochien marked, no this event in GST1.2
    if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
//            g_print ("%s: disable lost state is true\r\n", __FUNCTION__, masink->disable_lost_state);
      g_print ("%s: disable lost state is true\r\n", __FUNCTION__);
      masink->disable_lost_state = FALSE;
      gst_event_unref (event);
      gst_object_unref (masink);
      return TRUE;
    }
#endif
  }

  gst_object_unref (masink);

//    ret = masink->sink_event_func (pad, event);
  ret = masink->sink_event_func (pad, gst_pad_get_parent (pad), event);

  return ret;
}


#if 0
static void
gst_masink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "MStar Audio Sink",
      "Sink/Audio", "MStar Audio Output", "MStar Semiconductor Inc.");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&masink_sink_factory));
}
#endif

/*
Usage : create gst_masink_query instead of base gstbasesink_class->query 
        1. check sink and dec caps
        2. force result as TRUE temporarily
*/
static gboolean
gst_masink_query (GstMasink * masink, GstQuery * query)
{
//  GstMasink *alsa = GST_ALSA_SINK (sink);
  GstBaseSink *sink = GST_BASE_SINK (masink);
  gboolean ret;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps, *allowed;
      gst_query_parse_accept_caps (query, &caps);

//    allowed = gst_base_sink_query_caps (sink, sink->sinkpad, NULL);
      allowed = gst_pad_query_caps (GST_BASE_SINK_PAD (masink), caps);
      g_print ("allowed GAPS: %s\n", gst_caps_to_string (allowed));

      g_print ("query GAPS: %s\n", gst_caps_to_string (caps));

      //ret = gst_alsasink_acceptcaps (alsa, caps);
      ret = TRUE;
      gst_query_set_accept_caps_result (query, ret);
      ret = TRUE;
      break;
    }

    case GST_QUERY_CAPS:
    default:
      ret = GST_BASE_SINK_CLASS (gst_masink_parent_class)->query (sink, query);
      break;
  }
  return ret;
}

static void
gst_masink_class_init (GstMasinkClass * klass)
{
  GObjectClass *gobject_class;
  GstAudioSinkClass *gstaudiosink_class;
  GstBaseSinkClass *gstbasesink_class;  //create gst_masink_query instead of base gstbasesink_class->query 
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  gstaudiosink_class = (GstAudioSinkClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;       //create gst_masink_query instead of base gstbasesink_class->query 

  GST_DEBUG_CATEGORY_INIT (gst_masink_debug, "masink", 0, "Masink");

  gst_masink_parent_class =
      (GstAudioSinkClass *) g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_masink_finalise;
  gobject_class->set_property = gst_masink_set_property;
  gobject_class->get_property = gst_masink_get_property;

  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_masink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_masink_unprepare);
  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_masink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_masink_close);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_masink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_masink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_masink_reset);

  gstbasesink_class->query = (void *) GST_DEBUG_FUNCPTR (gst_masink_query);     //create gst_masink_query instead of base gstbasesink_class->query 

  gst_element_class_set_details_simple (element_class,
      "MStar Audio Sink",
      "Sink/Audio", "MStar Audio Output",
      "kochien.kuo <kochien.kuo@mstarsemi.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&masink_sink_factory));

  g_object_class_install_property (gobject_class,
      PROP_VOLUME,
      g_param_spec_uint ("volume", "Volume",
          "Volume of this stream", 0, 32, 32,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_SPDIF_NON_PCM,
      g_param_spec_boolean ("SPDIF",
          "SPDIF",
          "SPDIF NON-PCM output mode", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_HDMI_NON_PCM,
      g_param_spec_boolean ("HDMI",
          "HDMI",
          "HDMI NON-PCM output mode", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_MUTE,
      g_param_spec_boolean ("mute",
          "mute",
          "mute audio output", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_APP_TYPE,
      g_param_spec_string ("app-type",
          "app-type",
          "Set app type",
          DEFAULT_APP_TYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_DISABLE_LOST_STATE,
      g_param_spec_boolean ("disable-lost-state",
          "disable-lost-state",
          "Disable seek event if TRUE", FALSE, G_PARAM_READWRITE));

}

static void
gst_masink_init (GstMasink * masink)
{

//    mvsink->sink_event_func = GST_PAD_EVENTFUNC (GST_BASE_SINK(mvsink)->sinkpad);
//    gst_pad_set_event_function (GST_BASE_SINK(mvsink)->sinkpad, GST_DEBUG_FUNCPTR (gst_mvsink_sink_event));

  masink->sink_event_func = GST_PAD_EVENTFUNC (GST_BASE_SINK (masink)->sinkpad);
  gst_pad_set_event_function (GST_BASE_SINK (masink)->sinkpad,
      GST_DEBUG_FUNCPTR (gst_masink_sink_event));
//    #define gst_pad_set_event_function(p,f)         gst_pad_set_event_function_full((p),(f),NULL,NULL)
//gstpad.h:910:8: note: expected 'GstPadEventFunction' but argument is of type 'gboolean (*)(struct GstPad *, struct GstEvent *)'

  masink->cached_caps = NULL;
  masink->buffer_size = 0;
  masink->volume = 100;
  masink->SinkHandle = NULL;
  masink->resetflag = 0;
  masink->masink_lock = g_mutex_new ();
  masink->app_type = g_strdup (DEFAULT_APP_TYPE);

  gst_audio_base_sink_set_provide_clock (GST_AUDIO_BASE_SINK (masink), FALSE);
  //gst_base_sink_set_async_enabled (GST_BASE_SINK (masink), FALSE);
  //gst_base_audio_sink_set_slave_method (GST_BASE_SINK (masink), GST_BASE_AUDIO_SINK_SLAVE_NONE);
  gst_audio_base_sink_set_alignment_threshold (GST_AUDIO_BASE_SINK (masink),
      (400 * GST_MSECOND));

  masink->disable_lost_state = FALSE;
}

static gboolean
masink_init (GstPlugin * plugin)
{

  gst_element_register (plugin, "masink", GST_RANK_SECONDARY, GST_TYPE_MASINK);

  return TRUE;
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "mstar_masink"
#endif

#define MASINK_VERSION "0.0.0.01"

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    masink,
    "MStar's masink elements",
    masink_init, MASINK_VERSION, "Proprietary", "MStar's Plug-ins", "MStar")
