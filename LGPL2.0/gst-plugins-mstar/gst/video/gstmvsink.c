////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2008-2014 MStar Semiconductor, Inc.
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

///////////////////////////////////////////////////////////////////////////////////////////////////
///
/// file    mvsink.c
/// @brief  mstar video sink
/// @author MStar Semiconductor Inc.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "gstmvsink.h"

#include "msil_version.h"

#define GST_OMX_VSYNC_BRIDGE_FRAME_ADV 0
#define GST_OMX_VSYNC_BRIDGE_DROP_FRAME


typedef struct
{
  GstBuffer *WMV3D_buf[MAX_FRAME_BUFF_SIZE];
  volatile int buf_state[MAX_FRAME_BUFF_SIZE];
  int buf_w_index;
  gboolean flushing;
} WMV3D_Struct;

#define TEMP_DISABLE_DUAL_3D
#define UHD_Width_Bound  4000
#define UHD_Height_Bound 2000

static WMV3D_Struct WMV3D[2];
static int WMV3D_W_Index;
static int WMV3D_R_Index;
#include "pthread.h"
static pthread_mutex_t m_Lock = PTHREAD_MUTEX_INITIALIZER;

extern void msil_SC_ForceFreerun (MZ_BOOL bEnable);
extern void msil_PNL_OverDriver_Enable (MZ_BOOL bEnable);
extern MZ_U32 MsOS_GetSystemTime (void);

#define HERE                    //g_print("func:%d:%s:%d\n", MsOS_GetSystemTime(), __FUNCTION__, __LINE__)

enum
{
  ARG_0,
  PROP_INPUT,
  PROP_DISP_X,
  PROP_DISP_Y,
  PROP_DISP_W,
  PROP_DISP_H,
  PROP_AVSYNC_TOLERANCE,
  PROP_TVMODE,
  PROP_PLANE,
  PROP_RECTANGLE,
  PROP_FLUSH_REPEAT_FRAME,
  PROP_CURRENT_PTS,
  PROP_INTER_FRAME_DELAY,
  PROP_SLOW_MODE_RATE,
  PROP_CONTENT_FRAMERATE,
  PROP_STEPFRAME,
  PROP_MUTE,
  PROP_THUMBNAIL_MODE,
  PROP_WINDOW_ID,
  PROP_SEAMLESS_PLAY,
  PROP_TWO_STREAM_3D,
  PROP_TWO_STREAM_3D_LR_MODE,
  PROP_APP_TYPE,
  PROP_LIPSYNC_OFFSET,
  PROP_LOW_DELAY,
  PROP_CONSTANT_DELAY,
  PROP_VSINK_ASYNC_HANDLING,
  PROP_VSINK_SYNC_ON_CLOCK,
  PROP_INTERLEAVING_TYPE,
  PROP_RESOURCE_INFO,
  PROP_RENDER_DELAY,
};

typedef enum ASF_MEDIA_3D_TYPES
{
  GST_ASF_3D_NONE = 0x00,

  //added, interim format - half
  GST_ASF_3D_SIDE_BY_SIDE_HALF = 0x01,
  GST_ASF_3D_SIDE_BY_SIDE_HALF_LR = GST_ASF_3D_SIDE_BY_SIDE_HALF,

  GST_ASF_3D_SIDE_BY_SIDE_HALF_RL = 0x02,
  GST_ASF_3D_TOP_AND_BOTTOM_HALF = 0x03,
  GST_ASF_3D_BOTTOM_AND_TOP_HALF = 0x04,
  GST_ASF_3D_CHECK_BOARD = 0x05,                    /**< for T/B, S/S, Checker, Frame Seq*/
  GST_ASF_3D_FRAME_SEQUENTIAL = 0x06,          /**< for T/B, S/S, Checker, Frame Seq*/
  GST_ASF_3D_COLUMN_INTERLEAVE = 0x07,        /**< for H.264*/

  //added, Full format
  GST_ASF_3D_SIDE_BY_SIDE_LR = 0x08,
  GST_ASF_3D_SIDE_BY_SIDE_RL = 0x09,
  GST_ASF_3D_FRAME_PACKING = 0x0A,               /**< Full format*/
  GST_ASF_3D_FIELD_ALTERNATIVE = 0x0B,        /**< Full format*/
  GST_ASF_3D_LINE_ALTERNATIVE = 0x0C,          /**< Full format*/
  GST_ASF_3D_DUAL_STREAM = 0x0D,                  /**< Full format*/
  GST_ASF_3D_2DTO3D = 0x0E,                             /**< Full format*/
} ASF_MEDIA_3D_TYPES_T;

typedef enum MEDIA_3D_TYPES
{
  MEDIA_3D_NONE = 0x00,

  //added, interim format - half
  MEDIA_3D_SIDE_BY_SIDE_HALF = 0x01,
  MEDIA_3D_SIDE_BY_SIDE_HALF_LR = MEDIA_3D_SIDE_BY_SIDE_HALF,

  MEDIA_3D_SIDE_BY_SIDE_HALF_RL = 0x02,
  MEDIA_3D_TOP_AND_BOTTOM_HALF = 0x03,
  MEDIA_3D_BOTTOM_AND_TOP_HALF = 0x04,
  MEDIA_3D_CHECK_BOARD = 0x05,                    /**< for T/B, S/S, Checker, Frame Seq*/
  MEDIA_3D_FRAME_SEQUENTIAL = 0x06,               /**< for T/B, S/S, Checker, Frame Seq*/
  MEDIA_3D_COLUMN_INTERLEAVE = 0x07,              /**< for H.264*/

  //added, Full format
  MEDIA_3D_SIDE_BY_SIDE_LR = 0x08,
  MEDIA_3D_SIDE_BY_SIDE_RL = 0x09,
  MEDIA_3D_FRAME_PACKING = 0x0A,                  /**< Full format*/
  MEDIA_3D_FIELD_ALTERNATIVE = 0x0B,              /**< Full format*/
  MEDIA_3D_LINE_ALTERNATIVE = 0x0C,               /**< Full format*/
  MEDIA_3D_DUAL_STREAM = 0x0D,                    /**< Full format*/
  MEDIA_3D_2DTO3D = 0x0E,                         /**< Full format*/
  MEDIA_3D_MAX
} MEDIA_3D_TYPES_T;

static MEDIA_3D_TYPES_T
map_asf3Dtype_to_media3Dtype (GSTMVSink * mvsink,
    ASF_MEDIA_3D_TYPES_T asf3DType)
{
  switch ((guint) asf3DType) {
    case GST_ASF_3D_SIDE_BY_SIDE_HALF:
      return MEDIA_3D_SIDE_BY_SIDE_HALF;
    case GST_ASF_3D_SIDE_BY_SIDE_HALF_RL:
      return MEDIA_3D_SIDE_BY_SIDE_HALF_RL;
    case GST_ASF_3D_TOP_AND_BOTTOM_HALF:
      return MEDIA_3D_TOP_AND_BOTTOM_HALF;
    case GST_ASF_3D_BOTTOM_AND_TOP_HALF:
      return MEDIA_3D_BOTTOM_AND_TOP_HALF;
    case GST_ASF_3D_CHECK_BOARD:
      return MEDIA_3D_CHECK_BOARD;
    case GST_ASF_3D_FRAME_SEQUENTIAL:
      return MEDIA_3D_FRAME_SEQUENTIAL;
    case GST_ASF_3D_COLUMN_INTERLEAVE:
      return MEDIA_3D_COLUMN_INTERLEAVE;
    case GST_ASF_3D_SIDE_BY_SIDE_LR:
      return MEDIA_3D_SIDE_BY_SIDE_LR;
    case GST_ASF_3D_SIDE_BY_SIDE_RL:
      return MEDIA_3D_SIDE_BY_SIDE_RL;
    case GST_ASF_3D_FRAME_PACKING:
      return MEDIA_3D_FRAME_PACKING;
    case GST_ASF_3D_FIELD_ALTERNATIVE:
      return MEDIA_3D_FIELD_ALTERNATIVE;
    case GST_ASF_3D_LINE_ALTERNATIVE:
      return MEDIA_3D_LINE_ALTERNATIVE;
    case GST_ASF_3D_DUAL_STREAM:
      return MEDIA_3D_DUAL_STREAM;
    case GST_ASF_3D_2DTO3D:
      return MEDIA_3D_2DTO3D;
  }
  GST_ERROR_OBJECT (mvsink, "unknown asf3DType type %u", asf3DType);
  return MEDIA_3D_NONE;
}

enum
{
  /* FILL ME */
  VIDEO_UNDERRUN,
  LAST_SIGNAL
};

static guint gst_omx_video_sink_signal[LAST_SIGNAL] = { 0 };

GST_DEBUG_CATEGORY_STATIC (gst_mvsink_debug);
#define GST_CAT_DEFAULT gst_mvsink_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_mvsink_debug, "mvsink", 0, \
      "debug category for mvsink");


G_DEFINE_TYPE_WITH_CODE (GSTMVSink, gst_mvsink, GST_TYPE_VIDEO_SINK,
    DEBUG_INIT);

static void gst_mvsink_class_init (GSTMVSinkClass * klass);

static gboolean gst_mvsink_setcaps (GstBaseSink * bsink, GstCaps * caps);
static void gst_mvsink_get_times (GstBaseSink * basesink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_mvsink_preroll (GstBaseSink * bsink, GstBuffer * buff);
static gboolean gst_mvsink_start (GstBaseSink * bsink);
static gboolean gst_mvsink_stop (GstBaseSink * bsink);
static GstFlowReturn gst_mvsink_show_frame (GstVideoSink * video_sink,
    GstBuffer * buf);

static void gst_mvsink_finalize (GObject * object);
static void gst_mvsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mvsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_mvsink_change_state (GstElement * element,
    GstStateChange transition);
static void gst_mvsink_post_video_info (GstElement * element,
    MS_DispFrameFormat * dff, MS_FrameFormat * sFrame);
static gboolean gst_mvsink_is_low_latency_app (GSTMVSink * mvsink);
static gboolean gst_mvsink_is_video_sync_player (GSTMVSink * mvsink);
static void mvsink_select_inputsel (GSTMVSink * mvsink,
    MS_DispFrameFormat * dff);
static void mvsink_setup_mvop (GSTMVSink * mvsink, MS_DispFrameFormat * dff);
static gint MS3D_2_LG3D (guint ms_value);
static MZ_BOOL bXCBlueScreen = FALSE;

#define MVOP_ID (mvsink->window_id ? MZ_MVOP_MODULE_SUB : MZ_MVOP_MODULE_MAIN)

#define DEFAULT_APP_TYPE        "default_app_type"
#define DEFAULT_CONSTANT_DELAY  4

#define MIN_CONSTANT_DELAY      1
#define MAX_CONSTANT_DELAY      5       // must not be greater than max frame count we can get from vdec,
                                   // otherwise we may never reach the vsync bridge drop threshold

#define GST_VIDEO_CAPS_YUV_EXT(fourcc,ext)                              \
        "video/x-raw-yuv, "                                             \
        "format = (fourcc) " fourcc ", "                                \
        "width = " GST_VIDEO_SIZE_RANGE ", "                            \
        "height = " GST_VIDEO_SIZE_RANGE ", "                           \
        "framerate = " GST_VIDEO_FPS_RANGE ", "                         \
        "ext = (fourcc) " ext

#define GST_MVSINK_TEMPLATE_CAPS \
     GST_VIDEO_CAPS_YUV ("YUY2") \
 ";" GST_VIDEO_CAPS_YUV_EXT ("TILE","H264") \
 ";" GST_VIDEO_CAPS_YUV_EXT ("TILE","MVD_") \
 ";" GST_VIDEO_CAPS_YUV_EXT ("TILE","RVD_") \
 ";" GST_VIDEO_CAPS_YUV_EXT ("TILE","CLIP") \
 ";" GST_VIDEO_CAPS_YUV_EXT ("TILE","JPD_")

#define UNUSED_PARA(x)  x = x

#define DEFAULT_INPUT MZ_MVOP_INPUT_UNKNOWN
////#define DEFAULT_AV_SYNC_TOLERANCE (2000*GST_MSECOND)

#define GST_TYPE_MVSINK_INPUT (gst_mvsink_input_get_type())

#define ENABLE_DUAL_USE_MAIN_VOP            1

//#define MVSINK_DEBUG_FILE
#ifdef MVSINK_DEBUG_FILE
static FILE *pFile = NULL;
#endif

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static gboolean
mvsink_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "mvsink", GST_RANK_PRIMARY + 1,
      GST_TYPE_MVSINK);

  return TRUE;
}

static GType
gst_mvsink_input_get_type (void)
{
  static GType mvsink_input_type = 0;
  static const GEnumValue input_types[] = {
    {MZ_MVOP_INPUT_DRAM, "DRAM", "dram"},
    {MZ_MVOP_INPUT_H264, "H264", "H264"},
    {MZ_MVOP_INPUT_MVD, "MVD", "mvd"},
    {MZ_MVOP_INPUT_RVD, "RVD", "rvd"},
    {MZ_MVOP_INPUT_CLIP, "CLIP", "clip"},
    {MZ_MVOP_INPUT_JPD, "JPD", "jpd"},
    {MZ_MVOP_INPUT_UNKNOWN, "UNKNOWN", "unknown"},
    {0, NULL, NULL}
  };

  if (!mvsink_input_type) {
    mvsink_input_type = g_enum_register_static ("GstMVSinkMode", input_types);
  }

  return mvsink_input_type;
}

