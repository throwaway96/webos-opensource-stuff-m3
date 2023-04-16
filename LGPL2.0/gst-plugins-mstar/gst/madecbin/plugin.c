////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2008-2015 MStar Semiconductor, Inc.
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

#include "gstmadecbin.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  ret =
      gst_element_register (plugin, "madecbin", GST_RANK_NONE,
      GST_TYPE_MADEC_BIN);

  return ret;
}

#ifndef PACKAGE
#define PACKAGE "mstar_adecbin"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mpegdashadecbin,
    "mstar audio decoder bin for MPEG-DASH",
    plugin_init, "0.0.0.1", "Proprietary", "MStar GStreamer Plugin", "http://");
