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

#include "gstomxmp3dec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_mp3_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_mp3_dec_debug_category

/* prototypes */
static gboolean gst_omx_mp3_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_mp3_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_mp3_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);
static GstFlowReturn gst_omx_mp3_dec_parse (GstAudioDecoder * dec,
    GstAdapter * adapter, gint * offset, gint * length);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_mp3_dec_debug_category, "omxmp3dec", 0, \
      "debug category for gst-omx audio decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXMP3Dec, gst_omx_mp3_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);


typedef enum
{
  MS_OMX_MP3_VERSION_2_5 = 0x0,
  MS_OMX_MP3_VERSION_RESERVED = 0x1,
  MS_OMX_MP3_VERSION_2 = 0x2,
  MS_OMX_MP3_VERSION_1 = 0x3,
  MS_OMX_MP3_VERSION_NOT_DETERMINE_YET = 0xFF,

} MS_OMX_MP3_VERSION;

typedef enum
{
  MS_OMX_MP3_LAYER_RESERVED = 0x0,
  MS_OMX_MP3_LAYER_3 = 0x1,
  MS_OMX_MP3_LAYER_2 = 0x2,
  MS_OMX_MP3_LAYER_1 = 0x3,
  MS_OMX_MP3_LAYER_NOT_DETERMINE_YET = 0xFF,

} MS_OMX_MP3_LAYER;

#define MS_OMX_MP3_INVALID_RATE          0xFFFF
#define MS_OMX_MP3_BIT_RATE_IDX_X_MAX    5
#define MS_OMX_MP3_BIT_RATE_IDX_Y_MAX    16

//fix MP3 wrong bitrate&total time issue while playing V2/V2.5 stream, share same table for V2&V2.5
static guint16
    MS_OMX_MP3_BitRateTable[MS_OMX_MP3_BIT_RATE_IDX_X_MAX]
    [MS_OMX_MP3_BIT_RATE_IDX_Y_MAX] = {
  {MS_OMX_MP3_INVALID_RATE, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, MS_OMX_MP3_INVALID_RATE},        // V1, L1
  {MS_OMX_MP3_INVALID_RATE, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, MS_OMX_MP3_INVALID_RATE},   // V1, L2
  {MS_OMX_MP3_INVALID_RATE, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, MS_OMX_MP3_INVALID_RATE},    // V1, L3
  {MS_OMX_MP3_INVALID_RATE, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, MS_OMX_MP3_INVALID_RATE},   // V2/V2.5, L1
  {MS_OMX_MP3_INVALID_RATE, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, MS_OMX_MP3_INVALID_RATE},        // V2/V2.5, L2/L3
};


#define MS_OMX_MP3_SAMP_RATE_IDX_X_MAX    3
#define MS_OMX_MP3_SAMP_RATE_IDX_Y_MAX    4

static guint16
    MS_OMX_MP3_SampleRateTable[MS_OMX_MP3_SAMP_RATE_IDX_X_MAX]
    [MS_OMX_MP3_SAMP_RATE_IDX_Y_MAX] = {
  {11025, 12000, 8000, MS_OMX_MP3_INVALID_RATE},        // v2.5
  {22050, 24000, 16000, MS_OMX_MP3_INVALID_RATE},       // v2
  {44100, 48000, 32000, MS_OMX_MP3_INVALID_RATE},       // v1
};

