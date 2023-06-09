/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OMX_AudioExt_h
#define OMX_AudioExt_h

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Each OMX header must include all required header files to allow the
 *  header to compile without errors.  The includes below are required
 *  for this header file to compile successfully
 */

#include <OMX_Core.h>


typedef enum OMX_AUDIO_CODINGEXTTYPE {
    OMX_AUDIO_ExtCodingUnused =OMX_AUDIO_CodingVendorStartUnused,
    OMX_AUDIO_CodingAC3,         /**< Any variant of AC3 encoded data */
    OMX_AUDIO_CodingDTS,         /**< Any variant of DTS encoded data */
    OMX_AUDIO_CodingMP1,         /**< Any variant of MP1 encoded data */
    OMX_AUDIO_CodingMP2,         /**< Any variant of MP2 encoded data */
} OMX_AUDIO_CODINGEXTTYPE;


/** MP1 params */
typedef struct OMX_AUDIO_PARAM_MP1TYPE {
    OMX_U32 nSize;                 /**< size of the structure in bytes */
    OMX_VERSIONTYPE nVersion;      /**< OMX specification version information */
    OMX_U32 nPortIndex;            /**< port that this structure applies to */
    OMX_U32 nChannels;             /**< Number of channels */
    OMX_U32 nBitRate;              /**< Bit rate of the input data.  Use 0 for variable
                                        rate or unknown bit rates */
    OMX_U32 nSampleRate;           /**< Sampling rate of the source data.  Use 0 for
                                        variable or unknown sampling rate. */
    OMX_U32 nAudioBandWidth;       /**< Audio band width (in Hz) to which an encoder should
                                        limit the audio signal. Use 0 to let encoder decide */
    OMX_AUDIO_CHANNELMODETYPE eChannelMode;   /**< Channel mode enumeration */
} OMX_AUDIO_PARAM_MP1TYPE;


/** MP2 params */
typedef struct OMX_AUDIO_PARAM_MP2TYPE {
    OMX_U32 nSize;                 /**< size of the structure in bytes */
    OMX_VERSIONTYPE nVersion;      /**< OMX specification version information */
    OMX_U32 nPortIndex;            /**< port that this structure applies to */
    OMX_U32 nChannels;             /**< Number of channels */
    OMX_U32 nBitRate;              /**< Bit rate of the input data.  Use 0 for variable
                                        rate or unknown bit rates */
    OMX_U32 nSampleRate;           /**< Sampling rate of the source data.  Use 0 for
                                        variable or unknown sampling rate. */
    OMX_U32 nAudioBandWidth;       /**< Audio band width (in Hz) to which an encoder should
                                        limit the audio signal. Use 0 to let encoder decide */
    OMX_AUDIO_CHANNELMODETYPE eChannelMode;   /**< Channel mode enumeration */
} OMX_AUDIO_PARAM_MP2TYPE;


/** AC3 params */
typedef struct OMX_AUDIO_PARAM_AC3TYPE {
    OMX_U32 nSize;                 /**< size of the structure in bytes */
    OMX_VERSIONTYPE nVersion;      /**< OMX specification version information */
    OMX_U32 nPortIndex;            /**< port that this structure applies to */
    OMX_U32 nChannels;             /**< Number of channels */
    OMX_U32 nBitRate;              /**< Bit rate of the input data.  Use 0 for variable
                                        rate or unknown bit rates */
    OMX_U32 nSampleRate;           /**< Sampling rate of the source data.  Use 0 for
                                        variable or unknown sampling rate. */
    // TODO: check if we need to differentiates EAC3 (DD+) and define AC3 format
    //       type if needed.
} OMX_AUDIO_PARAM_AC3TYPE;


/** DTS params */
// TODO: check the DTS decoder proposal from DD and sync to it.
typedef struct OMX_AUDIO_PARAM_DTSTYPE {
    OMX_U32 nSize;                 /**< size of the structure in bytes */
    OMX_VERSIONTYPE nVersion;      /**< OMX specification version information */
    OMX_U32 nPortIndex;            /**< port that this structure applies to */
    OMX_U32 nChannels;             /**< Number of channels */
    OMX_U32 nBitRate;              /**< Bit rate of the input data.  Use 0 for variable
                                        rate or unknown bit rates */
    OMX_U32 nSampleRate;           /**< Sampling rate of the source data.  Use 0 for
                                        variable or unknown sampling rate. */
} OMX_AUDIO_PARAM_DTSTYPE;

typedef struct OMX_AUDIO_PARAM_EASE
{
    OMX_U32        u32TargetVolume;
    OMX_U32        u32DurationInMs;
    OMX_STRING   strEaseType;
} OMX_AUDIO_PARAM_EASE;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* OMX_AudioExt_h */
