//<MStar Software>
//******************************************************************************
//MStar Software
//Copyright (c) 2010 - 2014 MStar Semiconductor, Inc. All rights reserved.
// All software, firmware and related documentation herein ("MStar Software") are
// intellectual property of MStar Semiconductor, Inc. ("MStar") and protected by
// law, including, but not limited to, copyright law and international treaties.
// Any use, modification, reproduction, retransmission, or republication of all
// or part of MStar Software is expressly prohibited, unless prior written
// permission has been granted by MStar.
//
// By accessing, browsing and/or using MStar Software, you acknowledge that you
// have read, understood, and agree, to be bound by below terms ("Terms") and to
// comply with all applicable laws and regulations:
//
// 1. MStar shall retain any and all right, ownership and interest to MStar
//    Software and any modification/derivatives thereof.
//    No right, ownership, or interest to MStar Software and any
//    modification/derivatives thereof is transferred to you under Terms.
//
// 2. You understand that MStar Software might include, incorporate or be
//    supplied together with third party's software and the use of MStar
//    Software may require additional licenses from third parties.
//    Therefore, you hereby agree it is your sole responsibility to separately
//    obtain any and all third party right and license necessary for your use of
//    such third party's software.
//
// 3. MStar Software and any modification/derivatives thereof shall be deemed as
//    MStar's confidential information and you agree to keep MStar's
//    confidential information in strictest confidence and not disclose to any
//    third party.
//
// 4. MStar Software is provided on an "AS IS" basis without warranties of any
//    kind. Any warranties are hereby expressly disclaimed by MStar, including
//    without limitation, any warranties of merchantability, non-infringement of
//    intellectual property rights, fitness for a particular purpose, error free
//    and in conformity with any international standard.  You agree to waive any
//    claim against MStar for any loss, damage, cost or expense that you may
//    incur related to your use of MStar Software.
//    In no event shall MStar be liable for any direct, indirect, incidental or
//    consequential damages, including without limitation, lost of profit or
//    revenues, lost or damage of data, and unauthorized system use.
//    You agree that this Section 4 shall still apply without being affected
//    even if MStar Software has been modified by MStar in accordance with your
//    request or instruction for your use, except otherwise agreed by both
//    parties in writing.
//
// 5. If requested, MStar may from time to time provide technical supports or
//    services in relation with MStar Software to you for your use of
//    MStar Software in conjunction with your or your customer's product
//    ("Services").
//    You understand and agree that, except otherwise agreed by both parties in
//    writing, Services are provided on an "AS IS" basis and the warranty
//    disclaimer set forth in Section 4 above shall apply.
//
// 6. Nothing contained herein shall be construed as by implication, estoppels
//    or otherwise:
//    (a) conferring any license or right to use MStar name, trademark, service
//        mark, symbol or any other identification;
//    (b) obligating MStar or any of its affiliates to furnish any person,
//        including without limitation, you and your customers, any assistance
//        of any kind whatsoever, or any information; or
//    (c) conferring any license or right under any intellectual property right.
//
// 7. These terms shall be governed by and construed in accordance with the laws
//    of Taiwan, R.O.C., excluding its conflict of law rules.
//    Any and all dispute arising out hereof or related hereto shall be finally
//    settled by arbitration referred to the Chinese Arbitration Association,
//    Taipei in accordance with the ROC Arbitration Law and the Arbitration
//    Rules of the Association by three (3) arbitrators appointed in accordance
//    with the said Rules.
//    The place of arbitration shall be in Taipei, Taiwan and the language shall
//    be English.
//    The arbitration award shall be final and binding to both parties.
//
//******************************************************************************
//<MStar Software>

#ifndef CRYPTOINFO_H
#define CRYPTOINFO_H

#define EXTENDED_KEY_SIZE 88

#include <stdint.h>

struct CryptoInfo {
    // Borrowed from /AndroidMaster/frameworks/native/include/media/hardware/CryptoAPI.h.
    // This class should be synced with com.google.android.tv.media.Mediasource.CryptoInfo.
    enum Mode {
        kMode_Unencrypted = 0,
        kMode_AES_CTR     = 1,

        // Neither key nor iv are being used in this mode.
        // Each subsample is encrypted w/ an iv of all zeroes.
        kMode_AES_WV      = 2,  // FIX constant
        kMode_AES_MARLIN = 3,
    };
    struct SubSample {
        size_t mNumBytesOfClearData;
        size_t mNumBytesOfEncryptedData;
    };

    uint32_t mMode;
    uint8_t mKey[16];
    // UUID of the DRM system.  Refer to PIFF 5.3.1.
    uint8_t mDrmId[16];
    uint8_t mIv[16];
    uint32_t mNumSubSamples;

    // mSubSamples will have the size specified in mNumSubSamples.
    // In case of WV, each sub-sample represents the crypto unit.

#ifdef __cplusplus
    SubSample mSubSamples[1];
#else
    struct SubSample mSubSamples[1];
#endif

#ifdef BUILD_WITH_MARLIN
    //key is encoded in a complex
    struct SubSample reserved[7]; //in case subsample number is more than 1
    uint32_t ext_key_size;
    uint8_t ext_key[EXTENDED_KEY_SIZE];
#endif

#ifdef __cplusplus
    static inline CryptoInfo *newCryptoInfo(uint32_t numSubSamples) {
        CHECK_GT(numSubSamples, 0u);
        uint32_t size = sizeof(CryptoInfo) + sizeof(SubSample) * (numSubSamples - 1);
        uint8_t *p = new uint8_t[size];
        memset(p, 0, size);
        CryptoInfo* cryptoInfo = reinterpret_cast<CryptoInfo *>(p);
        cryptoInfo->mNumSubSamples = numSubSamples;
        return cryptoInfo;
    }

    static inline CryptoInfo *newCryptoInfo(const uint8_t *byteArray, size_t size) {
        uint8_t *p = new uint8_t[size];
        memcpy(p, byteArray, size);
        CryptoInfo* cryptoInfo = reinterpret_cast<CryptoInfo *>(p);

        CHECK_EQ(size, sizeof(CryptoInfo) + sizeof(SubSample) * (cryptoInfo->mNumSubSamples - 1));
        return cryptoInfo;
    }

    static inline void deleteCryptoInfo(CryptoInfo *cryptoInfo) {
        uint8_t *p = reinterpret_cast<uint8_t *>(cryptoInfo);
        delete[] p;
    }
#endif
};

#define UUID_WIDEVINE  "\xed\xef\x8b\xa9\x79\xd6\x4a\xce\xa3\xc8\x27\xdc\xd5\x1d\x21\xed"
#define UUID_PLAYREADY "\x9a\x04\xf0\x79\x98\x40\x42\x86\xab\x92\xe6\x5b\xe0\x88\x5f\x95"

#endif  // CRYPTOINFO_H
