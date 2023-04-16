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

#include "gstomxamrdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_amr_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_amr_dec_debug_category

/* prototypes */
static gboolean gst_omx_amr_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_amr_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_amr_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_amr_dec_debug_category, "omxamrdec", 0, \
      "debug category for gst-omx audio decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXAMRDec, gst_omx_amr_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);


static void
gst_omx_amr_dec_class_init (GstOMXAMRDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_amr_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_amr_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_amr_dec_get_samples_per_frame);


  // input amr(ES) format
  audiodec_class->cdata.default_sink_template_caps = "audio/AMR;audio/AMR-WB, "
      "rate=(int)[8000,16000], " "channels=(int)[1,2] ";

  // output PCM format
  audiodec_class->cdata.default_src_template_caps = "audio/x-raw, " "rate=(int)[8000,48000], " "format=S16LE, " // new in GST1.0, signed, 2bytes, little endian
      "channels=(int)[1,10]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX AMR Audio Decoder",
      "Codec/Decoder/Audio",
      "Decode AMR audio streams", "MStar Semiconductor Inc");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.amrnb");
}

static void
gst_omx_amr_dec_init (GstOMXAMRDec * self)
{
  self->spf = -1;
}


static gboolean
gst_omx_amr_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXAMRDec *self = GST_OMX_AMR_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_AMRTYPE amr_param;
  OMX_ERRORTYPE err;
  GstStructure *structure;
  gint rate, channels;

  // port definition
  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingAMR;
  err = gst_omx_port_update_port_definition (port, &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set AMR format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }
  // get caps from parser
  // necessary caps for mpeg:
  // mpegversion,  rate,   channels, mpegaudioversion
  GST_DEBUG_OBJECT (caps, " ==== AudioCaps: ==== ");
  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "rate", &rate) ||
      !gst_structure_get_int (structure, "channels", &channels)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&amr_param);
  amr_param.nPortIndex = port->index;
  err = gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioAmr, &amr_param); // get componebt default setting
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get AMR parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  amr_param.nChannels = channels;
  amr_param.nBitRate = rate;
  if (amr_param.nBitRate == 8000) {
    amr_param.eAMRBandMode = OMX_AUDIO_AMRBandModeNB0;
  } else {
    amr_param.eAMRBandMode = OMX_AUDIO_AMRBandModeWB0;
  }

  err =
      gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioAmr,
      &amr_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting AMR parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_amr_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  return FALSE;
}

static gint
gst_omx_amr_dec_get_samples_per_frame (GstOMXAudioDec * dec, GstOMXPort * port)
{
  //because TEMPO algrithm will generate 1~3X frames size from decoder output(1 frame), total average is 2X frame size
  //gst_omx_audio_dec_loop() function will check two kinds of get_samples_per_frame() return value are described as below:
  //if not -1, it will recalculate frame value which is not correct only in case of 0.5X TEMPO.
  //if -1, always regards output size as one frame.
  //in case of rate 0.5X, this function will return -1 to keep nframe=1 , others will return its real spf
  if (dec->rate == 0.5) {
    return -1;
  } else {
    return GST_OMX_AMR_DEC (dec)->spf;
  }
}
