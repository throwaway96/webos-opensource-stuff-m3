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

#include "gstomxradec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_ra_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_ra_dec_debug_category

/* prototypes */
static gboolean gst_omx_ra_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_ra_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_ra_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_ra_dec_debug_category, "omxradec", 0, \
      "debug category for gst-omx audio decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXRADec, gst_omx_ra_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);


static void
gst_omx_ra_dec_class_init (GstOMXRADecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_ra_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_ra_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_ra_dec_get_samples_per_frame);

  /* COOK */
  audiodec_class->cdata.default_sink_template_caps = "audio/x-pn-realaudio, "
      "raversion=(int)8, " "rate=(int)[8000,48000], " "channels=(int)[1,2] ";

  // output PCM format
  audiodec_class->cdata.default_src_template_caps = "audio/x-raw, " "rate=(int)[8000,48000], " "format=S16LE, " // new in GST1.0, signed, 2bytes, little endian
      "channels=(int)[1,10]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX RA Audio Decoder",
      "Codec/Decoder/Audio",
      "Decode RA audio streams", "MStar Semiconductor Inc");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.ra");
}

static void
gst_omx_ra_dec_init (GstOMXRADec * self)
{
  self->spf = -1;
}

#define GECKO_VERSION               ((1L<<24)|(0L<<16)|(0L<<8)|(3L))
#define GECKO_MC1_VERSION           ((2L<<24)|(0L<<16)|(0L<<8)|(0L))

typedef struct ra8lbr_data_struct
{
  guint32 version;
  guint16 nSamples;
  guint16 nRegions;
  guint32 delay;
  guint16 cplStart;
  guint16 cplQBits;
  guint32 channelMask;
} ra8lbr_data;

guint32
AU_UNI_mc_pop (gint32 x)
{
  guint32 n = 0;
  while (x) {
    ++n;
    x &= (x - 1);               // repeatedly clear rightmost 1-bit
  }
  return n;
}

guint8 *
AU_UNI_mc_unpack (ra8lbr_data * ps_RA8LBR, guint8 * buf, guint32 len)
{
  guint8 *off = buf;
  //ra8lbr_data *ps_RA8LBR = (ra8lbr_data *)pData;
  ps_RA8LBR->version = 0;
  ps_RA8LBR->nSamples = 0;
  ps_RA8LBR->nRegions = 0;
  ps_RA8LBR->delay = 0;
  ps_RA8LBR->cplStart = 0;
  ps_RA8LBR->cplQBits = 0;
  ps_RA8LBR->channelMask = 0;

  if (!buf || !len)
    return 0;

  {
    ps_RA8LBR->version = ((gint32) * off++) << 24;
    ps_RA8LBR->version |= ((gint32) * off++) << 16;
    ps_RA8LBR->version |= ((gint32) * off++) << 8;
    ps_RA8LBR->version |= ((gint32) * off++);
  }

  {
    ps_RA8LBR->nSamples = *off++ << 8;
    ps_RA8LBR->nSamples |= *off++;
  }

  {
    ps_RA8LBR->nRegions = *off++ << 8;
    ps_RA8LBR->nRegions |= *off++;
  }

  if (ps_RA8LBR->version >= GECKO_VERSION) {
    {
      ps_RA8LBR->delay = ((gint32) * off++) << 24;
      ps_RA8LBR->delay |= ((gint32) * off++) << 16;
      ps_RA8LBR->delay |= ((gint32) * off++) << 8;
      ps_RA8LBR->delay |= ((gint32) * off++);
    }

    {
      ps_RA8LBR->cplStart = *off++ << 8;
      ps_RA8LBR->cplStart |= *off++;
    }

    {
      ps_RA8LBR->cplQBits = *off++ << 8;
      ps_RA8LBR->cplQBits |= *off++;
    }
  }

  if (ps_RA8LBR->version == GECKO_MC1_VERSION) {
    {
      ps_RA8LBR->channelMask = ((gint32) * off++) << 24;
      ps_RA8LBR->channelMask |= ((gint32) * off++) << 16;
      ps_RA8LBR->channelMask |= ((gint32) * off++) << 8;
      ps_RA8LBR->channelMask |= ((gint32) * off++);
    }
  }

  return off;
}

