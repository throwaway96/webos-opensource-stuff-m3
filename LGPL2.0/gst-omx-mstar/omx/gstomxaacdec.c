//<MStar Software>
//***********************************************************************************
//MStar Software
//Copyright (c) 2010 - 2012 MStar Semiconductor, Inc. All rights reserved.
//All software, firmware and related documentation herein ("MStar Software") are intellectual property of MStar Semiconductor, Inc. ("MStar") and protected by law, including, but not limited to, copyright law and international treaties.  Any use, modification, reproduction, retransmission, or republication of all or part of MStar Software is expressly prohibited, unless prior written permission has been granted by MStar.
//By accessing, browsing and/or using MStar Software, you acknowledge that you have read, understood, and agree, to be bound by below terms ("Terms") and to comply with all applicable laws and regulations:
//
// 1. MStar shall retain any and all right, ownership and interest to MStar Software and any modification/derivatives thereof.  No right, ownership, or interest to MStar Software and any modification/derivatives thereof is transferred to you under Terms.
// 2. You understand that MStar Software might include, incorporate or be supplied together with third party's software and the use of MStar Software may require additional licenses from third parties.  Therefore, you hereby agree it is your sole responsibility to separately obtain any and all third party right and license necessary for your use of such third party's software.
// 3. MStar Software and any modification/derivatives thereof shall be deemed as MStar's confidential information and you agree to keep MStar's confidential information in strictest confidence and not disclose to any third party.
// 4. MStar Software is provided on an "AS IS" basis without warranties of any kind. Any warranties are hereby expressly disclaimed by MStar, including without limitation, any warranties of merchantability, non-infringement of intellectual property rights, fitness for a particular purpose, error free and in conformity with any international standard.  You agree to waive any claim against MStar for any loss, damage, cost or expense that you may incur related to your use of MStar Software.  In no event shall MStar be liable for any direct, indirect, incidental or consequential damages, including without limitation, lost of profit or revenues, lost or damage of data, and unauthorized system use.  You agree that this Section 4 shall still apply without being affected even if MStar Software has been modified by MStar in accordance with your request or instruction for your use, except otherwise agreed by both parties in writing.
// 5. If requested, MStar may from time to time provide technical supports or services in relation with MStar Software to you for your use of MStar Software in conjunction with your or your customer's product ("Services").  You understand and agree that, except otherwise agreed by both parties in writing, Services are provided on an "AS IS" basis and the warranty disclaimer set forth in Section 4 above shall apply.
// 6. Nothing contained herein shall be construed as by implication, estoppels or otherwise: (a) conferring any license or right to use MStar name, trademark, service mark, symbol or any other identification; (b) obligating MStar or any of its affiliates to furnish any person, including without limitation, you and your customers, any assistance of any kind whatsoever, or any information; or (c) conferring any license or right under any intellectual property right.
// 7. These terms shall be governed by and construed in accordance with the laws of Taiwan, R.O.C., excluding its conflict of law rules.  Any and all dispute arising out hereof or related hereto shall be finally settled by arbitration referred to the Chinese Arbitration Association, Taipei in accordance with the ROC Arbitration Law and the Arbitration Rules of the Association by three (3) arbitrators appointed in accordance with the said Rules.  The place of arbitration shall be in Taipei, Taiwan and the language shall be English.  The arbitration award shall be final and binding to both parties.
//***********************************************************************************

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gst/base/gstbitreader.h"
#include "gst/base/gstbytereader.h"

#include "gstomxaacdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_aac_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_aac_dec_debug_category

/* prototypes */
static gboolean gst_omx_aac_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_aac_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_aac_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_aac_dec_debug_category, "omxaacdec", 0, \
      "debug category for gst-omx audio decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXAACDec, gst_omx_aac_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);


