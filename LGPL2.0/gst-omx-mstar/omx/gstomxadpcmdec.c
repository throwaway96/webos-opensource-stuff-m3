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

#include "gstomxadpcmdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_adpcm_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_adpcm_dec_debug_category

/* prototypes */
static gboolean gst_omx_adpcm_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_adpcm_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_adpcm_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);
static GstFlowReturn gst_omx_adpcm_dec_parse (GstAudioDecoder * dec,
    GstAdapter * adapter, gint * offset, gint * length);
static GstFlowReturn gst_omx_adpcm_dec_pre_push (GstAudioDecoder * dec,
    GstBuffer ** buffer);
static gboolean gst_omx_adpcm_dec_flush (GstOMXAudioDec * dec, gboolean hard);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_adpcm_dec_debug_category, "omxadpcmdec", 0, \
      "debug category for gst-omx audio decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXADPCMDec, gst_omx_adpcm_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);


static void
gst_omx_adpcm_dec_class_init (GstOMXADPCMDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *audio_decoder_class = GST_AUDIO_DECODER_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audio_decoder_class->parse = GST_DEBUG_FUNCPTR (gst_omx_adpcm_dec_parse);
  audio_decoder_class->pre_push =
      GST_DEBUG_FUNCPTR (gst_omx_adpcm_dec_pre_push);

  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_adpcm_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_adpcm_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_adpcm_dec_get_samples_per_frame);
  audiodec_class->flush = GST_DEBUG_FUNCPTR (gst_omx_adpcm_dec_flush);

  audiodec_class->cdata.default_sink_template_caps =
      "audio/x-adpcm;audio/x-mulaw;audio/x-alaw, " "rate = (int)[ 1, 48000 ], "
      "channels = (int)[1,2]";

  // output PCM format
  audiodec_class->cdata.default_src_template_caps = "audio/x-raw, " "rate=(int)[8000,48000], " "format=S16LE, " // new in GST1.0, signed, 2bytes, little endian
      "channels=(int)[1,10]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX ADPCM Audio Decoder",
      "Codec/Decoder/Audio",
      "Decode ADPCM audio streams", "MStar Semiconductor Inc");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.adpcm");
}

static void
gst_omx_adpcm_dec_init (GstOMXADPCMDec * self)
{
  self->block_align = 1;        /* default is 1, because quicktime no this information */
  self->spf = -1;
}

static gboolean
gst_omx_adpcm_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXADPCMDec *self = GST_OMX_ADPCM_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_ADPCMTYPE adpcm_param;
  OMX_ERRORTYPE err;
  GstStructure *structure;
  const gchar *mimetype;
  const gchar *layout;
  gint channels, rate, s32BlockAlign;

  //  get port def
  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingADPCM;
  err = gst_omx_port_update_port_definition (port, &port_def);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set ADPCM format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  self->adpcm_pts = GST_CLOCK_TIME_NONE;

  GST_OMX_INIT_STRUCT (&adpcm_param);

  adpcm_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioAdpcm,
      &adpcm_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get ADPCM parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (caps, " ==== AudioCaps: ==== ");

  // structure from front-end
  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &rate)
      || (!gst_structure_get_int (structure, "channels", &channels))) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  if (gst_structure_get_int (structure, "block_align", &s32BlockAlign)) {
    adpcm_param.nBlockSize = s32BlockAlign;
    self->block_align = s32BlockAlign;
  }

  mimetype = gst_structure_get_name (structure);
  layout = gst_structure_get_string (structure, "layout");

  // update settings from parser
  adpcm_param.nChannels = channels;
  adpcm_param.nSampleRate = rate;
  self->sample_rate = rate;
  if (g_str_equal (mimetype, "audio/x-adpcm")) {
    if (g_str_equal (layout, "microsoft")) {
      adpcm_param.eType = OMX_AUDIO_ADPCMTypeMS;
    } else if (g_str_equal (layout, "dvi")) {
      adpcm_param.eType = OMX_AUDIO_ADPCMTypeDVI;
    } else if (g_str_equal (layout, "quicktime")) {
      adpcm_param.eType = OMX_AUDIO_ADPCMTypeAPPLE;
      self->block_align = 1;    //the default value 1 means keeping availd es data from dexmuxer. no need to slice according block_align iin caps.
    }
  } else if (g_str_equal (mimetype, "audio/x-mulaw")) {
    adpcm_param.eType = OMX_AUDIO_ADPCMPTypeULAW;
  } else if (g_str_equal (mimetype, "audio/x-alaw")) {
    adpcm_param.eType = OMX_AUDIO_ADPCMPTypeALAW;
  }

  err =
      gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioAdpcm,
      &adpcm_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting ADPCM parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  return TRUE;
}

