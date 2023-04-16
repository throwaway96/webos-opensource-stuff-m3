
#ifndef __GST_DRM_FILE_SRC_H__
#define __GST_DRM_FILE_SRC_H__

#include <sys/types.h>

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS

#define GST_TYPE_DRM_FILE_SRC \
  (gst_drm_file_src_get_type())
#define GST_DRM_FILE_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DRM_FILE_SRC,GstDrmFileSrc))
#define GST_DRM_FILE_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DRM_FILE_SRC,GstDrmFileSrcClass))
#define GST_IS_DRM_FILE_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DRM_FILE_SRC))
#define GST_IS_DRM_FILE_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DRM_FILE_SRC))
#define GST_DRM_FILE_SRC_CAST(obj) ((GstDrmFileSrc*) obj)

#define NONE_DRM 0
#define INKA_DRM 1

typedef struct _GstDrmFileSrc GstDrmFileSrc;
typedef struct _GstDrmFileSrcClass GstDrmFileSrcClass;

/**
 * GstDrmFileSrc:
 *
 * Opaque #GstDrmFileSrc structure.
 */
struct _GstDrmFileSrc {
  GstBaseSrc element;

  /*< private >*/
  gchar *filename;                      /* filename */
  gchar *uri;                           /* caching the URI */
  gint fd;                              /* open file descriptor */
  gint drmtype;                  /* drm component load check*/
  guint64 read_position;                /* position of fd */

  gboolean seekable;                    /* whether the file is seekable */
  gboolean is_regular;                  /* whether it's a (symlink to a) regular file */
};

struct _GstDrmFileSrcClass {
  GstBaseSrcClass parent_class;
};

G_GNUC_INTERNAL GType gst_drm_file_src_get_type (void);

G_END_DECLS

#endif /* __GST_DRM_FILE_SRC_H__ */
