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
 * Foundation, Inc., 51 Franklin Street, Fifth Floors, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstomxdtsdec.h"


#define DTS_CORE_SYNCWORD      0x1fff
#define DTS_CORE_SYNCWORD_SWAP 0xff1f

#define DTS_CORE_SYNCWORD_1    0x7ffe
#define DTS_CORE_SYNCWORD_2    0x8001

#define DTS_CORE_SYNCWORD_1_SWAP    0xfe7f
#define DTS_CORE_SYNCWORD_2_SWAP    0x0180

#define DTS_SUBSTREAM_SYNCWORD_1    0x6458
#define DTS_SUBSTREAM_SYNCWORD_2    0x2025

#define DTS_SUBSTREAM_SYNCWORD_1_SWAP    0x5864
#define DTS_SUBSTREAM_SYNCWORD_2_SWAP    0x2520


#define DTS_SYNCWORD_CORE_24_16BIT      0x007ffe80      /* for data held as array of uint16 cast to uint32* */
#define DTS_SYNCWORD_CORE_16_16BIT      0x80017ffe
#define DTS_SYNCWORD_CORE_14_16BIT      0xe8001fff

#define DTS_SYNCWORD_CORE_24_32BIT      0xfe80007f
#define DTS_SYNCWORD_CORE_16_32BIT      0x7ffe8001
#define DTS_SYNCWORD_CORE_14_32BIT      0x1fffe800

#define DTS_SYNCWORD_CORE_24M_16BIT     0xfe7f0180
#define DTS_SYNCWORD_CORE_16M_16BIT     0xfe7f0180
#define DTS_SYNCWORD_CORE_14M_16BIT     0xff1f00e8

#define DTS_SYNCWORD_CORE_24M_32BIT     0x0108fe7f
#define DTS_SYNCWORD_CORE_16M_32BIT     0x0180fe7f
#define DTS_SYNCWORD_CORE_14M_32BIT     0x00e8ff1f
#define DTS_SYNCWORD_SUBSTREAM_16BIT        0x20256458
#define DTS_SYNCWORD_SUBSTREAM_32BIT        0x64582025
#define DTS_SYNCWORD_SUBSTREAMM_16BIT       0x58642520
#define DTS_SYNCWORD_SUBSTREAMM_32BIT       0x25205864


GST_DEBUG_CATEGORY_STATIC (gst_omx_dts_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_dts_dec_debug_category

/* prototypes */
static gboolean gst_omx_dts_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_dts_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_dts_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);
static GstFlowReturn gst_omx_dts_dec_parse (GstAudioDecoder * dec,
    GstAdapter * adapter, gint * offset, gint * length);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_dts_dec_debug_category, "omxdtsdec", 0, \
      "debug category for gst-omx audio decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXDTSDec, gst_omx_dts_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);


