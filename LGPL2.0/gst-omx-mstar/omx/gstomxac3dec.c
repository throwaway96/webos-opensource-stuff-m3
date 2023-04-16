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

#include "gstomxac3dec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_ac3_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_ac3_dec_debug_category

/* prototypes */
static gboolean gst_omx_ac3_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_ac3_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_ac3_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);
static GstFlowReturn gst_omx_ac3_dec_parse (GstAudioDecoder * dec,
    GstAdapter * adapter, gint * offset, gint * length);
static GstFlowReturn gst_omx_ac3_dec_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static gboolean gst_omx_ac3_dec_flush (GstOMXAudioDec * self, gboolean hard);
static GstFlowReturn gst_omx_ac3_dec_check_frame (GstOMXAC3Dec * dec,
    GstBuffer * buf);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_ac3_dec_debug_category, "omxac3dec", 0, \
      "debug category for gst-omx audio decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXAC3Dec, gst_omx_ac3_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);


static void
gst_omx_ac3_dec_class_init (GstOMXAC3DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *audio_decoder_class = GST_AUDIO_DECODER_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audio_decoder_class->parse = GST_DEBUG_FUNCPTR (gst_omx_ac3_dec_parse);

  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_ac3_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_ac3_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_ac3_dec_get_samples_per_frame);
  audiodec_class->flush = GST_DEBUG_FUNCPTR (gst_omx_ac3_dec_flush);

// kochien note, need to refine again for eac3 / ac3 private1
  audiodec_class->cdata.default_sink_template_caps =
      "audio/x-ac3;audio/x-eac3;audio/ac3;audio/x-private1-ac3, "
      "framed=(boolean) true, " "rate=(int)[32000,48000], "
      "channels=(int)[1,6], " "alignment=(string) frame";

  // output PCM format
  audiodec_class->cdata.default_src_template_caps = "audio/x-raw, "
      "rate=(int)[8000,48000], " "format=S16LE, " "channels=(int)[1,12]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX AC3 Audio Decoder",
      "Codec/Decoder/Audio",
      "Decode AC3 audio streams", "MStar Semiconductor Inc");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.ac3");
}

static void
gst_omx_ac3_dec_init (GstOMXAC3Dec * self)
{
  GstOMXAudioDec *omx_audiodec = GST_OMX_AUDIO_DEC (self);

  omx_audiodec->output_frame = 3;

  self->chain_func = GST_PAD_CHAINFUNC (GST_AUDIO_DECODER_SINK_PAD (self));
  gst_pad_set_chain_function (GST_AUDIO_DECODER_SINK_PAD (self),
      GST_DEBUG_FUNCPTR (gst_omx_ac3_dec_sink_chain));

  self->spf = -1;
}

static gboolean
gst_omx_ac3_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXAC3Dec *self = GST_OMX_AC3_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (caps, "AC3 Caps");
  //  get port def
  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingAutoDetect;
  err = gst_omx_port_update_port_definition (port, &port_def);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set AC3 format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  self->BlockNumber = 0;

  return TRUE;
}

static gboolean
gst_omx_ac3_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  return FALSE;
}

static gint
gst_omx_ac3_dec_get_samples_per_frame (GstOMXAudioDec * dec, GstOMXPort * port)
{
  return GST_OMX_AC3_DEC (dec)->spf;
}

static GstFlowReturn
gst_omx_ac3_dec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length)
{
  GstOMXAC3Dec *self = GST_OMX_AC3_DEC (dec);
  GstOMXAudioDec *omx_audio_dec = GST_OMX_AUDIO_DEC (dec);
  GstFlowReturn ret = GST_FLOW_EOS;
  gint av = 0;
  const guint8 *data;
  const guchar *tmp1 = NULL;
  guchar tmp2 = 0;
  guint8 BlockNum = 0;

  av = gst_adapter_available (adapter);
  data = gst_adapter_map (adapter, av);

  tmp1 = data + 5;

  /* check AC3+ first */
  tmp2 = *tmp1;
  tmp2 >>= 3;

  if (tmp2 <= 8) {

    BlockNum = 6;
    self->BlockNumber = 0;

    *length = av;
    *offset = 0;
    ret = GST_FLOW_OK;
  } else if (tmp2 > 10 && tmp2 <= 16) {
    guint8 tmp3 = 0;

    /* check stream type and stream ID */
    tmp1 = data + 2;
    tmp2 = *tmp1;
    tmp2 >>= 6;
    if (tmp2 == 0) {
      tmp3 = *tmp1;
      tmp3 <<= 2;
      tmp3 >>= 5;
      if (tmp3 == 0) {
        guint8 u8FsCod = 0;
        guint8 u8FsCod2 = 0;

        tmp1 = data + 4;
        tmp2 = *tmp1;

        u8FsCod = (tmp2 & 0xC0) >> 6;
        u8FsCod2 = (tmp2 & 0x30) >> 4;

        if (u8FsCod == 3) {
          BlockNum = 6;
        } else {
          if (u8FsCod2 == 0) {
            BlockNum = 1;
          } else if (u8FsCod2 == 1) {
            BlockNum = 2;
          } else if (u8FsCod2 == 2) {
            BlockNum = 3;
          } else {
            BlockNum = 6;
          }
        }

        self->BlockNumber += BlockNum;

        if (self->BlockNumber >= 6) {

          self->BlockNumber = 0;

          *length = av;
          *offset = 0;
          ret = GST_FLOW_OK;
        } else {
          *length = 0;
          *offset = 0;
        }
      } else {
        *length = 0;
        *offset = av;
      }
    } else {
      *length = 0;
      *offset = av;
    }
  } else {
    *length = 0;
    *offset = av;
  }

  if (omx_audio_dec->is_eos_received == TRUE) {
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
gst_omx_ac3_dec_check_frame (GstOMXAC3Dec * dec, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_EOS;
  GstMapInfo minfo;
  const guint8 *data;
  const guchar *tmp1 = NULL;
  guchar tmp2 = 0;

  gst_buffer_map (buf, &minfo, GST_MAP_READ);

  data = minfo.data;

  tmp1 = data + 5;

  /* check AC3+ first */
  tmp2 = *tmp1;
  tmp2 >>= 3;

  if (tmp2 <= 8) {
    ret = GST_FLOW_OK;
  } else if (tmp2 > 10 && tmp2 <= 16) {
    guint8 tmp3 = 0;

    /* check stream type and stream ID */
    tmp1 = data + 2;
    tmp2 = *tmp1;
    tmp2 >>= 6;
    if (tmp2 == 0) {
      tmp3 = *tmp1;
      tmp3 <<= 2;
      tmp3 >>= 5;
      if (tmp3 == 0) {
        ret = GST_FLOW_OK;
      }
    }
  }

  gst_buffer_unmap (buf, &minfo);

  return ret;
}

static GstFlowReturn
gst_omx_ac3_dec_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOMXAC3Dec *self = GST_OMX_AC3_DEC (parent);

  GST_AUDIO_DECODER_STREAM_LOCK (self);

  if (gst_omx_ac3_dec_check_frame (self, buf) != GST_FLOW_OK) {
    gst_buffer_unref (buf);

    GST_AUDIO_DECODER_STREAM_UNLOCK (self);

    return ret;
  }

  GST_AUDIO_DECODER_STREAM_UNLOCK (self);

  ret = self->chain_func (pad, parent, buf);

  return ret;
}

static gboolean
gst_omx_ac3_dec_flush (GstOMXAudioDec * self, gboolean hard)
{
  GstOMXAC3Dec *dec = GST_OMX_AC3_DEC (self);

  dec->BlockNumber = 0;

  return TRUE;
}