static char *
mvsink_get_codec_name (GSTMVSink * mvsink, Msil_VDEC_CodecType codecType)
{
  switch (codecType) {
    case E_MSIL_VDEC_CODEC_TYPE_NONE:
      return "";
    case E_MSIL_VDEC_CODEC_TYPE_MPEG2:
      return "MPEG 1/2";
    case E_MSIL_VDEC_CODEC_TYPE_H263:
      return "H263";
    case E_MSIL_VDEC_CODEC_TYPE_MPEG4:
      return "MPEG4";
    case E_MSIL_VDEC_CODEC_TYPE_DIVX311:
      return "DivX3";
    case E_MSIL_VDEC_CODEC_TYPE_DIVX412:
      return "DivX4";
    case E_MSIL_VDEC_CODEC_TYPE_FLV:
      return "FLV";
    case E_MSIL_VDEC_CODEC_TYPE_VC1_ADV:
      return "VC1";
    case E_MSIL_VDEC_CODEC_TYPE_VC1_MAIN:
      return "WMV";
    case E_MSIL_VDEC_CODEC_TYPE_RV8:
      return "RV8";
    case E_MSIL_VDEC_CODEC_TYPE_RV9:
      return "RV9";
    case E_MSIL_VDEC_CODEC_TYPE_H264:
      return "H264";
    case E_MSIL_VDEC_CODEC_TYPE_AVS:
      return "AVS";
    case E_MSIL_VDEC_CODEC_TYPE_MJPEG:
      return "MJPEG";
    case E_MSIL_VDEC_CODEC_TYPE_MVC:
      return "MVC";
    case E_MSIL_VDEC_CODEC_TYPE_VP8:
      return "VP8";
    case E_MSIL_VDEC_CODEC_TYPE_HEVC:
      return "HEVC";
    case E_MSIL_VDEC_CODEC_TYPE_VP9:
      return "VP9";
  }

  GST_ERROR_OBJECT (mvsink, "unknown codec type %d", codecType);
  return "";
}

static void *
gst_mvsink_free_loop (void *arg)
{
  GSTMVSink *mvsink = (GSTMVSink *) arg;
  unsigned long u32Cnt = 0, u32Cnt2 = 0;
  unsigned long u32SHMAddr =
      vsync_bridge_get_cmd (VSYNC_BRIDGE_GET_DISPQ_SHM_ADDR);
  GstMapInfo info;

  while (!mvsink->stop_thread) {
    u32SHMAddr = vsync_bridge_get_cmd (VSYNC_BRIDGE_GET_DISPQ_SHM_ADDR);

    if (mvsink->bThumbnaiMode) {
      if (mvsink->stop_thread) {
        break;
      }

      if (mvsink->frame_buf_r_index != mvsink->frame_buf_w_index) {
        guint next_index =
            (mvsink->frame_buf_r_index + 1) % MAX_FRAME_BUFF_SIZE;
        if (next_index == mvsink->frame_buf_w_index) {
          usleep (10000);
          continue;
        }
        mvsink->frame_buf_r_index = next_index;

        GST_DEBUG_OBJECT (mvsink, "thumbnail 11 free mvsink index %d",
            mvsink->frame_buf_r_index);
        gst_buffer_unref (mvsink->frame_buf[mvsink->frame_buf_r_index]);
      }
      usleep (10000);
    } else if (mvsink->XCInit) {
      if (mvsink->b2Stream3D) {
        if (mvsink->window_id == 1) {
          GST_DEBUG_OBJECT (mvsink, "b2Stream3D stream 1 no need free loop");
          break;
        }

        if (WMV3D_W_Index != WMV3D_R_Index) {
          WMV3D_R_Index = (WMV3D_R_Index + 1) % MAX_FRAME_BUFF_SIZE;

          gst_buffer_map (WMV3D[0].WMV3D_buf[WMV3D_R_Index], &info,
              GST_MAP_READ);
          MS_DispFrameFormat *dff = (MS_DispFrameFormat *) (MZ_U32) info.data;
          gst_buffer_unmap (WMV3D[0].WMV3D_buf[WMV3D_R_Index], &info);
          //volatile MCU_DISPQ_INFO *pSHM = (volatile  MCU_DISPQ_INFO *)MsOS_PA2KSEG1(u32SHMAddr + (dff->OverlayID * sizeof(MCU_DISPQ_INFO)));
          volatile MCU_DISPQ_INFO *pSHM =
              (volatile MCU_DISPQ_INFO *) Msil_PA2KSEG1 (u32SHMAddr +
              (dff->OverlayID * sizeof (MCU_DISPQ_INFO)));

          GST_DEBUG_OBJECT (mvsink,
              "[%d] WMV3D wait free mvsink index %d %lld Cur->%d",
              dff->OverlayID, WMV3D_R_Index, dff->u64Pts, dff->u8CurIndex);

        lb3DRetry:
          u32Cnt = 1000;
          u32Cnt2 = u32Cnt;

          if (pSHM && pSHM->u8McuDispSwitch) {
            while (--u32Cnt && ((pSHM->u8McuDispQRPtr == dff->u8CurIndex)
                    || (pSHM->McuDispQueue[dff->u8CurIndex].u8Tog_Time))) {
              usleep (1000);
              if (mvsink->stop_thread) {
                break;
              }
            }

            if (!u32Cnt) {
              if (WMV3D_R_Index != WMV3D_W_Index) {
                GST_DEBUG_OBJECT (mvsink, "WMV3D[%d]Wait frame done timeout!!",
                    dff->OverlayID);
              }
              goto lb3DRetry;
            }

            GST_DEBUG_OBJECT (mvsink,
                "WMV3D Overlay[%d] W %d, R %d, Cur %d, wait time = %ld ms",
                dff->OverlayID, pSHM->u8McuDispQWPtr, pSHM->u8McuDispQRPtr,
                dff->u8CurIndex, (u32Cnt2 - u32Cnt));

          } else if (!pSHM) {
            GST_DEBUG_OBJECT (mvsink,
                "WMV3D vsync_bridge_wait_frame_done pSHM is NULL");
          }

          GST_DEBUG_OBJECT (mvsink, "WMV3D[%d] free mvsink index %d",
              dff->OverlayID, WMV3D_R_Index);

          gst_buffer_unref (WMV3D[0].WMV3D_buf[WMV3D_R_Index]);
          gst_buffer_unref (WMV3D[1].WMV3D_buf[WMV3D_R_Index]);
          WMV3D[0].buf_state[WMV3D_R_Index] = 0;
          WMV3D[1].buf_state[WMV3D_R_Index] = 0;
        }
        usleep (10000);
      } else {
        if (mvsink->frame_buf_r_index != mvsink->frame_buf_w_index) {
          mvsink->frame_buf_r_index =
              (mvsink->frame_buf_r_index + 1) % MAX_FRAME_BUFF_SIZE;

          gst_buffer_map (mvsink->frame_buf[mvsink->frame_buf_r_index], &info,
              GST_MAP_READ);
          MS_DispFrameFormat *dff = (MS_DispFrameFormat *) (MZ_U32) info.data;
          gst_buffer_unmap (mvsink->frame_buf[mvsink->frame_buf_r_index],
              &info);

          //volatile MCU_DISPQ_INFO *pSHM = (volatile  MCU_DISPQ_INFO *)MsOS_PA2KSEG1(u32SHMAddr + (dff->OverlayID * sizeof(MCU_DISPQ_INFO)));
          volatile MCU_DISPQ_INFO *pSHM =
              (volatile MCU_DISPQ_INFO *) Msil_PA2KSEG1 (u32SHMAddr +
              (dff->OverlayID * sizeof (MCU_DISPQ_INFO)));

          GST_DEBUG_OBJECT (mvsink,
              "[%d] wait free mvsink index %d %lld Cur->%d", dff->OverlayID,
              mvsink->frame_buf_r_index, dff->u64Pts, dff->u8CurIndex);

        lbRetry:
          u32Cnt = 1000;
          u32Cnt2 = u32Cnt;

          if (pSHM && pSHM->u8McuDispSwitch) {

            while (--u32Cnt && ((pSHM->u8McuDispQRPtr == dff->u8CurIndex)
                    || (pSHM->McuDispQueue[dff->u8CurIndex].u8Tog_Time))) {
              usleep (1000);
              if (mvsink->stop_thread) {
                break;
              }
            }

            if (!u32Cnt) {
              if (mvsink->frame_buf_r_index != mvsink->frame_buf_w_index) {
                GST_DEBUG_OBJECT (mvsink, "[%d]Wait frame done timeout!!",
                    dff->OverlayID);
              }
              goto lbRetry;
            }
            GST_DEBUG_OBJECT (mvsink,
                "Overlay[%d] W %d, R %d, Cur %d, wait time = %ld ms",
                dff->OverlayID, pSHM->u8McuDispQWPtr, pSHM->u8McuDispQRPtr,
                dff->u8CurIndex, (u32Cnt2 - u32Cnt));

          } else if (!pSHM) {
            GST_DEBUG_OBJECT (mvsink,
                "vsync_bridge_wait_frame_done pSHM is NULL");
          }

          GST_DEBUG_OBJECT (mvsink, "11[%d] free mvsink index %d",
              dff->OverlayID, mvsink->frame_buf_r_index);
          gst_buffer_unref (mvsink->frame_buf[mvsink->frame_buf_r_index]);
        }
        usleep (1000);
      }
    } else
      usleep (1000);
  }

  if (!mvsink->b2Stream3D) {
    while (mvsink->frame_buf_r_index != mvsink->frame_buf_w_index) {
      mvsink->frame_buf_r_index =
          (mvsink->frame_buf_r_index + 1) % MAX_FRAME_BUFF_SIZE;
      GST_DEBUG_OBJECT (mvsink, "22 free mvsink index %d",
          mvsink->frame_buf_r_index);
      gst_buffer_unref (mvsink->frame_buf[mvsink->frame_buf_r_index]);
    }
  }

  GST_DEBUG_OBJECT (mvsink, "The function exits");
  return NULL;
}

static void
gst_mvsink_get_times (GstBaseSink * basesink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GSTMVSink *mvsink;

  HERE;

  mvsink = GST_MVSINK (basesink);

  if (!GST_BUFFER_PTS_IS_VALID (buffer)) {
    GST_DEBUG_OBJECT (mvsink, "TS is invalid");
    return;
  }

  *start = GST_BUFFER_PTS (buffer);

  if (mvsink->fps_n > 0) {
    guint64 u64Time = gst_util_uint64_scale_int (GST_SECOND, mvsink->fps_d,
        mvsink->fps_n);

    if (*start > (GST_OMX_VSYNC_BRIDGE_FRAME_ADV * u64Time)) {
      *start -= (GST_OMX_VSYNC_BRIDGE_FRAME_ADV * u64Time);
    }
  }

  if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
    *end = *start + GST_BUFFER_DURATION (buffer);
  } else {
    if (mvsink->fps_n > 0) {
      *end = *start +
          gst_util_uint64_scale_int (GST_SECOND, mvsink->fps_d, mvsink->fps_n);
    }
  }

  HERE;
}



static gboolean
gst_mvsink_setcaps (GstBaseSink * bsink, GstCaps * vscapslist)
{
  GSTMVSink *mvsink = NULL;
  GstStructure *structure;
  const gchar *vendor;
  const GValue *fps;
  GstClockTime total_delay;

  HERE;

  mvsink = GST_MVSINK (bsink);
  GST_DEBUG_OBJECT (mvsink, "caps count: %d", gst_caps_get_size (vscapslist));
  structure = gst_caps_get_structure (vscapslist, 0);

  vendor = gst_structure_get_string (structure, "vendor");
  mvsink->is_support_decoder = vendor && !g_strcmp0 (vendor, "mstar");
  GST_INFO_OBJECT (mvsink, "set is_support_decoder to %d",
      mvsink->is_support_decoder);

  fps = gst_structure_get_value (structure, "framerate");

  if (GST_VALUE_HOLDS_FRACTION (fps)) {
    mvsink->fps_n = gst_value_get_fraction_numerator (fps);
    mvsink->fps_d = gst_value_get_fraction_denominator (fps);
    if (mvsink->fps_n)
      gst_base_sink_set_max_lateness (bsink,
          (GST_OMX_VSYNC_BRIDGE_FRAME_ADV +
              1) * (mvsink->fps_d * 1000 / mvsink->fps_n) * GST_MSECOND);
  } else                        //no framerate info from container
  {
    mvsink->fps_n = 30;         //default: framerae = 30.0
    mvsink->fps_d = 1;
  }
  GST_DEBUG_OBJECT (mvsink, "framerate: %d / %d", mvsink->fps_n, mvsink->fps_d);

  total_delay = mvsink->average_delay + mvsink->renderDelay;
  if (total_delay) {
    GST_DEBUG_OBJECT (mvsink, "set new render delay %" GST_TIME_FORMAT,
        GST_TIME_ARGS (total_delay));
    gst_base_sink_set_render_delay (bsink, total_delay);
  }

  mvsink->bProgressive = 1;
  mvsink->u8AspectRate = 0;

  GST_DEBUG_OBJECT (mvsink,
      "enAsync:%d, tsOffset:%lld, enLastBuf:%d, delay:%llu",
      gst_base_sink_is_async_enabled (bsink),
      gst_base_sink_get_ts_offset (bsink),
      gst_base_sink_is_last_sample_enabled (bsink),
      gst_base_sink_get_render_delay (bsink));

  HERE;

  return TRUE;
}

/*
#define DO_RUNNING_AVG(avg,val,size) (((val) + ((size)-1) * (avg)) / (size))

static void
mvsink_update_render_delay (GSTMVSink * mvsink, int queue_count)
{
  GstClockTime frame_duration =
      mvsink->mvop_framerate ? GST_SECOND * 1000 / mvsink->mvop_framerate : 0;
  GstClockTime current_delay = queue_count * frame_duration;
  GstClockTime applied_delay =
      gst_base_sink_get_render_delay (GST_BASE_SINK (mvsink));
  GstClockTime total_delay;

  mvsink->average_delay =
      DO_RUNNING_AVG (mvsink->average_delay, current_delay, 30);
  total_delay = mvsink->average_delay + mvsink->renderDelay;

  if (ABS (GST_CLOCK_DIFF (applied_delay, total_delay)) > 10 * GST_MSECOND) {
    GST_DEBUG_OBJECT (mvsink, "set new render delay %" GST_TIME_FORMAT,
        GST_TIME_ARGS (total_delay));
    gst_base_sink_set_render_delay (GST_BASE_SINK (mvsink), total_delay);
  }
}
*/
static void
mvsink_update_current_pts (GSTMVSink * mvsink, GstBuffer * buf)
{
  mvsink->current_pts = GST_BUFFER_PTS (buf);
}