gboolean
gst_omx_GetCookParameter (OMX_AUDIO_PARAM_RATYPE * pra_param)
{
  gint32 channelMask, mNumCodecs, TotalNumCh, sampRate;
  guint32 i, j;
  guint32 len;
  guint8 *pOpaqueData = pra_param->pSpecificData;
  guint32 opaqueDataLength = pra_param->nSpecificDataSize;
  ra8lbr_data s_unpackedData[MAX_RA_NUM_CODES];
  mNumCodecs = 0;
  channelMask = 0;
  TotalNumCh = pra_param->nChnnelCounts;        //pAudioStreamInfo->channels;
  sampRate = pra_param->nSamplingRate;  //pAudioStreamInfo->sampleRate;

  /* keep reading opaque data until we have configured all the channels */
  while (AU_UNI_mc_pop (channelMask) < TotalNumCh) {
    if (mNumCodecs > 4) {
      return FALSE;
    }

    len =
        AU_UNI_mc_unpack (&(s_unpackedData[mNumCodecs]), pOpaqueData,
        opaqueDataLength) - pOpaqueData;
    if (opaqueDataLength < len) {
      return FALSE;
    }

    opaqueDataLength -= len;
    pOpaqueData += len;

    if (TotalNumCh == 1) {
      s_unpackedData[mNumCodecs].channelMask = 0x00004;
    } else if (TotalNumCh == 2) {
      s_unpackedData[mNumCodecs].channelMask = 0x00003;
    }

    channelMask |= s_unpackedData[mNumCodecs].channelMask;
    ++mNumCodecs;
  }

  /* make sure that the opaque data claims the number of channels we expect */
  if (AU_UNI_mc_pop (channelMask) != TotalNumCh) {
    GST_ERROR ("SD_NOT_OK");
    return FALSE;
  }

  pra_param->nNumCodecs = mNumCodecs;
  //pCOOKSettings->ChannelMask = channelMask;
  //pCOOKSettings->TotalBytes  = pAudioStreamInfo->blockAlign;
  pra_param->nBitsPerFrame[0] = pra_param->nFrameSize;

  for (i = 0; i < pra_param->nNumCodecs; i++) {
    pra_param->nChannels[i] = 0;

    for (j = 0; j < 32; j++) {
      if (s_unpackedData[i].channelMask & (1 << j)) {
        pra_param->nChannels[i]++;
      }
    }

    pra_param->nNumRegions[i] = s_unpackedData[i].nRegions;
    pra_param->nCouplingStartRegion[i] = s_unpackedData[i].cplStart;
    pra_param->nCouplingQuantBits[i] = s_unpackedData[i].cplQBits;
    pra_param->nSamplePerFrame = s_unpackedData[i].nSamples / pra_param->nChannels[i];  //pCOOKSettings->Samples[i] = s_unpackedData[i].nSamples / pra_param->nChannels[i];
  }

  GST_DEBUG ("CodesNum=0x%04x", (unsigned int) pra_param->nNumCodecs);
  GST_DEBUG ("sampRate=0x%04x", (unsigned int) sampRate);
  GST_DEBUG ("Samples =0x%04x", (unsigned int) pra_param->nSamplePerFrame);     //GST_DEBUG("Samples =0x%04x", (unsigned int) pCOOKSettings->Samples[0]);

  for (i = 0; i < pra_param->nNumCodecs; i++) {
    GST_DEBUG ("Channels[%d] = 0x%02x", i,
        (unsigned int) pra_param->nChannels[i]);
    GST_DEBUG ("Regions[%d]  = 0x%02x", i,
        (unsigned int) pra_param->nNumRegions[i]);
    GST_DEBUG ("cplStart[%d] = 0x%02x", i,
        (unsigned int) pra_param->nCouplingStartRegion[i]);
    GST_DEBUG ("cplQBits[%d] = 0x%02x", i,
        (unsigned int) pra_param->nCouplingQuantBits[i]);
    GST_DEBUG ("FrameBits[%d]= 0x%02x", i,
        (unsigned int) pra_param->nBitsPerFrame[i]);
  }

  return TRUE;
}

