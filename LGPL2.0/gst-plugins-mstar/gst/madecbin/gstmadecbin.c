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
/**
 * SECTION:element-adecsink
 *
 ********************************************************
 *                                             User Guide                                              *
 *                          How to add a new audio codec element                      *
 *  1. add its minetype in  madec_bin_sink_template                                 *
 *  2. check if its condition madec_bin_mimetype_to_audiocodectype()    *
  ********************************************************
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! adecsink ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#include "gstmadecbin.h"



GST_DEBUG_CATEGORY_STATIC (gst_madecbin_debug);
#define GST_CAT_DEFAULT gst_madecbin_debug

#define DEBUG_INIT  GST_DEBUG_CATEGORY_INIT (gst_madecbin_debug, "madecbin", 0, "debug category for madecbin");


G_DEFINE_TYPE_WITH_CODE (Gstmadecbin, gst_madec_bin, GST_TYPE_BIN, DEBUG_INIT);


static void gst_madec_bin_dispose (GObject * object);
static void gst_madec_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_madec_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_madec_bin_finalize (GObject * object);
static GstStateChangeReturn gst_madec_bin_change_state (GstElement * element,
    GstStateChange transition);
static char *_GetAudioTypeName (MADECBIN_CODECTYPE eAudioType);

/* Filter signals and args */
enum
{
  PROP_0,
  PROP_SILENT,
  PROP_RESOURCE_INFO,
  PROP_DTS_SEAMLESS,
};

typedef struct ST_MADECBIN_AUDIOCODEC_FACTORYNAME
{
  MADECBIN_CODECTYPE audio_codec_type;
  const gchar *audio_codec_factoryname;
} ST_MADECBIN_AUDIOCODEC_FACTORYNAME;

ST_MADECBIN_AUDIOCODEC_FACTORYNAME astAudioCode_FactoryName[] = {
  {E_MADECBIN_CODEC_TYPE_MPEG2, "omxmp2dec"},
  {E_MADECBIN_CODEC_TYPE_MP3, "omxmp3dec"},
  {E_MADECBIN_CODEC_TYPE_AAC, "omxaacdec"},
  {E_MADECBIN_CODEC_TYPE_AC3, "omxac3dec"},
  {E_MADECBIN_CODEC_TYPE_DTS, "omxdtsdec"},
  {E_MADECBIN_CODEC_TYPE_AMR, "omxamrdec"},
  {E_MADECBIN_CODEC_TYPE_RA, "omxradec"},
  {E_MADECBIN_CODEC_TYPE_VORBIS, "omxvorbisdec"},
  {E_MADECBIN_CODEC_TYPE_ADPCM, "omxadpcmdec"},
  {E_MADECBIN_CODEC_TYPE_WMA, "omxwmadec"},
  {E_MADECBIN_CODEC_TYPE_FLAC, "omxflacdec"},
  {E_MADECBIN_CODEC_TYPE_LPCM, "omxlpcmdec"},
};

static GstBinClass *parent_class;

/* generic templates */
static GstStaticPadTemplate madec_bin_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion=(int) [2, 4], "
        "rate=(int) [8000, 96000], "
        "channels=(int) [0,6]; "
        "audio/x-ac3, "
        "framed=(boolean) true, "
        "rate=(int) [32000, 48000], "
        "channels=(int)[1,6]; "
        "audio/x-eac3, "
        "framed=(boolean) true, "
        "rate=(int) [32000, 48000], "
        "channels=(int)[1,6];"
        "audio/x-dts;audio/x-dtsh;audio/x-dtsl;audio/x-dtse;"
        "audio/x-adpcm;audio/x-mulaw;audio/x-alaw;"
        "audio/x-private-ts-lpcm;audio/x-lpcm-1;audio/x-private-lg-lpcm;audio/x-private1-lpcm;"
        "audio/AMR;audio/AMR-WB;"
        "audio/x-flac;"
        "audio/x-pn-realaudio;" "audio/x-vorbis;" "audio/x-wma"));

