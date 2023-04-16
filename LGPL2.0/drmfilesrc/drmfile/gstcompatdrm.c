
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gst/gst.h>
#include "gstdrmfilesrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
 if (!gst_element_register (plugin, "drmfilesrc", GST_RANK_PRIMARY, GST_TYPE_DRM_FILE_SRC ))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    drmfilesrc,
    "Drm File Source  Plugin Library",
    plugin_init, VERSION, "Proprietary", PACKAGE_NAME, "http://lge.com/")
