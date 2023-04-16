/*
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

#include "gstomxvorbisdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_vorbis_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_vorbis_dec_debug_category

/* prototypes */
static gboolean gst_omx_vorbis_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_vorbis_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_vorbis_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_vorbis_dec_debug_category, "omxvorbisdec", 0, \
      "debug category for gst-omx audio decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXVORBISDec, gst_omx_vorbis_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);


static void
gst_omx_vorbis_dec_class_init (GstOMXVORBISDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audiodec_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_vorbis_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_vorbis_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_vorbis_dec_get_samples_per_frame);

  audiodec_class->cdata.default_sink_template_caps = "audio/x-vorbis, "
      "rate=(int)[8000,48000], " "channels=(int)[1,2] ";

  // output PCM format
  audiodec_class->cdata.default_src_template_caps = "audio/x-raw, " "rate=(int)[8000,48000], " "format=S16LE, " // new in GST1.0, signed, 2bytes, little endian
      "channels=(int)[1,10]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX VORBIS Audio Decoder",
      "Codec/Decoder/Audio",
      "Decode VORBIS audio streams", "MStar Semiconductor Inc");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.vorbis");
}

static void
gst_omx_vorbis_dec_init (GstOMXVORBISDec * self)
{
  self->spf = -1;
}

static gboolean
gst_omx_vorbis_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXVORBISDec *self = GST_OMX_VORBIS_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_VORBISTYPE vorbis_param;
  OMX_ERRORTYPE err;
  GstStructure *s;
  gint rate, channels;

  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingVORBIS;
  err = gst_omx_port_update_port_definition (port, &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set VORBIS format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&vorbis_param);
  vorbis_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioVorbis,
      &vorbis_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get VORBIS parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }
  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "rate", &rate) ||
      !gst_structure_get_int (s, "channels", &channels)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  vorbis_param.nChannels = channels;
  vorbis_param.nSampleRate = rate;

  err =
      gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioVorbis,
      &vorbis_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting VORBIS parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  return TRUE;
}

static gboolean
gst_omx_vorbis_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  return FALSE;
}

static gint
gst_omx_vorbis_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port)
{
  //because TEMPO algrithm will generate 1~3X frames size from decoder output(1 frame), total average is 2X frame size
  //gst_omx_audio_dec_loop() function will check two kinds of get_samples_per_frame() return value are described as below:
  //if not -1, it will recalculate frame value which is not correct only in case of 0.5X TEMPO.
  //if -1, always regards output size as one frame.
  //in case of rate 0.5X, this function will return -1 to keep nframe=1 , others will return its real spf
  if (dec->rate == 0.5 || dec->rate == 2) {
    return -1;
  } else {
    return GST_OMX_VORBIS_DEC (dec)->spf;
  }
}