static int
dts_m6_parser (GstOMXDTSDec * self, unsigned char *pBitstream, int es_size,
    int *frame_num)
{
  unsigned char *p;
  unsigned int m1 = 0, m2 = 0;
  unsigned int syncword;
  int flag, i, nframe, rc, fsize;
  unsigned int work_buf[3];
  int index = -1;
  int resize = 0;
  nframe = 0;
  rc = 0;

  while (es_size > 8 && rc == 0) {
    p = pBitstream;
    syncword = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
    if (syncword == DTS_SYNCWORD_CORE_14M_32BIT ||      //0x00e8ff1f
        syncword == DTS_SYNCWORD_CORE_14_32BIT) //0x1fffe800
    {
      unsigned int m, a, b, c;
      int bits = 32;
      unsigned int *b_ptr = work_buf;
      p = (unsigned char *) pBitstream;

      m = 0;
      for (i = 0; i < 5; i++) {
        a = *p++;
        b = *p++;
        c = ((a << 8) | b) & 0x3FFF;
        if (bits <= 14) {
          m = (m << bits) | (c >> (14 - bits));
          *b_ptr++ = m;
          m = c & (0x3FFF >> bits);
          bits = 32 - (14 - bits);
        } else {
          m = (m << 14) | c;
          bits -= 14;
        }
      }
      //*b_ptr++ = m;
      p = (unsigned char *) work_buf;
    } else if (syncword == DTS_SYNCWORD_CORE_14M_16BIT ||       //0xff1f00e8
        syncword == DTS_SYNCWORD_CORE_14_16BIT) //0xe8001fff
    {
      unsigned int m, a, b, c;
      int bits = 32;
      unsigned int *b_ptr = work_buf;
      resize = 1;               // re-calculate frame for 14bit depth
      p = (unsigned char *) pBitstream;
      m = 0;
      for (i = 0; i < 5; i++) {
        a = *p++;
        b = *p++;
        c = ((b << 8) | a) & 0x3FFF;
        if (bits < 14) {
          m = (m << bits) | (c >> (14 - bits));
          *b_ptr++ = m;
          m = c & (0x3FFF >> bits);
          bits = 32 - (14 - bits);
        } else {
          m = (m << 14) | c;
          bits -= 14;
        }
      }
      //*b_ptr++ = m;
      p = (unsigned char *) work_buf;
    }

    syncword = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
    p += 4;
    flag = 0;
    if (syncword == DTS_SYNCWORD_CORE_16M_32BIT)        //0x0180fe7f
    {
      m1 = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
      flag = 1;
    } else if (syncword == DTS_SYNCWORD_CORE_16_16BIT   /*0x80017ffe */
        || syncword == DTS_SYNCWORD_CORE_16M_16BIT /*0xfe7f0180 */ ) {
      m1 = (p[1] << 24) | (p[0] << 16) | (p[3] << 8) | p[2];
      flag = 2;
    } else if (syncword == DTS_SYNCWORD_CORE_16_32BIT)  //0x7ffe8001
    {
      m1 = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
      flag = 3;
    }

    if (flag) {
      fsize = (m1 >> 4) & 0x3FFF;
      fsize++;
      if ((resize) && (es_size % fsize != 0)) {
        fsize = fsize * 16 / 14;
      }

      es_size -= fsize;
      pBitstream = pBitstream + fsize;
      p = pBitstream;
      syncword = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
      p += 4;
      index = -1;

      if (syncword != DTS_SYNCWORD_SUBSTREAMM_32BIT
          && syncword != DTS_SYNCWORD_SUBSTREAMM_16BIT
          && syncword != DTS_SYNCWORD_SUBSTREAM_16BIT
          && syncword != DTS_SYNCWORD_SUBSTREAM_32BIT) {
        nframe++;
        continue;
      }
    }

    flag = 0;
    if (syncword == DTS_SYNCWORD_SUBSTREAMM_32BIT)      //0x25205864
    {
      m1 = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
      m2 = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
      flag = 1;
    } else if (syncword == DTS_SYNCWORD_SUBSTREAMM_16BIT /*0x20256458 */  || syncword == DTS_SYNCWORD_SUBSTREAM_16BIT) {        /*0x58642520 */
      m1 = (p[1] << 24) | (p[0] << 16) | (p[3] << 8) | p[2];
      m2 = (p[5] << 24) | (p[4] << 16) | (p[7] << 8) | p[6];
      flag = 2;
    } else if (syncword == DTS_SYNCWORD_SUBSTREAM_32BIT) {      /*0x64582025 */
      m1 = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
      m2 = (p[7] << 24) | (p[6] << 16) | (p[5] << 8) | p[4];
      flag = 3;
    }

    if (flag) {
      int tmp_idx = -1;

      tmp_idx = p[1];
      tmp_idx = tmp_idx >> 6;

      if ((m1 & 0x200000) == 0) {
        fsize = ((m1 & 0x1fff) << 3) | (m2 >> (32 - 3));
      } else {
        fsize = ((m1 & 0x1ff) << 11) | (m2 >> (32 - 11));
      }
      fsize++;
      es_size -= fsize;
      pBitstream = pBitstream + fsize;

      if (index == -1) {        /* if index == -1, it mean it is new es or have core es, so we will save the index value and nframe++ */
        nframe++;
        index = tmp_idx;
      } else if (index == tmp_idx) {    /* if index != -1, it mean already got the index from substream, so if index is same, mean it is a new frame */
        nframe++;
      }
    } else {
      rc = -1;
    }
  }

  *frame_num = nframe;

  return rc;
}