static GstStaticPadTemplate madec_bin_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "rate=(int) [8000, 48000], " "format=S32LE, " "channels=(int) 2;"));

static MADECBIN_CODECTYPE
madec_bin_caps_to_audiocodectype (Gstmadecbin * self, GstCaps * caps)
{
  GstStructure *structure;
  const gchar *mimetype;
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  GST_DEBUG_OBJECT (self, "mimetype is %s", mimetype);

  if (g_str_has_prefix (mimetype, "audio/mpeg")) {
    gint layer, mpegversion;

    if (gst_structure_get_int (structure, "mpegversion", &mpegversion)) {
      GST_DEBUG_OBJECT (self, "mpegversion: %d", mpegversion);
      if (mpegversion == 1) {
        if (gst_structure_get_int (structure, "layer", &layer)) {
          GST_DEBUG_OBJECT (self, "layer: %d", layer);
          if (layer == 2) {
            return E_MADECBIN_CODEC_TYPE_MPEG2;
          } else {
            return E_MADECBIN_CODEC_TYPE_MP3;
          }
        }
      }
      return E_MADECBIN_CODEC_TYPE_AAC;
    }
  } else if (g_str_has_prefix (mimetype, "audio/x-ac3")
      || g_str_has_prefix (mimetype, "audio/x-eac3")) {
    return E_MADECBIN_CODEC_TYPE_AC3;
  } else if (g_str_has_prefix (mimetype, "audio/x-dts")
      || g_str_has_prefix (mimetype, "audio/x-dtsh")
      || g_str_has_prefix (mimetype, "audio/x-dtsl")
      || g_str_has_prefix (mimetype, "audio/x-dtse")) {
    return E_MADECBIN_CODEC_TYPE_DTS;
  } else if (g_str_has_prefix (mimetype, "audio/x-adpcm")
      || g_str_has_prefix (mimetype, "audio/x-mulaw")
      || g_str_has_prefix (mimetype, "audio/x-alaw")) {
    return E_MADECBIN_CODEC_TYPE_ADPCM;
  } else if (g_str_has_prefix (mimetype, "audio/x-private-ts-lpcm")
      || g_str_has_prefix (mimetype, "audio/x-lpcm-1")
      || g_str_has_prefix (mimetype, "audio/x-private-lg-lpcm")
      || g_str_has_prefix (mimetype, "audio/x-private1-lpcm")) {
    return E_MADECBIN_CODEC_TYPE_LPCM;
  } else if (g_str_has_prefix (mimetype, "audio/AMR")
      || g_str_has_prefix (mimetype, "audio/AMR-WB")) {
    return E_MADECBIN_CODEC_TYPE_AMR;
  } else if (g_str_has_prefix (mimetype, "audio/x-flac")) {
    return E_MADECBIN_CODEC_TYPE_FLAC;
  } else if (g_str_has_prefix (mimetype, "audio/x-pn-realaudio")) {
    return E_MADECBIN_CODEC_TYPE_RA;
  } else if (g_str_has_prefix (mimetype, "audio/x-vorbis")) {
    return E_MADECBIN_CODEC_TYPE_VORBIS;
  } else if (g_str_has_prefix (mimetype, "audio/x-wma")) {
    return E_MADECBIN_CODEC_TYPE_WMA;
  }

  return E_MADECBIN_CODEC_TYPE_NONE;
}

