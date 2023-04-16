#include "mstar_video_meta.h"

GType
mstar_video_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "mstar", "video", NULL };
  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("MStarVideoMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}


const GstMetaInfo *mstar_video_meta_get_info (void);
#define MSTAR_VIDEO_META_INFO (mstar_video_meta_get_info())

static gboolean
mstar_video_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  MStarVideoMeta *mstarmeta = (MStarVideoMeta *) meta;

  mstarmeta->comp = NULL;
  mstarmeta->omx_buf = NULL;

  return TRUE;
}

MStarVideoMeta *
gst_buffer_add_mstar_video_meta (GstBuffer * buffer, GstOMXVideoDec * comp,
    GstOMXBuffer * omx_buf)
{
  MStarVideoMeta *mstarmeta;
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  mstarmeta =
      (MStarVideoMeta *) gst_buffer_add_meta (buffer, MSTAR_VIDEO_META_INFO,
      NULL);

  mstarmeta->comp = comp;
  mstarmeta->omx_buf = omx_buf;

  return mstarmeta;
}

static gboolean
mstar_video_meta_transform (GstBuffer * transbuf, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  MStarVideoMeta *mstarmeta = (MStarVideoMeta *) meta;
  /* we always copy no matter what transform */
  gst_buffer_add_mstar_video_meta (transbuf, mstarmeta->comp,
      mstarmeta->omx_buf);
  return TRUE;
}

//static void mstar_video_meta_free (GstMeta * meta, GstBuffer * buffer)
static void
mstar_video_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  MStarVideoMeta *mstarmeta = (MStarVideoMeta *) meta;
  gst_omx_port_release_buffer (mstarmeta->comp->dec_out_port,
      mstarmeta->omx_buf);
}

const GstMetaInfo *
mstar_video_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;
  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (MSTAR_VIDEO_META_API_TYPE,
        "MstarVideoMeta",
        sizeof (MStarVideoMeta),
        mstar_video_meta_init,
        mstar_video_meta_free,
        mstar_video_meta_transform);
    g_once_init_leave (&meta_info, mi);
  }
  return meta_info;
}