static gboolean
gst_omx_mp3_dec_check_frame_size (GstOMXMP3Dec * self, const guint8 * data,
    gsize av)
{
  gboolean ret = FALSE;
  guint16 SyncWord = 0;
  guint8 Version = 0;
  guint8 Layer = 0;
  guint8 BitRateIdx = 0;
  guint8 SampleRateIdx = 0;
  guint8 PaddingFlag = 0;
  guint16 u16SampleRate = MS_OMX_MP3_INVALID_RATE;
  guint32 u32BitRate = MS_OMX_MP3_INVALID_RATE;
  guint32 u32FrameSize = 0;
  guint8 *pTmp2 = NULL;

  if (data == NULL) {
    GST_ERROR_OBJECT (self, "data should not be NULL !!");
    goto EXIT;
  }

  if (av == 0) {
    GST_ERROR_OBJECT (self, "InputSize should not be 0 !!");
    goto EXIT;
  }

  pTmp2 = (guint8 *) data;

  /* Check MP3 Sync Word */
  SyncWord = *pTmp2;
  SyncWord = (SyncWord << 8) | *(pTmp2 + 1);
  SyncWord = (SyncWord & 0xFFE0);
  if (SyncWord != 0xFFE0) {
    GST_ERROR_OBJECT (self, "Invalid SyncWord (0x%04X) !!", SyncWord);
    goto EXIT;
  }

  /* Get MP3 Version */
  Version = *(pTmp2 + 1);
  Version <<= 3;
  Version >>= 6;
  if (Version > MS_OMX_MP3_VERSION_1) {
    GST_ERROR_OBJECT (self, "Invalid Version (0x%02X) !!", Version);
    goto EXIT;
  }

  /* Get MP3 Layer */
  Layer = *(pTmp2 + 1);
  Layer <<= 5;
  Layer >>= 6;

  /* Get MP3 Bit Rate */
  BitRateIdx = *(pTmp2 + 2);
  BitRateIdx >>= 4;
  if (BitRateIdx >= MS_OMX_MP3_BIT_RATE_IDX_Y_MAX) {
    GST_ERROR_OBJECT (self, "Invalid BitRateIdx (0x%02X) !!", BitRateIdx);
    goto EXIT;
  }
  if ((Version == MS_OMX_MP3_VERSION_1) && (Layer == MS_OMX_MP3_LAYER_1))       // V1 L1
  {
    u32BitRate = (OMX_U32) MS_OMX_MP3_BitRateTable[0][BitRateIdx];
  } else if ((Version == MS_OMX_MP3_VERSION_1) && (Layer == MS_OMX_MP3_LAYER_2))        // V1 L2
  {
    u32BitRate = (OMX_U32) MS_OMX_MP3_BitRateTable[1][BitRateIdx];
  } else if ((Version == MS_OMX_MP3_VERSION_1) && (Layer == MS_OMX_MP3_LAYER_3))        // V1 L3
  {
    u32BitRate = (OMX_U32) MS_OMX_MP3_BitRateTable[2][BitRateIdx];
  }
  //fix MP3 wrong bitrate&total time issue while playing V2/V2.5 stream, share same table for V2&V2.5
  else if (((Version == MS_OMX_MP3_VERSION_2) || (Version == MS_OMX_MP3_VERSION_2_5)) && (Layer == MS_OMX_MP3_LAYER_1)) // V2/V2.5 L1
  {
    u32BitRate = (OMX_U32) MS_OMX_MP3_BitRateTable[3][BitRateIdx];
  } else if (((Version == MS_OMX_MP3_VERSION_2) || (Version == MS_OMX_MP3_VERSION_2_5)) && ((Layer == MS_OMX_MP3_LAYER_2) || (Layer == MS_OMX_MP3_LAYER_3)))    // V2/V2.5 L2/L3
  {
    u32BitRate = (OMX_U32) MS_OMX_MP3_BitRateTable[4][BitRateIdx];
  } else {
    GST_ERROR_OBJECT (self, "Invalid version or layer to get bit rate !!");
    goto EXIT;
  }

  /* Get MP3 Sample Rate */
  SampleRateIdx = *(pTmp2 + 2);
  SampleRateIdx <<= 4;
  SampleRateIdx >>= 6;
  if (SampleRateIdx >= MS_OMX_MP3_SAMP_RATE_IDX_Y_MAX) {
    GST_ERROR_OBJECT (self, "Invalid SampleRateIdx (0x%02X) !!", SampleRateIdx);
    goto EXIT;
  }
  if (Version == MS_OMX_MP3_VERSION_2_5) {      // v2.5
    u16SampleRate = MS_OMX_MP3_SampleRateTable[0][SampleRateIdx];
  } else if (Version == MS_OMX_MP3_VERSION_2) { // v2
    u16SampleRate = MS_OMX_MP3_SampleRateTable[1][SampleRateIdx];
  } else if (Version == MS_OMX_MP3_VERSION_1) { // v1
    u16SampleRate = MS_OMX_MP3_SampleRateTable[2][SampleRateIdx];
  } else {
    GST_ERROR_OBJECT (self, "Invalid Version (0x%02X) !!", Version);
    goto EXIT;
  }

  /* Get MP3 Pad Bit */
  PaddingFlag = *(pTmp2 + 2);
  PaddingFlag = (PaddingFlag & 0x02) ? 1 : 0;

  if ((u16SampleRate == MS_OMX_MP3_INVALID_RATE)
      || (u32BitRate == MS_OMX_MP3_INVALID_RATE)) {
    GST_ERROR_OBJECT (self, "Invalid sample rate (%u) or bit rate (%u) !!",
        u16SampleRate, u32BitRate);
    goto EXIT;
  } else {
    // Samples Per Frame: ref:http://www.codeproject.com/KB/audio-video/mpegaudioinfo.aspx
    // Frame Size = ( (Samples Per Frame / 8 * Bitrate) / Sampling Rate) + Padding Size
    //             | MPEG 1 | MPEG 2 (LSF) | MPEG 2.5 (LSF)
    // Layer I     |   384  |     384      |     384
    // Layer II    |  1152  |    1152      |    1152
    // Layer III   |  1152  |     576      |     576
    if (Layer == MS_OMX_MP3_LAYER_1) {
      u32FrameSize = (((48 * u32BitRate) * 1000) / u16SampleRate);
      u32FrameSize = ((u32FrameSize >> 2) << 2);
      //In Layer I, a slot is always 4 byte long, in all other the layers a slot is 1 byte long.
      if (PaddingFlag) {
        u32FrameSize += 4;
      }
    } else if ((Layer == MS_OMX_MP3_LAYER_2)
        || ((Version == MS_OMX_MP3_VERSION_1)
            && (Layer == MS_OMX_MP3_LAYER_3))) {
      u32FrameSize = ((144 * u32BitRate * 1000) / u16SampleRate) + PaddingFlag;
    } else {
      u32FrameSize = ((72 * u32BitRate * 1000) / u16SampleRate) + PaddingFlag;
    }
  }

  if (u32FrameSize == av) {
    ret = TRUE;
  } else {
    GST_ERROR_OBJECT (self, "FrameSize (%u) different with InputSize (%u) !!",
        u32FrameSize, av);
  }

EXIT:

  return ret;
}