static gboolean
madec_bin_decode_element_disconnect (Gstmadecbin * self)
{
  gboolean res = TRUE;
  GstPad *dsinkpad = NULL;
  GstPad *dsrcpad = NULL;

  if (gst_element_set_state (self->decoder,
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (self, "Couldn't set %s to NULL",
        GST_ELEMENT_NAME (self->decoder));
    res = FALSE;
    goto exit;
  }

  dsinkpad = gst_element_get_static_pad (self->decoder, "sink");
  if (dsinkpad == NULL) {
    GST_ERROR_OBJECT (self, "Couldn't get %s sink pad",
        GST_ELEMENT_NAME (self->decoder));
    res = FALSE;
    goto exit;
  }

  dsrcpad = gst_element_get_static_pad (self->decoder, "src");
  if (dsrcpad == NULL) {
    GST_ERROR_OBJECT (self, "Couldn't get %s src pad",
        GST_ELEMENT_NAME (self->decoder));
    res = FALSE;
    goto exit;
  }

  gst_object_unref (dsinkpad);
  gst_object_unref (dsrcpad);

  gst_bin_remove (GST_BIN (self), self->decoder);
  self->decoder = NULL;

exit:
  return res;
}

static gboolean
madec_bin_decode_element_connect (Gstmadecbin * self)
{
  gboolean res = TRUE;
  GstPad *sinkPad = NULL;
  GstPad *srcPad = NULL;
  gint NumberOfAudiocodec =
      sizeof (astAudioCode_FactoryName) / sizeof (astAudioCode_FactoryName[0]);
  gint TypeIndex = 0;

  //find the corresponding type in struct of astAudioCode_FactoryName
  if ((E_MADECBIN_CODEC_TYPE_NONE < self->audio_format) &&
      (self->audio_format < E_MADECBIN_CODEC_TYPE_NUM)) {
    for (TypeIndex = 0; TypeIndex < NumberOfAudiocodec; TypeIndex++) {
      if (self->audio_format ==
          astAudioCode_FactoryName[TypeIndex].audio_codec_type) {
        self->decoder =
            gst_element_factory_make (astAudioCode_FactoryName[TypeIndex].
            audio_codec_factoryname, NULL);
        break;
      }
    }
  } else {
    GST_ERROR_OBJECT (self, "audio format %s not in the list",
        _GetAudioTypeName (self->audio_format));
    res = FALSE;
    goto exit;
  }

  if (self->decoder == NULL) {
    GST_ERROR_OBJECT (self, "can not create decode element for %s",
        _GetAudioTypeName (self->audio_format));
    self->audio_format = E_MADECBIN_CODEC_TYPE_NONE;
    res = FALSE;
    goto exit;
  }

  if ((sinkPad = gst_element_get_static_pad (self->decoder, "sink")) == NULL) {
    /* if no pad is found we can't do anything */
    GST_ERROR_OBJECT (self, "could not find sinkpad in decode element");
    self->audio_format = E_MADECBIN_CODEC_TYPE_NONE;
    res = FALSE;
    goto exit;
  }

  /* now add the element to the bin first */
  GST_DEBUG_OBJECT (self, "adding %s", GST_OBJECT_NAME (self->decoder));
  res = gst_bin_add (GST_BIN (self), self->decoder);
  if (res == FALSE) {
    /* if no pad is found we can't do anything */
    GST_ERROR_OBJECT (self, "add decode element to bin fail !!");
    self->audio_format = E_MADECBIN_CODEC_TYPE_NONE;
    res = FALSE;
    goto exit;
  }

  res = gst_element_sync_state_with_parent (self->decoder);
  if (res == FALSE) {
    /* if no pad is found we can't do anything */
    GST_ERROR_OBJECT (self, "sync state with parent fail !!");
    self->audio_format = E_MADECBIN_CODEC_TYPE_NONE;
    res = FALSE;
    goto exit;
  }

  if ((res =
          gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->gpad_sink),
              sinkPad)) == TRUE) {
    GST_DEBUG_OBJECT (self, "linked on pad %s:%s",
        GST_DEBUG_PAD_NAME (self->gpad_sink));

    /* get rid of the sinkpad now */
    gst_object_unref (sinkPad);
  } else {
    GST_DEBUG_OBJECT (self, "link failed on pad %s:%s",
        GST_DEBUG_PAD_NAME (self->gpad_sink));

    /* get rid of the sinkpad */
    gst_object_unref (sinkPad);

    /* this element did not work, remove it again and continue trying
     * other elements, the element will be disposed. */
    gst_element_set_state (self->decoder, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self), self->decoder);

    self->audio_format = E_MADECBIN_CODEC_TYPE_NONE;
    res = FALSE;
    goto exit;
  }

  if ((srcPad = gst_element_get_static_pad (self->decoder, "src")) == NULL) {
    /* if no pad is found we can't do anything */
    GST_ERROR_OBJECT (self, "could not find sinkpad in decode element");
    self->audio_format = E_MADECBIN_CODEC_TYPE_NONE;
    res = FALSE;
    goto exit;
  }

  if ((res =
          gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->gpad_src),
              srcPad)) == TRUE) {
    GST_DEBUG_OBJECT (self, "linked on pad %s:%s",
        GST_DEBUG_PAD_NAME (self->gpad_src));

    /* get rid of the sinkpad now */
    gst_object_unref (srcPad);
  } else {
    GST_DEBUG_OBJECT (self, "link failed on pad %s:%s",
        GST_DEBUG_PAD_NAME (self->gpad_src));

    /* get rid of the sinkpad */
    gst_object_unref (srcPad);

    /* this element did not work, remove it again and continue trying
     * other elements, the element will be disposed. */
    gst_element_set_state (self->decoder, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self), self->decoder);

    self->audio_format = E_MADECBIN_CODEC_TYPE_NONE;
    res = FALSE;
    goto exit;
  }