static gint
MS3D_2_LG3D (guint ms_value)
{
  static const MEDIA_3D_TYPES_T convert_table[] = {
    MEDIA_3D_NONE,              // OMX_3D_NOT_3D
    MEDIA_3D_SIDE_BY_SIDE_HALF_LR,      // OMX_3D_SIDE_BY_SIDE_RF, note that LG requires us to always report LR for side by side streams
    MEDIA_3D_SIDE_BY_SIDE_HALF_LR,      // OMX_3D_SIDE_BY_SIDE_LF
    MEDIA_3D_BOTTOM_AND_TOP_HALF,       // OMX_3D_TOP_BOTTOM_RT
    MEDIA_3D_TOP_AND_BOTTOM_HALF,       // OMX_3D_TOP_BOTTOM_LT
    MEDIA_3D_DUAL_STREAM,       // OMX_3D_DUAL_STREAM
    0,                          // OMX_3D_MULTI_STREAM
    MEDIA_3D_COLUMN_INTERLEAVE, // OMX_3D_VERTICAL_LINE_INTERLEAVED_TYPE_ODD_LINE
    MEDIA_3D_COLUMN_INTERLEAVE, // OMX_3D_VERTICAL_LINE_INTERLEAVED_TYPE_EVEN_LINE
    MEDIA_3D_FRAME_SEQUENTIAL,  // OMX_3D_FRAME_SEQUENTIAL_TYPE_ODD_FRAME
    MEDIA_3D_FRAME_SEQUENTIAL,  // OMX_3D_FRAME_SEQUENTIAL_TYPE_EVEN_FRAME
    MEDIA_3D_FRAME_SEQUENTIAL,  // OMX_3D_LEFT_RIGHT_VIEW_SEQUENCE_TYPE_PRIMARY_VIEW
    MEDIA_3D_FRAME_SEQUENTIAL,  // OMX_3D_LEFT_RIGHT_VIEW_SEQUENCE_TYPE_SECONDARY_VIEW
    MEDIA_3D_LINE_ALTERNATIVE,  // OMX_3D_HORIZONTAL_LINE_INTERLEAVED_TYPE_ODD_LINE,
    MEDIA_3D_LINE_ALTERNATIVE,  // OMX_3D_HORIZONTAL_LINE_INTERLEAVED_TYPE_EVEN_LINE,
    MEDIA_3D_FRAME_PACKING,     // OMX_3D_FRAME_PACKING,
    MEDIA_3D_CHECK_BOARD,       // OMX_3D_CHECKERBOARD_INTERLEAVED_TYPE,
    MEDIA_3D_FIELD_ALTERNATIVE  // OMX_3D_FRAME_ALTERNATIVE,
  };

  if (ms_value >= sizeof (convert_table) / sizeof (MEDIA_3D_TYPES_T))
    return MEDIA_3D_NONE;
  return convert_table[ms_value];
}

static inline guint64
U64Gcd (guint64 Num1, guint64 Num2)
{
  return Num2 == 0 ? Num1 : U64Gcd (Num2, Num1 % Num2);
}

static void
mvsink_calculate_par (MS_DispFrameFormat * dff, guint32 * pu32ParWidth,
    guint32 * pu32ParHeight)
{
  MS_FrameFormat *sFrame = &dff->sFrames[MS_VIEW_TYPE_CENTER];
  // PAR_W : PAR_H = S_H * D_W : S_W * D_H
  guint32 u32SrcWidth =
      sFrame->u32Width - sFrame->u32CropLeft - sFrame->u32CropRight;
  guint32 u32SrcHeight =
      sFrame->u32Height - sFrame->u32CropTop - sFrame->u32CropBottom;
  guint32 u32DarWidth = dff->u32AspectWidth;
  guint32 u32DarHeight = dff->u32AspectHeight;
  guint64 u64ParWidth = (guint64) u32SrcHeight * u32DarWidth;
  guint64 u64ParHeight = (guint64) u32SrcWidth * u32DarHeight;
  guint64 u64GCD = U64Gcd (u64ParWidth, u64ParHeight);

  *pu32ParWidth = 1;
  *pu32ParHeight = 1;
  if (u64GCD) {
    u64ParWidth /= u64GCD;
    u64ParHeight /= u64GCD;
    // Shrink Until less than U16_MAX
    while (u64ParWidth > 0xFFFF && u64ParHeight > 0xFFFF) {
      u64ParWidth /= 2;
      u64ParHeight /= 2;
    }
    if (u64ParWidth && u64ParHeight) {
      *pu32ParWidth = (guint32) u64ParWidth;
      *pu32ParHeight = (guint32) u64ParHeight;
    }
  }
  GST_TRACE_OBJECT (NULL,
      "width %d, height %d, dar width %d, dar height %d, par width %d, par height %d",
      u32SrcWidth, u32SrcHeight, u32DarWidth, u32DarHeight, *pu32ParWidth,
      *pu32ParHeight);
}

static void
mvsink_reset_video_info (GSTMVSink * mvsink)
{
  mvsink->frame_width = 0;
  mvsink->frame_height = 0;
  mvsink->par_width = 0;
  mvsink->par_height = 0;
  mvsink->the3DLayout = MEDIA_3D_NONE;
  mvsink->interlace = FALSE;
  memset (&mvsink->hdr_info, 0, sizeof (MS_HDRInfo));
}

static void
mvsink_set_video_info (GSTMVSink * mvsink, MS_DispFrameFormat * dff,
    MS_FrameFormat * sFrame)
{
  guint32 par_width, par_height;

  mvsink_calculate_par (dff, &par_width, &par_height);
  mvsink->frame_width = sFrame->u32Width;
  mvsink->frame_height = sFrame->u32Height;
  mvsink->par_width = par_width;
  mvsink->par_height = par_height;
  mvsink->the3DLayout = MS3D_2_LG3D (dff->u83DLayout);
  mvsink->interlace = dff->u8Interlace;
  memcpy (&mvsink->hdr_info, &dff->stHDRInfo, sizeof (MS_HDRInfo));
}

static gboolean
mvsink_is_video_info_changed (GSTMVSink * mvsink, MS_DispFrameFormat * dff,
    MS_FrameFormat * sFrame, gboolean * reset_mvop)
{
  guint32 par_width, par_height;

  mvsink_calculate_par (dff, &par_width, &par_height);
  *reset_mvop = mvsink->interlace != dff->u8Interlace;
  return mvsink->frame_width != sFrame->u32Width ||
      mvsink->frame_height != sFrame->u32Height ||
      mvsink->par_width != par_width ||
      mvsink->par_height != par_height ||
      mvsink->the3DLayout != MS3D_2_LG3D (dff->u83DLayout) ||
      mvsink->interlace != dff->u8Interlace ||
      memcmp (&mvsink->hdr_info, &dff->stHDRInfo, sizeof (MS_HDRInfo));
}

static GstFlowReturn
gst_mvsink_init_display (GstBaseSink * bsink, GstBuffer * buf)
{
  GSTMVSink *mvsink;
  mvsink = GST_MVSINK (bsink);
  static int GotPair = 0;
  GstMapInfo info;

  GST_DEBUG_OBJECT (mvsink, "Init display");

  gst_buffer_map (buf, &info, GST_MAP_READ);
  if ((info.data == 0)) {
    gst_buffer_unmap (buf, &info);
    return GST_FLOW_OK;
  }

  MS_DispFrameFormat *dff = (MS_DispFrameFormat *) (MZ_U32) info.data;
  MS_FrameFormat *sFrame = &dff->sFrames[MS_VIEW_TYPE_CENTER];

  mvsink->fps_n = dff->u32FrameRate;
  mvsink->fps_d = 1000;
  GST_DEBUG_OBJECT (mvsink, "update framerate: %d / %d", mvsink->fps_n,
      mvsink->fps_d);

  mvsink_set_video_info (mvsink, dff, sFrame);
  mvsink_update_current_pts (mvsink, buf);
  mvsink_select_inputsel (mvsink, dff);

#ifdef TEMP_DISABLE_DUAL_3D
  if (mvsink->b2Stream3D) {
    if (mvsink->window_id == 1) {
      return GST_FLOW_OK;
    } else {
      mvsink->b2Stream3D = 0;
    }
  }
#endif

  if (mvsink->b2Stream3D) {
    /*
       pthread_mutex_lock(&m_Lock);
       GstMapInfo info3D0;
       GstMapInfo info3D1;
       int w_index = WMV3D[dff->OverlayID].buf_w_index;
       w_index = (w_index + 1) % MAX_FRAME_BUFF_SIZE;

       GST_DEBUG_OBJECT(mvsink,"[%d] w_index = %d", dff->OverlayID, w_index);

       gst_buffer_ref(buf);
       WMV3D[dff->OverlayID].WMV3D_buf[w_index] = buf;
       WMV3D[dff->OverlayID].buf_w_index = w_index;
       WMV3D[dff->OverlayID].buf_state[w_index] = 1;

       gst_buffer_map(WMV3D[0].WMV3D_buf[w_index], &info3D0, GST_MAP_READ);
       gst_buffer_map(WMV3D[1].WMV3D_buf[w_index], &info3D1, GST_MAP_READ);
       if (WMV3D[0].buf_state[w_index] && WMV3D[1].buf_state[w_index]) {
       MS_DispFrameFormat *WMVdff0 = (MS_DispFrameFormat *)(MZ_U32)info3D0.data;
       MS_DispFrameFormat *WMVdff1 = (MS_DispFrameFormat *)(MZ_U32)info3D1.data;

       WMVdff0->FrameNum = 2;
       WMVdff0->CodecType = E_MSIL_VDEC_CODEC_TYPE_MVC;
       WMVdff0->sFrames[1].sHWFormat.u32LumaAddr = WMVdff1->sFrames[0].sHWFormat.u32LumaAddr;
       WMVdff0->sFrames[1].sHWFormat.u32ChromaAddr = WMVdff1->sFrames[0].sHWFormat.u32ChromaAddr;

       dff = (MS_DispFrameFormat *)(MZ_U32)info3D0.data;
       GotPair = 1;
       GST_DEBUG_OBJECT(mvsink,"WMV3D got pair");
       }
       gst_buffer_unmap(WMV3D[0].WMV3D_buf[w_index], &info3D0);
       gst_buffer_unmap(WMV3D[1].WMV3D_buf[w_index], &info3D1);
       pthread_mutex_unlock(&m_Lock);
     */
    mvsink->XCInit = 1;         // not reset disp path

    if (mvsink->enInputSel == MZ_MVOP_INPUT_MVD) {
      mvsink->enInputSel = MZ_MVOP_INPUT_MVD_3DLR;
    }

    GotPair |= (1 << (dff->OverlayID));

    if (GotPair != 0x3) {
      return GST_FLOW_OK;
    }
    GotPair = 0;
  }

  if (mvsink->bThumbnaiMode) {
    gst_buffer_unmap (buf, &info);
    return gst_mvsink_show_frame ((GstVideoSink *) bsink, buf);
  }

  mvsink_setup_mvop (mvsink, dff);

  MZ_WINDOW_TYPE stCorpWin;
  stCorpWin.x = sFrame->u32CropLeft;
  stCorpWin.y = sFrame->u32CropTop;
  stCorpWin.width = sFrame->u32Width - sFrame->u32CropRight;
  stCorpWin.height = sFrame->u32Height - sFrame->u32CropBottom;

  msil_scaler_DisplaySetXCWindow (&mvsink->stDispWin, &stCorpWin,
      dff->OverlayID ? MZ_SUB_WIN : MZ_MAIN_WIN);

#if VSYNC_BRIDGE_DS
  vsync_bridge_ds_init ((void *) dff);
  vsync_bridge_ds_enable (mvsink->seamlessPlay, dff->OverlayID);
#endif

  if (Msil_XC_Sys_IsPanelConnectType4K ())
    vsync_bridge_open ((void *) dff, 3840, 2160, 0);
  else
    vsync_bridge_open ((void *) dff, 1920, 1080, 0);

#if VSYNC_BRIDGE_DS
  if (mvsink->seamlessPlay) {
    vsync_bridge_set_thread (dff->OverlayID, TRUE);     // Enable thread aftre vsync open is done.
  }
#endif
  /*
     if (mvsink->b2Stream3D && dff->OverlayID == 0)
     {
     WMV3D_W_Index = 1;
     }
     else
     {
     int w_index = mvsink->frame_buf_w_index;
     w_index = (w_index + 1) % MAX_FRAME_BUFF_SIZE;
     mvsink->frame_buf[w_index] = buf;
     gst_buffer_ref(buf);
     mvsink->frame_buf_w_index = w_index;
     GST_DEBUG_OBJECT(mvsink,"preroll [%d] add mvsink index %d %lld, Cur->%d",
     dff->OverlayID, mvsink->frame_buf_w_index, dff->u64Pts, dff->u8CurIndex);
     }
   */
  mvsink->XCInit = 1;
  gst_mvsink_post_video_info (GST_ELEMENT_CAST (bsink), dff, sFrame);
  gst_buffer_unmap (buf, &info);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mvsink_preroll (GstBaseSink * bsink, GstBuffer * buf)
{
  GSTMVSink *mvsink = GST_MVSINK (bsink);

  if (!mvsink->is_support_decoder) {
    GST_INFO_OBJECT (mvsink,
        "receiving buffer from non-MStar decoder, ignore it");
    return GST_FLOW_OK;
  }

  if (!mvsink->XCInit)
    return gst_mvsink_init_display (bsink, buf);

  mvsink_update_current_pts (mvsink, buf);
  return GST_FLOW_OK;
}

static void
gst_mvsink_flush_3d_queue (GSTMVSink * mvsink, guint id)
{
  int w_3d_index;
  int w_index;

  if (!mvsink->b2Stream3D)
    return;

  pthread_mutex_lock (&m_Lock);

  w_3d_index = WMV3D_W_Index;
  w_index = WMV3D[id].buf_w_index;
  GST_DEBUG_OBJECT (mvsink, "WMV3D[%d] flush %d -> %d", mvsink->window_id,
      w_3d_index, w_index);
  while (w_3d_index != w_index) {
    w_3d_index = (w_3d_index + 1) % MAX_FRAME_BUFF_SIZE;
    if (WMV3D[id].buf_state[w_3d_index]) {
      gst_buffer_unref (WMV3D[id].WMV3D_buf[w_3d_index]);
      WMV3D[id].buf_state[w_3d_index] = 0;
      GST_DEBUG_OBJECT (mvsink, "WMV3D[%d] flush mvsink index %d",
          mvsink->window_id, w_3d_index);
    }
  }

  WMV3D[id].buf_w_index = WMV3D_W_Index;

  pthread_mutex_unlock (&m_Lock);
}

static gboolean
gst_mvsink_is_flushing (GSTMVSink * mvsink)
{
  if (mvsink->flushing)
    return TRUE;
  if (mvsink->b2Stream3D)
    return WMV3D[mvsink->window_id].flushing;
  return FALSE;
}

static gboolean
gst_mvsink_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret;
  GSTMVSink *mvsink = GST_MVSINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (mvsink, "gst_mvsink_sink_event[%d] %s, start",
      mvsink->window_id, GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      mvsink->flushing = TRUE;
      WMV3D[0].flushing = WMV3D[1].flushing = 1;
      break;

    case GST_EVENT_FLUSH_STOP:
      mvsink->flushing = FALSE;
      mvsink->firstsync = TRUE;
      WMV3D[0].flushing = WMV3D[1].flushing = 0;
      gst_mvsink_flush_3d_queue (mvsink, mvsink->window_id);
      break;
    case GST_EVENT_EOS:
      if (mvsink->disp_frame_count == 0) {
        GST_DEBUG_OBJECT (mvsink,
            "gst_mvsink_sink_event[%d] error, no valid video frame",
            mvsink->window_id);
        GST_ELEMENT_ERROR (GST_ELEMENT_CAST (mvsink), STREAM, TYPE_NOT_FOUND,
            (NULL), ("No valid video frame"));
      }
      break;

    case GST_EVENT_SEGMENT:
    {
      const GstSegment *seg;
      gst_event_parse_segment (event, &seg);
      GST_DEBUG_OBJECT (mvsink,
          "NS fmt=%d(%s), rate = %lf, start=%lld, stop=%lld, time=%lld",
          seg->format, gst_format_get_name (seg->format), seg->rate, seg->start,
          seg->stop, seg->time);
      mvsink->playrate = seg->rate;
      mvsink->firstFrame = TRUE;
      if (mvsink->frame_width > UHD_Width_Bound &&
          mvsink->frame_height > UHD_Height_Bound) {
        if (mvsink->playrate > 0.0 && mvsink->playrate < 1.0) {
          msil_scaler_set_gst_ff_one_half_rate_status (1);
        } else {
          msil_scaler_set_gst_ff_one_half_rate_status (0);
        }
      }
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *structure;
      structure = gst_event_get_structure (event);
      if (structure == NULL)
        break;
      if (g_strcmp0 ("typeof3D", gst_structure_get_name (structure)) == 0) {
        guint type3D = 0;
        gst_structure_get_uint (structure, "3DTypeValue", &type3D);
        mvsink->asf3DType = type3D;
        mvsink->bSetAsf3DType = TRUE;
        GST_DEBUG_OBJECT (mvsink, "mvsink get event of type3D: %d", type3D);
      } else if (g_strcmp0 ("BUFFER_UNDERRUN",
              gst_structure_get_name (structure)) == 0) {
        g_signal_emit (mvsink, gst_omx_video_sink_signal[VIDEO_UNDERRUN], 0, 0);
        GST_DEBUG_OBJECT (mvsink, "mvsink emit underrun signal");
      }
      break;
    }
    default:
      break;
  }

  ret = mvsink->sink_event_func (pad, GST_OBJECT_CAST (mvsink), event);

  gst_object_unref (mvsink);
  GST_DEBUG_OBJECT (mvsink, "gst_mvsink_sink_event[%d] %s, end",
      mvsink->window_id, GST_EVENT_TYPE_NAME (event));

  return ret;
}