static void
gst_omx_mp3_dec_class_init (GstOMXMP3DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *audio_decoder_class = GST_AUDIO_DECODER_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audio_decoder_class->parse = GST_DEBUG_FUNCPTR (gst_omx_mp3_dec_parse);

  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_mp3_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_mp3_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_mp3_dec_get_samples_per_frame);

  // input mp3(ES) format
  audiodec_class->cdata.default_sink_template_caps = "audio/mpeg, "
      "mpegversion=(int)1, "
      "layer=(int)3, "
      "mpegaudioversion=(int)[1,3], "
      "rate=(int)[8000,48000], "
      "channels=(int)[1,2], " "parsed=(boolean) true";


  // output PCM format
  audiodec_class->cdata.default_src_template_caps = "audio/x-raw, " "rate=(int)[8000,48000], " "format=S16LE, " // new in GST1.0, signed, 2bytes, little endian
      "channels=(int)[1,10]";


  gst_element_class_set_static_metadata (element_class,
      "OpenMAX MP3 Audio Decoder",
      "Codec/Decoder/Audio",
      "Decode MP3 audio streams", "MStar Semiconductor Inc");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.mp3");
}

static void
gst_omx_mp3_dec_init (GstOMXMP3Dec * self)
{
  self->spf = -1;
}