static int
dts_m6_get_frame_length (GstOMXDTSDec * self, unsigned char *pBitstream,
    int es_size, int *frame_size)
{
  unsigned char *p;
  unsigned int m1 = 0, m2 = 0;
  unsigned int syncword;
  int flag, i, rc, fsize;
  unsigned int work_buf[3];
  int index = -1;
  int resize = 0;
  *frame_size = 0;
  rc = 0;

  if (es_size <= 8) {
    GST_WARNING_OBJECT (self, "es size not enough, es_size=%d", es_size);
    return -1;
  }

  p = pBitstream;
  syncword = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
  if (syncword == DTS_SYNCWORD_CORE_14M_32BIT ||        //0x00e8ff1f
      syncword == DTS_SYNCWORD_CORE_14_32BIT)   //0x1fffe800
  {
    unsigned int m, a, b, c;
    int bits = 32;
    unsigned int *b_ptr = work_buf;

    p = (unsigned char *) pBitstream;

    m = 0;
    for (i = 0; i < 5; i++) {
      a = *p++;
      b = *p++;
      c = ((a << 8) | b) & 0x3FFF;
      if (bits <= 14) {
        m = (m << bits) | (c >> (14 - bits));
        *b_ptr++ = m;
        m = c & (0x3FFF >> bits);
        bits = 32 - (14 - bits);
      } else {
        m = (m << 14) | c;
        bits -= 14;
      }
    }
    //*b_ptr++ = m;
    p = (unsigned char *) work_buf;
  } else if (syncword == DTS_SYNCWORD_CORE_14M_16BIT || //0xff1f00e8
      syncword == DTS_SYNCWORD_CORE_14_16BIT)   //0xe8001fff
  {
    unsigned int m, a, b, c;
    int bits = 32;
    unsigned int *b_ptr = work_buf;
    resize = 1;                 // re-calculate frame for 14bit depth
    p = (unsigned char *) pBitstream;
    m = 0;
    for (i = 0; i < 5; i++) {
      a = *p++;
      b = *p++;
      c = ((b << 8) | a) & 0x3FFF;
      if (bits < 14) {
        m = (m << bits) | (c >> (14 - bits));
        *b_ptr++ = m;
        m = c & (0x3FFF >> bits);
        bits = 32 - (14 - bits);
      } else {
        m = (m << 14) | c;
        bits -= 14;
      }
    }
    //*b_ptr++ = m;
    p = (unsigned char *) work_buf;
  }

  syncword = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
  p += 4;
  flag = 0;
  if (syncword == DTS_SYNCWORD_CORE_16M_32BIT)  //0x0180fe7f
  {
    m1 = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    flag = 1;
  } else if (syncword == DTS_SYNCWORD_CORE_16_16BIT     /*0x80017ffe */
      || syncword == DTS_SYNCWORD_CORE_16M_16BIT /*0xfe7f0180 */ ) {
    m1 = (p[1] << 24) | (p[0] << 16) | (p[3] << 8) | p[2];
    flag = 2;
  } else if (syncword == DTS_SYNCWORD_CORE_16_32BIT)    //0x7ffe8001
  {
    m1 = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
    flag = 3;
  }

  if (flag) {
    fsize = (m1 >> 4) & 0x3FFF;
    fsize++;
    if ((resize) && (es_size % fsize != 0)) {
      fsize = fsize * 16 / 14;
    }
    es_size -= fsize;
    *frame_size += fsize;
    pBitstream = pBitstream + fsize;
    p = pBitstream;
    syncword = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
    p += 4;
    index = -1;

    if (syncword != DTS_SYNCWORD_SUBSTREAMM_32BIT
        && syncword != DTS_SYNCWORD_SUBSTREAMM_16BIT
        && syncword != DTS_SYNCWORD_SUBSTREAM_16BIT
        && syncword != DTS_SYNCWORD_SUBSTREAM_32BIT) {
      return rc;
    }
  }

  flag = 0;
  if (syncword == DTS_SYNCWORD_SUBSTREAMM_32BIT)        //0x25205864
  {
    m1 = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    m2 = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
    flag = 1;
  } else if (syncword == DTS_SYNCWORD_SUBSTREAMM_16BIT /*0x20256458 */  || syncword == DTS_SYNCWORD_SUBSTREAM_16BIT) {  /*0x58642520 */
    m1 = (p[1] << 24) | (p[0] << 16) | (p[3] << 8) | p[2];
    m2 = (p[5] << 24) | (p[4] << 16) | (p[7] << 8) | p[6];
    flag = 2;
  } else if (syncword == DTS_SYNCWORD_SUBSTREAM_32BIT) {        /*0x64582025 */
    m1 = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
    m2 = (p[7] << 24) | (p[6] << 16) | (p[5] << 8) | p[4];
    flag = 3;
  }

  if (flag) {
    int tmp_idx = -1;

    tmp_idx = p[1];
    tmp_idx = tmp_idx >> 6;

    if ((m1 & 0x200000) == 0) {
      fsize = ((m1 & 0x1fff) << 3) | (m2 >> (32 - 3));
    } else {
      fsize = ((m1 & 0x1ff) << 11) | (m2 >> (32 - 11));
    }
    fsize++;
    es_size -= fsize;
    *frame_size += fsize;
    pBitstream = pBitstream + fsize;

    if (index == -1) {          /* if index == -1, it mean it is new es or have core es, so we will save the index value and nframe++ */
//            nframe++;
      index = tmp_idx;
    } else if (index == tmp_idx) {      /* if index != -1, it mean already got the index from substream, so if index is same, mean it is a new frame */
//            nframe++;
    }
  } else {
    rc = -1;
  }

//    *frame_num = nframe;

  return rc;
}