//#define MVSINK_RENDER_TIME_DIFF
static GstFlowReturn
gst_mvsink_show_frame (GstVideoSink * video_sink, GstBuffer * buf)
{
  GSTMVSink *mvsink = GST_MVSINK (video_sink);
  MZ_U32 u32YPAddr = 0;
  MZ_U32 u32UVPAddr = 0;
  GstMapInfo info;
#ifdef MVSINK_RENDER_TIME_DIFF
  GstClock *clock;
  static GstClockTime sCurTime = 0;
  GstClockTime sOldTime;
  static long count = 0;
  sOldTime = sCurTime;
  clock = GST_ELEMENT_CLOCK (video_sink);
  sCurTime = gst_clock_get_time (clock);

  GST_DEBUG_OBJECT (mvsink, "count is %d. clock is %" GST_TIME_FORMAT "\n",
      ++count, GST_TIME_ARGS (sCurTime));
  GST_DEBUG_OBJECT (mvsink,
      "Diff with last render time is %" GST_TIME_FORMAT "\n\n",
      GST_TIME_ARGS (sCurTime - sOldTime));
#endif
  HERE;
  GST_DEBUG_OBJECT (mvsink, "Show frame");

  if (!mvsink->is_support_decoder) {
    GST_INFO_OBJECT (mvsink,
        "receiving buffer from non-MStar decoder, ignore it");
    return GST_FLOW_OK;
  }

  if (!mvsink->XCInit)
    return gst_mvsink_init_display (GST_BASE_SINK (mvsink), buf);

  if (gst_mvsink_is_flushing (mvsink)) {
    // skip data, return OK to let previous element push more data s.t mvsink could flush it
    HERE;
    return GST_FLOW_OK;
  }

  if (mvsink->enInputSel == MZ_MVOP_INPUT_UNKNOWN) {
    GST_ERROR_OBJECT (mvsink, "Invalid input type!!!");
    HERE;
    return GST_FLOW_CUSTOM_ERROR;
  }

  mvsink->disp_frame_count++;
  if (mvsink->bThumbnaiMode) {
    HERE;
    gst_buffer_map (buf, &info, GST_MAP_READ);
    if ((info.data != 0) && (info.memory != NULL)) {
      MS_DispFrameFormat *dff = (MS_DispFrameFormat *) (MZ_U32) info.data;

      int w_index = mvsink->frame_buf_w_index;
      w_index = (w_index + 1) % MAX_FRAME_BUFF_SIZE;
      mvsink->frame_buf[w_index] = buf;
      gst_buffer_ref (buf);
      mvsink->frame_buf_w_index = w_index;

      GST_DEBUG_OBJECT (mvsink,
          "thumbnail [%d] add mvsink index %d %lld, Cur->%d", dff->OverlayID,
          mvsink->frame_buf_w_index, dff->u64Pts, dff->u8CurIndex);
    }
    gst_buffer_unmap (buf, &info);
    return GST_FLOW_OK;
  }
#ifdef TEMP_DISABLE_DUAL_3D
  if (mvsink->b2Stream3D && mvsink->window_id == 1) {
    return GST_FLOW_OK;
  }
#endif

  if (mvsink->enInputSel == MZ_MVOP_INPUT_DRAM) {
    MZ_MemoryInfo *mminfo = msil_system_get_mminfo ();
#if 1                           // tmp solution
    MZ_MMap stMmap;
    msil_system_get_mmap (MZ_MMAP_ID_MHEG5_CI_PLUS_BUFFER, &stMmap);
    gst_buffer_map (buf, &info, GST_MAP_READ);

    memcpy ((unsigned char *) msil_system_PA2VA (((stMmap.b_is_miu0 ==
                    1) ? (stMmap.u32Addr) : (stMmap.u32Addr +
                    mminfo->miu_boundary))), info.data, info.size);
    msil_system_flush_memory ();
    u32YPAddr =
        ((stMmap.b_is_miu0 ==
            1) ? (stMmap.u32Addr) : (stMmap.u32Addr + mminfo->miu_boundary));
    u32UVPAddr =
        ((stMmap.b_is_miu0 ==
            1) ? (stMmap.u32Addr) : (stMmap.u32Addr + mminfo->miu_boundary)) +
        (info.size / 2);
#else
    u32YPAddr = msil_system_VA2PA ((MZ_U32) GST_BUFFER_DATA (buf));
    u32UVPAddr =
        msil_system_VA2PA ((MZ_U32) (GST_BUFFER_DATA (buf) +
            GST_BUFFER_SIZE (buf) / 2));
#endif

    mvsink->stMVopCfg.u32YOffset = u32YPAddr;
    mvsink->stMVopCfg.u32UVOffset = u32UVPAddr;
    gst_buffer_unmap (buf, &info);
  } else {
    gboolean reset_mvop;
    gst_buffer_map (buf, &info, GST_MAP_READ);
    if (info.data == 0) {
      GST_ERROR_OBJECT (mvsink, "info data is null");
      return GST_FLOW_ERROR;
    }
    MZ_VDEC_DispFrm stDispFrm;
    memset (&stDispFrm, 0, sizeof (MZ_VDEC_DispFrm));
    stDispFrm.u32VdecPtr = (MZ_U32) info.data;

#if VSYNC_BRIDGE
    MS_DispFrameFormat *dff = (MS_DispFrameFormat *) (MZ_U32) info.data;
    MS_FrameFormat *sFrame = &dff->sFrames[MS_VIEW_TYPE_CENTER];
    mvsink_update_current_pts (mvsink, buf);
#endif

    if (mvsink->firstFrame || mvsink->playrate < 0.0 || mvsink->playrate > 2.0) {
      mvsink->firstFrame = FALSE;
      dff->u8FieldCtrl = MS_FIELD_CTRL_TOP;
    }

    if (mvsink_is_video_info_changed (mvsink, dff, sFrame, &reset_mvop)) {
      GST_DEBUG_OBJECT (mvsink, "ORG : w %u, h %u, par w %u, par h %u, 3d %u",
          mvsink->frame_width, mvsink->frame_height, mvsink->par_width,
          mvsink->par_width, mvsink->the3DLayout);

      mvsink_set_video_info (mvsink, dff, sFrame);
      GST_DEBUG_OBJECT (mvsink, "CHG : w %u, h %u, par w %u, par h %u, 3d %u",
          mvsink->frame_width, mvsink->frame_height, mvsink->par_width,
          mvsink->par_width, mvsink->the3DLayout);
      gst_mvsink_post_video_info (GST_ELEMENT (mvsink), dff, sFrame);

      if (reset_mvop) {
        GST_DEBUG_OBJECT (mvsink, "Need to reset MVOP");
        mvsink_setup_mvop (mvsink, dff);
      }

      if (mvsink->app_type && g_str_equal (mvsink->app_type, "RTC")
          && mvsink->seamlessPlay == FALSE) {
        GST_WARNING_OBJECT (mvsink,
            "disable output since resolution change without DS in RTC app_type");
        mvsink->is_support_decoder = FALSE;
        gst_buffer_unmap (buf, &info);
        return GST_FLOW_OK;
      }
    }

    if (mvsink->b2Stream3D) {
      pthread_mutex_lock (&m_Lock);

      int w_index = WMV3D[dff->OverlayID].buf_w_index;
      int w_prev_index = w_index;

      w_index = (w_index + 1) % MAX_FRAME_BUFF_SIZE;

      gst_buffer_ref (buf);
      WMV3D[dff->OverlayID].WMV3D_buf[w_index] = buf;
      WMV3D[dff->OverlayID].buf_w_index = w_index;
      WMV3D[dff->OverlayID].buf_state[w_index] = 1;
      GST_DEBUG_OBJECT (mvsink, "WMV3D[%d] update write index to %d",
          dff->OverlayID, w_index);
      if (mvsink->firstsync) {
        while (WMV3D[!dff->OverlayID].buf_state[w_index] == 0) {
          pthread_mutex_unlock (&m_Lock);
          usleep (10000);
          pthread_mutex_lock (&m_Lock);
          if (mvsink->stop_thread == TRUE)
            goto NEXT;
        }
      }

      if (WMV3D[0].buf_state[w_index] && WMV3D[1].buf_state[w_index]) {
        GstClockTime t0 = GST_BUFFER_PTS (WMV3D[0].WMV3D_buf[w_index]);
        GstClockTime t1 = GST_BUFFER_PTS (WMV3D[1].WMV3D_buf[w_index]);

        if (mvsink->firstsync) {
          if (t0 != t1) {
            if (mvsink->playrate >= 0) {
              if (t1 > t0) {
                gst_buffer_unref (WMV3D[0].WMV3D_buf[w_index]);
                WMV3D[0].buf_state[w_index] = 0;
                WMV3D[0].buf_w_index = w_prev_index;
                goto NEXT;
              } else {
                gst_buffer_unref (WMV3D[1].WMV3D_buf[w_index]);
                WMV3D[1].buf_state[w_index] = 0;
                WMV3D[1].buf_w_index = w_prev_index;
                goto NEXT;
              }
            } else {
              if (t1 > t0) {
                gst_buffer_unref (WMV3D[1].WMV3D_buf[w_index]);
                WMV3D[1].buf_state[w_index] = 0;
                WMV3D[1].buf_w_index = w_prev_index;
                goto NEXT;
              } else {
                gst_buffer_unref (WMV3D[0].WMV3D_buf[w_index]);
                WMV3D[0].buf_state[w_index] = 0;
                WMV3D[0].buf_w_index = w_prev_index;
                goto NEXT;
              }
            }
          } else {
            mvsink->firstsync = FALSE;
          }
        }

        GstMapInfo info3D0, info3D1;
        gst_buffer_map (WMV3D[0].WMV3D_buf[w_index], &info3D0, GST_MAP_READ);
        gst_buffer_map (WMV3D[1].WMV3D_buf[w_index], &info3D1, GST_MAP_READ);

        MS_DispFrameFormat *WMVdff0 =
            (MS_DispFrameFormat *) (MZ_U32) info3D0.data;
        MS_DispFrameFormat *WMVdff1 =
            (MS_DispFrameFormat *) (MZ_U32) info3D1.data;

        WMVdff0->FrameNum = 2;
        WMVdff0->CodecType = E_MSIL_VDEC_CODEC_TYPE_MVC;
        WMVdff0->sFrames[1].sHWFormat.u32LumaAddr =
            WMVdff1->sFrames[0].sHWFormat.u32LumaAddr;
        WMVdff0->sFrames[1].sHWFormat.u32ChromaAddr =
            WMVdff1->sFrames[0].sHWFormat.u32ChromaAddr;
#if VSYNC_BRIDGE
        if (mvsink->seamlessPlay) {
          vsync_bridge_set_thread (mvsink->window_id, FALSE);
        }
#endif
        vsync_bridge_render_frame ((void *) WMVdff0, VSYNC_BRIDGE_UPDATE);
        GST_DEBUG_OBJECT (mvsink, "WMV3D[%d] got pair, update index to %d",
            dff->OverlayID, w_index);
        WMV3D_W_Index = w_index;
        gst_buffer_unmap (WMV3D[0].WMV3D_buf[w_index], &info3D0);
        gst_buffer_unmap (WMV3D[1].WMV3D_buf[w_index], &info3D1);
      }
    NEXT:
      pthread_mutex_unlock (&m_Lock);

      gst_buffer_unmap (buf, &info);
      return GST_FLOW_OK;
    }
#ifdef GST_OMX_VSYNC_BRIDGE_DROP_FRAME
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf) && mvsink->max_lateness != -1) {
      unsigned long u32SHMAddr =
          vsync_bridge_get_cmd (VSYNC_BRIDGE_GET_DISPQ_SHM_ADDR);
      volatile MCU_DISPQ_INFO *pSHM =
          (volatile MCU_DISPQ_INFO *) Msil_PA2KSEG1 (u32SHMAddr +
          (dff->OverlayID * sizeof (MCU_DISPQ_INFO)));
      gint queue_thresh;
      gint queue_count;

      queue_count = (gint) pSHM->u8McuDispQWPtr - (gint) pSHM->u8McuDispQRPtr;
      if (queue_count < 0)
        queue_count += pSHM->u8DispQueNum;

      if (gst_mvsink_is_low_latency_app (mvsink))
        queue_thresh = mvsink->uiConstantDelay;
      else if (gst_mvsink_is_video_sync_player (mvsink))
        queue_thresh = 2;
      else
        queue_thresh = 4;

      GST_DEBUG_OBJECT (mvsink, "display queue count %d, thresh %d",
          queue_count, queue_thresh);
      /*
         if (!gst_mvsink_is_video_sync_player (mvsink))
         mvsink_update_render_delay (mvsink, queue_count);
       */

      if (queue_count >= queue_thresh) {
        gboolean use_drop = TRUE;

        // drop at most 1/2
        if (gst_mvsink_is_low_latency_app (mvsink)
            && mvsink->consecutive_drop_count)
          use_drop = FALSE;

        if (use_drop) {
          GST_DEBUG_OBJECT (mvsink,
              "%ld : mvsink drop pts = %lld, threshold = %u, consecutive count %d",
              MsOS_GetSystemTime (), dff->u64Pts, queue_thresh,
              mvsink->consecutive_drop_count);
          mvsink->consecutive_drop_count++;
          gst_buffer_unmap (buf, &info);
          return GST_FLOW_OK;
        }
      }
    }
