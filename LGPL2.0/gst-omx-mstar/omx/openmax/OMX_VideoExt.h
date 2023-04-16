/*
 * Copyright (c) 2010 The Khronos Group Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/** OMX_VideoExt.h - OpenMax IL version 1.1.2
 * The OMX_VideoExt header file contains extensions to the
 * definitions used by both the application and the component to
 * access video items.
 */

#ifndef OMX_VideoExt_h
#define OMX_VideoExt_h

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Each OMX header shall include all required header files to allow the
 * header to compile without errors.  The includes below are required
 * for this header file to compile successfully
 */
#include <OMX_Core.h>

/** NALU Formats */
typedef enum OMX_NALUFORMATSTYPE {
    OMX_NaluFormatStartCodes = 1,
    OMX_NaluFormatOneNaluPerBuffer = 2,
    OMX_NaluFormatOneByteInterleaveLength = 4,
    OMX_NaluFormatTwoByteInterleaveLength = 8,
    OMX_NaluFormatFourByteInterleaveLength = 16,
    OMX_NaluFormatCodingMax = 0x7FFFFFFF
} OMX_NALUFORMATSTYPE;

/** NAL Stream Format */
typedef struct OMX_NALSTREAMFORMATTYPE{
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_NALUFORMATSTYPE eNaluFormat;
} OMX_NALSTREAMFORMATTYPE;

/** VP8 profiles */
typedef enum OMX_VIDEO_VP8PROFILETYPE {
    OMX_VIDEO_VP8ProfileMain = 0x01,
    OMX_VIDEO_VP8ProfileUnknown = 0x6EFFFFFF,
    OMX_VIDEO_VP8ProfileMax = 0x7FFFFFFF
} OMX_VIDEO_VP8PROFILETYPE;

/** VP8 levels */
typedef enum OMX_VIDEO_VP8LEVELTYPE {
    OMX_VIDEO_VP8Level_Version0 = 0x01,
    OMX_VIDEO_VP8Level_Version1 = 0x02,
    OMX_VIDEO_VP8Level_Version2 = 0x04,
    OMX_VIDEO_VP8Level_Version3 = 0x08,
    OMX_VIDEO_VP8LevelUnknown = 0x6EFFFFFF,
    OMX_VIDEO_VP8LevelMax = 0x7FFFFFFF
} OMX_VIDEO_VP8LEVELTYPE;

/** VP8 Param */
typedef struct OMX_VIDEO_PARAM_VP8TYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_VIDEO_VP8PROFILETYPE eProfile;
    OMX_VIDEO_VP8LEVELTYPE eLevel;
    OMX_U32 nDCTPartitions;
    OMX_BOOL bErrorResilientMode;
} OMX_VIDEO_PARAM_VP8TYPE;

/** Structure for configuring VP8 reference frames */
typedef struct OMX_VIDEO_VP8REFERENCEFRAMETYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_BOOL bPreviousFrameRefresh;
    OMX_BOOL bGoldenFrameRefresh;
    OMX_BOOL bAlternateFrameRefresh;
    OMX_BOOL bUsePreviousFrame;
    OMX_BOOL bUseGoldenFrame;
    OMX_BOOL bUseAlternateFrame;
} OMX_VIDEO_VP8REFERENCEFRAMETYPE;

/** Structure for querying VP8 reference frame type */
typedef struct OMX_VIDEO_VP8REFERENCEFRAMEINFOTYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_BOOL bIsIntraFrame;
    OMX_BOOL bIsGoldenOrAlternateFrame;
} OMX_VIDEO_VP8REFERENCEFRAMEINFOTYPE;

// MStar Android Patch Begin
typedef enum OMX_EVENTTYPE_EXT
{
    OMX_EventRendererStart = OMX_EventVendorStartUnused + 1,
    OMX_EventVideoInfo,
} OMX_EVENTTYPE_EXT;

typedef enum OMX_VENDOR_3D_TYPES
{
    /* from Frame Packing Arrangement(FPA) */
    OMX_VENDER_3DTYPES_CHECKERBOARD = 0,        // pixels are alternatively from L and R
    OMX_VENDER_3DTYPES_COLUMN_ALTERNATION,      // L and R are interlaced by column
    OMX_VENDER_3DTYPES_ROW_ALTERNATION,         // L and R are interlaced by row
    OMX_VENDER_3DTYPES_SIDE_BY_SIDE,            // L is on the left, R on the right
    OMX_VENDER_3DTYPES_TOP_BOTTOM,              // L is on top, R on bottom
    OMX_VENDER_3DTYPES_FRAME_ALTERNATION,       // one view per frame
    OMX_VENDER_3DTYPES_FPA_END = OMX_VENDER_3DTYPES_FRAME_ALTERNATION,
    OMX_VENDER_3DTYPES_2D = OMX_VENDER_3DTYPES_FPA_END + 4,
}OMX_VENDER_3D_TYPES;

typedef struct OMX_VENDOR_VIDEOINFO
{
    OMX_U32 nSize;                          /* Size of the structure in bytes */
    OMX_U32 nWidth;                         /* Video Width */
    OMX_U32 nHeight;                        /* Video Height */
    OMX_U32 nFramerateNum;                  /* framerate numerator */
    OMX_U32 nFramerateDen;                  /* framerate denominator */
    OMX_VENDER_3D_TYPES n3DType;            /* 3D Type */
    OMX_U32 nParWidth;                      /* Width of PAR( Pixel Aspect Ratio) */
    OMX_U32 nParHeight;                     /* Height of PAR( Pixel Aspect Ratio) */
    OMX_U32 nScanType;                      /* 0 is progressive, 1 is interaced */
} OMX_VENDOR_VIDEOINFO;

/** Param for set renderer display format */
typedef struct OMX_VIDEO_PARAM_DISPLAYFORMAT {
    OMX_U32 nDispWin;
    OMX_U32 nDisplayX;
    OMX_U32 nDisplayY;
    OMX_U32 nDisplayWidth;
    OMX_U32 nDisplayHeight;
    OMX_U32 nCropX;
    OMX_U32 nCropY;
    OMX_U32 nCropWidth;
    OMX_U32 nCropHeight;
} OMX_VIDEO_PARAM_DISPLAYFORMAT;
// MStar Android Patch End

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* OMX_VideoExt_h */
/* File EOF */