static gboolean
gst_omx_ra_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXRADec *self = GST_OMX_RA_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_RATYPE ra_param;
  OMX_ERRORTYPE err;
  GstStructure *structure;
  gint s32rate, s32channels, s32leaf_size, s32PacketSize, s32raversion;
  const GValue *value;
  gboolean ret = TRUE;

  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingRA;
  err = gst_omx_port_update_port_definition (port, &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set RA format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&ra_param);
  ra_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioRa,
      &ra_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get RA parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (caps, " ==== AudioCaps: ====");

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "channels", &s32channels) ||
      !gst_structure_get_int (structure, "rate", &s32rate) ||
      !gst_structure_get_int (structure, "leaf_size", &s32leaf_size) ||
      !gst_structure_get_int (structure, "packet_size", &s32PacketSize) ||
      !gst_structure_get_int (structure, "raversion", &s32raversion)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  if (s32raversion != 8) {
    GST_ERROR_OBJECT (self, "Not support except RA8!");
    return FALSE;
  }

  ra_param.nChnnelCounts = s32channels;
  ra_param.nSamplingRate = s32rate;
  ra_param.nFrameSize = s32leaf_size;
  ra_param.nBlockAlign = s32PacketSize;
  ra_param.nFramesPerBlock = s32PacketSize / s32leaf_size;
  ra_param.eFormat = OMX_AUDIO_RA8;

  GST_DEBUG_OBJECT (self, "%s nChnnelCounts %ld", __FUNCTION__,
      ra_param.nChnnelCounts);
  GST_DEBUG_OBJECT (self, "%s nSamplingRate %ld", __FUNCTION__,
      ra_param.nSamplingRate);
  GST_DEBUG_OBJECT (self, "%s nFrameSize %ld", __FUNCTION__,
      ra_param.nFrameSize);
  GST_DEBUG_OBJECT (self, "%s nBlockAlign %ld", __FUNCTION__,
      ra_param.nBlockAlign);
  GST_DEBUG_OBJECT (self, "%s nFramesPerBlock %ld", __FUNCTION__,
      ra_param.nFramesPerBlock);
  GST_DEBUG_OBJECT (self, "%s raversion %d", __FUNCTION__, s32raversion);

  //get cook parma from codec_data
  value = gst_structure_get_value (structure, "codec_data");
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

      gst_buffer_map (codec_data, &info, GST_MAP_READ);
      GST_MEMDUMP ("Converted data", info.data, info.size);

      ra_param.nSpecificDataSize = info.size;
      ra_param.pSpecificData = info.data;

      gst_omx_GetCookParameter (&ra_param);
    }
  }

  err =
      gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioRa,
      &ra_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting RA parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  return ret;
}

static gboolean
gst_omx_ra_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  return FALSE;
}

static gint
gst_omx_ra_dec_get_samples_per_frame (GstOMXAudioDec * dec, GstOMXPort * port)
{
  //because TEMPO algrithm will generate 1~3X frames size from decoder output(1 frame), total average is 2X frame size
  //gst_omx_audio_dec_loop() function will check two kinds of get_samples_per_frame() return value are described as below:
  //if not -1, it will recalculate frame value which is not correct only in case of 0.5X TEMPO.
  //if -1, always regards output size as one frame.
  //in case of rate 0.5X, this function will return -1 to keep nframe=1 , others will return its real spf
  if (dec->rate == 0.5) {
    return -1;
  } else {
    return GST_OMX_RA_DEC (dec)->spf;
  }
}