#endif

    int w_index = mvsink->frame_buf_w_index;
    w_index = (w_index + 1) % MAX_FRAME_BUFF_SIZE;
    mvsink->frame_buf[w_index] = buf;
    gst_buffer_ref (buf);
#if VSYNC_BRIDGE
    if (mvsink->seamlessPlay) {
      vsync_bridge_set_thread (mvsink->window_id, FALSE);
    }
#endif
    vsync_bridge_render_frame ((void *) dff, VSYNC_BRIDGE_UPDATE);
    mvsink->frame_buf_w_index = w_index;
    mvsink->consecutive_drop_count = 0;

    GST_DEBUG_OBJECT (mvsink, "[%d]add mvsink index %d %lld, Cur->%d",
        dff->OverlayID, mvsink->frame_buf_w_index, dff->u64Pts,
        dff->u8CurIndex);

    gst_buffer_unmap (buf, &info);
  }

  if (mvsink->enInputSel == MZ_MVOP_INPUT_DRAM) {
    //Disable bluescreen immediately [FXIME]
    msil_scaler_SetBlueScreen (DISABLE, 0, 0, MZ_MAIN_WIN);
  } else {
    //Disable bluescreen after delay to wait XC consume the previous frames
    if (!bXCBlueScreen) {
      bXCBlueScreen = TRUE;
      msil_scaler_SetBlueScreen (DISABLE, 0, 0, MZ_MAIN_WIN);
    }
  }

  if (mvsink->enInputSel == MZ_MVOP_INPUT_DRAM) {
    msil_mvop_DisplayUpdateMVOPFrameAdd (MVOP_ID, mvsink->stMVopCfg.u32YOffset,
        mvsink->stMVopCfg.u32UVOffset, mvsink->stMVopCfg.bProgressive,
        mvsink->stMVopCfg.b422pack);
  }

  HERE;
  return GST_FLOW_OK;
}


static gboolean
gst_mvsink_start (GstBaseSink * bsink)
{
  UNUSED_PARA (bsink);
  //GST_DEBUG_OBJECT(mvsink,"Func: %s\n", __FUNCTION__);
  //msil_scaler_init_XC(); //no need

  HERE;
  return TRUE;
}

static gboolean
gst_mvsink_stop (GstBaseSink * bsink)
{
  UNUSED_PARA (bsink);
  //GST_DEBUG_OBJECT(mvsink,"Func: %s\n", __FUNCTION__);

  HERE;
  return TRUE;
}

static void
gst_mvsink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GSTMVSink *mvsink;

  HERE;
  mvsink = GST_MVSINK (object);
  GST_DEBUG_OBJECT (mvsink, "set prop %d", prop_id);

  switch (prop_id) {
    case PROP_INPUT:
      mvsink->enInputSel = g_value_get_enum (value);
      break;

    case PROP_DISP_X:
      mvsink->stDispWin.x = g_value_get_uint (value);
      break;

    case PROP_DISP_Y:
      mvsink->stDispWin.y = g_value_get_uint (value);
      break;

    case PROP_DISP_W:
      mvsink->stDispWin.width = g_value_get_uint (value);
      break;

    case PROP_DISP_H:
      mvsink->stDispWin.height = g_value_get_uint (value);
      break;

    case PROP_AVSYNC_TOLERANCE:
      mvsink->max_lateness = g_value_get_int64 (value);
      gst_base_sink_set_max_lateness ((GstBaseSink *) mvsink,
          mvsink->max_lateness);
      GST_DEBUG_OBJECT (mvsink, "#### SET PROP_AVSYNC_TOLERANCE [%lld]",
          mvsink->max_lateness);
      break;

    case PROP_TVMODE:
      mvsink->tvmode = g_value_get_uint (value);
      break;

    case PROP_PLANE:
      mvsink->plane = g_value_get_uint (value);
      break;

    case PROP_RECTANGLE:
    {
      GArray *array = (GArray *) g_value_get_boxed (value);
      GValue *v = g_array_index (array, GValue *, 0);
      mvsink->stDispWin.x = g_value_get_uint (v);
      v = g_array_index (array, GValue *, 1);
      mvsink->stDispWin.y = g_value_get_uint (v);
      v = g_array_index (array, GValue *, 2);
      mvsink->stDispWin.width = g_value_get_uint (v);
      v = g_array_index (array, GValue *, 3);
      mvsink->stDispWin.height = g_value_get_uint (v);

      if (mvsink->rectangle != NULL) {
        g_array_unref (mvsink->rectangle);
        mvsink->rectangle = NULL;
      }
      mvsink->rectangle = (GArray *) g_array_ref (array);
#if VSYNC_BRIDGE
      vsync_bridge_ds_set_win (mvsink->stDispWin.x, mvsink->stDispWin.y,
          mvsink->stDispWin.width, mvsink->stDispWin.height, mvsink->window_id);
#endif

      GST_DEBUG_OBJECT (mvsink, "Set Rectangle: %d %d %d %d",
          mvsink->stDispWin.x, mvsink->stDispWin.y, mvsink->stDispWin.width,
          mvsink->stDispWin.height);
    }
      break;

    case PROP_FLUSH_REPEAT_FRAME:
      mvsink->flush_repeat_frame = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (mvsink, "flush_repeat_frame %d",
          mvsink->flush_repeat_frame);

      break;

    case PROP_INTER_FRAME_DELAY:
      mvsink->inter_frame_delay = g_value_get_uint64 (value);
      break;

    case PROP_SLOW_MODE_RATE:
      mvsink->slow_mode_rate = g_value_get_uint (value);
      break;

    case PROP_STEPFRAME:
      mvsink->step_frame = g_value_get_uint (value);    // FIXME: remove variable?
      gst_element_send_event ((GstElement *) object,
          gst_event_new_step (GST_FORMAT_BUFFERS, 1, 1.0, TRUE, FALSE));
      break;

    case PROP_MUTE:
      mvsink->mute = g_value_get_boolean (value);

      if (mvsink->mute) {
        msil_scaler_EnableWindow (FALSE, MZ_MAIN_WIN);
      } else {
        msil_scaler_EnableWindow (TRUE, MZ_MAIN_WIN);
      }

      break;
    case PROP_THUMBNAIL_MODE:
      mvsink->bThumbnaiMode = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (mvsink, "#### SET PROP_THUMBNAIL_MODE [%d]",
          mvsink->bThumbnaiMode);
      break;
    case PROP_WINDOW_ID:
      mvsink->window_id = g_value_get_uint (value);
      GST_DEBUG_OBJECT (mvsink, "#### SET PROP_WINDOW_ID [%d]",
          mvsink->window_id);
      break;
    case PROP_SEAMLESS_PLAY:
      mvsink->seamlessPlay = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (mvsink, "#### SET PROP_SEAMLESS_PLAY [%d]",
          mvsink->seamlessPlay);
      vsync_bridge_set_seamless_play (mvsink->seamlessPlay);
      break;
    case PROP_TWO_STREAM_3D:
      mvsink->b2Stream3D = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (mvsink, "Enable Two Stream 3D Mode");
      break;
    case PROP_APP_TYPE:
      g_free (mvsink->app_type);
      mvsink->app_type = g_value_dup_string (value);
      /* setting NULL restores the default device */
      if (mvsink->app_type == NULL) {
        mvsink->app_type = g_strdup (DEFAULT_APP_TYPE);
      } else {
        if (gst_mvsink_is_low_latency_app (mvsink)) {
          gst_base_sink_set_max_lateness ((GstBaseSink *) mvsink,
              -1 /*360000000000 */ );
        }
        if (g_str_equal (mvsink->app_type, "SKYPE"))
          gst_base_sink_set_sync (GST_BASE_SINK (mvsink), FALSE);
      }
      GST_DEBUG_OBJECT (mvsink, "app_type=%s", mvsink->app_type);
      break;
    case PROP_LOW_DELAY:
      mvsink->uiLowDelay = g_value_get_uint (value);
      break;
    case PROP_CONSTANT_DELAY:
      mvsink->uiConstantDelay = g_value_get_uint (value);
      mvsink->uiConstantDelay =
          CLAMP (mvsink->uiConstantDelay, MIN_CONSTANT_DELAY,
          MAX_CONSTANT_DELAY);
      GST_DEBUG_OBJECT (mvsink, "constant-delay %u, adjusted to %d",
          g_value_get_uint (value), mvsink->uiConstantDelay);
      break;
    case PROP_VSINK_SYNC_ON_CLOCK:
      gst_base_sink_set_sync (GST_BASE_SINK (object),
          g_value_get_boolean (value));
      GST_DEBUG_OBJECT (mvsink, "vsink-sync-on-clock %d",
          g_value_get_boolean (value));
      break;
    case PROP_VSINK_ASYNC_HANDLING:
      gst_base_sink_set_async_enabled (GST_BASE_SINK (object),
          g_value_get_boolean (value));
      GST_DEBUG_OBJECT (mvsink, "vsink-async-handling %d",
          g_value_get_boolean (value));
      break;
    case PROP_INTERLEAVING_TYPE:
      mvsink->iInterleaving = g_value_get_int (value);
      GST_DEBUG_OBJECT (mvsink, "interleaving type %d", mvsink->iInterleaving);
      break;
    case PROP_RESOURCE_INFO:
    {
      const GstStructure *s = gst_value_get_structure (value);
      gint index;

      if (gst_structure_has_field (s, "video-port")) {
        gst_structure_get_int (s, "video-port", &index);
        mvsink->window_id = (guint) index;
        GST_DEBUG_OBJECT (mvsink, "#### SET Video-Port [%d]",
            mvsink->window_id);
      }
      //MFTEVENTFT-24570: adaptive streaming problem
      //According to LG's request, if both of max-width/mac-heith are exist,
      //regard it as seamless play mode.
      if (gst_structure_has_field (s, "max-width")
          && gst_structure_has_field (s, "max-height")) {
        mvsink->seamlessPlay = TRUE;
        GST_DEBUG_OBJECT (mvsink, "#### SET PROP_SEAMLESS_PLAY [%d]",
            mvsink->seamlessPlay);
        vsync_bridge_set_seamless_play (mvsink->seamlessPlay);
      }
    }
      break;
    case PROP_RENDER_DELAY:
      mvsink->renderDelay = g_value_get_ulong (value);
      GST_DEBUG_OBJECT (mvsink, "render-delay %lu\n", mvsink->renderDelay);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  HERE;
}