exit:
  return res;
}

static gboolean
madec_bin_check_need_to_reconnect (Gstmadecbin * self, GstCaps * caps)
{
  gboolean res = FALSE;
  gint aFormat = E_MADECBIN_CODEC_TYPE_NONE;

  /* FIXME, iterate over more structures? I guess it is possible that
   * this pad has some encoded and some raw pads. This code will fail
   * then if the first structure is not the raw type... */

  aFormat = madec_bin_caps_to_audiocodectype (self, caps);

  if ((self->audio_format != aFormat)
      && (aFormat != E_MADECBIN_CODEC_TYPE_NONE)) {
    self->audio_format = aFormat;
    res = TRUE;
  }

  return res;
}

/* called when a new pad is discovered. It will perform some basic actions
 * before trying to link something to it.
 *
 *  - Check the caps, don't do anything when there are no caps or when they have
 *    no good type.
 *  - signal AUTOPLUG_CONTINUE to check if we need to continue autoplugging this
 *    pad.
 *  - if the caps are non-fixed, setup a handler to continue autoplugging when
 *    the caps become fixed (connect to notify::caps).
 *  - get list of factories to autoplug.
 *  - continue autoplugging to one of the factories.
 */
static void
madec_bin_analyze_new_pad (Gstmadecbin * self, GstCaps * caps)
{
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (self, "Pad caps:%" GST_PTR_FORMAT, caps);

  if ((caps == NULL) || gst_caps_is_empty (caps))
    goto unknown_type;

  if (gst_caps_is_any (caps))
    goto any_caps;

  res = madec_bin_check_need_to_reconnect (self, caps);
  if (res == TRUE) {
    GST_DEBUG_OBJECT (self, "Need %s element to %s",
        self->decoder == NULL ? "connect" : "re-connect",
        _GetAudioTypeName (self->audio_format));

    if (self->decoder == NULL) {
      res = madec_bin_decode_element_connect (self);
      if (res == FALSE) {
        GST_ERROR_OBJECT (self, "connect decode element fail !!");
        return;
      }
    } else {
      res = madec_bin_decode_element_disconnect (self);
      if (res == FALSE) {
        GST_ERROR_OBJECT (self, "disconnect decode element fail !!");
        return;
      }

      res = madec_bin_decode_element_connect (self);
      if (res == FALSE) {
        GST_ERROR_OBJECT (self, "re-connect decode element fail !!");
        return;
      }
    }

    GST_DEBUG_OBJECT (self, "link ok !!");
  } else {
    GST_DEBUG_OBJECT (self, "Don't Need to change %s element",
        _GetAudioTypeName (self->audio_format));

    res = TRUE;
  }

  if (!res)
    goto unknown_type;

//set property to decoder element
  if (self->audio_format == E_MADECBIN_CODEC_TYPE_DTS) {
    GST_LOG_OBJECT (self, "set property Dts Seamless(%d)", self->bDtsSeamless);
    g_object_set (self->decoder, "dts-seamless", self->bDtsSeamless, NULL);
  }

  return;

unknown_type:
  GST_ERROR_OBJECT (self,
      "Unknown type, can not know which decode element should be reload");
  return;

any_caps:
  GST_ERROR_OBJECT (self,
      "pad has ANY caps, can not know which decode element should be reload");
  return;
}

