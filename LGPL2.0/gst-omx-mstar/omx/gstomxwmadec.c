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

#include "gstomxwmadec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_wma_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_wma_dec_debug_category

/* prototypes */
static gboolean gst_omx_wma_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_wma_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_wma_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);
static GstFlowReturn gst_omx_wma_dec_pre_push (GstAudioDecoder * dec,
    GstBuffer ** buffer);
static gboolean gst_omx_wma_dec_flush (GstOMXAudioDec * dec, gboolean hard);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_wma_dec_debug_category, "omxwmadec", 0, \
      "debug category for gst-omx audio decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXWMADec, gst_omx_wma_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);


static void
gst_omx_wma_dec_class_init (GstOMXWMADecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *audio_decoder_class = GST_AUDIO_DECODER_CLASS (klass);
  GstOMXAudioDecClass *omxaudiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  omxaudiodec_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_wma_dec_set_format);
  omxaudiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_wma_dec_is_format_change);
  omxaudiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_wma_dec_get_samples_per_frame);
  omxaudiodec_class->flush = GST_DEBUG_FUNCPTR (gst_omx_wma_dec_flush);

  audio_decoder_class->pre_push = GST_DEBUG_FUNCPTR (gst_omx_wma_dec_pre_push);

  // input wma format
  omxaudiodec_class->cdata.default_sink_template_caps = "audio/x-wma, "
      "wmaversion=(int)[2,4], "
      "rate=(int)[8000,96000], " "channels=(int)[1,6] ";

  // output PCM format
  omxaudiodec_class->cdata.default_src_template_caps = "audio/x-raw, " "rate=(int)[8000,48000], " "format=S16LE, "      // new in GST1.0, signed, 2bytes, little endian
      "channels=(int)[1,2]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX WMA Audio Decoder",
      "Codec/Decoder/Audio",
      "Decode WMA audio streams", "MStar Semiconductor Inc");

  gst_omx_set_default_role (&omxaudiodec_class->cdata, "audio_decoder.wma");
}

static void
gst_omx_wma_dec_init (GstOMXWMADec * self)
{
  self->spf = -1;
}

static gboolean
gst_omx_wma_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXWMADec *self = GST_OMX_WMA_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_WMATYPE wma_param;
  OMX_ERRORTYPE err;
  GstStructure *s;
  gint s32rate, s32channels, s32bitrate, s32wmaversion, s32block_align,
      s32depth;
  const GValue *value;
  gboolean ret = TRUE;
  guint32 u32Encopt;
  guint32 u32ChannelMask;
  gint s32Version;

  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingWMA;
  err = gst_omx_port_update_port_definition (port, &port_def);

  self->wma_pts = GST_CLOCK_TIME_NONE;

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set WMA format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&wma_param);
  wma_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioWma,
      &wma_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting WMA parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }
  //Ex:audio/x-wma, wmaversion=(int)2, bitrate=(int)96024, depth=(int)16, rate=(int)44100, channels=(int)2, block_align=(int)4459, codec_data=(buffer)008800000f00ad450000, word_size=(int)16, codec_id=(int)353
  GST_DEBUG_OBJECT (caps, "Wma Caps:");

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "rate", &s32rate) ||
      !gst_structure_get_int (s, "channels", &s32channels) ||
      !gst_structure_get_int (s, "wmaversion", &s32wmaversion)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  wma_param.nChannels = s32channels;
  wma_param.nSamplingRate = s32rate;
  self->sample_rate = s32rate;

  if (gst_structure_get_int (s, "bitrate", &s32bitrate)) {
    wma_param.nBitRate = s32bitrate;
  }

  if (s32wmaversion == 1) {
    wma_param.eProfile = OMX_AUDIO_WMAProfileL1;
  } else if (s32wmaversion == 2) {
    wma_param.eProfile = OMX_AUDIO_WMAProfileL2;
  } else if (s32wmaversion == 3) {
    wma_param.eProfile = OMX_AUDIO_WMAProfileL3;
  }

  if (gst_structure_get_int (s, "block_align", &s32block_align)) {
    wma_param.nBlockAlign = s32block_align;
    GST_DEBUG_OBJECT (self, "block_align %d", s32block_align);
  } else {
    GST_ERROR ("error !! no block_align exist !");
  }

  if (gst_structure_get_int (s, "depth", &s32depth)) {
    wma_param.nBitsPerSample = s32depth;
    GST_DEBUG_OBJECT (self, "s32depth %d", s32depth);
  }

  value = gst_structure_get_value (s, "codec_data");
  if (!value) {
    GST_ERROR ("error !! no codec_data exist !");
    ret = FALSE;
  } else {
    GstBuffer *codec_data = gst_value_get_buffer (value);

    if (!codec_data) {
      GST_ERROR ("error2 !! no codec_data exist !");
      ret = FALSE;
    } else {
      GstMapInfo info;
      guint8 *pData;

      gst_buffer_map (codec_data, &info, GST_MAP_READ);
      GST_MEMDUMP ("Converted data", info.data, info.size);
      pData = info.data;
      gst_structure_get_int (s, "wmaversion", &s32Version);
      switch (s32Version) {
        case 0x1:
          u32Encopt = pData[2] + (pData[3] << 8);
          //madec->asf_header_length = 35;
          break;
        case 0x2:
          u32Encopt = pData[4] + (pData[5] << 8);
          //madec->asf_header_length = 35;
          break;
        case 0x3:
          u32ChannelMask =
              pData[2] + (pData[3] << 8) + (pData[4] << 16) + (pData[5] << 24);
          u32Encopt = pData[14] + (pData[15] << 8);
          //madec->asf_header_length = 42;
          break;
        default:
          GST_ERROR ("Unsupport WMA format !!");
          ret = FALSE;
          break;
      }
    }
    wma_param.nEncodeOptions = u32Encopt;
    wma_param.nChannelMask = u32ChannelMask;
    GST_DEBUG_OBJECT (self, "Encoptu32Encopt %d ChannelMask %d Encopt 0x%08x",
        u32Encopt, u32ChannelMask, u32Encopt);
  }

  err =
      gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioWma,
      &wma_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting WMA parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  return ret;
}