static gboolean
gst_omx_mp3_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXMP3Dec *self = GST_OMX_MP3_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_MP3TYPE mp3_param;
  OMX_ERRORTYPE err;
  GstStructure *structure;
  gint rate, channels, layer, mpegaudioversion, mpegversion;

  // port definition
  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingMP3;
  err = gst_omx_port_update_port_definition (port, &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set MP3 format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  // get caps from parser
  // necessary caps for mpeg:
  // mpegversion,  rate,   channels, mpegaudioversion
  GST_DEBUG_OBJECT (self, " ==== AudioCaps: %" GST_PTR_FORMAT " ==== ", caps);
  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "mpegversion", &mpegversion) ||
      !gst_structure_get_int (structure, "mpegaudioversion", &mpegaudioversion)
      || !gst_structure_get_int (structure, "layer", &layer)
      || !gst_structure_get_int (structure, "rate", &rate)
      || !gst_structure_get_int (structure, "channels", &channels)) {

    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  if (mpegversion != 1) {
    GST_ERROR_OBJECT (self, "mpeg version incorrect %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }
  self->spf = (mpegaudioversion == 1 ? 1152 : 576);


  GST_OMX_INIT_STRUCT (&mp3_param);
  mp3_param.nPortIndex = port->index;
  err = gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioMp3, &mp3_param); // get componebt default setting
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get MP3 parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  mp3_param.nChannels = channels;
  mp3_param.nSampleRate = rate;

  if (mpegaudioversion == 1) {
    mp3_param.eFormat = OMX_AUDIO_MP3StreamFormatMP1Layer3;
  } else if (mpegaudioversion == 2) {
    mp3_param.eFormat = OMX_AUDIO_MP3StreamFormatMP2Layer3;
  } else {
    mp3_param.eFormat = OMX_AUDIO_MP3StreamFormatMP2_5Layer3;
  }

  err =
      gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioMp3,
      &mp3_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting MP3 parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_mp3_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXMP3Dec *self = GST_OMX_MP3_DEC (dec);
  OMX_AUDIO_PARAM_MP3TYPE mp3_param;
  OMX_ERRORTYPE err;
  GstStructure *s;
  gint rate, channels, layer, mpegaudioversion;

  GST_OMX_INIT_STRUCT (&mp3_param);
  mp3_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioMp3,
      &mp3_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get MP3 parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "mpegaudioversion", &mpegaudioversion) ||
      !gst_structure_get_int (s, "layer", &layer) ||
      !gst_structure_get_int (s, "rate", &rate) ||
      !gst_structure_get_int (s, "channels", &channels)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  if (mp3_param.nChannels != channels)
    return TRUE;

  if (mp3_param.nSampleRate != rate)
    return TRUE;

  if (mpegaudioversion == 1
      && (mp3_param.eFormat != OMX_AUDIO_MP3StreamFormatMP1Layer3)) {
    return TRUE;
  }
  if (mpegaudioversion == 2
      && (mp3_param.eFormat != OMX_AUDIO_MP3StreamFormatMP2Layer3)) {
    return TRUE;
  }

  if (mpegaudioversion == 3
      && (mp3_param.eFormat != OMX_AUDIO_MP3StreamFormatMP2_5Layer3)) {
    return TRUE;
  }

  return FALSE;
}

static gint
gst_omx_mp3_dec_get_samples_per_frame (GstOMXAudioDec * dec, GstOMXPort * port)
{
  //because TEMPO algrithm will generate 1~3X frames size from decoder output(1 frame), total average is 2X frame size
  //gst_omx_audio_dec_loop() function will check two kinds of get_samples_per_frame() return value are described as below:
  //if not -1, it will recalculate frame value which is not correct only in case of 0.5X TEMPO.
  //if -1, always regards output size as one frame.
  //in case of rate 0.5X, this function will return -1 to keep nframe=1 , others will return its real spf
  if (dec->rate == 0.5) {
    return -1;
  } else {
    return GST_OMX_MP3_DEC (dec)->spf;
  }
}

static GstFlowReturn
gst_omx_mp3_dec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length)
{
  GstOMXMP3Dec *self = GST_OMX_MP3_DEC (dec);
  GstFlowReturn ret = GST_FLOW_EOS;
  const guint8 *data;
  gsize av = 0;
  gboolean res;

  av = gst_adapter_available (adapter);
  data = gst_adapter_map (adapter, av);

  res = gst_omx_mp3_dec_check_frame_size (self, data, av);
  if (res == TRUE) {
    *length = av;
    *offset = 0;
    ret = GST_FLOW_OK;
  } else {
    *length = 0;
    *offset = av;
  }

  gst_adapter_unmap (adapter);

  return ret;
}
