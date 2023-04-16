#include <gst/gst.h>
#include "gstomxvideodec.h"

typedef struct _MStarVideoMeta MStarVideoMeta;

struct _MStarVideoMeta{
    GstMeta       meta;
    GstOMXBuffer  *omx_buf;
    GstOMXVideoDec *comp;
};

MStarVideoMeta * gst_buffer_add_mstar_video_meta(GstBuffer *buffer, GstOMXVideoDec *comp, GstOMXBuffer *omx_buf);
GType mstar_video_meta_api_get_type (void);
#define MSTAR_VIDEO_META_API_TYPE (mstar_video_meta_api_get_type())

#define gst_buffer_get_mstar_video_meta(b) \
      ((MStarVideoMeta*)gst_buffer_get_meta((b),MSTAR_VIDEO_META_API_TYPE))