static gboolean
gst_madec_bin_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  Gstmadecbin *self = GST_MADEC_BIN (parent);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (pad, "got event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      GST_DEBUG_OBJECT (self, "found caps %" GST_PTR_FORMAT, caps);
      madec_bin_analyze_new_pad (self, caps);
      ret = gst_pad_event_default (pad, parent, event);
      GST_LOG_OBJECT (self, "type_found %s", (ret == TRUE) ? "ok" : "failed");
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_madec_bin_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = FALSE;
  GstCaps *query_caps;
  gboolean subset;

  GST_DEBUG_OBJECT (pad, "handling query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstPadTemplate *pad_template;
      GstCaps *pad_caps;
      pad_template = gst_static_pad_template_get (&madec_bin_sink_template);
      pad_caps = gst_pad_template_get_caps (pad_template);
      GST_DEBUG_OBJECT (pad, "pad_caps: %" GST_PTR_FORMAT, pad_caps);

      gst_query_parse_accept_caps (query, &query_caps);
      GST_DEBUG_OBJECT (pad, "query_caps: %" GST_PTR_FORMAT, query_caps);
      subset = gst_caps_is_subset (query_caps, pad_caps);
      GST_DEBUG_OBJECT (pad, "is subset: %s", (subset == TRUE) ? "yes" : "no");
      gst_query_set_accept_caps_result (query, subset);

      gst_caps_unref (pad_caps);
      gst_object_unref (pad_template);
      res = subset;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}


/* initialize the adecsink's class */
static void
gst_madec_bin_class_init (GstmadecbinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_madec_bin_dispose;
  gobject_class->set_property = gst_madec_bin_set_property;
  gobject_class->get_property = gst_madec_bin_get_property;
  gobject_class->finalize = gst_madec_bin_finalize;

  g_object_class_install_property (gobject_class,
      PROP_SILENT,
      g_param_spec_boolean ("silent",
          "Silent", "Produce verbose output ?", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_RESOURCE_INFO,
      g_param_spec_boxed ("resource-info",
          "Resource information",
          "Hold various information for managing resource",
          GST_TYPE_STRUCTURE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DTS_SEAMLESS,
      g_param_spec_boolean ("dts-seamless",
          "dts seamless", "dts seamless", FALSE, G_PARAM_READWRITE));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&madec_bin_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&madec_bin_src_template));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_madec_bin_change_state);

  gst_element_class_set_details_simple (gstelement_class,
      "MStar Adec Bin for MPEG-DASH",
      "Specific/Bin/Decoder",
      "MStar ADEC Bin synamic switch decode element for MPEG-DASH",
      "MStar's Audio Decoder Bin");

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_madec_bin_init (Gstmadecbin * self)
{
  GstPadTemplate *pad_tmpl;

  /* get the pad template */
  pad_tmpl = gst_static_pad_template_get (&madec_bin_sink_template);

  self->gpad_sink =
      gst_ghost_pad_new_no_target_from_template ("sink", pad_tmpl);
  gst_pad_set_active (self->gpad_sink, TRUE);
  gst_element_add_pad (GST_ELEMENT (self), self->gpad_sink);

  gst_pad_set_event_function (GST_PAD_CAST (self->gpad_sink),
      GST_DEBUG_FUNCPTR (gst_madec_bin_sink_event));
  gst_pad_set_query_function (GST_PAD_CAST (self->gpad_sink),
      GST_DEBUG_FUNCPTR (gst_madec_bin_sink_query));

  gst_object_unref (pad_tmpl);


  /* get the pad template */
  pad_tmpl = gst_static_pad_template_get (&madec_bin_src_template);

  self->gpad_src = gst_ghost_pad_new_no_target_from_template ("src", pad_tmpl);
  gst_pad_set_active (self->gpad_src, TRUE);
  gst_element_add_pad (GST_ELEMENT (self), self->gpad_src);

  gst_object_unref (pad_tmpl);


  self->silent = FALSE;
  self->decoder = NULL;
  self->index = 0;
  self->audio_format = E_MADECBIN_CODEC_TYPE_NONE;
  self->bDtsSeamless = 0;
  GST_LOG_OBJECT (self, "init ok");

}

static GstStateChangeReturn
gst_madec_bin_change_state (GstElement * element, GstStateChange transition)
{
  Gstmadecbin *self = GST_MADEC_BIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_LOG_OBJECT (self, "%s ==> %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_madec_bin_parent_class)->change_state (element,
      transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static void
gst_madec_bin_dispose (GObject * object)
{
  Gstmadecbin *self = GST_MADEC_BIN (object);

  GST_LOG_OBJECT (self, "dispose ok");

  G_OBJECT_CLASS (gst_madec_bin_parent_class)->dispose (object);
}

static void
gst_madec_bin_finalize (GObject * object)
{
  Gstmadecbin *self = GST_MADEC_BIN (object);

  GST_LOG_OBJECT (self, "finalize ok");

  G_OBJECT_CLASS (gst_madec_bin_parent_class)->finalize (object);
}

static void
gst_madec_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstmadecbin *self = GST_MADEC_BIN (object);

  GST_DEBUG_OBJECT (self, "set prop %d", prop_id);

  switch (prop_id) {
    case PROP_SILENT:
      self->silent = g_value_get_boolean (value);
      GST_LOG_OBJECT (self, "silent is %s", self->silent ? "TRUE" : "FALSE");
      break;

    case PROP_RESOURCE_INFO:
    {
      const GstStructure *s = gst_value_get_structure (value);

      if (gst_structure_has_field (s, "audio-port")) {
        if (gst_structure_get_int (s, "audio-port", &self->index)) {
          GST_DEBUG_OBJECT (self, "resource info adec ID %d", self->index);
        }
      }
      break;
    }

    case PROP_DTS_SEAMLESS:
    {
      self->bDtsSeamless = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (self, "record the flag : bDtsSeamless(%d)",
          self->bDtsSeamless);
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_madec_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  Gstmadecbin *self = GST_MADEC_BIN (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, self->silent);
      GST_LOG_OBJECT (self, "silent is %s", self->silent ? "TRUE" : "FALSE");
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gchar *
_GetAudioTypeName (MADECBIN_CODECTYPE eAudioType)
{
  switch (eAudioType) {
    case E_MADECBIN_CODEC_TYPE_MPEG2:
      return "mp2";

    case E_MADECBIN_CODEC_TYPE_MP3:
      return "mp3";

    case E_MADECBIN_CODEC_TYPE_AAC:
      return "aac";

    case E_MADECBIN_CODEC_TYPE_VORBIS:
      return "vorbis";

    case E_MADECBIN_CODEC_TYPE_AMR:
      return "amr";

    case E_MADECBIN_CODEC_TYPE_ADPCM:
      return "adpcm";

    case E_MADECBIN_CODEC_TYPE_AC3:
      return "ac3";

    case E_MADECBIN_CODEC_TYPE_DTS:
      return "dts";

    case E_MADECBIN_CODEC_TYPE_RA:
      return "ra";

    case E_MADECBIN_CODEC_TYPE_WMA:
      return "wma";

    case E_MADECBIN_CODEC_TYPE_FLAC:
      return "flac";

    case E_MADECBIN_CODEC_TYPE_LPCM:
      return "lpcm";
    default:
      break;
  }

  return "NULL";
}