static void
gst_mvsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GSTMVSink *mvsink;

  UNUSED_PARA (value);
  HERE;

  mvsink = GST_MVSINK (object);

  switch (prop_id) {
    case PROP_INPUT:
      g_value_set_enum (value, mvsink->enInputSel);
      break;

    case PROP_DISP_X:
      g_value_set_uint (value, mvsink->stDispWin.x);
      break;

    case PROP_DISP_Y:
      g_value_set_uint (value, mvsink->stDispWin.y);
      break;

    case PROP_DISP_W:
      g_value_set_uint (value, mvsink->stDispWin.width);
      break;

    case PROP_DISP_H:
      g_value_set_uint (value, mvsink->stDispWin.height);
      break;

    case PROP_AVSYNC_TOLERANCE:
      g_value_set_int64 (value, mvsink->max_lateness);
      break;

    case PROP_TVMODE:
      g_value_set_uint (value, mvsink->tvmode);
      break;

    case PROP_PLANE:
      g_value_set_uint (value, mvsink->plane);
      break;

    case PROP_RECTANGLE:
      if (mvsink->rectangle != NULL) {
        g_value_set_boxed (value, mvsink->rectangle);
      }
      break;

    case PROP_FLUSH_REPEAT_FRAME:
      g_value_set_boolean (value, mvsink->flush_repeat_frame);
      break;

    case PROP_CURRENT_PTS:
      g_value_set_uint64 (value, mvsink->current_pts);
      break;

    case PROP_CONTENT_FRAMERATE:
      g_value_set_uint (value, mvsink->content_framerate);
      break;

    case PROP_MUTE:
      g_value_set_boolean (value, mvsink->mute);
      break;

    case PROP_SEAMLESS_PLAY:
      g_value_set_boolean (value, mvsink->seamlessPlay);
      break;

    case PROP_LOW_DELAY:
      g_value_set_uint (value, mvsink->uiLowDelay);
      break;
    case PROP_CONSTANT_DELAY:
      g_value_set_uint (value, mvsink->uiConstantDelay);
      break;
      /*
         case PROP_VSINK_SYNC_ON_CLOCK:
         g_value_set_boolean(value, mvsink->bSyncOnClock);
         break;
         case PROP_VSINK_ASYNC_HANDLING:
         g_value_set_boolean(value, mvsink->bAsyncHandle);
         break;
       */
    case PROP_INTERLEAVING_TYPE:
      g_value_set_int (value, mvsink->iInterleaving);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  HERE;
}

static GstStateChangeReturn
gst_mvsink_change_state (GstElement * element, GstStateChange transition)
{
  GSTMVSink *mvsink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstState srcState = (transition >> 3) & 0x7;
  GstState dstState = transition & 0x7;

  g_return_val_if_fail (GST_IS_FBDEVSINK (element), GST_STATE_CHANGE_FAILURE);
  mvsink = GST_MVSINK (element);
  mvsink->state = dstState;
  GST_DEBUG_OBJECT (mvsink, "%s:%lu: %s ==> %s", __FUNCTION__,
      MsOS_GetSystemTime (), gst_element_state_get_name (srcState),
      gst_element_state_get_name (dstState));

  HERE;
  vsync_bridge_set_gstpipeline_state ((GstVsyncState) dstState);
#ifdef MVSINK_DEBUG_FILE
  pFile = fopen ("/tmp/mvsink_log.txt", "a");
  if (pFile == NULL) {
    GST_DEBUG_OBJECT (mvsink, "open mvsink_log.txt fail\n");
  } else {
    fprintf (pFile,
        "[MVSINK CHANGE STATE] T = %lu, mvsink->window_id[%d], seamlessPlay[%d], mvsink->state[%d], %s ==> %s\n",
        MsOS_GetSystemTime (), mvsink->window_id, mvsink->seamlessPlay,
        mvsink->state, gst_element_state_get_name (srcState),
        gst_element_state_get_name (dstState));
  }
#endif

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_DEBUG_OBJECT (mvsink, "GST_STATE_CHANGE_NULL_TO_READY");
      msil_system_init ();

#if VSYNC_BRIDGE
      vsync_bridge_init ();
#endif
      //<-- patch for free run  -->:start
      msil_SC_ForceFreerun (TRUE);
      //<-- patch for free run  -->:end
      memset (&mvsink->stMVopCfg, 0, sizeof (MZ_MVOP_CFG));
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      mvsink->frame_buf_w_index = 0;
      mvsink->frame_buf_r_index = 0;
      mvsink->stop_thread = false;
      pthread_create (&mvsink->free_thread, NULL, gst_mvsink_free_loop, mvsink);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_mvsink_parent_class)->change_state (element,
      transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_DEBUG_OBJECT (mvsink, "fail!");
    HERE;
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
#if VSYNC_BRIDGE
      if (mvsink->seamlessPlay) {
        vsync_bridge_set_thread (mvsink->window_id, TRUE);
      }
#endif
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (mvsink, "GST_STATE_CHANGE_PAUSED_TO_READY");

      mvsink->stop_thread = true;
      pthread_join (mvsink->free_thread, NULL);
#if VSYNC_BRIDGE
      if (mvsink->seamlessPlay) {
        vsync_bridge_set_thread (mvsink->window_id, FALSE);
      }
#endif

      if (mvsink->b2Stream3D) {
        int i, j;
        pthread_mutex_lock (&m_Lock);
        for (j = 0; j < 2; j++) {
          for (i = 0; i < MAX_FRAME_BUFF_SIZE; i++) {
            if (WMV3D[j].buf_state[i]) {
              gst_buffer_unref (WMV3D[j].WMV3D_buf[i]);
              WMV3D[j].buf_state[i] = 0;
              GST_DEBUG_OBJECT (mvsink,
                  "window_id[%d] WMV3D[%d][%d] free mvsink index",
                  mvsink->window_id, j, i);
            }
          }
          WMV3D[j].buf_w_index = 0;
          WMV3D[j].flushing = 0;
        }
        WMV3D_W_Index = 0;
        WMV3D_R_Index = 0;
        pthread_mutex_unlock (&m_Lock);
      }
      //<-- patch for free run  -->:start
      msil_SC_ForceFreerun (FALSE);
      //<-- patch for free run  -->:end
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_DEBUG_OBJECT (mvsink, "GST_STATE_CHANGE_READY_TO_NULL");

#if VSYNC_BRIDGE
      vsync_bridge_close (mvsink->window_id);
#endif

#if VSYNC_BRIDGE_DS
      vsync_bridge_ds_enable (0, mvsink->window_id);
      vsync_bridge_ds_deinit ();
#endif

      // mvop disable after DS disable. Vise versa could make DS dead.
      msil_mvop_Enable (MVOP_ID, FALSE);
#ifdef MVSINK_DEBUG_FILE
      if (pFile != NULL) {
        fprintf (pFile, "[MVSINK CHANGE STATE] T = %lu, MVOP Stop !!!! \n",
            MsOS_GetSystemTime ());
      }
#endif

      if (msil_system_GetStartupMode () == MZ_STARTUP_MODE_APPLICATION) {
        msil_scaler_Exit ();
      }
      msil_scaler_SetBlueScreen (TRUE, 0, 0, MZ_MAIN_WIN);

      bXCBlueScreen = FALSE;
      break;

    default:
      break;
  }

#ifdef MVSINK_DEBUG_FILE
  if (pFile != NULL) {
    fclose (pFile);
    pFile = NULL;
  }
#endif
  HERE;
  return ret;
}


static void
gst_play_marshal_BUFFER__BOXED (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef GstBuffer *(*GMarshalFunc_OBJECT__BOXED) (gpointer data1,
      gpointer arg_1, gpointer data2);
  register GMarshalFunc_OBJECT__BOXED callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  GstBuffer *v_return;
  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 2);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_OBJECT__BOXED) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1, g_value_get_boxed (param_values + 1), data2);

  gst_value_take_buffer (return_value, v_return);
}