static gboolean
gst_omx_aac_adts_header_change (GstOMXAACDec * self, GstStructure * structure)
{
  const GValue *codec_data;
  gboolean ret = FALSE;

  codec_data = gst_structure_get_value (structure, "codec_data");

  if (codec_data && (G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER)) {
    GstBuffer *codec_buf;
    GstBitReader bits;
    GstMapInfo map;

    guint8 _object_id = 0, _sample_idx = 0;
    guint _rate = 0, _channel = 0;
    gint32 sample_rate;

    codec_buf = gst_value_get_buffer (codec_data);

    gst_buffer_map (codec_buf, &map, GST_MAP_READ);
    gst_bit_reader_init (&bits, map.data, map.size);

    gst_bit_reader_get_bits_uint8 (&bits, &_object_id, 5);
    if (_object_id == 0x1F) {
      gst_bit_reader_get_bits_uint8 (&bits, &_object_id, 6);
      _object_id += 32;
    } else {
      gst_bit_reader_get_bits_uint8 (&bits, &_sample_idx, 4);
      if (_sample_idx == 0xF) {
        gst_bit_reader_get_bits_uint32 (&bits, (guint *) & _rate, 24);
      } else if ((_object_id == 29) && (_sample_idx < 3)) {
        _object_id += 13;
      }
      gst_bit_reader_get_bits_uint32 (&bits, (guint *) & _channel, 4);
    }

    GST_ERROR_OBJECT (self, "_object_id=%u", _object_id);

    //Sample rate
    switch (_sample_idx) {
      case 0:
        sample_rate = 96000;
        break;

      case 1:
        sample_rate = 88200;
        break;

      case 2:
        sample_rate = 64000;
        break;

      case 3:
        sample_rate = 48000;
        break;

      case 4:
        sample_rate = 44100;
        break;

      case 5:
        sample_rate = 32000;
        break;

      case 6:
        sample_rate = 24000;
        break;

      case 7:
        sample_rate = 22050;
        break;

      case 8:
        sample_rate = 16000;
        break;

      case 9:
        sample_rate = 12000;
        break;

      case 10:
        sample_rate = 11025;
        break;

      case 11:
        sample_rate = 8000;
        break;

      case 12:
      default:
        sample_rate = _rate;
        break;
    }
    GST_ERROR_OBJECT (self, "sample_rate=%d", sample_rate);
    if (self->aac_sample_rate != sample_rate) {
      self->aac_sample_rate = sample_rate;
      ret = TRUE;
    }
    //Channels
    GST_ERROR_OBJECT (self, "channel=%u", _channel);
    if (self->aac_channels != _channel) {
      self->aac_channels = _channel;
      ret = TRUE;
    }
  }

  return ret;
}

static void
gst_omx_aac_dec_class_init (GstOMXAACDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_aac_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_aac_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_aac_dec_get_samples_per_frame);

  // input aac format
  audiodec_class->cdata.default_sink_template_caps = "audio/mpeg, "
      "mpegversion=(int)[2,4], "
      "rate=(int)[8000,96000], " "channels=(int)[0,8] ";

  // output PCM format
  audiodec_class->cdata.default_src_template_caps = "audio/x-raw, "
      "rate=(int)[8000,48000], " "format=S16LE, " "channels=(int)[1,12]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX AAC Audio Decoder",
      "Codec/Decoder/Audio",
      "Decode AAC audio streams", "MStar Semiconductor Inc");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.aac");
}

static void
gst_omx_aac_dec_init (GstOMXAACDec * self)
{
  GstOMXAudioDec *omx_audiodec = GST_OMX_AUDIO_DEC (self);

  omx_audiodec->output_frame = 3;

  self->spf = -1;
  self->aac_sample_rate = 0;
  self->aac_channels = 0;
}

static gboolean
gst_omx_aac_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXAACDec *self = GST_OMX_AAC_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_AACPROFILETYPE aac_param;
  OMX_ERRORTYPE err;
  GstStructure *structure;
  gint rate, channels, mpegaudioversion;
  const gchar *profile_string, *stream_format_string;

  //  get port def
  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingAAC;
  err = gst_omx_port_update_port_definition (port, &port_def);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set AAC format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&aac_param);

  aac_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioAac,
      &aac_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get AAC parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (caps, "AAC Caps: ");

  // structure from front-end
  structure = gst_caps_get_structure (caps, 0);

  gst_omx_aac_adts_header_change (self, structure);

  if (!gst_structure_get_int (structure, "mpegversion", &mpegaudioversion)
      || !gst_structure_get_int (structure, "rate", &rate)
      || (!gst_structure_get_int (structure, "channels", &channels))) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }
//fill base-profile
  if (mpegaudioversion == 1)    //mp3
  {
    GST_ERROR_OBJECT (self, "It is mp3 format! not AAC.");
    return FALSE;
  } else {
    profile_string =
        gst_structure_get_string (structure,
        ((mpegaudioversion == 2) ? "profile" : "base-profile"));

    if (profile_string) {
      if (g_str_equal (profile_string, "main")) {
        aac_param.eAACProfile = OMX_AUDIO_AACObjectMain;
      } else if (g_str_equal (profile_string, "lc")) {
        aac_param.eAACProfile = OMX_AUDIO_AACObjectLC;
      } else if (g_str_equal (profile_string, "ssr")) {
        aac_param.eAACProfile = OMX_AUDIO_AACObjectSSR;
      } else if (g_str_equal (profile_string, "ltp")) {
        aac_param.eAACProfile = OMX_AUDIO_AACObjectLTP;
      } else {
        GST_ERROR_OBJECT (self, "Unsupported profile '%s'", profile_string);
        return FALSE;
      }
    }
  }

//fill stream-format
  stream_format_string = gst_structure_get_string (structure, "stream-format");
  if (stream_format_string) {
    if (g_str_equal (stream_format_string, "raw")) {
      aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatRAW;
    } else if (g_str_equal (stream_format_string, "adts")) {
      if (mpegaudioversion == 2) {
        aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP2ADTS;
      } else {
        aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4ADTS;
      }
    } else if (g_str_equal (stream_format_string, "loas")) {
      aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4LOAS;
    } else if (g_str_equal (stream_format_string, "latm")) {
      aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4LATM;
    } else if (g_str_equal (stream_format_string, "adif")) {
      aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatADIF;
    } else {
      GST_ERROR_OBJECT (self, "Unsupported stream-format '%s'",
          stream_format_string);
      return FALSE;
    }
  }
  // update settings from parser
    aac_param.nChannels = self->aac_channels;

  if (self->aac_sample_rate) {
    aac_param.nSampleRate = self->aac_sample_rate;
  } else {
    aac_param.nSampleRate = rate;
  }

  err =
      gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioAac,
      &aac_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting AA3 parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  return TRUE;
}

static gboolean
gst_omx_aac_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXAACDec *self = GST_OMX_AAC_DEC (dec);
  GstStructure *structure;

  if (self->aac_channels && self->aac_sample_rate) {
    // structure from front-end
    structure = gst_caps_get_structure (caps, 0);
    if (gst_omx_aac_adts_header_change (self, structure)) {
      OMX_AUDIO_PARAM_AACPROFILETYPE aac_param;
      OMX_ERRORTYPE err;

      GST_OMX_INIT_STRUCT (&aac_param);

      aac_param.nPortIndex = port->index;

      err =
          gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioAac,
          &aac_param);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self,
            "Failed to get AAC parameters from component: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        return FALSE;
      }

      aac_param.nChannels = self->aac_channels;
      aac_param.nSampleRate = self->aac_sample_rate;

      err =
          gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioAac,
          &aac_param);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self, "Error setting AA3 parameters: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
      }

      return TRUE;
    }
  }

  return FALSE;
}

static gint
gst_omx_aac_dec_get_samples_per_frame (GstOMXAudioDec * dec, GstOMXPort * port)
{
  //because TEMPO algrithm will generate 1~3X frames size from decoder output(1 frame), total average is 2X frame size
  //gst_omx_audio_dec_loop() function will check two kinds of get_samples_per_frame() return value are described as below:
  //if not -1, it will recalculate frame value which is not correct only in case of 0.5X TEMPO.
  //if -1, always regards output size as one frame.
  //in case of rate 0.5X, this function will return -1 to keep nframe=1 , others will return its real spf
  if (dec->rate == 0.5) {
    return -1;
  } else {
    return GST_OMX_AAC_DEC (dec)->spf;
  }
}