static gboolean
gst_omx_adpcm_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  return FALSE;
}

static gint
gst_omx_adpcm_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port)
{
  //because TEMPO algrithm will generate 1~3X frames size from decoder output(1 frame), total average is 2X frame size
  //gst_omx_audio_dec_loop() function will check two kinds of get_samples_per_frame() return value are described as below:
  //if not -1, it will recalculate frame value which is not correct only in case of 0.5X TEMPO.
  //if -1, always regards output size as one frame.
  //in case of rate 0.5X, this function will return -1 to keep nframe=1 , others will return its real spf
  if (dec->rate == 0.5) {
    return -1;
  } else {
    return GST_OMX_ADPCM_DEC (dec)->spf;
  }
}

static GstFlowReturn
gst_omx_adpcm_dec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length)
{
  GstOMXADPCMDec *self = GST_OMX_ADPCM_DEC (dec);
  GstOMXAudioDec *omx_audio_dec = GST_OMX_AUDIO_DEC (dec);
  GstFlowReturn ret = GST_FLOW_EOS;
  gint av = 0;

  av = gst_adapter_available (adapter);

  if (av >= self->block_align) {
    if (self->block_align == 1) {
      *length = av;             /* if block_align == 1, mean it is quicktime, no block_align, so length set to av */
    } else {
      *length = self->block_align;
    }
    *offset = 0;
    ret = GST_FLOW_OK;
  } else {
    *offset = 0;
    *length = 0;
    ret = GST_FLOW_EOS;
  }
  if (omx_audio_dec->is_eos_received == TRUE) {
    GST_DEBUG_OBJECT (self, "flush data after eos received av %d", av);
    //this case of adaptor has data which is not enough as a frame(block_align)
    //When received eos event, pass the data into OMX_IL to handle eos event
    *offset = 0;
    *length = av;
    ret = GST_FLOW_OK;
  }

  gst_adapter_unmap (adapter);

  return ret;
}

static GstFlowReturn
gst_omx_adpcm_dec_pre_push (GstAudioDecoder * dec, GstBuffer ** buffer)
{
  GstOMXADPCMDec *self = GST_OMX_ADPCM_DEC (dec);
  GstBuffer *buf = *buffer;
  GstMapInfo minfo;
  GstClockTime duration;

#if  CALC_PTS_FROM_PCM

  if (!GST_CLOCK_TIME_IS_VALID (self->adpcm_pts)) {
    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buf))) {
      self->adpcm_pts = GST_BUFFER_PTS (buf);
    } else {
      self->adpcm_pts = 0;
    }
  }

  GST_BUFFER_PTS (buf) = self->adpcm_pts;

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
      "adpcm output pts %" GST_TIME_FORMAT " dura %" GST_TIME_FORMAT " Size %d",
      GST_TIME_ARGS (GST_BUFFER_PTS (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), minfo.size);

  self->adpcm_pts += duration;

  gst_buffer_unmap (buf, &minfo);

#endif

  return GST_FLOW_OK;
}

static gboolean
gst_omx_adpcm_dec_flush (GstOMXAudioDec * dec, gboolean hard)
{
#if  CALC_PTS_FROM_PCM

  GstOMXADPCMDec *self = GST_OMX_ADPCM_DEC (dec);

  self->adpcm_pts = GST_CLOCK_TIME_NONE;

#endif

  return TRUE;
}