static int
dts_m6_get_multi_frame_length (GstOMXDTSDec * self, unsigned char *pBitstream,
    int es_size, int frame_num, int *multi_frame_size)
{
  unsigned char *p;
  int i, rc, fsize, esize;

  *multi_frame_size = rc = i = 0;
  esize = es_size;
  p = pBitstream;

  do {
    fsize = 0;

    rc = dts_m6_get_frame_length (self, p, esize, &fsize);
    if (rc == -1) {
      GST_WARNING_OBJECT (self, "can not get frame size");
      break;
    }

    p += fsize;
    esize -= fsize;
    *multi_frame_size += fsize;
    i++;

  } while (i < frame_num && rc == 0);

  return rc;
}

static void
gst_omx_dts_dec_class_init (GstOMXDTSDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *audio_decoder_class = GST_AUDIO_DECODER_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_dts_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_dts_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_dts_dec_get_samples_per_frame);

  audiodec_class->cdata.default_sink_template_caps =
      "audio/x-dts;audio/x-dtsh;audio/x-dtsl;audio/x-dtse";

  audio_decoder_class->parse = GST_DEBUG_FUNCPTR (gst_omx_dts_dec_parse);

  // output PCM format
  audiodec_class->cdata.default_src_template_caps = "audio/x-raw, "
      "rate=(int)[8000,96000], " "format=S16LE, " "channels=(int)[1,12]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX DTS Audio Decoder",
      "Codec/Decoder/Audio",
      "Decode DTS audio streams", "MStar Semiconductor Inc");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.dts");
}

static void
gst_omx_dts_dec_init (GstOMXDTSDec * self)
{
  self->spf = -1;
}