static gboolean
gst_omx_wma_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  return FALSE;
}

static gint
gst_omx_wma_dec_get_samples_per_frame (GstOMXAudioDec * dec, GstOMXPort * port)
{
  //because TEMPO algrithm will generate 1~3X frames size from decoder output(1 frame), total average is 2X frame size
  //gst_omx_audio_dec_loop() function will check two kinds of get_samples_per_frame() return value are described as below:
  //if not -1, it will recalculate frame value which is not correct only in case of 0.5X TEMPO.
  //if -1, always regards output size as one frame.
  //in case of rate 0.5X, this function will return -1 to keep nframe=1 , others will return its real spf
  if (dec->rate == 0.5 || dec->rate == 2) {
    return -1;
  } else {
    return GST_OMX_WMA_DEC (dec)->spf;
  }
}

static GstFlowReturn
gst_omx_wma_dec_pre_push (GstAudioDecoder * dec, GstBuffer ** buffer)
{
#if  CALC_PTS_FROM_PCM_WMA

  GstOMXWMADec *self = GST_OMX_WMA_DEC (dec);
  GstBuffer *buf = *buffer;

  GstMapInfo minfo;
  GstClockTime duration;

  GST_DEBUG_OBJECT (self,
      "gst_omx_wma_dec_pre_push OUTPUT TS=%" GST_TIME_FORMAT " DUR=%"
      GST_TIME_FORMAT "", GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));


  if (!GST_CLOCK_TIME_IS_VALID (self->wma_pts)) {
    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buf))) {
      self->wma_pts = GST_BUFFER_PTS (buf);
    } else {
      self->wma_pts = -1;
    }
  }

  GST_BUFFER_PTS (buf) = self->wma_pts;

  gst_buffer_map (buf, &minfo, GST_MAP_READ);

  duration = minfo.size;

  //because decoder output is 48000
  if (self->sample_rate == 96000) {
    self->sample_rate = 48000;
  }

  duration /= 4;
  duration *= 1000;
  duration /= self->sample_rate;
  duration *= GST_MSECOND;

  GST_BUFFER_DURATION (buf) = duration;

  GST_DEBUG_OBJECT (self,
      "wma output pts %" GST_TIME_FORMAT " dura %" GST_TIME_FORMAT " Size %d",
      GST_TIME_ARGS (GST_BUFFER_PTS (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), minfo.size);

  self->wma_pts += duration;

  gst_buffer_unmap (buf, &minfo);

#endif

  return GST_FLOW_OK;
}

static gboolean
gst_omx_wma_dec_flush (GstOMXAudioDec * dec, gboolean hard)
{
#if  CALC_PTS_FROM_PCM_WMA

  GstOMXWMADec *self = GST_OMX_WMA_DEC (dec);

  self->wma_pts = GST_CLOCK_TIME_NONE;

#endif

  return TRUE;
}