static void
gst_mvsink_class_init (GSTMVSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstvs_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstvs_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  HERE;

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details_simple (element_class, "mstar video sink",
      "Sink/Video", "mstar videosink",
      "elendil.luo <elendil.luo@mstarsemi.com>");

  gobject_class->set_property = gst_mvsink_set_property;
  gobject_class->get_property = gst_mvsink_get_property;
  gobject_class->finalize = gst_mvsink_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_mvsink_change_state);

  gstvs_class->set_caps = GST_DEBUG_FUNCPTR (gst_mvsink_setcaps);
  gstvs_class->get_times = GST_DEBUG_FUNCPTR (gst_mvsink_get_times);
  gstvs_class->preroll = GST_DEBUG_FUNCPTR (gst_mvsink_preroll);
  gstvs_class->start = GST_DEBUG_FUNCPTR (gst_mvsink_start);
  gstvs_class->stop = GST_DEBUG_FUNCPTR (gst_mvsink_stop);
  gstvideosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_mvsink_show_frame);

  // input type
  g_object_class_install_property (gobject_class, PROP_INPUT,
      g_param_spec_enum ("input", "INPUT",
          "Input Select for MVOP", GST_TYPE_MVSINK_INPUT,
          DEFAULT_INPUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  // display window
  g_object_class_install_property (gobject_class, PROP_DISP_X,
      g_param_spec_uint ("disp_x", "disp_x",
          "X position for display window ", 0, G_MAXUINT16, 0,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DISP_Y,
      g_param_spec_uint ("disp_y", "disp_y",
          "Y position for display window ", 0, G_MAXUINT16, 0,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DISP_W,
      g_param_spec_uint ("disp_w", "disp_w",
          "Width for display window ", 0, G_MAXUINT16, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DISP_H,
      g_param_spec_uint ("disp_h", "disp_h",
          "Height for display window ", 0, G_MAXUINT16, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_AVSYNC_TOLERANCE,
      g_param_spec_int64 ("max_lateness", "max_lateness",
          "Max. lateness/tolerance for avsync. -1 is unlimited.", -1,
          G_MAXINT64, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_TVMODE,
      g_param_spec_uint ("tvmode", "tvmode",
          "Define the television mode", 0, G_MAXUINT32, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PLANE,
      g_param_spec_uint ("plane", "plane",
          "Define the Pixel Plane to be used", 0, G_MAXUINT32, 0,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_RECTANGLE,
      g_param_spec_value_array ("rectangle", "rectangle",
          "The destination rectangle",
          g_param_spec_uint ("Element", "Rectangle Element",
              "Element of the rectangle", 0, G_MAXUINT32, 0,
              G_PARAM_READWRITE), G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_FLUSH_REPEAT_FRAME,
      g_param_spec_boolean ("flush-repeat-frame", "flush-repeat-frame",
          "Keep displaying the last frame", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CURRENT_PTS,
      g_param_spec_uint64 ("current-pts", "current-pts",
          "Get the last presented PTS value", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_INTER_FRAME_DELAY,
      g_param_spec_uint64 ("inter-frame-delay", "inter-frame-delay",
          "Enables fixed frame rate mode", 0, G_MAXUINT64, 0,
          G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_SLOW_MODE_RATE,
      g_param_spec_uint ("slow-mode-rate", "slow-mode-rate",
          "Define the television mode", 0, G_MAXUINT32, 0, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_CONTENT_FRAMERATE,
      g_param_spec_uint ("contentframerate", "contentframerate",
          "Get the content frame rate", 0, G_MAXUINT32, 0, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_STEPFRAME,
      g_param_spec_uint ("stepframe", "stepframe",
          "Step frame one by one", 0, G_MAXUINT32, 0, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "mute",
          "Mute screen", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_THUMBNAIL_MODE,
      g_param_spec_boolean ("thumbnail-mode", "Thumbnail mode",
          "thumbnail generation mode", FALSE, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_WINDOW_ID,
      g_param_spec_uint ("window-id", "window-id", "Get window id (stream id)",
          0, G_MAXUINT32, 1, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_LOW_DELAY,
      g_param_spec_uint ("low-delay", "low-delay",
          "Define low-delay", 0, G_MAXUINT32, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CONSTANT_DELAY,
      g_param_spec_uint ("constant-delay", "constant-delay",
          "Define constant-delay", 0, G_MAXUINT32, DEFAULT_CONSTANT_DELAY,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_VSINK_ASYNC_HANDLING,
      g_param_spec_boolean ("vsink-async-handling", "vsink-async-handling",
          "vsink-async-handling", FALSE, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_VSINK_SYNC_ON_CLOCK,
      g_param_spec_boolean ("vsink-sync-on-clock", "vsink-sync-on-clock",
          "vsink-sync-on-clock", FALSE, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_INTERLEAVING_TYPE,
      g_param_spec_int ("interleaving-type", "interleaving-type",
          "define interleaving type", 0, G_MAXINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_RESOURCE_INFO,
      g_param_spec_boxed ("resource-info", "Resource information",
          "Hold various information for managing resource",
          GST_TYPE_STRUCTURE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SEAMLESS_PLAY,
      g_param_spec_boolean ("seamless-play", "seamless-play", "seamless-play",
          FALSE, G_PARAM_READWRITE));
/*
    g_object_class_install_property (gobject_class, PROP_TWO_STREAM_3D,
      g_param_spec_boolean ("two-stream-3d", "two-stream-3d", "If two stream 3D mode?",
            FALSE, G_PARAM_READWRITE));

    g_object_class_install_property (gobject_class, PROP_TWO_STREAM_3D_LR_MODE,
      g_param_spec_boolean ("two-stream-3d-LR", "two-stream-3d-LR", "If two stream 3D LR mode?",
            FALSE, G_PARAM_READWRITE));
*/
  g_object_class_install_property (gobject_class, PROP_APP_TYPE,
      g_param_spec_string ("app-type", "app-type", "Set app type",
          DEFAULT_APP_TYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RENDER_DELAY,
      g_param_spec_ulong ("render-delay", "render-delay",
          "set render delay to basesink, unit is microsecond", 0, G_MAXUINT32,
          0, G_PARAM_READWRITE));

  g_signal_new ("vdec-convert-frame", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GSTMVSinkClass, convert_frame), NULL, NULL,
      gst_play_marshal_BUFFER__BOXED, GST_TYPE_BUFFER, 1, GST_TYPE_CAPS);

  g_signal_new ("last-sample", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GSTMVSinkClass, get_last_sample), NULL, NULL,
      gst_play_marshal_BUFFER__BOXED, GST_TYPE_BUFFER, 1, GST_TYPE_CAPS);

  gst_omx_video_sink_signal[VIDEO_UNDERRUN] =
      g_signal_new ("video-underrun", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);

  klass->convert_frame = gst_mvsink_convert_frame;
  klass->get_last_sample = gst_mvsink_get_last_sample;

  HERE;
}

void
mvsink_decode_yuv420sp (MZ_U8 * out, MZ_U32 outwidth, MZ_U32 outheight,
    void *srcy, void *srcuv, MZ_U32 width, MZ_U32 height, MZ_U32 cropleft,
    MZ_U32 croptop, MZ_U32 cropright, MZ_U32 cropbottom)
{
  MZ_U32 frameSize = width * height;
  MZ_U8 *src_y = (MZ_U8 *) srcy;
  MZ_U8 *src_uv = (MZ_U8 *) srcuv;
  MZ_U32 i, j, yp;
  MZ_S32 *rgb = (MZ_S32 *) malloc (frameSize * sizeof (MZ_S32));

  MZ_U32 cropwidth, cropheight, cropframeSize;

  cropwidth = width - cropleft - cropright;
  cropheight = height - croptop - cropbottom;

  cropframeSize = cropwidth * cropheight;

  MZ_S32 *croprgb = (MZ_S32 *) malloc (cropframeSize * sizeof (MZ_S32));
  MZ_S32 cropoffset = croptop * width + cropleft;

  MZ_S32 *ppSrc;
  MZ_S32 *ppDst;

  MZ_S32 u32Offset, u32Temp;
  MZ_S32 *pX_cord;

  for (j = 0, yp = 0; j < height; j++) {
    MZ_U32 uvp = (j >> 1) * width;
    MZ_S32 u = 0, v = 0;
    for (i = 0; i < width; i++, yp++) {
      MZ_S32 y = (0xff & ((MZ_S32) src_y[yp])) - 16;
      if (y < 0)
        y = 0;
      if ((i & 1) == 0) {
        v = (0xff & ((MZ_S32) src_uv[uvp++])) - 128;
        u = (0xff & ((MZ_S32) src_uv[uvp++])) - 128;
      }

      MZ_S32 y1192 = 1192 * y;
      MZ_S32 r = (y1192 + 1634 * v);
      MZ_S32 g = (y1192 - 833 * v - 400 * u);
      MZ_S32 b = (y1192 + 2066 * u);

      if (r < 0)
        r = 0;
      else if (r > 262143)
        r = 262143;
      if (g < 0)
        g = 0;
      else if (g > 262143)
        g = 262143;
      if (b < 0)
        b = 0;
      else if (b > 262143)
        b = 262143;

      //rgb[yp] = 0xff000000 | ((r << 6) & 0xff0000) | ((g >> 2) & 0xff00) | ((b >> 10) & 0xff);
      rgb[yp] =
          0xff000000 | ((b << 6) & 0xff0000) | ((g >> 2) & 0xff00) | ((r >> 10)
          & 0xff);
    }
  }

  for (j = 0, yp = 0; j < cropheight; j++) {
    for (i = 0; i < cropwidth; i++, yp++) {
      croprgb[yp] = rgb[cropoffset + i];
    }
    cropoffset += (cropwidth + cropright + cropleft);
  }

  ppSrc = croprgb;
  ppDst = (MZ_S32 *) out;

  pX_cord = (MZ_S32 *) malloc (outwidth * sizeof (MZ_S32));

  u32Temp = outwidth >> 1;      // for Rounding

  for (i = 0; i < outwidth; i++) {
    pX_cord[i] = u32Temp / outwidth;
    u32Temp += cropwidth;
  }

  u32Temp = outheight >> 1;     // for Rounding

  for (j = 0; j < outheight; j++) {
    u32Offset = u32Temp / outheight * cropwidth;

    for (i = 0; i < outwidth; i++) {
      *ppDst = ppSrc[u32Offset + pX_cord[i]];
      ppDst++;
    }
    u32Temp += cropheight;
  }

  free (pX_cord);
  free (rgb);
  free (croprgb);

#if 0
  MZ_U8 *bufferout;
  FILE *pFileout;
  pFileout = fopen ("myfileout.rgb", "wb");
  fwrite (bufferout, sizeof (MZ_U8), sizeof (rgb), pFileout);
  fclose (pFileout);
#endif
}

MZ_VDEC_Result
mvsink_convert_frame (GSTMVSink * playsink, MS_DispFrameFormat * dff,
    guint8 * thumb_buff, gint thumb_width, gint thumb_height)
{
  MZ_VDEC_DispFrm stDispFrm;
  MZ_VDEC_DispInfo stDistInfo;
  MZ_VDEC_FrameInfo *pDecFrmInfo = NULL;

  memset ((void *) &stDispFrm, 0, sizeof (MZ_VDEC_DispFrm));

  if (!thumb_buff) {
    GST_ERROR_OBJECT (playsink, "output buffer is NULL");
    return MZ_VDEC_RET_NOT_INIT;
  }

  if (!dff->sFrames[MS_VIEW_TYPE_CENTER].sHWFormat.u32LumaAddr) {
    GST_ERROR_OBJECT (playsink, "luma addr is NULL");
    return MZ_VDEC_FAIL;
  }

  MZ_VDEC_FrameInfo stDecFrmInfo;
  MS_FrameFormat *sFrame = &dff->sFrames[MS_VIEW_TYPE_CENTER];

  if (dff->CodecType == E_MSIL_VDEC_CODEC_TYPE_MJPEG) {
    // FIXME: jpd driver reply real width in MApi_VDEC_EX_GetNextDispFrame, it is different with MApi_VDEC_EX_GetDispInfo
    sFrame->u32Width =
        sFrame->u32Width + sFrame->u32CropLeft + sFrame->u32CropRight;
  }

  stDecFrmInfo.u16Width = sFrame->u32Width;
  stDecFrmInfo.u16Height = sFrame->u32Height;
  stDecFrmInfo.u16Pitch = sFrame->sHWFormat.u32LumaPitch;
  stDecFrmInfo.u32LumaAddr = sFrame->sHWFormat.u32LumaAddr;
  stDecFrmInfo.u32ChromaAddr = sFrame->sHWFormat.u32ChromaAddr;
  stDecFrmInfo.eFrameType = sFrame->eFrameType;
  stDecFrmInfo.eFieldType = sFrame->eFieldType;
  stDecFrmInfo.u32TimeStamp = dff->u64Pts / 1000;

  pDecFrmInfo = &stDecFrmInfo;
  GST_DEBUG_OBJECT (playsink,
      "Luma:0x%lx, Chroma:0x%lx,picth=%d,WxH=%dx%d,frameType=%d eFieldType=%d TimeStamp=0x%lx u32ID_L=0x%lx u32ID_H=0x%lx",
      pDecFrmInfo->u32LumaAddr, pDecFrmInfo->u32ChromaAddr,
      pDecFrmInfo->u16Pitch, pDecFrmInfo->u16Width, pDecFrmInfo->u16Height,
      pDecFrmInfo->eFrameType, pDecFrmInfo->eFieldType,
      pDecFrmInfo->u32TimeStamp, pDecFrmInfo->u32ID_L, pDecFrmInfo->u32ID_H);

  stDistInfo.u16HorSize = sFrame->u32Width;
  stDistInfo.u16VerSize = sFrame->u32Height;
  stDistInfo.u16CropLeft = sFrame->u32CropLeft;
  stDistInfo.u16CropTop = sFrame->u32CropTop;
  stDistInfo.u16CropRight = sFrame->u32CropRight;
  stDistInfo.u16CropBottom = sFrame->u32CropBottom;

  GST_DEBUG_OBJECT (playsink, "[%ld] convert frame begin",
      MsOS_GetSystemTime ());
  if (dff->CodecType == E_MSIL_VDEC_CODEC_TYPE_VP9) {
    mvsink_decode_yuv420sp (thumb_buff, thumb_width, thumb_height,
        (void *) Msil_PA2KSEG1 (pDecFrmInfo->u32LumaAddr),
        (void *) Msil_PA2KSEG1 (pDecFrmInfo->u32ChromaAddr), sFrame->u32Width,
        sFrame->u32Height, sFrame->u32CropLeft, sFrame->u32CropTop,
        sFrame->u32CropRight, sFrame->u32CropBottom);
  } else {
    msil_scaler_convert_frame (thumb_buff, thumb_width, thumb_height,
        pDecFrmInfo, &stDistInfo, playsink->enInputSel);
  }
  GST_DEBUG_OBJECT (playsink, "[%ld] convert frame finish",
      MsOS_GetSystemTime ());

  return MZ_VDEC_OK;
}

GstBuffer *
gst_mvsink_get_last_sample (GSTMVSink * playsink, GstCaps * caps)
{
  GST_DEBUG_OBJECT (playsink, "get last sample");
  return gst_mvsink_convert_frame (playsink, caps);
}

GstBuffer *
gst_mvsink_convert_frame (GSTMVSink * playsink, GstCaps * caps)
{
  const GstStructure *s = gst_caps_get_structure (caps, 0);
  MZ_VDEC_Result eRet = MZ_VDEC_FAIL;
  GstMapInfo in_info;
  GstMapInfo out_info;
  GstSample *last_sample;
  GstBuffer *in_buff;
  GstBuffer *out_buff;
  MS_DispFrameFormat *dff;
  MS_FrameFormat *sFrame;
  gint thumb_width, thumb_height;

  GST_DEBUG_OBJECT (playsink, "convert frame with caps: %" GST_PTR_FORMAT,
      caps);

  if (!gst_structure_get_int (s, "width", &thumb_width)
      || !gst_structure_get_int (s, "height", &thumb_height)) {
    GST_ERROR_OBJECT (playsink, "unable to get width/height");
    return NULL;
  }

  last_sample = gst_base_sink_get_last_sample (GST_BASE_SINK (playsink));
  if (!last_sample) {
    GST_ERROR_OBJECT (playsink, "unable to get last sample");
    return NULL;
  }

  in_buff = gst_sample_get_buffer (last_sample);
  gst_buffer_map (in_buff, &in_info, GST_MAP_READ);
  dff = (MS_DispFrameFormat *) in_info.data;
  sFrame = &dff->sFrames[MS_VIEW_TYPE_CENTER];

  GST_DEBUG_OBJECT (playsink, "input buffer %p, pts %llu, luma addr 0x%x",
      in_buff, dff->u64Pts, sFrame->sHWFormat.u32LumaAddr);
  GST_DEBUG_OBJECT (playsink, "output width %d, height %d", thumb_width,
      thumb_height);

  out_buff =
      gst_buffer_new_allocate (NULL, thumb_width * thumb_height * 4, NULL);
  if (!out_buff) {
    GST_ERROR_OBJECT (playsink, "unable to allocate output buffer");
    gst_buffer_unmap (in_buff, &in_info);
    gst_sample_unref (last_sample);
    return NULL;
  }

  gst_buffer_map (out_buff, &out_info, GST_MAP_WRITE);
  eRet =
      mvsink_convert_frame (playsink, dff, out_info.data, thumb_width,
      thumb_height);
  gst_buffer_unmap (out_buff, &out_info);
  gst_buffer_unmap (in_buff, &in_info);
  gst_sample_unref (last_sample);

  if (eRet != MZ_VDEC_OK) {
    GST_DEBUG_OBJECT (playsink, "convert frame error");
    gst_buffer_unref (out_buff);
    return NULL;
  }

  GST_DEBUG_OBJECT (playsink, "convert frame success, output buffer %p",
      out_buff);
  return out_buff;
}

static void
gst_mvsink_init (GSTMVSink * mvsink)
{
  GSTMVSinkClass *gclass = NULL;
  UNUSED_PARA (gclass);

  mvsink->sink_event_func = GST_PAD_EVENTFUNC (GST_BASE_SINK (mvsink)->sinkpad);
  gst_pad_set_event_function (GST_BASE_SINK (mvsink)->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mvsink_sink_event));

  mvsink->fps_n = 0;
  mvsink->fps_d = 0;
  mvsink->u8AspectRate = 0;
  mvsink->bProgressive = FALSE;

  mvsink->tvmode = 0;
  mvsink->plane = 0;
  mvsink->rectangle = NULL;
  mvsink->current_pts = 0;
  mvsink->flush_repeat_frame = FALSE;
  mvsink->inter_frame_delay = 0;
  mvsink->slow_mode_rate = 0;
  mvsink->step_frame = 0;
  mvsink->disp_frame_count = 0;
  mvsink->content_framerate = 0;
  mvsink->mute = FALSE;
  mvsink->XCInit = FALSE;
  mvsink->bThumbnaiMode = FALSE;
  mvsink->b2Stream3D = FALSE;
  mvsink->seamlessPlay = FALSE;
  mvsink->flushing = FALSE;
  mvsink->app_type = g_strdup (DEFAULT_APP_TYPE);
  mvsink->enInputSel = MZ_MVOP_INPUT_UNKNOWN;
  memset (&mvsink->stDispWin, 0, sizeof (MZ_WINDOW_TYPE));
  mvsink->playrate = 1.0;
  mvsink->firstFrame = TRUE;
  mvsink->firstsync = TRUE;
  mvsink->uiConstantDelay = DEFAULT_CONSTANT_DELAY;
  mvsink->uiLowDelay = 0;
  mvsink->iInterleaving = 0;
  mvsink->renderDelay = 150;
  mvsink->asf3DType = 0;
  mvsink->bSetAsf3DType = FALSE;
  mvsink->is_support_decoder = FALSE;
  mvsink->consecutive_drop_count = 0;
  mvsink->mvop_framerate = 0;
  mvsink->average_delay = 0;
  mvsink->mvop_module = -1;
  mvsink_reset_video_info (mvsink);
  msil_system_init ();
  msil_scaler_init_XC ();

  //<-- patch for LM15  -->:start
  msil_PNL_OverDriver_Enable (FALSE);
  //<-- patch for LM15  -->:end
}

static void
gst_mvsink_finalize (GObject * object)
{
  GSTMVSink *mvsink = GST_MVSINK (object);
  GST_DEBUG_OBJECT (mvsink, "enter function");
  if (mvsink->rectangle != NULL) {
    g_array_unref (mvsink->rectangle);
    mvsink->rectangle = NULL;
  }

  msil_scaler_FreezeImg (FALSE, MZ_MAIN_WIN);
  msil_scaler_EnableWindow (TRUE, MZ_MAIN_WIN);
  if (mvsink->mvop_module != -1)
    msil_mvop_ResetSetting (mvsink->mvop_module);
  g_free (mvsink->app_type);
  mvsink->app_type = NULL;

  G_OBJECT_CLASS (gst_mvsink_parent_class)->finalize (object);
}

static void
gst_mvsink_post_video_info (GstElement * element, MS_DispFrameFormat * dff,
    MS_FrameFormat * sFrame)
{
  GSTMVSink *mvsink = GST_MVSINK (element);
  MS_HDRInfo *hdr_info = &dff->stHDRInfo;
  guint u3DMode = 0;
  guint actualWidth =
      sFrame->u32Width - sFrame->u32CropLeft - sFrame->u32CropRight;
  guint actualHeight =
      sFrame->u32Height - sFrame->u32CropTop - sFrame->u32CropBottom;

  if (dff->CodecType == MZ_VDEC_CODEC_TYPE_MVC) {
    u3DMode = MEDIA_3D_TOP_AND_BOTTOM_HALF;
    GST_DEBUG_OBJECT (mvsink, "MVC");
  } else if (mvsink->bSetAsf3DType == TRUE) {
    GST_DEBUG_OBJECT (mvsink, "mvsink asf3DType:%u\n", mvsink->asf3DType);
    u3DMode = map_asf3Dtype_to_media3Dtype (mvsink, mvsink->asf3DType);
    GST_DEBUG_OBJECT (mvsink, "mvsink: the 3d mode:%u\n", u3DMode);
    if (u3DMode == MEDIA_3D_SIDE_BY_SIDE_RL
        || u3DMode == MEDIA_3D_SIDE_BY_SIDE_LR) {
      GST_DEBUG_OBJECT (mvsink,
          "3D type is full mode, orignal actualWidth:%d\n", actualWidth);
      actualWidth = actualWidth / 2;
      GST_DEBUG_OBJECT (mvsink,
          "3D type is full mode, change the width of image into half, actualWidth:%d\n",
          actualWidth);
    }
  } else if (mvsink->iInterleaving == 0) {
    u3DMode = mvsink->the3DLayout;
    GST_DEBUG_OBJECT (mvsink, "first mvsink: the 3d mode:%u\n", u3DMode);
  } else {
    u3DMode = mvsink->iInterleaving;
    GST_DEBUG_OBJECT (mvsink, "second mvsink: the 3d mode:%u\n", u3DMode);
  }

  GST_DEBUG_OBJECT (mvsink,
      "video-info, codec=%s, width=%u(%d - %d - %d), height=%u(%d - %d - %d), framerate=%u, 3D_Type=%d, PAR_Width=%d, PAR_Height=%d, Scan_Type=%d",
      mvsink_get_codec_name (mvsink, dff->CodecType), actualWidth,
      sFrame->u32Width, sFrame->u32CropLeft, sFrame->u32CropRight, actualHeight,
      sFrame->u32Height, sFrame->u32CropTop, sFrame->u32CropBottom,
      (dff->u32FrameRate == 0) ? 30000 : dff->u32FrameRate, u3DMode,
      mvsink->par_width, mvsink->par_height, dff->u8Interlace ? 1 : 0);

  GstMessage *message;
  GstStructure *s;
  gchar *stream_id = NULL;
  stream_id = gst_pad_get_stream_id (GST_BASE_SINK (element)->sinkpad);

  if (stream_id != NULL) {
    //new media-info
    //For Scan_Type, 0: Progressive, 1: Interlace
    s = gst_structure_new ("media-info",
        "stream-id", G_TYPE_STRING, stream_id,
        "type", G_TYPE_INT, STREAM_VIDEO,
        "codec", G_TYPE_STRING, mvsink_get_codec_name (mvsink, dff->CodecType),
        "width", G_TYPE_INT, actualWidth,
        "height", G_TYPE_INT, actualHeight,
        "framerate", GST_TYPE_FRACTION,
        dff->u32FrameRate == 0 ? 30000 : dff->u32FrameRate, 1000, "3D_Type",
        G_TYPE_UINT, u3DMode, "PAR_Width", G_TYPE_UINT, mvsink->par_width,
        "PAR_Height", G_TYPE_UINT, mvsink->par_height, "Scan_Type", G_TYPE_UINT,
        dff->u8Interlace ? 1 : 0, (char *) 0);

    if (hdr_info->u32FrmInfoExtAvail & 0x1) {
      gst_structure_set (s,
          "transfer-characteristics", G_TYPE_UINT,
          (guint) hdr_info->stColorDescription.u8TransferCharacteristics,
          "color-primaries", G_TYPE_UINT,
          (guint) hdr_info->stColorDescription.u8ColorPrimaries,
          "matrix-coeffs", G_TYPE_UINT,
          (guint) hdr_info->stColorDescription.u8MatrixCoefficients,
          (char *) 0);
    }

    if (hdr_info->u32FrmInfoExtAvail & 0x2) {
      gst_structure_set (s,
          "display-primaries-x0", G_TYPE_UINT,
          (guint) hdr_info->stMasterColorDisplay.u16DisplayPrimaries[0][0],
          "display-primaries-x1", G_TYPE_UINT,
          (guint) hdr_info->stMasterColorDisplay.u16DisplayPrimaries[1][0],
          "display-primaries-x2", G_TYPE_UINT,
          (guint) hdr_info->stMasterColorDisplay.u16DisplayPrimaries[2][0],
          "display-primaries-y0", G_TYPE_UINT,
          (guint) hdr_info->stMasterColorDisplay.u16DisplayPrimaries[0][1],
          "display-primaries-y1", G_TYPE_UINT,
          (guint) hdr_info->stMasterColorDisplay.u16DisplayPrimaries[1][1],
          "display-primaries-y2", G_TYPE_UINT,
          (guint) hdr_info->stMasterColorDisplay.u16DisplayPrimaries[2][1],
          "white-point-x", G_TYPE_UINT,
          (guint) hdr_info->stMasterColorDisplay.u16WhitePoint[0],
          "white-point-y", G_TYPE_UINT,
          (guint) hdr_info->stMasterColorDisplay.u16WhitePoint[1],
          "max-display-mastering-luminance", G_TYPE_UINT,
          (guint) hdr_info->stMasterColorDisplay.u32MaxLuminance,
          "min-display-mastering-luminance", G_TYPE_UINT,
          (guint) hdr_info->stMasterColorDisplay.u32MinLuminance, (char *) 0);
    }

    GST_DEBUG_OBJECT (mvsink, "Posting video-info: %" GST_PTR_FORMAT, s);

    // generate info msg
    message =
        gst_message_new_custom (GST_MESSAGE_APPLICATION, GST_OBJECT (element),
        s);

    // posting msg to upper-layer
    gst_element_post_message (GST_ELEMENT_CAST (element), message);

    g_free (stream_id);
  } else {
    GST_DEBUG_OBJECT (mvsink, "Can not find valid stream_id");
  }
}

static void
mvsink_select_inputsel (GSTMVSink * mvsink, MS_DispFrameFormat * dff)
{
  if (dff->CodecType <= E_MSIL_VDEC_CODEC_TYPE_VC1_MAIN) {
    mvsink->enInputSel = MZ_MVOP_INPUT_MVD;
  } else if (dff->CodecType <= E_MSIL_VDEC_CODEC_TYPE_RV9) {
    mvsink->enInputSel = MZ_MVOP_INPUT_RVD;
  } else if (dff->CodecType == E_MSIL_VDEC_CODEC_TYPE_HEVC) {
    mvsink->enInputSel = MZ_MVOP_INPUT_EVD;
  } else if (dff->CodecType == E_MSIL_VDEC_CODEC_TYPE_MJPEG) {
    mvsink->enInputSel = MZ_MVOP_INPUT_JPD;
  } else if (dff->CodecType == E_MSIL_VDEC_CODEC_TYPE_MVC) {
    mvsink->enInputSel = MZ_MVOP_INPUT_HVD_3DLR;
  } else if (dff->CodecType == E_MSIL_VDEC_CODEC_TYPE_VP9) {
    mvsink->enInputSel = MZ_MVOP_INPUT_VP9;
  } else {
    mvsink->enInputSel = MZ_MVOP_INPUT_H264;
  }
}

static gboolean
gst_mvsink_is_low_latency_app (GSTMVSink * mvsink)
{
  if (mvsink->app_type && g_str_equal (mvsink->app_type, "RTC"))
    return TRUE;
  if (mvsink->app_type && g_str_equal (mvsink->app_type, "CAMERA"))
    return TRUE;
  return FALSE;
}

static gboolean
gst_mvsink_is_video_sync_player (GSTMVSink * mvsink)
{
  if (mvsink->app_type && g_str_equal (mvsink->app_type, "VSP"))
    return TRUE;
  return FALSE;
}

static void
mvsink_setup_mvop (GSTMVSink * mvsink, MS_DispFrameFormat * dff)
{
  MS_FrameFormat *sFrame = &dff->sFrames[MS_VIEW_TYPE_CENTER];
  MZ_MVOP_CFG stInputCfg = { 0, };
  stInputCfg.u32YOffset = sFrame->sHWFormat.u32LumaAddr;
  stInputCfg.u32UVOffset = sFrame->sHWFormat.u32ChromaAddr;
  stInputCfg.u16HSize = sFrame->u32Width;
  stInputCfg.u16VSize = sFrame->u32Height;

  if (dff->CodecType == MZ_VDEC_CODEC_TYPE_MVC) {
    stInputCfg.u16HSize =
        sFrame->u32Width - sFrame->u32CropLeft - sFrame->u32CropRight;
    stInputCfg.u16VSize =
        sFrame->u32Height - sFrame->u32CropTop - sFrame->u32CropBottom;
  }

  stInputCfg.u16StripSize = sFrame->sHWFormat.u32LumaPitch;

  // frame rate setting for mvop can't be too low
  stInputCfg.u32FrameRate =
      (dff->u32FrameRate < 15000) ? 30000 : dff->u32FrameRate;
  stInputCfg.bProgressive = dff->u8Interlace ? 0 : 1;

  mvsink->mvop_framerate = stInputCfg.u32FrameRate;

  if (mvsink->seamlessPlay) {
    // monaco always use 4K DS
    if (stInputCfg.bProgressive && (dff->CodecType != MZ_VDEC_CODEC_TYPE_MVC)) {
      if (Msil_XC_Sys_IsPanelConnectType4K ()) {
        stInputCfg.u16HSize = DS_UHD_MAX_WIDTH;
        stInputCfg.u16VSize = DS_UHD_MAX_HEIGHT;
      } else {
        stInputCfg.u16HSize = DS_MAX_WIDTH;
        stInputCfg.u16VSize = DS_MAX_HEIGHT;
      }
    } else {
      if (stInputCfg.u16HSize <= DS_MAX_WIDTH) {
        stInputCfg.u16HSize = DS_MAX_WIDTH;
      }
      if (stInputCfg.u16VSize <= DS_MAX_HEIGHT) {
        stInputCfg.u16VSize = DS_MAX_HEIGHT;
      }
    }
  }
#if ENABLE_DUAL_USE_MAIN_VOP
  /*For only ONE mvop hardware, always set MAIN */
  mvsink->mvop_module = MZ_MVOP_MODULE_MAIN;
  GST_DEBUG_OBJECT (mvsink, "set mvop_module as MAIN");
#else
  mvsink->mvop_module =
      dff->OverlayID ? MZ_MVOP_MODULE_SUB : MZ_MVOP_MODULE_MAIN;
#endif

  GST_DEBUG_OBJECT (mvsink, "init MVOP module %d", mvsink->mvop_module);
  msil_mvop_Init (mvsink->mvop_module);
  GST_DEBUG_OBJECT (mvsink, "setup display path");
  msil_mvop_SetupDisplayPath (mvsink->mvop_module, mvsink->enInputSel,
      &stInputCfg);
#if ENABLE_DUAL_USE_MAIN_VOP
  msil_mvop_SetTimingToVdec (dff->OverlayID, mvsink->enInputSel, &stInputCfg);
#endif
}


#define MVSINK_VERSION "0.0.0.0"
#ifndef PACKAGE
#define PACKAGE "mstar_vsink"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mvsink,
    "mstar video sink",
    mvsink_init, MVSINK_VERSION, "Proprietary", "MStar GStreamer Plugin",
    "http://")