static gboolean
gst_omx_dts_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXDTSDec *self = GST_OMX_DTS_DEC (dec);
  GstStructure *structure;
  const gchar *mimetype;
  OMX_ERRORTYPE ret;
  OMX_PARAM_COMPONENTROLETYPE ComponentRole;

  GST_DEBUG_OBJECT (caps, "DTS Caps");

  // structure from front-end
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  GST_OMX_INIT_STRUCT (&ComponentRole);

  GST_OBJECT_LOCK (self);
  if (g_str_equal (mimetype, "audio/x-dtse")) {
    g_stpcpy ((gchar *) ComponentRole.cRole, "audio_decoder.dtslbr");
    ret =
        gst_omx_component_set_parameter (dec->dec,
        OMX_IndexParamStandardComponentRole, &ComponentRole);
  } else if (g_str_equal (mimetype, "audio/x-dtsl")
      || g_str_equal (mimetype, "audio/x-dtsh")) {
    g_stpcpy ((gchar *) ComponentRole.cRole, "audio_decoder.dtsxll");
    ret =
        gst_omx_component_set_parameter (dec->dec,
        OMX_IndexParamStandardComponentRole, &ComponentRole);
  }
  GST_OBJECT_UNLOCK (self);

  if (ret) {
    GST_DEBUG_OBJECT (self, "ret: 0x%08x", ret);
  }

  return TRUE;
}

static gboolean
gst_omx_dts_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  return FALSE;
}

static gint
gst_omx_dts_dec_get_samples_per_frame (GstOMXAudioDec * dec, GstOMXPort * port)
{
  //because TEMPO algrithm will generate 1~3X frames size from decoder output(1 frame), total average is 2X frame size
  //gst_omx_audio_dec_loop() function will check two kinds of get_samples_per_frame() return value are described as below:
  //if not -1, it will recalculate frame value which is not correct only in case of 0.5X TEMPO.
  //if -1, always regards output size as one frame.
  //in case of rate 0.5X, this function will return -1 to keep nframe=1 , others will return its real spf
  if (dec->rate == 0.5) {
    return -1;
  } else {
    return GST_OMX_DTS_DEC (dec)->spf;
  }
}

static GstFlowReturn
gst_omx_dts_dec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length)
{
  GstOMXDTSDec *self = GST_OMX_DTS_DEC (dec);
  GstOMXAudioDec *omx_dec = GST_OMX_AUDIO_DEC (self);
  GstFlowReturn ret = GST_FLOW_EOS;
  const guint8 *data;
  int frameNum = 0;
  int rc = 0;
  gint av = 0;
  gint osize = 0;
  gint outputFrame = 2;

  av = gst_adapter_available (adapter);

  *length = 0;
  *offset = 0;

  if (outputFrame == 1) {
    *length = av;
    return GST_FLOW_OK;
  }

  if (omx_dec->is_eos_received) {
    GST_ERROR_OBJECT (self, "DTS recieve EOS");
    *length = av;
    return GST_FLOW_OK;
  }

  data = gst_adapter_map (adapter, av);

  rc = dts_m6_parser (self, (unsigned char *) data, av, &frameNum);
  //GST_DEBUG_OBJECT (self, "rc=%d, frameNum=%d, av=%d", rc, frameNum, av);
  if (rc == -1) {
    GST_ERROR_OBJECT (self, "Can not parse dts stream");
    gst_adapter_unmap (adapter);
    *offset = av;
    return ret;
  }

  if (frameNum >= outputFrame) {
    rc = dts_m6_get_multi_frame_length (self, (unsigned char *) data, av,
        outputFrame, &osize);
    if (rc == -1) {
      GST_ERROR_OBJECT (self, "Can not get multi frame size");
      gst_adapter_unmap (adapter);
      *offset = av;
      return ret;
    }

    GST_DEBUG_OBJECT (self, "output %d frame size=%d", outputFrame, osize);

    *length = osize;
    ret = GST_FLOW_OK;
  }

  gst_adapter_unmap (adapter);

  return ret;
}
