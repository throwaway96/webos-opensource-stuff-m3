/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2013, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <string.h>

#include "gstomxvideodec.h"

#define MSTAR_GSTOMX
#ifdef MSTAR_GSTOMX
#include "mstar_video_meta.h"
#include "OMX_Video.h"
#endif

GST_DEBUG_CATEGORY_STATIC (gst_omx_video_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_video_dec_debug_category

#define DEFAULT_APP_TYPE        "default_app_type"
#define DEFAULT_SVP_VERSION     0

typedef struct _GstOMXMemory GstOMXMemory;
typedef struct _GstOMXMemoryAllocator GstOMXMemoryAllocator;
typedef struct _GstOMXMemoryAllocatorClass GstOMXMemoryAllocatorClass;

struct _GstOMXMemory
{
  GstMemory mem;

  GstOMXBuffer *buf;
};

struct _GstOMXMemoryAllocator
{
  GstAllocator parent;
};

struct _GstOMXMemoryAllocatorClass
{
  GstAllocatorClass parent_class;
};

#define GST_OMX_MEMORY_TYPE "openmax"

static GstMemory *
gst_omx_memory_allocator_alloc_dummy (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_assert_not_reached ();
  return NULL;
}

static void
gst_omx_memory_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstOMXMemory *omem = (GstOMXMemory *) mem;

  /* TODO: We need to remember which memories are still used
   * so we can wait until everything is released before allocating
   * new memory
   */

  g_slice_free (GstOMXMemory, omem);
}

static gpointer
gst_omx_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstOMXMemory *omem = (GstOMXMemory *) mem;

  return omem->buf->omx_buf->pBuffer + omem->mem.offset;
}

static void
gst_omx_memory_unmap (GstMemory * mem)
{
}

static GstMemory *
gst_omx_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  g_assert_not_reached ();
  return NULL;
}

GType gst_omx_memory_allocator_get_type (void);
G_DEFINE_TYPE (GstOMXMemoryAllocator, gst_omx_memory_allocator,
    GST_TYPE_ALLOCATOR);

#define GST_TYPE_OMX_MEMORY_ALLOCATOR   (gst_omx_memory_allocator_get_type())
#define GST_IS_OMX_MEMORY_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_OMX_MEMORY_ALLOCATOR))

static void
gst_omx_memory_allocator_class_init (GstOMXMemoryAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = gst_omx_memory_allocator_alloc_dummy;
  allocator_class->free = gst_omx_memory_allocator_free;
}

static void
gst_omx_memory_allocator_init (GstOMXMemoryAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_OMX_MEMORY_TYPE;
  alloc->mem_map = gst_omx_memory_map;
  alloc->mem_unmap = gst_omx_memory_unmap;
  alloc->mem_share = gst_omx_memory_share;

  /* default copy & is_span */

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static GstMemory *
gst_omx_memory_allocator_alloc (GstAllocator * allocator, GstMemoryFlags flags,
    GstOMXBuffer * buf)
{
  GstOMXMemory *mem;

  /* FIXME: We don't allow sharing because we need to know
   * when the memory becomes unused and can only then put
   * it back to the pool. Which is done in the pool's release
   * function
   */
  flags |= GST_MEMORY_FLAG_NO_SHARE;

  mem = g_slice_new (GstOMXMemory);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (mem), flags, allocator, NULL,
      buf->omx_buf->nAllocLen, buf->port->port_def.nBufferAlignment,
      0, buf->omx_buf->nAllocLen);

  mem->buf = buf;

  return GST_MEMORY_CAST (mem);
}

/* Buffer pool for the buffers of an OpenMAX port.
 *
 * This pool is only used if we either passed buffers from another
 * pool to the OMX port or provide the OMX buffers directly to other
 * elements.
 *
 *
 * A buffer is in the pool if it is currently owned by the port,
 * i.e. after OMX_{Fill,Empty}ThisBuffer(). A buffer is outside
 * the pool after it was taken from the port after it was handled
 * by the port, i.e. {Empty,Fill}BufferDone.
 *
 * Buffers can be allocated by us (OMX_AllocateBuffer()) or allocated
 * by someone else and (temporarily) passed to this pool
 * (OMX_UseBuffer(), OMX_UseEGLImage()). In the latter case the pool of
 * the buffer will be overriden, and restored in free_buffer(). Other
 * buffers are just freed there.
 *
 * The pool always has a fixed number of minimum and maximum buffers
 * and these are allocated while starting the pool and released afterwards.
 * They correspond 1:1 to the OMX buffers of the port, which are allocated
 * before the pool is started.
 *
 * Acquiring a buffer from this pool happens after the OMX buffer has
 * been acquired from the port. gst_buffer_pool_acquire_buffer() is
 * supposed to return the buffer that corresponds to the OMX buffer.
 *
 * For buffers provided to upstream, the buffer will be passed to
 * the component manually when it arrives and then unreffed. If the
 * buffer is released before reaching the component it will be just put
 * back into the pool as if EmptyBufferDone has happened. If it was
 * passed to the component, it will be back into the pool when it was
 * released and EmptyBufferDone has happened.
 *
 * For buffers provided to downstream, the buffer will be returned
 * back to the component (OMX_FillThisBuffer()) when it is released.
 */

static GQuark gst_omx_buffer_data_quark = 0;

#define GST_OMX_BUFFER_POOL(pool) ((GstOMXBufferPool *) pool)
typedef struct _GstOMXBufferPool GstOMXBufferPool;
typedef struct _GstOMXBufferPoolClass GstOMXBufferPoolClass;

struct _GstOMXBufferPool
{
  GstVideoBufferPool parent;

  GstElement *element;

  GstCaps *caps;
  gboolean add_videometa;
  GstVideoInfo video_info;

  /* Owned by element, element has to stop this pool before
   * it destroys component or port */
  GstOMXComponent *component;
  GstOMXPort *port;

  /* For handling OpenMAX allocated memory */
  GstAllocator *allocator;

  /* Set from outside this pool */
  /* TRUE if we're currently allocating all our buffers */
  gboolean allocating;

  /* TRUE if the pool is not used anymore */
  gboolean deactivated;

  /* For populating the pool from another one */
  GstBufferPool *other_pool;
  GPtrArray *buffers;

  /* Used during acquire for output ports to
   * specify which buffer has to be retrieved
   * and during alloc, which buffer has to be
   * wrapped
   */
  gint current_buffer_index;
};

struct _GstOMXBufferPoolClass
{
  GstVideoBufferPoolClass parent_class;
};

GType gst_omx_buffer_pool_get_type (void);

G_DEFINE_TYPE (GstOMXBufferPool, gst_omx_buffer_pool, GST_TYPE_BUFFER_POOL);

static gboolean
gst_omx_buffer_pool_start (GstBufferPool * bpool)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);

  /* Only allow to start the pool if we still are attached
   * to a component and port */
  GST_OBJECT_LOCK (pool);
  if (!pool->component || !pool->port) {
    GST_OBJECT_UNLOCK (pool);
    return FALSE;
  }
  GST_OBJECT_UNLOCK (pool);

  return
      GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->start (bpool);
}

static gboolean
gst_omx_buffer_pool_stop (GstBufferPool * bpool)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);

  /* Remove any buffers that are there */
  g_ptr_array_set_size (pool->buffers, 0);

  if (pool->caps)
    gst_caps_unref (pool->caps);
  pool->caps = NULL;

  pool->add_videometa = FALSE;

  return GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->stop (bpool);
}

static const gchar **
gst_omx_buffer_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *raw_video_options[] =
      { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL };
  static const gchar *options[] = { NULL };
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);

  GST_OBJECT_LOCK (pool);
  if (pool->port && pool->port->port_def.eDomain == OMX_PortDomainVideo
      && pool->port->port_def.format.video.eCompressionFormat ==
      OMX_VIDEO_CodingUnused) {
    GST_OBJECT_UNLOCK (pool);
    return raw_video_options;
  }
  GST_OBJECT_UNLOCK (pool);

  return options;
}

static gboolean
gst_omx_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstCaps *caps;

  GST_OBJECT_LOCK (pool);

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  if (pool->port && pool->port->port_def.eDomain == OMX_PortDomainVideo
      && pool->port->port_def.format.video.eCompressionFormat ==
      OMX_VIDEO_CodingUnused) {
    GstVideoInfo info;

    /* now parse the caps from the config */
    if (!gst_video_info_from_caps (&info, caps))
      goto wrong_video_caps;

    /* enable metadata based on config of the pool */
    pool->add_videometa =
        gst_buffer_pool_config_has_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    pool->video_info = info;
  }

  if (pool->caps)
    gst_caps_unref (pool->caps);
  pool->caps = gst_caps_ref (caps);

  GST_OBJECT_UNLOCK (pool);

  return GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->set_config
      (bpool, config);

  /* ERRORS */
wrong_config:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_video_caps:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static GstFlowReturn
gst_omx_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstBuffer *buf;
  GstOMXBuffer *omx_buf;

  g_return_val_if_fail (pool->allocating, GST_FLOW_ERROR);

  omx_buf = g_ptr_array_index (pool->port->buffers, pool->current_buffer_index);
  g_return_val_if_fail (omx_buf != NULL, GST_FLOW_ERROR);

  if (pool->other_pool) {
    guint i, n;

    buf = g_ptr_array_index (pool->buffers, pool->current_buffer_index);
    g_assert (pool->other_pool == buf->pool);
    gst_object_replace ((GstObject **) & buf->pool, NULL);

    n = gst_buffer_n_memory (buf);
    for (i = 0; i < n; i++) {
      GstMemory *mem = gst_buffer_peek_memory (buf, i);

      /* FIXME: We don't allow sharing because we need to know
       * when the memory becomes unused and can only then put
       * it back to the pool. Which is done in the pool's release
       * function
       */
      GST_MINI_OBJECT_FLAG_SET (mem, GST_MEMORY_FLAG_NO_SHARE);
    }

    if (pool->add_videometa) {
      GstVideoMeta *meta;

      meta = gst_buffer_get_video_meta (buf);
      if (!meta) {
        gst_buffer_add_video_meta (buf, GST_VIDEO_FRAME_FLAG_NONE,
            GST_VIDEO_INFO_FORMAT (&pool->video_info),
            GST_VIDEO_INFO_WIDTH (&pool->video_info),
            GST_VIDEO_INFO_HEIGHT (&pool->video_info));
      }
    }
  } else {
    GstMemory *mem;

    mem = gst_omx_memory_allocator_alloc (pool->allocator, 0, omx_buf);
    buf = gst_buffer_new ();
    gst_buffer_append_memory (buf, mem);
    g_ptr_array_add (pool->buffers, buf);

    if (pool->add_videometa) {
      gsize offset[4] = { 0, };
      gint stride[4] = { 0, };

      switch (pool->video_info.finfo->format) {
        case GST_VIDEO_FORMAT_I420:
          offset[0] = 0;
          stride[0] = pool->port->port_def.format.video.nStride;
          offset[1] =
              stride[0] * pool->port->port_def.format.video.nSliceHeight;
          stride[1] = pool->port->port_def.format.video.nStride / 2;
          offset[2] =
              offset[1] +
              stride[1] * (pool->port->port_def.format.video.nSliceHeight / 2);
          stride[2] = pool->port->port_def.format.video.nStride / 2;
          break;
        case GST_VIDEO_FORMAT_NV12:
          offset[0] = 0;
          stride[0] = pool->port->port_def.format.video.nStride;
          offset[1] =
              stride[0] * pool->port->port_def.format.video.nSliceHeight;
          stride[1] = pool->port->port_def.format.video.nStride;
          break;
        default:
          g_assert_not_reached ();
          break;
      }

      gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_INFO_FORMAT (&pool->video_info),
          GST_VIDEO_INFO_WIDTH (&pool->video_info),
          GST_VIDEO_INFO_HEIGHT (&pool->video_info),
          GST_VIDEO_INFO_N_PLANES (&pool->video_info), offset, stride);
    }
  }

  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (buf),
      gst_omx_buffer_data_quark, omx_buf, NULL);

  *buffer = buf;

  pool->current_buffer_index++;

  return GST_FLOW_OK;
}

static void
gst_omx_buffer_pool_free_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);

  /* If the buffers belong to another pool, restore them now */
  GST_OBJECT_LOCK (pool);
  if (pool->other_pool) {
    gst_object_replace ((GstObject **) & buffer->pool,
        (GstObject *) pool->other_pool);
  }
  GST_OBJECT_UNLOCK (pool);

  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (buffer),
      gst_omx_buffer_data_quark, NULL, NULL);

  GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->free_buffer (bpool,
      buffer);
}

static GstFlowReturn
gst_omx_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);

  if (pool->port->port_def.eDir == OMX_DirOutput) {
    GstBuffer *buf;

    g_return_val_if_fail (pool->current_buffer_index != -1, GST_FLOW_ERROR);

    buf = g_ptr_array_index (pool->buffers, pool->current_buffer_index);
    g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);
    *buffer = buf;
    ret = GST_FLOW_OK;

    /* If it's our own memory we have to set the sizes */
    if (!pool->other_pool) {
      GstMemory *mem = gst_buffer_peek_memory (*buffer, 0);

      g_assert (mem
          && g_strcmp0 (mem->allocator->mem_type, GST_OMX_MEMORY_TYPE) == 0);
      mem->size = ((GstOMXMemory *) mem)->buf->omx_buf->nFilledLen;
      mem->offset = ((GstOMXMemory *) mem)->buf->omx_buf->nOffset;
    }
  } else {
    /* Acquire any buffer that is available to be filled by upstream */
    ret =
        GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->acquire_buffer
        (bpool, buffer, params);
  }

  return ret;
}

static void
gst_omx_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  OMX_ERRORTYPE err;
  GstOMXBuffer *omx_buf;

  g_assert (pool->component && pool->port);

  if (!pool->allocating && !pool->deactivated) {
    omx_buf =
        gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (buffer),
        gst_omx_buffer_data_quark);
    if (pool->port->port_def.eDir == OMX_DirOutput && !omx_buf->used) {
      /* Release back to the port, can be filled again */
      err = gst_omx_port_release_buffer (pool->port, omx_buf);
      if (err != OMX_ErrorNone) {
        GST_ELEMENT_ERROR (pool->element, LIBRARY, SETTINGS, (NULL),
            ("Failed to relase output buffer to component: %s (0x%08x)",
                gst_omx_error_to_string (err), err));
      }
    } else if (!omx_buf->used) {
      /* TODO: Implement.
       *
       * If not used (i.e. was not passed to the component) this should do
       * the same as EmptyBufferDone.
       * If it is used (i.e. was passed to the component) this should do
       * nothing until EmptyBufferDone.
       *
       * EmptyBufferDone should release the buffer to the pool so it can
       * be allocated again
       *
       * Needs something to call back here in EmptyBufferDone, like keeping
       * a ref on the buffer in GstOMXBuffer until EmptyBufferDone... which
       * would ensure that the buffer is always unused when this is called.
       */
      g_assert_not_reached ();
      GST_BUFFER_POOL_CLASS (gst_omx_buffer_pool_parent_class)->release_buffer
          (bpool, buffer);
    }
  }
}

static void
gst_omx_buffer_pool_finalize (GObject * object)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (object);

  if (pool->element)
    gst_object_unref (pool->element);
  pool->element = NULL;

  if (pool->buffers)
    g_ptr_array_unref (pool->buffers);
  pool->buffers = NULL;

  if (pool->other_pool)
    gst_object_unref (pool->other_pool);
  pool->other_pool = NULL;

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (pool->caps)
    gst_caps_unref (pool->caps);
  pool->caps = NULL;

  G_OBJECT_CLASS (gst_omx_buffer_pool_parent_class)->finalize (object);
}

static void
gst_omx_buffer_pool_class_init (GstOMXBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gst_omx_buffer_data_quark = g_quark_from_static_string ("GstOMXBufferData");

  gobject_class->finalize = gst_omx_buffer_pool_finalize;
  gstbufferpool_class->start = gst_omx_buffer_pool_start;
  gstbufferpool_class->stop = gst_omx_buffer_pool_stop;
  gstbufferpool_class->get_options = gst_omx_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_omx_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_omx_buffer_pool_alloc_buffer;
  gstbufferpool_class->free_buffer = gst_omx_buffer_pool_free_buffer;
  gstbufferpool_class->acquire_buffer = gst_omx_buffer_pool_acquire_buffer;
  gstbufferpool_class->release_buffer = gst_omx_buffer_pool_release_buffer;
}

static void
gst_omx_buffer_pool_init (GstOMXBufferPool * pool)
{
  pool->buffers = g_ptr_array_new ();
  pool->allocator = g_object_new (gst_omx_memory_allocator_get_type (), NULL);
}

static GstBufferPool *
gst_omx_buffer_pool_new (GstElement * element, GstOMXComponent * component,
    GstOMXPort * port)
{
  GstOMXBufferPool *pool;

  pool = g_object_new (gst_omx_buffer_pool_get_type (), NULL);
  pool->element = gst_object_ref (element);
  pool->component = component;
  pool->port = port;

  return GST_BUFFER_POOL (pool);
}

typedef struct _BufferIdentification BufferIdentification;
struct _BufferIdentification
{
  guint64 timestamp;
  GstClockTime start_time;
  GstClockTime expire_time;
};

static void
buffer_identification_free (BufferIdentification * id)
{
  g_slice_free (BufferIdentification, id);
}

/* prototypes */
static void gst_omx_video_dec_finalize (GObject * object);

static GstStateChangeReturn
gst_omx_video_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_omx_video_dec_open (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_close (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_start (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_omx_video_dec_reset (GstVideoDecoder * decoder,
    gboolean hard);
static GstFlowReturn gst_omx_video_dec_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos);
static GstFlowReturn gst_omx_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_omx_video_dec_finish (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_decide_allocation (GstVideoDecoder * bdec,
    GstQuery * query);

static GstFlowReturn gst_omx_video_dec_drain (GstOMXVideoDec * self,
    gboolean is_eos);

static OMX_ERRORTYPE gst_omx_video_dec_allocate_output_buffers (GstOMXVideoDec *
    self);
static OMX_ERRORTYPE gst_omx_video_dec_deallocate_output_buffers (GstOMXVideoDec
    * self);
static void gst_omx_video_dec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void gst_omx_video_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void push_3DType_event (GstOMXVideoDec * vdec, const GValue * val);

static void push_buffer_underrun_event (GstOMXVideoDec * vdec);

static gboolean gst_omx_video_dec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_omx_video_dec_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean parse_h264_nal (GstOMXVideoDec * self, const guint8 * data,
    gint size, gboolean * au_start, gboolean * sync_point);
static gboolean parse_h265_nal (GstOMXVideoDec * self, const guint8 * data,
    gint size, gboolean * au_start, gboolean * sync_point);
static gboolean parse_m2v_start_code (GstOMXVideoDec * self,
    const guint8 * data, gint size, gboolean * au_start, gboolean * sync_point);

static const guint8 *search_nal_unit (const guint8 * data, gint size);
static const guint8 *search_m2v_start_code (const guint8 * data, gint size);

static void set_packetized_mode (GstOMXVideoDec * self);
static void set_trick_mode (GstOMXVideoDec * self);

static void signal_rewind_unblocking (GstOMXVideoDec * self);

enum
{
  PROP_0,
  PROP_IS_SVP,
  PROP_DECODED_SIZE,
  PROP_UNDECODED_SIZE,
  PROP_VDEC_CH,
  PROP_APP_TYPE,
  PROP_PORT,
  PROP_RESOURCE_INFO,
  PROP_SVP_VERSION,
  PROP_CLIP_MODE,
  PROP_DISPLAYED_FRAMES,
  PROP_DROPPED_FRAMES
};

typedef enum
{
  GST_NONE_PARSE_FORMAT_NONE = 0,
  GST_H264_PARSE_FORMAT_AVC,
  GST_H264_PARSE_FORMAT_AVC3,
  GST_H265_PARSE_FORMAT_HVC1,
  GST_H265_PARSE_FORMAT_HEV1
} ParseFormat;

ParseFormat parse_format_array[] = {
  GST_NONE_PARSE_FORMAT_NONE,
  GST_H264_PARSE_FORMAT_AVC,
  GST_H264_PARSE_FORMAT_AVC3,
  GST_H265_PARSE_FORMAT_HVC1,
  GST_H265_PARSE_FORMAT_HEV1
};

enum
{
  /* FILL ME */
  SVP_HANDLE_CALLBACK,
  LAST_SIGNAL
};
static guint gst_omx_video_dec_signal[LAST_SIGNAL] = { 0 };


enum TrickMode
{
  TRICK_ALL,
  TRICK_IP,
  TRICK_I,
  TRICK_SINGLE_DECODE,
  TRICK_DUAL_DECODE,
  TRICK_DROP_ERR_ON,
  TRICK_DROP_ERR_OFF,
  TRICK_SEAMLESS_ON,
  TRICK_SEAMLESS_OFF,
  TRICK_USE_MAIN,
  TRICK_USE_SUB,
  TRICK_SEAMLESS_DS,
};

enum InputAlignment
{
  ALIGNMENT_NONE,
  ALIGNMENT_SC,
  ALIGNMENT_AU
};

// id 0  : MAIN STREAM
// other : SUB STREAM
static void
set_stream_by_port (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (decoder);
  guint32 stream_mode;

  if (self->port) {
    stream_mode = TRICK_USE_SUB;
    GST_WARNING_OBJECT (self, "port %d, use sub", self->port);
  } else {
    stream_mode = TRICK_USE_MAIN;
    GST_WARNING_OBJECT (self, "port %d, use main", self->port);
  }
  gst_omx_component_set_parameter (self->dec, OMX_IndexMstarTrickMode,
      &stream_mode);
}

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_video_dec_debug_category, "omxvideodec", 0, \
      "debug category for gst-omx video decoder base class");


G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXVideoDec, gst_omx_video_dec,
    GST_TYPE_VIDEO_DECODER, DEBUG_INIT);

static void
gst_omx_video_dec_class_init (GstOMXVideoDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_omx_video_dec_finalize;
  gobject_class->set_property = gst_omx_video_dec_set_property;
  gobject_class->get_property = gst_omx_video_dec_get_property;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_change_state);

  video_decoder_class->open = GST_DEBUG_FUNCPTR (gst_omx_video_dec_open);
  video_decoder_class->close = GST_DEBUG_FUNCPTR (gst_omx_video_dec_close);
  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_video_dec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_video_dec_stop);
  video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_omx_video_dec_reset);
  video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_set_format);
  video_decoder_class->parse = GST_DEBUG_FUNCPTR (gst_omx_video_dec_parse);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_handle_frame);
  video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_omx_video_dec_finish);
  video_decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_decide_allocation);

  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_uint ("port", "port", "port index (stream id), set only",
          0, G_MAXUINT32, 1, G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_IS_SVP,
      g_param_spec_boolean ("is-svp", "is-svp", "is svp, set only",
          FALSE, G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_DECODED_SIZE,
      g_param_spec_uint64 ("decoded-size", "decoded-size",
          "decode size, read only", 0, G_MAXUINT64, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_UNDECODED_SIZE,
      g_param_spec_uint64 ("undecoded-size", "undecoded-size",
          "undecoded size, read only", 0, G_MAXUINT64, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VDEC_CH,
      g_param_spec_uint ("vdec-ch", "vdec-ch", "vdec channel, set only", 0,
          G_MAXUINT32, 1, G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_APP_TYPE,
      g_param_spec_string ("app-type", "app-type", "Set app type",
          DEFAULT_APP_TYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_RESOURCE_INFO,
      g_param_spec_boxed ("resource-info", "Resource information",
          "Hold various information for managing resource", GST_TYPE_STRUCTURE,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SVP_VERSION,
      g_param_spec_uint ("svp-version", "svp-version",
          "svp version information", 0, G_MAXUINT32, DEFAULT_SVP_VERSION,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CLIP_MODE,
      g_param_spec_boolean ("clip-mode", "clip-mode",
          "Clip frames which are outside of segment", FALSE, G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_DISPLAYED_FRAMES,
      g_param_spec_int ("displayed-frames", "displayed-frames",
          "the accumulated number of rendered frames", 0, G_MAXINT, 1,
          G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, PROP_DROPPED_FRAMES,
      g_param_spec_int ("dropped-frames", "dropped-frames",
          "the accumulated number of dropped frames", 0, G_MAXINT, 1,
          G_PARAM_READABLE));

  klass->cdata.default_src_template_caps = "video/x-raw, "
      "vendor = mstar, "
      "width = " GST_VIDEO_SIZE_RANGE ", "
      "height = " GST_VIDEO_SIZE_RANGE ", " "framerate = " GST_VIDEO_FPS_RANGE;

  gst_omx_video_dec_signal[SVP_HANDLE_CALLBACK] =
      g_signal_new ("svp-handle", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 1, G_TYPE_ULONG);
}

static void
gst_omx_video_dec_init (GstOMXVideoDec * self)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

  g_mutex_init (&self->drain_lock);
  g_cond_init (&self->drain_cond);
  g_mutex_init (&self->rewind_lock);
  g_cond_init (&self->rewind_cond);
  // USE MAIN by default
  self->port = 0;
  self->max_width = 0;
  self->max_height = 0;
  self->is_svp = FALSE;
  self->clip_mode = FALSE;
  self->direct_rewind_push = TRUE;
  self->decoded_size = 0;
  self->undecoded_size = 0;
  self->vdec_ch = 0;
  self->interleaving = 0;
  self->app_type = g_strdup (DEFAULT_APP_TYPE);;
  self->svp_version = DEFAULT_SVP_VERSION;
  self->str2stream_type = g_hash_table_new (g_str_hash, g_str_equal);
  if (self->str2stream_type == NULL) {
    GST_ERROR_OBJECT (self,
        "can't construct string to stream type table and it will cause errors\n");
  }
  g_hash_table_insert (self->str2stream_type, "avc", &parse_format_array[1]);
  g_hash_table_insert (self->str2stream_type, "avc3", &parse_format_array[2]);
  g_hash_table_insert (self->str2stream_type, "hvc1", &parse_format_array[3]);
  g_hash_table_insert (self->str2stream_type, "hev1", &parse_format_array[4]);
  gst_pad_set_event_function (GST_VIDEO_DECODER_SINK_PAD (self),
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_sink_event));
  gst_pad_set_event_function (GST_VIDEO_DECODER_SRC_PAD (self),
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_src_event));
}

static gboolean
gst_omx_video_dec_open (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (decoder);
  GstOMXVideoDecClass *klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);
  gint in_port_index, out_port_index;

  GST_DEBUG_OBJECT (self, "Opening decoder");

  self->dec =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->cdata.core_name,
      klass->cdata.component_name, klass->cdata.component_role,
      klass->cdata.hacks);
  self->started = FALSE;
  self->input_drop = TRUE;
  self->no_soft_flush = TRUE;
  self->trick_mode = TRICK_ALL;
  self->avg_decode = GST_CLOCK_TIME_NONE;
  self->first_decoded_time = GST_CLOCK_TIME_NONE;
  self->first_decoded_pts = GST_CLOCK_TIME_NONE;
  self->displayed_frames = 0;
  self->dropped_frames = 0;
  self->has_first_vcl = TRUE;
  self->has_first_au = FALSE;
  self->sync_point = FALSE;
  self->parse_alignment = ALIGNMENT_NONE;
  self->caps = NULL;
  self->media_type = NULL;
  self->skip_to_i = TRUE;
  self->use_dts = FALSE;
  self->search_start_code = NULL;
  self->parse_start_code = NULL;
  set_stream_by_port (decoder);

  if (!self->dec) {
    GST_DEBUG_OBJECT (self, "Opening decoder fail");
    return FALSE;
  }
  if (gst_omx_component_get_state (self->dec,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded) {
    GST_DEBUG_OBJECT (self, "Decoder state is not OMX_StateLoaded");
    return FALSE;
  }
  in_port_index = klass->cdata.in_port_index;
  out_port_index = klass->cdata.out_port_index;

  if (in_port_index == -1 || out_port_index == -1) {
    OMX_PORT_PARAM_TYPE param;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->dec, OMX_IndexParamVideoInit,
        &param);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Couldn't get port information: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* Fallback */
      in_port_index = 0;
      out_port_index = 1;
    } else {
      GST_DEBUG_OBJECT (self, "Detected %u ports, starting at %u",
          (guint) param.nPorts, (guint) param.nStartPortNumber);
      in_port_index = param.nStartPortNumber + 0;
      out_port_index = param.nStartPortNumber + 1;
    }
  }
  self->dec_in_port = gst_omx_component_add_port (self->dec, in_port_index);
  self->dec_out_port = gst_omx_component_add_port (self->dec, out_port_index);

  if (!self->dec_in_port || !self->dec_out_port)
    return FALSE;

  GST_DEBUG_OBJECT (self, "Opened decoder");

  return TRUE;
}

static gboolean
gst_omx_video_dec_shutdown (GstOMXVideoDec * self)
{
  OMX_STATETYPE state;
  OMX_ERRORTYPE last_error;

  GST_DEBUG_OBJECT (self, "Shutting down decoder");

  state = gst_omx_component_get_state (self->dec, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    /* get_state is not work when we have error */
    if (state == OMX_StateInvalid) {
      last_error = gst_omx_component_get_last_error (self->dec);
      if (last_error != OMX_ErrorNone) {
        /*clear error and get real state */
        gst_omx_component_clear_last_error (self->dec);
        state = gst_omx_component_get_state (self->dec, 0);
      }
    }
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->dec, OMX_StateIdle);
      gst_omx_component_get_state (self->dec, 5 * GST_SECOND);
    }
    gst_omx_component_set_state (self->dec, OMX_StateLoaded);
    gst_omx_port_deallocate_buffers (self->dec_in_port);
    gst_omx_video_dec_deallocate_output_buffers (self);
    if (state > OMX_StateLoaded)
      gst_omx_component_get_state (self->dec, 5 * GST_SECOND);
  }

  return TRUE;
}

static gboolean
gst_omx_video_dec_close (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Closing decoder");

  if (!gst_omx_video_dec_shutdown (self))
    return FALSE;

  self->dec_in_port = NULL;
  self->dec_out_port = NULL;
  if (self->dec)
    gst_omx_component_free (self->dec);
  self->dec = NULL;

  self->started = FALSE;
  gst_caps_replace (&self->caps, NULL);

  GST_DEBUG_OBJECT (self, "Closed decoder");

  return TRUE;
}

static void
gst_omx_video_dec_finalize (GObject * object)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (object);

  GST_DEBUG_OBJECT (object, "finalize decoder");

  g_free (self->app_type);

  g_mutex_clear (&self->drain_lock);
  g_cond_clear (&self->drain_cond);
  g_mutex_clear (&self->rewind_lock);
  g_cond_clear (&self->rewind_cond);

  g_hash_table_destroy (self->str2stream_type);

  G_OBJECT_CLASS (gst_omx_video_dec_parent_class)->finalize (object);
}

static gboolean
gst_omx_video_dec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstOMXVideoDec *self;
  GstVideoDecoder *decoder;
  GstVideoDecoderClass *decoder_class;
  gboolean ret = FALSE;

  decoder = GST_VIDEO_DECODER (parent);
  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);
  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      signal_rewind_unblocking (self);
      break;
    default:
      break;
  }

  if (decoder_class->sink_event)
    ret = decoder_class->sink_event (decoder, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      set_packetized_mode (self);
      set_trick_mode (self);
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
gst_omx_video_dec_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstOMXVideoDec *self;
  GstVideoDecoder *decoder;
  GstVideoDecoderClass *decoder_class;
  gboolean ret = FALSE;

  decoder = GST_VIDEO_DECODER (parent);
  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);
  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  if (decoder_class->src_event)
    ret = decoder_class->src_event (decoder, event);

  return ret;
}

static void
gst_omx_video_dec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (object);

  GST_DEBUG_OBJECT (self, "set prop %d", prop_id);

  switch (prop_id) {
    case PROP_PORT:
      self->port = g_value_get_uint (value);
      break;
    case PROP_IS_SVP:
      self->is_svp = g_value_get_boolean (value);
      break;
    case PROP_VDEC_CH:
      self->vdec_ch = g_value_get_uint (value);
      break;
    case PROP_APP_TYPE:
      g_free (self->app_type);
      self->app_type = g_value_dup_string (value);
      /* setting NULL restores the default device */
      if (self->app_type == NULL) {
        self->app_type = g_strdup (DEFAULT_APP_TYPE);
      }
      GST_DEBUG_OBJECT (self, "app_type=%s", self->app_type);
      break;
    case PROP_RESOURCE_INFO:
    {
      const GstStructure *s = gst_value_get_structure (value);
      gint index;

      if (gst_structure_has_field (s, "video-port")) {
        gst_structure_get_int (s, "video-port", &index);
        self->port = (guint) index;
        GST_DEBUG_OBJECT (self, "video-port %d", self->port);
      }
      if (gst_structure_has_field (s, "max-width")) {
        gst_structure_get_int (s, "max-width", &index);
        self->max_width = index;
        GST_DEBUG_OBJECT (self, "max-width %d", self->max_width);
      }
      if (gst_structure_has_field (s, "max-height")) {
        gst_structure_get_int (s, "max-height", &index);
        self->max_height = index;
        GST_DEBUG_OBJECT (self, "max-height %d", self->max_height);
      }
    }
      break;
    case PROP_SVP_VERSION:
      self->svp_version = g_value_get_uint (value);
      break;
    case PROP_CLIP_MODE:
      self->clip_mode = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_video_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (object);

  GST_DEBUG_OBJECT (self, "get prop %d", prop_id);

  switch (prop_id) {
    case PROP_UNDECODED_SIZE:
    {
      MSTAR_OMX_VIDEO_DECODER_BUFFER_INFO info = { 0 };
      gst_omx_component_get_parameter (self->dec,
          OMX_IndexMstarVideoDecoderBufferInfo, &info);
      GST_LOG_OBJECT (self, "undecoded size %d, free %d\n",
          (int) (info.nTotalSize - info.nFreeSize), (int) info.nFreeSize);
      g_value_set_uint64 (value, info.nTotalSize - info.nFreeSize);
    }
      break;
    case PROP_DECODED_SIZE:
      GST_LOG_OBJECT (self, "decoded size %llu\n", self->decoded_size);
      g_value_set_uint64 (value, self->decoded_size);
      break;
    case PROP_SVP_VERSION:
      g_value_set_uint (value, self->svp_version);
      break;
    case PROP_DISPLAYED_FRAMES:
      g_value_set_int(value, self->displayed_frames);
      self->displayed_frames = 0;
      break;
    case PROP_DROPPED_FRAMES:
      g_value_set_int(value, self->dropped_frames);
      self->dropped_frames = 0;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_omx_video_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstOMXVideoDec *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstState srcState = (transition >> 3) & 0x7;
  GstState dstState = transition & 0x7;

  g_return_val_if_fail (GST_IS_OMX_VIDEO_DEC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_OMX_VIDEO_DEC (element);
  GST_DEBUG_OBJECT (self, "%s ==> %s", gst_element_state_get_name (srcState),
      gst_element_state_get_name (dstState));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->downstream_flow_ret = GST_FLOW_OK;
      self->draining = FALSE;
      self->started = FALSE;
      self->rewind_blocking = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->dec_in_port)
        gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
      if (self->dec_out_port)
        gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

      g_mutex_lock (&self->drain_lock);
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      g_mutex_unlock (&self->drain_lock);

      g_mutex_lock (&self->rewind_lock);
      self->rewind_blocking = FALSE;
      g_cond_broadcast (&self->rewind_cond);
      g_mutex_unlock (&self->rewind_lock);
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret =
      GST_ELEMENT_CLASS (gst_omx_video_dec_parent_class)->change_state
      (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_DEBUG_OBJECT (self, "reset underrun time in state change");
      // don't send underrun until playback start for a while
      self->last_underrun_time = gst_util_get_timestamp ();
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->downstream_flow_ret = GST_FLOW_FLUSHING;
      self->started = FALSE;

      if (!gst_omx_video_dec_shutdown (self))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

#define MAX_FRAME_DIST_TICKS  (5 * OMX_TICKS_PER_SECOND)
#define MAX_FRAME_DIST_FRAMES (100)

static GstVideoCodecFrame *
_find_nearest_frame (GstOMXVideoDec * self, GstOMXBuffer * buf)
{
  GList *l, *best_l = NULL;
  GList *finish_frames = NULL;
  GstVideoCodecFrame *best = NULL;
  guint64 best_timestamp = 0;
  guint64 best_diff = G_MAXUINT64;
  BufferIdentification *best_id = NULL;
  GList *frames;

  frames = gst_video_decoder_get_frames (GST_VIDEO_DECODER (self));

  for (l = frames; l; l = l->next) {
    GstVideoCodecFrame *tmp = l->data;
    BufferIdentification *id = gst_video_codec_frame_get_user_data (tmp);
    guint64 timestamp, diff;

    /* This happens for frames that were just added but
     * which were not passed to the component yet. Ignore
     * them here!
     */
    if (!id)
      continue;

    timestamp = id->timestamp;

    /* If timestamp in omx_buf is -1, we need to find exact match */
    if (buf->omx_buf->nTimeStamp == -1) {
      if (timestamp == -1)
        diff = 0;
      else
        diff = (guint64) - 1;   /* max value of guint64 */
    } else {
      if (timestamp > buf->omx_buf->nTimeStamp)
        diff = timestamp - buf->omx_buf->nTimeStamp;
      else
        diff = buf->omx_buf->nTimeStamp - timestamp;
    }

    /* Only allow differences under 1 ms */
    if (diff < OMX_TICKS_PER_SECOND / 1000 && (best == NULL
            || diff < best_diff)) {
      best = tmp;
      best_timestamp = timestamp;
      best_diff = diff;
      best_l = l;
      best_id = id;

      /* For frames without timestamp we simply take the first frame */
      if (diff == 0)
        break;
    }
  }

  if (best_id) {
    for (l = frames; l && l != best_l; l = l->next) {
      GstVideoCodecFrame *tmp = l->data;
      BufferIdentification *id = gst_video_codec_frame_get_user_data (tmp);
      guint64 diff_ticks, diff_frames;

      /* This happens for frames that were just added but
       * which were not passed to the component yet. Ignore
       * them here!
       */
      if (!id)
        continue;

      if (id->timestamp == -1 || best_timestamp == -1)
        diff_ticks = 0;
      else if (id->timestamp > best_timestamp)
        break;
      else
        diff_ticks = best_timestamp - id->timestamp;
      diff_frames = best->system_frame_number - tmp->system_frame_number;

      if (diff_ticks > MAX_FRAME_DIST_TICKS
          || diff_frames > MAX_FRAME_DIST_FRAMES) {
        finish_frames =
            g_list_prepend (finish_frames, gst_video_codec_frame_ref (tmp));
      }
    }
  }

  if (finish_frames) {
    GST_LOG_OBJECT (self, "Release too old frames");
    for (l = finish_frames; l; l = l->next) {
      gst_video_decoder_release_frame (GST_VIDEO_DECODER (self), l->data);
    }
    g_list_free (finish_frames);
  }

  if (best)
    gst_video_codec_frame_ref (best);

  g_list_foreach (frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (frames);

  return best;
}

static void
check_abnormal_timestamp (GstOMXVideoDec * self, GstOMXBuffer * buf,
    GstVideoCodecFrame * frame)
{
  if (buf->omx_buf->nTimeStamp != -1) {
    GstClockTime ts =
        gst_util_uint64_scale (buf->omx_buf->nTimeStamp, GST_SECOND,
        OMX_TICKS_PER_SECOND);
    if (GST_CLOCK_TIME_IS_VALID (self->last_downstream_ts)
        && GST_CLOCK_DIFF (self->last_downstream_ts, ts) > 100 * GST_SECOND
        && GST_VIDEO_DECODER (self)->input_segment.rate > 0.0) {
      GST_WARNING_OBJECT (self,
          "Abnormal ts %" GST_TIME_FORMAT " detected, using last %"
          GST_TIME_FORMAT, GST_TIME_ARGS (ts),
          GST_TIME_ARGS (self->last_downstream_ts));
      buf->omx_buf->nTimeStamp =
          gst_util_uint64_scale (self->last_downstream_ts, OMX_TICKS_PER_SECOND,
          GST_SECOND);
      if (frame)
        frame->pts = self->last_downstream_ts;
    }
    self->last_downstream_ts = ts;
  }
}

static gboolean
is_frame_expired (GstVideoCodecFrame * frame)
{
  if (frame) {
    BufferIdentification *id = gst_video_codec_frame_get_user_data (frame);
    GstClockTime curr_time = gst_util_get_timestamp ();
    if (GST_CLOCK_TIME_IS_VALID (id->expire_time)
        && curr_time > id->expire_time)
      return TRUE;
  }
  return FALSE;
}

static gboolean
is_out_of_segment (GstVideoCodecFrame * frame, GstSegment * segment)
{
  if (frame) {
    guint64 start, stop;
    guint64 cstart, cstop;
    GstClockTime duration;

    duration = frame->duration;
    start = frame->pts;
    stop = start;
    if (GST_CLOCK_TIME_IS_VALID (start) && GST_CLOCK_TIME_IS_VALID (duration)) {
      stop = start + duration;
    }
    return !gst_segment_clip (segment, GST_FORMAT_TIME, start, stop, &cstart,
        &cstop);
  }
  return FALSE;
}

#define DO_RUNNING_AVG(avg,val,size) (((val) + ((size)-1) * (avg)) / (size))

/* generic running average, this has a neutral window size */
#define UPDATE_RUNNING_AVG(avg,val)   DO_RUNNING_AVG(avg,val,8)

static void
update_decode_time (GstOMXVideoDec * self, GstVideoCodecFrame * frame)
{
  if (frame) {
    BufferIdentification *id = gst_video_codec_frame_get_user_data (frame);
    GstClockTime stop_time = gst_util_get_timestamp ();
    GstClockTimeDiff elapsed;
    if (GST_CLOCK_TIME_IS_VALID (id->start_time) &&
        (elapsed = GST_CLOCK_DIFF (id->start_time, stop_time)) > 0) {
      if (GST_CLOCK_TIME_IS_VALID (self->avg_decode))
        self->avg_decode = UPDATE_RUNNING_AVG (self->avg_decode, elapsed);
      else
        self->avg_decode = elapsed;
    }

    if (!GST_CLOCK_TIME_IS_VALID (self->first_decoded_pts)
        && GST_CLOCK_TIME_IS_VALID (frame->pts)) {
      self->first_decoded_time = stop_time;
      self->first_decoded_pts = frame->pts;
    }
  }
}

static void
update_qos (GstOMXVideoDec * self, GstVideoCodecFrame *frame)
{
  GstClockTimeDiff max_decode;
  max_decode =
      gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (self), frame);
  if (max_decode >= 0)
    self->displayed_frames++;
  else
    self->dropped_frames++;
}

static void
signal_rewind_unblocking (GstOMXVideoDec * self)
{
  g_mutex_lock (&self->rewind_lock);
  if (self->rewind_blocking) {
    GST_DEBUG_OBJECT (self, "signal rewind unblocking");
    self->rewind_blocking = FALSE;
    g_cond_broadcast (&self->rewind_cond);
  }
  g_mutex_unlock (&self->rewind_lock);
}

static gboolean
wait_rewind_unblocking (GstOMXVideoDec * self, GstClockTime expire_time)
{
  GST_DEBUG_OBJECT (self, "Waiting rewind unblocking (timeout = %lld ms)...",
      GST_CLOCK_DIFF (gst_util_get_timestamp (), expire_time) / GST_MSECOND);

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  g_mutex_lock (&self->rewind_lock);
  {
    GstClockTimeDiff timeout =
        GST_CLOCK_DIFF (gst_util_get_timestamp (), expire_time);
    while (self->rewind_blocking && timeout > 0) {
      gint64 wait_until =
          g_get_monotonic_time () + GST_TIME_AS_USECONDS (timeout);
      g_cond_wait_until (&self->rewind_cond, &self->rewind_lock, wait_until);
      timeout = GST_CLOCK_DIFF (gst_util_get_timestamp (), expire_time);
    }
  }
  g_mutex_unlock (&self->rewind_lock);
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  if (self->rewind_blocking) {
    GST_WARNING_OBJECT (self, "Waiting rewind unblocking timed out");
    return FALSE;
  }
  GST_DEBUG_OBJECT (self, "Waiting rewind unblocking success");
  return TRUE;
}

static gboolean
gst_omx_video_dec_fill_buffer (GstOMXVideoDec * self,
    GstOMXBuffer * inbuf, GstBuffer * outbuf)
{
  GstVideoCodecState *state =
      gst_video_decoder_get_output_state (GST_VIDEO_DECODER (self));
  GstVideoInfo *vinfo = &state->info;
  OMX_PARAM_PORTDEFINITIONTYPE *port_def = &self->dec_out_port->port_def;
  gboolean ret = FALSE;
  GstVideoFrame frame;

#if 0
  if (vinfo->width != port_def->format.video.nFrameWidth ||
      vinfo->height != port_def->format.video.nFrameHeight) {
    GST_ERROR_OBJECT (self, "Resolution do not match. port: %ux%u vinfo: %ux%u",
        (guint) port_def->format.video.nFrameWidth,
        (guint) port_def->format.video.nFrameHeight, (guint) vinfo->width,
        (guint) vinfo->height);
    goto done;
  }
#endif

  /* Same strides and everything */
  GstMapInfo map = GST_MAP_INFO_INIT;
  if (!gst_buffer_map (outbuf, &map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Failed to map output buffer");
    goto done;
  }
  if (map.size < inbuf->omx_buf->nFilledLen) {
    GST_ERROR_OBJECT (self,
        "Output buffer size (%" G_GSIZE_FORMAT
        ") smaller than OMX filled size (%lu)", map.size,
        inbuf->omx_buf->nFilledLen);
    gst_buffer_unmap (outbuf, &map);
    goto done;
  }
  memcpy (map.data,
      inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset,
      inbuf->omx_buf->nFilledLen);
  gst_buffer_unmap (outbuf, &map);
  ret = TRUE;
  goto done;


  /* Different strides */

  switch (vinfo->finfo->format) {
    case GST_VIDEO_FORMAT_I420:{
      gint i, j, height, width;
      guint8 *src, *dest;
      gint src_stride, dest_stride;

      gst_video_frame_map (&frame, vinfo, outbuf, GST_MAP_WRITE);
      for (i = 0; i < 3; i++) {
        if (i == 0) {
          src_stride = port_def->format.video.nStride;
          dest_stride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, i);

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        } else {
          src_stride = port_def->format.video.nStride / 2;
          dest_stride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, i);

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        }

        src = inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset;
        if (i > 0)
          src +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;
        if (i == 2)
          src +=
              (port_def->format.video.nSliceHeight / 2) *
              (port_def->format.video.nStride / 2);

        dest = GST_VIDEO_FRAME_COMP_DATA (&frame, i);
        height = GST_VIDEO_FRAME_COMP_HEIGHT (&frame, i);
        width = GST_VIDEO_FRAME_COMP_WIDTH (&frame, i);

        for (j = 0; j < height; j++) {
          memcpy (dest, src, width);
          src += src_stride;
          dest += dest_stride;
        }
      }
      gst_video_frame_unmap (&frame);
      ret = TRUE;
      break;
    }
    case GST_VIDEO_FORMAT_NV12:{
      gint i, j, height, width;
      guint8 *src, *dest;
      gint src_stride, dest_stride;

      gst_video_frame_map (&frame, vinfo, outbuf, GST_MAP_WRITE);
      for (i = 0; i < 2; i++) {
        if (i == 0) {
          src_stride = port_def->format.video.nStride;
          dest_stride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, i);

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        } else {
          src_stride = port_def->format.video.nStride;
          dest_stride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, i);

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        }

        src = inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset;
        if (i == 1)
          src +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;

        dest = GST_VIDEO_FRAME_COMP_DATA (&frame, i);
        height = GST_VIDEO_FRAME_COMP_HEIGHT (&frame, i);
        width = GST_VIDEO_FRAME_COMP_WIDTH (&frame, i) * (i == 0 ? 1 : 2);

        for (j = 0; j < height; j++) {
          memcpy (dest, src, width);
          src += src_stride;
          dest += dest_stride;
        }
      }
      gst_video_frame_unmap (&frame);
      ret = TRUE;
      break;
    }
    default:
      GST_ERROR_OBJECT (self, "Unsupported format");
      goto done;
      break;
  }


done:
  if (ret) {
    GST_BUFFER_PTS (outbuf) =
        gst_util_uint64_scale (inbuf->omx_buf->nTimeStamp, GST_SECOND,
        OMX_TICKS_PER_SECOND);
    if (inbuf->omx_buf->nTickCount != 0)
      GST_BUFFER_DURATION (outbuf) =
          gst_util_uint64_scale (inbuf->omx_buf->nTickCount, GST_SECOND,
          OMX_TICKS_PER_SECOND);
  }

  gst_video_codec_state_unref (state);

  return ret;
}

static OMX_ERRORTYPE
gst_omx_video_dec_allocate_output_buffers (GstOMXVideoDec * self)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;
  GstOMXPort *port;
  GstBufferPool *pool;
  GstStructure *config;
  gboolean eglimage = FALSE, add_videometa = FALSE;
  GstCaps *caps = NULL;
  guint min = 0, max = 0;
  GstVideoCodecState *state =
      gst_video_decoder_get_output_state (GST_VIDEO_DECODER (self));

  port = self->dec_out_port;

  pool = gst_video_decoder_get_buffer_pool (GST_VIDEO_DECODER (self));
  /* FIXME: Enable this once there's a way to request downstream to
   * release all our buffers, e.g.
   * http://cgit.freedesktop.org/~wtay/gstreamer/log/?h=release-pool */
  if (FALSE && pool) {
    GstAllocator *allocator;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &caps, NULL, &min, &max);
    gst_buffer_pool_config_get_allocator (config, &allocator, NULL);

    /* Need at least 2 buffers for anything meaningful */
    min = MAX (MAX (min, port->port_def.nBufferCountMin), 4);
    if (max == 0) {
      max = min;
    } else if (max < port->port_def.nBufferCountMin || max < 2) {
      /* Can't use pool because can't have enough buffers */
      gst_caps_replace (&caps, NULL);
    } else {
      min = max;
    }

    add_videometa = gst_buffer_pool_config_has_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    /* TODO: Implement something here */
    eglimage = FALSE;
    caps = caps ? gst_caps_ref (caps) : NULL;

    GST_DEBUG_OBJECT (self, "Trying to use pool %p with caps %" GST_PTR_FORMAT
        " and memory type %s", pool, caps,
        (allocator ? allocator->mem_type : "(null)"));
  } else {
    gst_caps_replace (&caps, NULL);
    min = max = port->port_def.nBufferCountMin;
    GST_DEBUG_OBJECT (self, "No pool available, not negotiated yet");
  }

  if (caps)
    self->out_port_pool =
        gst_omx_buffer_pool_new (GST_ELEMENT_CAST (self), self->dec, port);

  /* TODO: Implement EGLImage handling and usage of other downstream buffers */

  /* If not using EGLImage or trying to use EGLImage failed */
  if (!eglimage) {
    gboolean was_enabled = TRUE;

    if (min != port->port_def.nBufferCountActual) {
      err = gst_omx_port_update_port_definition (port, NULL);
      if (err == OMX_ErrorNone) {
        port->port_def.nBufferCountActual = min;
        err = gst_omx_port_update_port_definition (port, &port->port_def);
      }

      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self,
            "Failed to configure %u output buffers: %s (0x%08x)", min,
            gst_omx_error_to_string (err), err);
        goto done;
      }
    }

    if (!gst_omx_port_is_enabled (port)) {
      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone) {
        GST_INFO_OBJECT (self,
            "Failed to enable port: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto done;
      }
      was_enabled = FALSE;
    }

    err = gst_omx_port_allocate_buffers (port);
    if (err != OMX_ErrorNone && min > port->port_def.nBufferCountMin) {
      GST_ERROR_OBJECT (self,
          "Failed to allocate required number of buffers %d, trying less and copying",
          min);
      min = port->port_def.nBufferCountMin;

      if (!was_enabled)
        gst_omx_port_set_enabled (port, FALSE);

      if (min != port->port_def.nBufferCountActual) {
        err = gst_omx_port_update_port_definition (port, NULL);
        if (err == OMX_ErrorNone) {
          port->port_def.nBufferCountActual = min;
          err = gst_omx_port_update_port_definition (port, &port->port_def);
        }

        if (err != OMX_ErrorNone) {
          GST_ERROR_OBJECT (self,
              "Failed to configure %u output buffers: %s (0x%08x)", min,
              gst_omx_error_to_string (err), err);
          goto done;
        }
      }

      err = gst_omx_port_allocate_buffers (port);

      /* Can't provide buffers downstream in this case */
      gst_caps_replace (&caps, NULL);
    }

    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to allocate %d buffers: %s (0x%08x)", min,
          gst_omx_error_to_string (err), err);
      goto done;
    }

    if (!was_enabled) {
      err = gst_omx_port_wait_enabled (port, 2 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self,
            "Failed to wait until port is enabled: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto done;
      }
    }


  }

  err = OMX_ErrorNone;

  if (caps) {
    config = gst_buffer_pool_get_config (self->out_port_pool);

    if (add_videometa)
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_META);

    gst_buffer_pool_config_set_params (config, caps,
        self->dec_out_port->port_def.nBufferSize, min, max);

    if (!gst_buffer_pool_set_config (self->out_port_pool, config)) {
      GST_INFO_OBJECT (self, "Failed to set config on internal pool");
      gst_object_unref (self->out_port_pool);
      self->out_port_pool = NULL;
      goto done;
    }

    GST_OMX_BUFFER_POOL (self->out_port_pool)->allocating = TRUE;
    /* This now allocates all the buffers */
    if (!gst_buffer_pool_set_active (self->out_port_pool, TRUE)) {
      GST_INFO_OBJECT (self, "Failed to activate internal pool");
      gst_object_unref (self->out_port_pool);
      self->out_port_pool = NULL;
    } else {
      GST_OMX_BUFFER_POOL (self->out_port_pool)->allocating = FALSE;
    }
  } else if (self->out_port_pool) {
    gst_object_unref (self->out_port_pool);
    self->out_port_pool = NULL;
  }

done:
  if (!self->out_port_pool && err == OMX_ErrorNone)
    GST_DEBUG_OBJECT (self,
        "Not using our internal pool and copying buffers for downstream");

  if (caps)
    gst_caps_unref (caps);
  if (pool)
    gst_object_unref (pool);
  if (state)
    gst_video_codec_state_unref (state);

  return err;
}

static OMX_ERRORTYPE
gst_omx_video_dec_deallocate_output_buffers (GstOMXVideoDec * self)
{
  OMX_ERRORTYPE err;

  if (self->out_port_pool) {
    gst_buffer_pool_set_active (self->out_port_pool, FALSE);
    GST_OMX_BUFFER_POOL (self->out_port_pool)->deactivated = TRUE;
    gst_object_unref (self->out_port_pool);
    self->out_port_pool = NULL;
  }
  err = gst_omx_port_deallocate_buffers (self->dec_out_port);

  return err;
}

static gboolean
is_low_latency_app_type (GstOMXVideoDec * self)
{
  return (self->app_type && (g_str_equal (self->app_type, "RTC")
          || g_str_equal (self->app_type, "CAMERA")));
}

#define FAKE_OUTPUT_WIDTH   (32)
#define FAKE_OUTPUT_HEIGHT  (32)

static void
gst_omx_video_dec_loop (GstOMXVideoDec * self)
{
  GstOMXPort *port = self->dec_out_port;
  GstOMXBuffer *buf = NULL;
  GstVideoCodecFrame *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;
  OMX_ERRORTYPE err;
  gboolean release_after_push = TRUE;
  gboolean expired = FALSE;

  MSTAR_OMX_VIDEO_DECODER_BUFFER_INFO info = { 0 };
  gst_omx_component_get_parameter (self->dec,
      OMX_IndexMstarVideoDecoderBufferInfo, &info);
  GST_LOG_OBJECT (self, "undecoded size %d, free %d",
      (int) (info.nTotalSize - info.nFreeSize), (int) info.nFreeSize);
  if (GST_CLOCK_TIME_IS_VALID (self->last_underrun_time)
      && gst_util_get_timestamp () > self->last_underrun_time + GST_SECOND) {
    GST_DEBUG_OBJECT (self, "reset underrun time");
    self->last_underrun_time = GST_CLOCK_TIME_NONE;
  }
  if (info.nTotalSize == info.nFreeSize
      && !GST_CLOCK_TIME_IS_VALID (self->last_underrun_time)) {
    self->last_underrun_time = gst_util_get_timestamp ();
    push_buffer_underrun_event (self);
  }

  acq_return = gst_omx_port_acquire_buffer (port, &buf);
  if (acq_return == GST_OMX_ACQUIRE_BUFFER_ERROR) {
    goto component_error;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
    goto flushing;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_EOS) {
    goto eos;
  }

  if (!gst_pad_has_current_caps (GST_VIDEO_DECODER_SRC_PAD (self)) ||
      acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
    GstVideoCodecState *state;
    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    GstVideoFormat format;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    /* Reallocate all buffers */
    if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE
        && gst_omx_port_is_enabled (port)) {
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_video_dec_deallocate_output_buffers (self);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;
    }

    GST_VIDEO_DECODER_STREAM_LOCK (self);

    gst_omx_port_get_port_definition (port, &port_def);
    g_assert (port_def.format.video.eCompressionFormat ==
        OMX_VIDEO_CodingUnused);

    switch (port_def.format.video.eColorFormat) {
      case OMX_COLOR_FormatYUV420Planar:
      case OMX_COLOR_FormatYUV420PackedPlanar:
        GST_DEBUG_OBJECT (self, "Output is I420 (%d)",
            port_def.format.video.eColorFormat);
        format = GST_VIDEO_FORMAT_I420;
        break;
      case OMX_COLOR_FormatYUV420SemiPlanar:
        GST_DEBUG_OBJECT (self, "Output is NV12 (%d)",
            port_def.format.video.eColorFormat);
        format = GST_VIDEO_FORMAT_NV12;
        break;
      case OMX_COLOR_FormatYCbYCr:
        GST_DEBUG_OBJECT (self, "Output is YUY2 (%d)",
            port_def.format.video.eColorFormat);
        format = GST_VIDEO_FORMAT_YUY2;
        break;
      default:
        GST_ERROR_OBJECT (self, "Unsupported color format: %d",
            port_def.format.video.eColorFormat);
        if (buf)
          gst_omx_port_release_buffer (self->dec_out_port, buf);
        GST_VIDEO_DECODER_STREAM_UNLOCK (self);
        goto caps_failed;
        break;
    }

    GST_DEBUG_OBJECT (self,
        "Setting output state: format %s, width %u, height %u",
        gst_video_format_to_string (format),
        (guint) FAKE_OUTPUT_WIDTH, (guint) FAKE_OUTPUT_HEIGHT);

    state = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
        format, FAKE_OUTPUT_WIDTH, FAKE_OUTPUT_HEIGHT, self->input_state);

    /* Add vendor field */
    if (state->caps == NULL)
      state->caps = gst_video_info_to_caps (&state->info);
    gst_caps_set_simple (state->caps, "vendor", G_TYPE_STRING, "mstar", NULL);

    /* Take framerate and pixel-aspect-ratio from sinkpad caps */

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      if (buf)
        gst_omx_port_release_buffer (self->dec_out_port, buf);
      gst_video_codec_state_unref (state);
      goto caps_failed;
    }

    gst_video_codec_state_unref (state);

    GST_VIDEO_DECODER_STREAM_UNLOCK (self);

    if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      err = gst_omx_video_dec_allocate_output_buffers (self);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_populate (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;
    }

    /* Now get a buffer */
    if (acq_return != GST_OMX_ACQUIRE_BUFFER_OK) {
      return;
    }
  }

  g_assert (acq_return == GST_OMX_ACQUIRE_BUFFER_OK);

  /* This prevents a deadlock between the srcpad stream
   * lock and the videocodec stream lock, if ::reset()
   * is called at the wrong time
   */
  if (gst_omx_port_is_flushing (self->dec_out_port)) {
    GST_DEBUG_OBJECT (self, "Flushing");
    gst_omx_port_release_buffer (self->dec_out_port, buf);
    goto flushing;
  }

  if (buf->omx_buf->nFilledLen > 0) {
    GST_DEBUG_OBJECT (self, "Handling buffer: 0x%08x %lu",
        (guint) buf->omx_buf->nFlags, (gulong) buf->omx_buf->nTimeStamp);

    GST_VIDEO_DECODER_STREAM_LOCK (self);
    frame = _find_nearest_frame (self, buf);
    check_abnormal_timestamp (self, buf, frame);

    if (is_frame_expired (frame)) {
      /* Don't call _drop_frame() for expired frame in order
       * to avoid decreasing timestamp issue. */
      GST_WARNING_OBJECT (self, "Frame expired during rewind, dropping");
      gst_video_decoder_release_frame (GST_VIDEO_DECODER (self), frame);
      flow_ret = GST_FLOW_OK;
      expired = TRUE;
      frame = NULL;
    } else if (self->clip_mode
        && is_out_of_segment (frame,
            &GST_VIDEO_DECODER (self)->input_segment)) {
      GST_WARNING_OBJECT (self, "Frame out of segment, dropping");
      gst_video_decoder_release_frame (GST_VIDEO_DECODER (self), frame);
      flow_ret = GST_FLOW_OK;
      frame = NULL;
    } else if (frame && !GST_CLOCK_TIME_IS_VALID (frame->pts)
        && GST_VIDEO_DECODER (self)->input_segment.rate < 0.0) {
      GST_WARNING_OBJECT (self, "Invalid PTS during rewind, dropping");
      flow_ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
      frame = NULL;
    } else if (!frame) {
      GstBuffer *outbuf;

      /* This sometimes happens at EOS or if the input is not properly framed,
       * let's handle it gracefully by allocating a new buffer for the current
       * caps and filling it
       */

      if (GST_VIDEO_DECODER (self)->input_segment.rate < 0.0
          && self->direct_rewind_push)
        GST_DEBUG_OBJECT (self, "Directly push pad during rewind");
      else
        GST_ERROR_OBJECT (self, "No corresponding frame found");
      /* abnormal case, don't signal rewind unblocking */
      expired = TRUE;

      if (self->out_port_pool) {
        gint i, n;
        GstBufferPoolAcquireParams params = { 0, };

        n = port->buffers->len;
        for (i = 0; i < n; i++) {
          GstOMXBuffer *tmp = g_ptr_array_index (port->buffers, i);

          if (tmp == buf)
            break;
        }
        g_assert (i != n);

        GST_OMX_BUFFER_POOL (self->out_port_pool)->current_buffer_index = i;
        flow_ret =
            gst_buffer_pool_acquire_buffer (self->out_port_pool, &outbuf,
            &params);
        if (flow_ret != GST_FLOW_OK) {
          gst_omx_port_release_buffer (port, buf);
          goto invalid_buffer;
        }
        buf = NULL;
      } else {
        outbuf =
            gst_video_decoder_allocate_output_buffer (GST_VIDEO_DECODER (self));
        if (!gst_omx_video_dec_fill_buffer (self, buf, outbuf)) {
          gst_buffer_unref (outbuf);
          gst_omx_port_release_buffer (port, buf);
          goto invalid_buffer;
        }
        if (self->share_outport_buffer == TRUE) {
          gst_buffer_add_mstar_video_meta (outbuf, self, buf);
          release_after_push = FALSE;
        }
      }

      flow_ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), outbuf);
    } else {
      update_decode_time (self, frame);
      update_qos (self, frame);

      /* FIXME: When decreasing timestamp is detected in gst_video_decoder_prepare_finish_frame(),
       *        it sets reordered_output flag and trust none of the PTS from now on, which may cause
       *        lipsync problem. Therefore we reset ts & ts2 here so it won't erase actual PTS even
       *        if reordered_output is set.
       */
      frame->abidata.ABI.ts = GST_CLOCK_TIME_NONE;
      frame->abidata.ABI.ts2 = GST_CLOCK_TIME_NONE;

      if (self->out_port_pool) {
        gint i, n;
        GstBufferPoolAcquireParams params = { 0, };

        n = port->buffers->len;
        for (i = 0; i < n; i++) {
          GstOMXBuffer *tmp = g_ptr_array_index (port->buffers, i);

          if (tmp == buf)
            break;
        }
        g_assert (i != n);

        GST_OMX_BUFFER_POOL (self->out_port_pool)->current_buffer_index = i;
        flow_ret =
            gst_buffer_pool_acquire_buffer (self->out_port_pool,
            &frame->output_buffer, &params);
        if (flow_ret != GST_FLOW_OK) {
          flow_ret =
              gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
          frame = NULL;
          gst_omx_port_release_buffer (port, buf);
          goto invalid_buffer;
        }
        flow_ret =
            gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
        frame = NULL;
        buf = NULL;
      } else {
        if ((flow_ret =
                gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER
                    (self), frame)) == GST_FLOW_OK) {
          /* FIXME: This currently happens because of a race condition too.
           * We first need to reconfigure the output port and then the input
           * port if both need reconfiguration.
           */
          if (!gst_omx_video_dec_fill_buffer (self, buf, frame->output_buffer)) {
            gst_buffer_replace (&frame->output_buffer, NULL);
            flow_ret =
                gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
            frame = NULL;
            gst_omx_port_release_buffer (port, buf);
            goto invalid_buffer;
          }
          if (self->share_outport_buffer == TRUE) {
            gst_buffer_add_mstar_video_meta (frame->output_buffer, self, buf);
            release_after_push = FALSE;
          }
          flow_ret =
              gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
          frame = NULL;
        }
      }
    }
  }

  GST_DEBUG_OBJECT (self, "Read frame from component");

  GST_DEBUG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));

  if (buf && release_after_push == TRUE) {
    err = gst_omx_port_release_buffer (port, buf);
    if (err != OMX_ErrorNone)
      goto release_error;
  }

  self->downstream_flow_ret = flow_ret;

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  /* If this frame is expired, don't unlock rewind lock.
   * Otherwise it may accidentally unlock the lock of next frame. */
  if (!expired)
    signal_rewind_unblocking (self);

  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  return;

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->dec),
            gst_omx_component_get_last_error (self->dec)));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }

flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_FLUSHING;
    self->started = FALSE;
    GST_DEBUG_OBJECT (self, "reset underrun time in flushing");
    // don't send underrun until playback start for a while
    self->last_underrun_time = gst_util_get_timestamp ();
    return;
  }

eos:
  {
    g_mutex_lock (&self->drain_lock);
    if (self->draining) {
      GST_DEBUG_OBJECT (self, "Drained");
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      flow_ret = GST_FLOW_OK;
      gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    } else {
      GST_DEBUG_OBJECT (self, "Component signalled EOS");
      flow_ret = GST_FLOW_EOS;
    }
    g_mutex_unlock (&self->drain_lock);

    signal_rewind_unblocking (self);

    self->downstream_flow_ret = flow_ret;

    /* Here we fallback and pause the task for the EOS case */
    if (flow_ret != GST_FLOW_OK)
      goto flow_error;

    GST_VIDEO_DECODER_STREAM_UNLOCK (self);

    return;
  }

flow_error:
  {
    if (flow_ret == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    } else if (flow_ret == GST_FLOW_NOT_LINKED || flow_ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED,
          ("Internal data stream error."), ("stream stopped, reason %s",
              gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    }
    self->started = FALSE;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }

reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }

invalid_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Invalid sized input buffer"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }

caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    return;
  }
release_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase output buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }
}

static gboolean
gst_omx_video_dec_start (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);
  GST_TRACE_OBJECT (self, "Starting decoder");

  self->last_upstream_ts = GST_CLOCK_TIME_NONE;
  self->last_downstream_ts = GST_CLOCK_TIME_NONE;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;

  GST_TRACE_OBJECT (self, "Started decoder");
  return TRUE;
}

static gboolean
gst_omx_video_dec_stop (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Stopping decoder");

  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

  gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (decoder));

  if (gst_omx_component_get_state (self->dec, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->dec, OMX_StateIdle);

  self->downstream_flow_ret = GST_FLOW_FLUSHING;
  self->started = FALSE;
  self->eos = FALSE;

  g_mutex_lock (&self->drain_lock);
  self->draining = FALSE;
  g_cond_broadcast (&self->drain_cond);
  g_mutex_unlock (&self->drain_lock);

  g_mutex_lock (&self->rewind_lock);
  self->rewind_blocking = FALSE;
  g_cond_broadcast (&self->rewind_cond);
  g_mutex_unlock (&self->rewind_lock);

  gst_omx_component_get_state (self->dec, 5 * GST_SECOND);

  gst_buffer_replace (&self->codec_data, NULL);

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = NULL;

  GST_DEBUG_OBJECT (self, "Stopped decoder");

  return TRUE;
}

typedef struct
{
  GstVideoFormat format;
  OMX_COLOR_FORMATTYPE type;
} VideoNegotiationMap;

static void
video_negotiation_map_free (VideoNegotiationMap * m)
{
  g_slice_free (VideoNegotiationMap, m);
}

static GList *
gst_omx_video_dec_get_supported_colorformats (GstOMXVideoDec * self)
{
  GstOMXPort *port = self->dec_out_port;
  GstVideoCodecState *state = self->input_state;
  OMX_VIDEO_PARAM_PORTFORMATTYPE param;
  OMX_ERRORTYPE err;
  GList *negotiation_map = NULL;
  gint old_index;
  GST_TRACE_OBJECT (self, "get supported color format");

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = port->index;
  param.nIndex = 0;
  if (!state || state->info.fps_n == 0)
    param.xFramerate = 0;
  else
    param.xFramerate = (state->info.fps_n << 16) / (state->info.fps_d);

  old_index = -1;
  do {
    VideoNegotiationMap *m;

    err =
        gst_omx_component_get_parameter (self->dec,
        OMX_IndexParamVideoPortFormat, &param);

    /* FIXME: Workaround for Bellagio that simply always
     * returns the same value regardless of nIndex and
     * never returns OMX_ErrorNoMore
     */
    if (old_index == param.nIndex)
      break;

    if (err == OMX_ErrorNone || err == OMX_ErrorNoMore) {
      switch (param.eColorFormat) {
        case OMX_COLOR_FormatYUV420Planar:
        case OMX_COLOR_FormatYUV420PackedPlanar:
          m = g_slice_new (VideoNegotiationMap);
          m->format = GST_VIDEO_FORMAT_I420;
          m->type = param.eColorFormat;
          negotiation_map = g_list_append (negotiation_map, m);
          GST_DEBUG_OBJECT (self, "Component supports I420 (%u) at index %u",
              (guint) param.eColorFormat, (guint) param.nIndex);
          break;
        case OMX_COLOR_FormatYUV420SemiPlanar:
          m = g_slice_new (VideoNegotiationMap);
          m->format = GST_VIDEO_FORMAT_NV12;
          m->type = param.eColorFormat;
          negotiation_map = g_list_append (negotiation_map, m);
          GST_DEBUG_OBJECT (self, "Component supports NV12 (%u) at index %u",
              (guint) param.eColorFormat, (guint) param.nIndex);
          break;
        default:
          GST_DEBUG_OBJECT (self,
              "Component supports unsupported color format %u at index %u",
              (guint) param.eColorFormat, (guint) param.nIndex);
          break;
      }
    }
    old_index = param.nIndex++;
  } while (err == OMX_ErrorNone);

  return negotiation_map;
}

static gboolean
gst_omx_video_dec_negotiate (GstOMXVideoDec * self)
{
  OMX_VIDEO_PARAM_PORTFORMATTYPE param;
  OMX_ERRORTYPE err;
  GstCaps *comp_supported_caps;
  GList *negotiation_map = NULL, *l;
  GstCaps *templ_caps, *intersection;
  GstVideoFormat format;
  GstStructure *s;
  const gchar *format_str;

  GST_DEBUG_OBJECT (self, "Trying to negotiate a video format with downstream");

  templ_caps = gst_pad_get_pad_template_caps (GST_VIDEO_DECODER_SRC_PAD (self));
  intersection =
      gst_pad_peer_query_caps (GST_VIDEO_DECODER_SRC_PAD (self), templ_caps);
  gst_caps_unref (templ_caps);

  GST_DEBUG_OBJECT (self, "Allowed downstream caps: %" GST_PTR_FORMAT,
      intersection);

  negotiation_map = gst_omx_video_dec_get_supported_colorformats (self);
  comp_supported_caps = gst_caps_new_empty ();
  for (l = negotiation_map; l; l = l->next) {
    VideoNegotiationMap *map = l->data;

    gst_caps_append_structure (comp_supported_caps,
        gst_structure_new ("video/x-raw",
            "vendor", G_TYPE_STRING, "mstar",
            "format", G_TYPE_STRING,
            gst_video_format_to_string (map->format), NULL));
  }

  if (!gst_caps_is_empty (comp_supported_caps)) {
    GstCaps *tmp;

    tmp = gst_caps_intersect (comp_supported_caps, intersection);
    gst_caps_unref (intersection);
    intersection = tmp;
  }
  gst_caps_unref (comp_supported_caps);

  if (gst_caps_is_empty (intersection)) {
    gst_caps_unref (intersection);
    GST_ERROR_OBJECT (self, "Empty caps");
    g_list_free_full (negotiation_map,
        (GDestroyNotify) video_negotiation_map_free);
    return FALSE;
  }

  intersection = gst_caps_truncate (intersection);
  intersection = gst_caps_fixate (intersection);

  s = gst_caps_get_structure (intersection, 0);
  format_str = gst_structure_get_string (s, "format");
  if (!format_str ||
      (format =
          gst_video_format_from_string (format_str)) ==
      GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Invalid caps: %" GST_PTR_FORMAT, intersection);
    gst_caps_unref (intersection);
    g_list_free_full (negotiation_map,
        (GDestroyNotify) video_negotiation_map_free);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = self->dec_out_port->index;

  for (l = negotiation_map; l; l = l->next) {
    VideoNegotiationMap *m = l->data;

    if (m->format == format) {
      param.eColorFormat = m->type;
      break;
    }
  }

  GST_DEBUG_OBJECT (self, "Negotiating color format %s (%d)", format_str,
      param.eColorFormat);

  /* We must find something here */
  g_assert (l != NULL);
  g_list_free_full (negotiation_map,
      (GDestroyNotify) video_negotiation_map_free);

  err =
      gst_omx_component_set_parameter (self->dec,
      OMX_IndexParamVideoPortFormat, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set video port format: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  gst_caps_unref (intersection);
  return (err == OMX_ErrorNone);
}

static void
parse_stream_format (GstOMXVideoDec * self)
{
  GstStructure *s = gst_caps_get_structure (self->caps, 0);
  const gchar *stream_format;
  gint *type = NULL;

  stream_format = gst_structure_get_string (s, "stream-format");
  if (stream_format)
    type = (gint *) g_hash_table_lookup (self->str2stream_type, stream_format);

  self->stream_format = type ? *type : GST_NONE_PARSE_FORMAT_NONE;
  GST_DEBUG_OBJECT (self, "stream-format %d", self->stream_format);
}

static void
set_ts_mode (GstOMXVideoDec * self)
{
  GstStructure *s = gst_caps_get_structure (self->caps, 0);
  const gchar *container;

  container = gst_structure_get_string (s, "container");
  if (!g_strcmp0 (container, "ts") || !g_strcmp0 (container, "ps")) {
    OMX_U32 pts_mode = 1;
    GST_DEBUG_OBJECT (self, "Enable PTS mode");
    gst_omx_component_set_parameter (self->dec, OMX_IndexMstarInputPTSMode,
        &pts_mode);
  } else if (!g_strcmp0 (container, "avi")) {
    GST_DEBUG_OBJECT (self, "Use DTS");
    self->use_dts = TRUE;
  }
}

static void
set_framerate (GstOMXVideoDec * self)
{
  GstStructure *s = gst_caps_get_structure (self->caps, 0);
  const gchar *container;
  gint framerate_num, framerate_den;
  gboolean suggest_framerate;
  gboolean has_framerate;

  has_framerate =
      gst_structure_get_fraction (s, "framerate", &framerate_num,
      &framerate_den);
  container = gst_structure_get_string (s, "container");
  suggest_framerate = !g_strcmp0 (container, "ISO MP4/M4A");

  if (has_framerate && framerate_den && framerate_num / framerate_den >= 1000) {
    GST_WARNING_OBJECT (self, "Invalid framerate %d/%d", framerate_num,
        framerate_den);
    has_framerate = FALSE;
  }

  /* provide container frame rate to component */
  if (has_framerate) {
    if (suggest_framerate) {
      GST_DEBUG_OBJECT (self, "Suggest framerate %d/%d", framerate_num,
          framerate_den);
      gst_omx_component_set_parameter (self->dec, OMX_IndexMstarMstFrameRateNum,
          &framerate_num);
      gst_omx_component_set_parameter (self->dec, OMX_IndexMstarMstFrameRateDen,
          &framerate_den);
    } else {
      GST_DEBUG_OBJECT (self, "Set framerate %d/%d", framerate_num,
          framerate_den);
      gst_omx_component_set_parameter (self->dec, OMX_IndexMstarFrameRateNum,
          &framerate_num);
      gst_omx_component_set_parameter (self->dec, OMX_IndexMstarFrameRateDen,
          &framerate_den);
    }
  }
}

static void
set_max_dimension (GstOMXVideoDec * self)
{
  OMX_U32 max_width = self->max_width;  // -1(unlimited) is converted to max value of OMX_U32
  OMX_U32 max_height = self->max_height;        // -1(unlimited) is converted to max value of OMX_U32
  gst_omx_component_set_parameter (self->dec, OMX_IndexMstarMaxWidth,
      &max_width);
  gst_omx_component_set_parameter (self->dec, OMX_IndexMstarMaxHeight,
      &max_height);
  GST_TRACE_OBJECT (self, "max_width:%u max_height:%u",
      (unsigned int) max_width, (unsigned int) max_height);
}

static void
set_parse_functions (GstOMXVideoDec * self)
{
  GstStructure *s = gst_caps_get_structure (self->caps, 0);
  gint mpegversion;

  if (!g_strcmp0 (self->media_type, "video/mpeg")
      && gst_structure_get_int (s, "mpegversion", &mpegversion)) {
    if (mpegversion == 1 || mpegversion == 2) {
      self->search_start_code = search_m2v_start_code;
      self->parse_start_code = parse_m2v_start_code;
    }
  } else if (!g_strcmp0 (self->media_type, "video/x-h264")) {
    self->search_start_code = search_nal_unit;
    self->parse_start_code = parse_h264_nal;
  } else if (!g_strcmp0 (self->media_type, "video/x-h265")) {
    self->search_start_code = search_nal_unit;
    self->parse_start_code = parse_h265_nal;
  }
}

static void
set_packetized_mode (GstOMXVideoDec * self)
{
  GstStructure *s = gst_caps_get_structure (self->caps, 0);
  gboolean packetized = TRUE;
  gint mpegversion;

  self->parse_alignment = ALIGNMENT_NONE;
  if (!g_strcmp0 (self->media_type, "video/x-h264")
      || !g_strcmp0 (self->media_type, "video/x-h265")) {
    const gchar *alignment = gst_structure_get_string (s, "alignment");
    if (!g_strcmp0 (alignment, "nal")) {
      self->parse_alignment = ALIGNMENT_SC;
      packetized = FALSE;
    }
  } else if (!g_strcmp0 (self->media_type, "video/mpeg")
      && gst_structure_get_int (s, "mpegversion", &mpegversion)) {
    if (mpegversion == 1 || mpegversion == 2) {
      const gchar *container = gst_structure_get_string (s, "container");
      /* FIXME: workaround for AVI/MPEG rewind problem. AVI demuxer may set sync point
       * incorrectly, so we need to check which frame is key frame to make rewind work. */
      if (!g_strcmp0 (container, "avi")
          && GST_VIDEO_DECODER (self)->input_segment.rate < 0.0) {
        self->parse_alignment = ALIGNMENT_AU;
        packetized = FALSE;
      }
    }
  }
  GST_DEBUG_OBJECT (self, "%s packetized, parse_alignment %d",
      packetized ? "Enable" : "Disable", self->parse_alignment);
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), packetized);
}

static void
set_skip_to_i_frame (GstOMXVideoDec * self)
{
  if (!g_strcmp0 (self->app_type, "RTC")) {
    guint32 temp = 1;
    GST_DEBUG_OBJECT (self, "Don't skip to I frame");
    gst_omx_component_set_parameter (self->dec,
        OMX_IndexMstarForceStartNonISlice, &temp);
    self->skip_to_i = FALSE;
  }
}

static void
set_low_latency_mode (GstOMXVideoDec * self)
{
  if (is_low_latency_app_type (self)) {
    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    guint32 temp = 1;
    GST_DEBUG_OBJECT (self, "Enable low latency mode");
    gst_omx_component_set_parameter (self->dec,
        OMX_IndexMstarLiveStreamingPlayback, &temp);
    gst_omx_port_get_port_definition (self->dec_out_port, &port_def);
    port_def.nBufferCountActual = 7;    // must be smaller then MAX_FRAME_BUFF_SIZE in mvsink
    gst_omx_port_update_port_definition (self->dec_out_port, &port_def);
  }
}

static void
set_svp_support (GstOMXVideoDec * self)
{
  if (self->svp_version == 20) {
    OMX_U32 u32SupportSvp = 1;
    OMX_PRIORITYMGMTTYPE param;

    GST_DEBUG_OBJECT (self, "Enabling SVP path");
    gst_omx_component_set_parameter (self->dec, OMX_IndexMstarSupportSvp,
        &u32SupportSvp);
    GST_OMX_INIT_STRUCT (&param);
    param.nGroupPriority = TRUE;
    gst_omx_component_set_parameter (self->dec, OMX_IndexMstarTeePath, &param);
  }
}

static gboolean
gst_omx_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstOMXVideoDec *self;
  GstOMXVideoDecClass *klass;
  GstVideoInfo *info = &state->info;
  gboolean is_format_change = FALSE;
  gboolean needs_disable = FALSE;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  GstStructure *s;

  self = GST_OMX_VIDEO_DEC (decoder);
  klass = GST_OMX_VIDEO_DEC_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, state->caps);
  gst_caps_replace (&self->caps, state->caps);
  s = gst_caps_get_structure (self->caps, 0);
  self->media_type = gst_structure_get_name (s);

  const GValue *val;
  guint u3DType = 0;
  val = gst_structure_get_value (s, "typeof3D");
  gboolean bRet = gst_structure_get_uint (s, "typeof3D", &u3DType);
  GST_DEBUG_OBJECT (self, "Get typeof3D bRet=%d", bRet);
  if (bRet == TRUE) {
    GST_DEBUG_OBJECT (self, "Get typeof3D success");
    push_3DType_event (self, val);
  } else {
    GST_DEBUG_OBJECT (self, "Get typeof3D fail");
  }

  parse_stream_format (self);
  set_ts_mode (self);
  set_framerate (self);
  set_max_dimension (self);
  set_skip_to_i_frame (self);
  set_parse_functions (self);
  set_packetized_mode (self);

  if (gst_omx_port_get_port_definition (self->dec_in_port,
          &port_def) != OMX_ErrorNone) {
    GST_DEBUG_OBJECT (self, "fail to get port definition!");
  }
  // Elendil patch for HEVC set format twice w/o width & hight. Reject wo width and height
  if (info->width == 0 || info->height == 0) {
    info->width = 3840;
    info->height = 2160;
  }
  if (klass->cdata.hacks & GST_OMX_HACK_SHARE_OUTPORT_BUFFER)
    self->share_outport_buffer = TRUE;
  else
    self->share_outport_buffer = FALSE;
  // patch end

  /* Check if the caps change is a real format change or if only irrelevant
   * parts of the caps have changed or nothing at all.
   */

  // Remove format change and needs disable checking
  // Because MStart platform do not need to reset OMX IL when format change
  /*
     is_format_change |= port_def.format.video.nFrameWidth != info->width;
     is_format_change |= port_def.format.video.nFrameHeight != info->height;
     is_format_change |= (port_def.format.video.xFramerate == 0
     && info->fps_n != 0)
     || (port_def.format.video.xFramerate !=
     (info->fps_n << 16) / (info->fps_d));
     is_format_change |= (self->codec_data != state->codec_data);
     if (klass->is_format_change)
     is_format_change |=
     klass->is_format_change (self, self->dec_in_port, state);
   */
  needs_disable =
      gst_omx_component_get_state (self->dec,
      GST_CLOCK_TIME_NONE) != OMX_StateLoaded;


  /* If the component is not in Loaded state and a real format change happens
   * we have to disable the port and re-allocate all buffers. If no real
   * format change happened we can just exit here.
   */
  if (needs_disable && !is_format_change) {
    GST_DEBUG_OBJECT (self,
        "Already running and caps did not change the format");
    if (self->input_state)
      gst_video_codec_state_unref (self->input_state);
    self->input_state = gst_video_codec_state_ref (state);
    return TRUE;
  }

  if (needs_disable && is_format_change) {
    GST_DEBUG_OBJECT (self, "Need to disable and drain decoder");

    if (gst_omx_video_dec_drain (self, FALSE) != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "fail to drain data!");
    }
    gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

    /* Wait until the srcpad loop is finished,
     * unlock GST_VIDEO_DECODER_STREAM_LOCK to prevent deadlocks
     * caused by using this lock from inside the loop function */
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (decoder));
    GST_VIDEO_DECODER_STREAM_LOCK (self);

    if (klass->cdata.hacks & GST_OMX_HACK_NO_COMPONENT_RECONFIGURE) {
      GST_VIDEO_DECODER_STREAM_UNLOCK (self);
      gst_omx_video_dec_stop (GST_VIDEO_DECODER (self));
      gst_omx_video_dec_close (GST_VIDEO_DECODER (self));
      GST_VIDEO_DECODER_STREAM_LOCK (self);

      if (!gst_omx_video_dec_open (GST_VIDEO_DECODER (self)))
        return FALSE;
      needs_disable = FALSE;
    } else {
      if (gst_omx_port_set_enabled (self->dec_in_port, FALSE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_set_enabled (self->dec_out_port, FALSE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_buffers_released (self->dec_in_port,
              5 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_buffers_released (self->dec_out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_deallocate_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_video_dec_deallocate_output_buffers (self) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_enabled (self->dec_in_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_enabled (self->dec_out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
      port_def.bEnabled = FALSE;
    }
    if (self->input_state)
      gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;

    GST_DEBUG_OBJECT (self, "Decoder drained and disabled");
  }

  port_def.format.video.nFrameWidth = info->width;
  port_def.format.video.nFrameHeight = info->height;
  if (info->fps_n == 0)
    port_def.format.video.xFramerate = 0;
  else
    port_def.format.video.xFramerate = (info->fps_n << 16) / (info->fps_d);

  GST_DEBUG_OBJECT (self, "Setting inport port definition");

  if (gst_omx_port_update_port_definition (self->dec_in_port,
          &port_def) != OMX_ErrorNone)
    return FALSE;

  if (klass->set_format) {
    if (!klass->set_format (self, self->dec_in_port, state)) {
      GST_ERROR_OBJECT (self, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  gst_omx_port_get_port_definition (self->dec_out_port, &port_def);
  port_def.format.video.nFrameWidth = info->width;
  port_def.format.video.nFrameHeight = info->height;

  GST_DEBUG_OBJECT (self, "Updating outport port definition");
  if (gst_omx_port_update_port_definition (self->dec_out_port,
          &port_def) != OMX_ErrorNone)
    return FALSE;

  set_low_latency_mode (self);
  set_svp_support (self);

  gst_buffer_replace (&self->codec_data, state->codec_data);
  self->input_state = gst_video_codec_state_ref (state);

  GST_DEBUG_OBJECT (self, "Enabling component");

  if (needs_disable) {
    if (gst_omx_port_set_enabled (self->dec_in_port, TRUE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_allocate_buffers (self->dec_in_port) != OMX_ErrorNone)
      return FALSE;

    if ((klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      if (gst_omx_port_set_enabled (self->dec_out_port, TRUE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_allocate_buffers (self->dec_out_port) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->dec_out_port,
              5 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_port_wait_enabled (self->dec_in_port,
            5 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_mark_reconfigured (self->dec_in_port) != OMX_ErrorNone)
      return FALSE;
  } else {
    if (!gst_omx_video_dec_negotiate (self))
      GST_LOG_OBJECT (self, "Negotiation failed, will get output format later");

    if (!(klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      /* Disable output port */
      if (gst_omx_port_set_enabled (self->dec_out_port, FALSE) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->dec_out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_component_set_state (self->dec,
              OMX_StateIdle) != OMX_ErrorNone)
        return FALSE;

      /* Need to allocate buffers to reach Idle state */
      if (gst_omx_port_allocate_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;
    } else {
      if (gst_omx_component_set_state (self->dec,
              OMX_StateIdle) != OMX_ErrorNone)
        return FALSE;

      /* Need to allocate buffers to reach Idle state */
      if (gst_omx_port_allocate_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_allocate_buffers (self->dec_out_port) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_component_get_state (self->dec,
            GST_CLOCK_TIME_NONE) != OMX_StateIdle)
      return FALSE;

    if (gst_omx_component_set_state (self->dec,
            OMX_StateExecuting) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_component_get_state (self->dec,
            GST_CLOCK_TIME_NONE) != OMX_StateExecuting)
      return FALSE;
  }

  /* Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, FALSE);

  if (gst_omx_component_get_last_error (self->dec) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Component in error state: %s (0x%08x)",
        gst_omx_component_get_last_error_string (self->dec),
        gst_omx_component_get_last_error (self->dec));
    return FALSE;
  }

  /* Start the srcpad loop again */
  GST_DEBUG_OBJECT (self, "Starting task again");

  /* New segment event may be sent before _set_format() is called,
   * so we need to set trick mode again here. Note it can only be
   * set after the component is changed to idle state */
  set_trick_mode (self);

  /* Signal SVP handle */
  {
    OMX_U32 u32Handle;
    gst_omx_component_get_parameter (self->dec,
        OMX_IndexMstarVideoDecoderHandle, &u32Handle);
    GST_DEBUG_OBJECT (self, "signal svp handle 0x%08lx", u32Handle);
    g_signal_emit (self, gst_omx_video_dec_signal[SVP_HANDLE_CALLBACK], 0,
        u32Handle);
  }

  self->downstream_flow_ret = GST_FLOW_OK;
  // don't send underrun until playback start for a while
  self->last_underrun_time = gst_util_get_timestamp ();
  gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, decoder, NULL);

  return TRUE;
}

static gboolean
gst_omx_video_dec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  self->has_first_vcl = TRUE;
  self->has_first_au = FALSE;
  self->sync_point = FALSE;

  /* FIXME: Handle different values of hard */
  if (self->no_soft_flush && self->trick_mode == TRICK_I && !hard)
    return TRUE;

  GST_DEBUG_OBJECT (self, "Resetting decoder");

  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

  /* Wait until the srcpad loop is finished,
   * unlock GST_VIDEO_DECODER_STREAM_LOCK to prevent deadlocks
   * caused by using this lock from inside the loop function */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  GST_PAD_STREAM_LOCK (GST_VIDEO_DECODER_SRC_PAD (self));
  GST_PAD_STREAM_UNLOCK (GST_VIDEO_DECODER_SRC_PAD (self));
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_populate (self->dec_out_port);

  /* Start the srcpad loop again */
  self->last_upstream_ts = GST_CLOCK_TIME_NONE;
  self->last_downstream_ts = GST_CLOCK_TIME_NONE;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, decoder, NULL);

  if (hard) {
    self->first_decoded_time = GST_CLOCK_TIME_NONE;
    self->first_decoded_pts = GST_CLOCK_TIME_NONE;
    self->displayed_frames = 0;
    self->dropped_frames = 0;
  }

  GST_DEBUG_OBJECT (self, "Reset decoder");

  return TRUE;
}

enum
{
  GST_H264_NAL_SLICE = 1,
  GST_H264_NAL_SLICE_DPA = 2,
  GST_H264_NAL_SLICE_DPB = 3,
  GST_H264_NAL_SLICE_DPC = 4,
  GST_H264_NAL_SLICE_IDR = 5,
  GST_H264_NAL_SEI = 6,
  GST_H264_NAL_SPS = 7,
  GST_H264_NAL_PPS = 8,
  GST_H264_NAL_AU_DELIMITER = 9,
  GST_H264_NAL_SEQ_END = 10,
  GST_H264_NAL_STREAM_END = 11,
  GST_H264_NAL_FILTER_DATA = 12,
  GST_H264_NAL_PREFIX = 14,
};

enum
{
  GST_H265_NAL_SLICE_TRAIL_N = 0,
  GST_H265_NAL_SLICE_TRAIL_R = 1,
  GST_H265_NAL_SLICE_TSA_N = 2,
  GST_H265_NAL_SLICE_TSA_R = 3,
  GST_H265_NAL_SLICE_STSA_N = 4,
  GST_H265_NAL_SLICE_STSA_R = 5,
  GST_H265_NAL_SLICE_RADL_N = 6,
  GST_H265_NAL_SLICE_RADL_R = 7,
  GST_H265_NAL_SLICE_RASL_N = 8,
  GST_H265_NAL_SLICE_RASL_R = 9,
  GST_H265_NAL_SLICE_BLA_W_LP = 16,
  GST_H265_NAL_SLICE_BLA_W_RADL = 17,
  GST_H265_NAL_SLICE_BLA_N_LP = 18,
  GST_H265_NAL_SLICE_IDR_W_RADL = 19,
  GST_H265_NAL_SLICE_IDR_N_LP = 20,
  GST_H265_NAL_SLICE_CRA_NUT = 21,
  GST_H265_NAL_VPS = 32,
  GST_H265_NAL_SPS = 33,
  GST_H265_NAL_PPS = 34,
  GST_H265_NAL_AUD = 35,
  GST_H265_NAL_EOS = 36,
  GST_H265_NAL_EOB = 37,
  GST_H265_NAL_FD = 38,
  GST_H265_NAL_PREFIX_SEI = 39,
  GST_H265_NAL_SUFFIX_SEI = 40
};

/* simple bitstream parser, automatically skips over
 * emulation_prevention_three_bytes. */
typedef struct
{
  const guint8 *data;
  const guint8 *end;
  gint head;                    /* bitpos in the cache of next bit */
  guint64 cache;                /* cached bytes */
} GstNalBs;

static void
gst_nal_bs_init (GstNalBs * bs, const guint8 * data, guint size)
{
  bs->data = data;
  bs->end = data + size;
  bs->head = 0;
  /* fill with something other than 0 to detect emulation prevention bytes */
  bs->cache = 0xffffffff;
}

static guint32
gst_nal_bs_read (GstNalBs * bs, guint n)
{
  guint32 res = 0;
  gint shift;

  if (n == 0)
    return res;

  /* fill up the cache if we need to */
  while (bs->head < n) {
    guint8 byte;
    gboolean check_three_byte;

    check_three_byte = TRUE;
  next_byte:
    if (bs->data >= bs->end) {
      /* we're at the end, can't produce more than head number of bits */
      n = bs->head;
      break;
    }
    /* get the byte, this can be an emulation_prevention_three_byte that we need
     * to ignore. */
    byte = *bs->data++;
    if (check_three_byte && byte == 0x03 && ((bs->cache & 0xffff) == 0)) {
      /* next byte goes unconditionally to the cache, even if it's 0x03 */
      check_three_byte = FALSE;
      goto next_byte;
    }
    /* shift bytes in cache, moving the head bits of the cache left */
    bs->cache = (bs->cache << 8) | byte;
    bs->head += 8;
  }

  /* bring the required bits down and truncate */
  if ((shift = bs->head - n) > 0)
    res = bs->cache >> shift;
  else
    res = bs->cache;

  /* mask out required bits */
  if (n < 32)
    res &= (1 << n) - 1;

  bs->head = shift;

  return res;
}

static gboolean
gst_nal_bs_eos (GstNalBs * bs)
{
  return (bs->data >= bs->end) && (bs->head == 0);
}

/* read unsigned Exp-Golomb code */
static gint
gst_nal_bs_read_ue (GstNalBs * bs)
{
  gint i = 0;

  while (gst_nal_bs_read (bs, 1) == 0 && !gst_nal_bs_eos (bs) && i < 32)
    i++;

  return ((1 << i) - 1 + gst_nal_bs_read (bs, i));
}

static gboolean
parse_h264_nal (GstOMXVideoDec * self, const guint8 * data, gint size,
    gboolean * au_start, gboolean * sync_point)
{
  gboolean first_vcl = FALSE;
  guint8 nal_type;
  gint offset;

  *au_start = FALSE;
  *sync_point = FALSE;

  for (offset = 0; offset < size; offset++) {
    if (data[offset] != 0)
      break;
  }
  if (offset < 2 || offset + 2 >= size || data[offset] != 1) {
    GST_WARNING_OBJECT (self, "Invalid H.264 nal, prefix len %d, size %d",
        offset + 1, size);
    return FALSE;
  }

  offset++;
  nal_type = data[offset] & 0x1f;
  GST_LOG_OBJECT (self, "H.264 nal type %d, size %d", nal_type, size);

  switch (nal_type) {
    case GST_H264_NAL_SLICE:
    case GST_H264_NAL_SLICE_DPA:
    case GST_H264_NAL_SLICE_DPB:
    case GST_H264_NAL_SLICE_DPC:
    case GST_H264_NAL_SLICE_IDR:
    {
      GstNalBs bs;
      guint8 first_mb_in_slice;
      guint8 slice_type;

      gst_nal_bs_init (&bs, data + offset + 1, size - offset - 1);
      first_mb_in_slice = gst_nal_bs_read_ue (&bs);
      slice_type = gst_nal_bs_read_ue (&bs);

      if (!first_mb_in_slice)
        first_vcl = TRUE;
      if (self->has_first_vcl && first_vcl)
        *au_start = TRUE;
      if (slice_type == 2 || slice_type == 7 || slice_type == 4
          || slice_type == 9)
        *sync_point = TRUE;
      break;
    }
    case GST_H264_NAL_SEI:
    case GST_H264_NAL_SPS:
    case GST_H264_NAL_PPS:
    case GST_H264_NAL_AU_DELIMITER:
      if (self->has_first_vcl)
        *au_start = TRUE;
      break;
    default:
      break;
  }

  if (*au_start)
    self->has_first_vcl = FALSE;
  if (first_vcl)
    self->has_first_vcl = TRUE;
  return TRUE;
}

static gboolean
parse_h265_nal (GstOMXVideoDec * self, const guint8 * data, gint size,
    gboolean * au_start, gboolean * sync_point)
{
  gboolean first_vcl = FALSE;
  guint8 nal_type;
  gint offset;

  *au_start = FALSE;
  *sync_point = FALSE;

  for (offset = 0; offset < size; offset++) {
    if (data[offset] != 0)
      break;
  }
  if (offset < 2 || offset + 3 >= size || data[offset] != 1) {
    GST_WARNING_OBJECT (self, "Invalid H.265 nal, prefix len %d, size %d",
        offset + 1, size);
    return FALSE;
  }

  offset++;
  nal_type = (data[offset] >> 1) & 0x3f;
  GST_LOG_OBJECT (self, "H.265 nal type %d, size %d", nal_type, size);

  if (nal_type <= GST_H265_NAL_SLICE_CRA_NUT) {
    if (data[offset + 2] & 0x80)
      first_vcl = TRUE;
    if (self->has_first_vcl && first_vcl)
      *au_start = TRUE;
    if (nal_type >= GST_H265_NAL_SLICE_BLA_W_LP)
      *sync_point = TRUE;
  } else if (nal_type >= GST_H265_NAL_VPS
      && nal_type <= GST_H265_NAL_PREFIX_SEI) {
    if (self->has_first_vcl)
      *au_start = TRUE;
  }

  if (*au_start)
    self->has_first_vcl = FALSE;
  if (first_vcl)
    self->has_first_vcl = TRUE;
  return TRUE;
}

static const guint8 *
search_nal_unit (const guint8 * data, gint size)
{
  const guint8 *ptr = data;
  const guint8 *end = data + size;
  guint32 code = 0xffffffff;

  while (ptr < end) {
    code = (code << 8) | *ptr++;
    if (G_UNLIKELY ((code & 0xffffff) == 0x1)) {
      return code == 0x1 ? ptr - 4 : ptr - 3;
    }
  }
  return 0;
}

enum
{
  GST_M2V_START_CODE_PICTURE = 0x100,
  GST_M2V_START_CODE_SLICE_MIN = 0x101,
  GST_M2V_START_CODE_SLICE_MAX = 0x1af,
  GST_M2V_START_CODE_USER_DATA = 0x1b2,
  GST_M2V_START_CODE_SEQUENCE = 0x1b3,
  GST_M2V_START_CODE_EXTENSION = 0x1b5,
  GST_M2V_START_CODE_SEQUENCE_END = 0x1b7,
  GST_M2V_START_CODE_GOP = 0x1b8
};

static gboolean
parse_m2v_start_code (GstOMXVideoDec * self, const guint8 * data, gint size,
    gboolean * au_start, gboolean * sync_point)
{
  gboolean first_vcl = FALSE;
  guint32 start_code;

  *au_start = FALSE;
  *sync_point = FALSE;

  if (size < 4) {
    GST_WARNING_OBJECT (self, "Invalid M2V start code, size %d", size);
    return FALSE;
  }

  start_code = GST_READ_UINT32_BE (data);
  GST_LOG_OBJECT (self, "M2V start code 0x%x, size %d", start_code, size);

  switch (start_code) {
    case GST_M2V_START_CODE_PICTURE:
      first_vcl = TRUE;
      if (self->has_first_vcl)
        *au_start = TRUE;
      if (((data[5] >> 3) & 0x7) == 1)
        *sync_point = TRUE;
      break;
    case GST_M2V_START_CODE_SEQUENCE:
      if (self->has_first_vcl)
        *au_start = TRUE;
      *sync_point = TRUE;
      break;
  }

  if (*au_start)
    self->has_first_vcl = FALSE;
  if (first_vcl)
    self->has_first_vcl = TRUE;
  return TRUE;
}

static const guint8 *
search_m2v_start_code (const guint8 * data, gint size)
{
  const guint8 *ptr = data;
  const guint8 *end = data + size;
  guint32 code = 0xffffffff;

  while (ptr < end) {
    code = (code << 8) | *ptr++;
    if (G_UNLIKELY ((code & 0xffffff00) == 0x100)) {
      switch (code) {
        case GST_M2V_START_CODE_PICTURE:
        case GST_M2V_START_CODE_USER_DATA:
        case GST_M2V_START_CODE_SEQUENCE:
        case GST_M2V_START_CODE_EXTENSION:
        case GST_M2V_START_CODE_SEQUENCE_END:
        case GST_M2V_START_CODE_GOP:
          return ptr - 4;
        default:
          break;
      }
    }
  }
  return 0;
}

static gint
align_start_code_unit (GstOMXVideoDec * self, GstAdapter * adapter,
    gboolean align_end)
{
  const guint8 *sc_ptr;
  const guint8 *data;
  gint avail;

  avail = gst_adapter_available (adapter);
  data = gst_adapter_map (adapter, avail);
  sc_ptr = self->search_start_code (data, avail);
  gst_adapter_unmap (adapter);

  /* make sure the first nal is located at the beginning of the buffer */
  if (!sc_ptr) {
    gst_adapter_flush (adapter, MAX (0, avail - 3));
    return 0;
  } else if (sc_ptr > data) {
    gst_adapter_flush (adapter, sc_ptr - data);
    avail = gst_adapter_available (adapter);
  }

  if (avail > 3) {
    data = gst_adapter_map (adapter, avail);
    sc_ptr = self->search_start_code (data + 3, avail - 3);
    gst_adapter_unmap (adapter);
  } else {
    sc_ptr = 0;
  }

  /* return size of start code unit */
  if (sc_ptr)
    return sc_ptr - data;
  else if (align_end)
    return avail;
  else
    return 0;
}

static GstFlowReturn
parse_one_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame,
    GstAdapter * adapter)
{
  GstOMXVideoDec *self;
  GstFlowReturn ret = GST_FLOW_OK;
  const guint8 *data;
  gint avail;

  self = GST_OMX_VIDEO_DEC (decoder);

  while ((avail = align_start_code_unit (self, adapter, TRUE))) {
    gboolean au_start = FALSE;
    gboolean sync_point = FALSE;

    data = gst_adapter_map (adapter, avail);
    self->parse_start_code (self, data, avail, &au_start, &sync_point);
    gst_adapter_unmap (adapter);

    gst_video_decoder_add_to_frame (decoder, avail);
    if (sync_point)
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  }

  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  ret = gst_video_decoder_have_frame (decoder);
  GST_VIDEO_DECODER_STREAM_LOCK (self);
  return ret;
}

static GstFlowReturn
parse_one_start_code (GstVideoDecoder * decoder, GstVideoCodecFrame * frame,
    GstAdapter * adapter, gint avail)
{
  GstOMXVideoDec *self;
  gboolean au_start = FALSE;
  gboolean sync_point = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;
  const guint8 *data;

  self = GST_OMX_VIDEO_DEC (decoder);

  if (avail) {
    data = gst_adapter_map (adapter, avail);
    self->parse_start_code (self, data, avail, &au_start, &sync_point);
    gst_adapter_unmap (adapter);

    if (au_start) {
      if (self->has_first_au) {
        if (self->sync_point)
          GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
        GST_VIDEO_DECODER_STREAM_UNLOCK (self);
        ret = gst_video_decoder_have_frame (decoder);
        GST_VIDEO_DECODER_STREAM_LOCK (self);
      }
      self->has_first_au = TRUE;
      self->sync_point = FALSE;
    }
    if (sync_point)
      self->sync_point = TRUE;

    if (self->has_first_au)
      gst_video_decoder_add_to_frame (decoder, avail);
    else
      gst_adapter_flush (adapter, avail);
  }
  return ret;
}

static GstFlowReturn
gst_omx_video_dec_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos)
{
  GstOMXVideoDec *self;
  GstFlowReturn ret = GST_FLOW_ERROR;
  gint avail;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Parsing frame");
  if (!self->search_start_code || !self->parse_start_code) {
    GST_WARNING_OBJECT (self, "Flow error due to null parse functions");
    return GST_FLOW_ERROR;
  }

  switch (self->parse_alignment) {
    case ALIGNMENT_AU:
      ret = parse_one_frame (decoder, frame, adapter);
      break;
    case ALIGNMENT_SC:
      avail = gst_adapter_available (adapter);
      ret = parse_one_start_code (decoder, frame, adapter, avail);
      break;
    default:
      avail = align_start_code_unit (self, adapter, FALSE);
      ret = parse_one_start_code (decoder, frame, adapter, avail);
      break;
  }
  return ret;
}

static guint
gst_omx_video_dec_parse_h264_codec_data (GstOMXVideoDec * self,
    GstBuffer * codec_data, guint8 * dest, gsize dest_size)
{
  GstMapInfo map;
  guint8 *data;
  guint num_sps, num_pps;
  guint profile, off, dest_off = 0;
  guint nal_size;
  gint i;
  gsize size;

  gst_buffer_map (codec_data, &map, GST_MAP_READ);
  data = map.data;
  size = map.size;

  /* parse the avcC data */
  if (size < 7)                 /* when numSPS==0 and numPPS==0, length is 7 bytes */
    goto avcc_too_small;
  /* parse the version, this must be 1 */
  if (data[0] != 1)
    goto wrong_version;
  /* AVCProfileIndication */
  /* profile_compat */
  /* AVCLevelIndication */
  profile = (data[1] << 16) | (data[2] << 8) | data[3];
  GST_DEBUG_OBJECT (self, "profile %06x", profile);

  /* 6 bits reserved | 2 bits lengthSizeMinusOne */
  /* this is the number of bytes in front of the NAL units to mark their
   * length */
  self->nal_len_size = (data[4] & 0x03) + 1;
  GST_DEBUG_OBJECT (self, "nal length size %u", self->nal_len_size);

  num_sps = data[5] & 0x1f;
  GST_DEBUG_OBJECT (self, "sps cnt: %u", num_sps);
  off = 6;
  for (i = 0; i < num_sps; i++) {
    if (off + 2 > size)
      goto avcc_error_data;
    nal_size = GST_READ_UINT16_BE (data + off);
    GST_DEBUG_OBJECT (self, "sps size: %d %u", i, nal_size);
    off += 2;
    if (off + nal_size > size)
      goto avcc_error_data;
    if (dest_off + 4 + nal_size > dest_size)
      goto avcc_error_data;
    GST_WRITE_UINT32_BE (dest + dest_off, 1);   // nal start code prefix
    dest_off += 4;
    memcpy (dest + dest_off, data + off, nal_size);
    dest_off += nal_size;
    off += nal_size;
  }

  num_pps = data[off];
  GST_DEBUG_OBJECT (self, "pps cnt: %u", num_pps);
  off++;

  for (i = 0; i < num_pps; i++) {
    if (off + 2 > size)
      goto avcc_error_data;
    nal_size = GST_READ_UINT16_BE (data + off);
    GST_DEBUG_OBJECT (self, "pps size: %d %u", i, nal_size);
    off += 2;
    if (off + nal_size > size)
      goto avcc_error_data;
    if (dest_off + 4 + nal_size > dest_size)
      goto avcc_error_data;
    GST_WRITE_UINT32_BE (dest + dest_off, 1);   // nal start code prefix
    dest_off += 4;
    memcpy (dest + dest_off, data + off, nal_size);
    dest_off += nal_size;
    off += nal_size;
  }

  gst_buffer_unmap (codec_data, &map);
  return dest_off;
avcc_error_data:
  {
    GST_WARNING_OBJECT (self, "avcC error data");
    gst_buffer_unmap (codec_data, &map);
    return 0;
  }
avcc_too_small:
  {
    GST_WARNING_OBJECT (self, "avcC size %" G_GSIZE_FORMAT " < 8", size);
    gst_buffer_unmap (codec_data, &map);
    return 0;
  }
wrong_version:
  {
    GST_WARNING_OBJECT (self, "wrong avcC version");
    gst_buffer_unmap (codec_data, &map);
    return 0;
  }
}

static guint
gst_omx_video_dec_parse_h265_codec_data (GstOMXVideoDec * self,
    GstBuffer * codec_data, guint8 * dest, gsize dest_size)
{
  GstMapInfo map;
  guint8 *data;
  guint off, dest_off = 0;
  guint num_nals, i, j;
  guint nal_size;
  gsize size;

  gst_buffer_map (codec_data, &map, GST_MAP_READ);
  data = map.data;
  size = map.size;

  /* parse the hvcC data */
  if (size < 23)
    goto hvcc_too_small;
  /* parse the version, this must be one but
   * is zero until the spec is finalized */
  if (data[0] != 0 && data[0] != 1)
    goto wrong_version;

  self->nal_len_size = (data[21] & 0x03) + 1;
  GST_DEBUG_OBJECT (self, "nal length size %u", self->nal_len_size);

  off = 23;
  for (i = 0; i < data[22]; i++) {
    if (off + 3 > size)
      goto hvcc_error_data;
    num_nals = GST_READ_UINT16_BE (data + off + 1);
    off += 3;
    for (j = 0; j < num_nals; j++) {
      if (off + 2 > size)
        goto hvcc_error_data;
      nal_size = GST_READ_UINT16_BE (data + off);
      GST_DEBUG_OBJECT (self, "nal size: %d %d %u", i, j, nal_size);
      off += 2;
      if (off + nal_size > size)
        goto hvcc_error_data;
      if (dest_off + 4 + nal_size > dest_size)
        goto hvcc_error_data;
      GST_WRITE_UINT32_BE (dest + dest_off, 1); // nal start code prefix
      dest_off += 4;
      memcpy (dest + dest_off, data + off, nal_size);
      dest_off += nal_size;
      off += nal_size;
    }
  }

  gst_buffer_unmap (codec_data, &map);
  return dest_off;

hvcc_error_data:
  {
    GST_WARNING_OBJECT (self, "hvcC error data");
    gst_buffer_unmap (codec_data, &map);
    return 0;
  }
hvcc_too_small:
  {
    GST_WARNING_OBJECT (self, "hvcC size %" G_GSIZE_FORMAT " < 23", size);
    gst_buffer_unmap (codec_data, &map);
    return 0;
  }
wrong_version:
  {
    GST_WARNING_OBJECT (self, "wrong hvcC version");
    gst_buffer_unmap (codec_data, &map);
    return 0;
  }
}

static guint
gst_omx_video_dec_parse_frame_data (GstOMXVideoDec * self,
    GstBuffer * frame_data, guint8 * dest, gsize dest_size)
{
  GstMapInfo map;
  guint8 *data;
  guint off, nal_size, dest_off = 0;
  gint i;
  gsize size;

  gst_buffer_map (frame_data, &map, GST_MAP_READ);
  data = map.data;
  size = map.size;
  off = 0;

  while (off < size) {
    if (off + self->nal_len_size > size)
      goto data_too_large;

    nal_size = 0;
    for (i = 0; i < self->nal_len_size; i++)
      nal_size = (nal_size << 8) | (*(data + off + i));
    off += self->nal_len_size;

    if (dest_off + 4 + nal_size > dest_size || off + nal_size > size)
      goto data_too_large;

    GST_WRITE_UINT32_BE (dest + dest_off, 1);   // nal start code prefix
    dest_off += 4;

    memcpy (dest + dest_off, data + off, nal_size);
    dest_off += nal_size;
    off += nal_size;
  }

  gst_buffer_unmap (frame_data, &map);
  return dest_off;

data_too_large:
  GST_WARNING_OBJECT (self, "data too large");
  gst_buffer_unmap (frame_data, &map);
  return 0;
}

static void
set_trick_mode (GstOMXVideoDec * self)
{
  guint trick_mode;
  guint32 width = self->dec_in_port->port_def.format.video.nFrameWidth;
  guint32 height = self->dec_in_port->port_def.format.video.nFrameHeight;

  GST_DEBUG_OBJECT (self, "rate %f, width %d, height %d",
      GST_VIDEO_DECODER (self)->input_segment.rate, width, height);

  if (GST_VIDEO_DECODER (self)->input_segment.rate == 2.0 && width > 2000) {
    trick_mode = TRICK_I;
  } else if (GST_VIDEO_DECODER (self)->input_segment.rate > 0.0
      && GST_VIDEO_DECODER (self)->input_segment.rate < 2.5) {
    trick_mode = TRICK_ALL;
  } else {
    trick_mode = TRICK_I;
  }

  if (trick_mode != self->trick_mode) {
    OMX_ERRORTYPE err;
    err =
        gst_omx_component_set_parameter (self->dec, OMX_IndexMstarTrickMode,
        &trick_mode);
    if (err == OMX_ErrorNone) {
      GST_DEBUG_OBJECT (self, "Set trick mode %u success", trick_mode);
      self->trick_mode = trick_mode;
    } else {
      GST_DEBUG_OBJECT (self, "Set trick mode %u failed, err 0x%x", trick_mode,
          err);
    }
  }
}

static GstClockTime
calculate_expire_time (GstOMXVideoDec * self, GstVideoCodecFrame * frame,
    GstClockTime start_time, GstClockTimeDiff max_decode)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (self);
  GstClockTime expire_time;
  GstClockTimeDiff time_diff;

  /* During rewind, decide expire time according to 3 cases:
   * 1. start_time + max_decode, because it may take longer time than usual if
   *    basesink queues too many frames and not release their frame buffers
   *    immediately.
   * 2. If max_decode equals G_MAXINT64, then deadline hasn't been decided yet.
   *    Then we estimate expire_time using pts of first frame and the time when
   *    it was decoded.
   * 3. If max_decode equals G_MAXINT64 and first frame hasn't been decoded, we
   *    choose default timeout value (500 ms).
   */
  if (max_decode == G_MAXINT64) {
    expire_time = start_time + 500 * GST_MSECOND;
    if (GST_CLOCK_TIME_IS_VALID (self->first_decoded_pts)
        && GST_CLOCK_TIME_IS_VALID (frame->pts)) {
      time_diff = GST_CLOCK_DIFF (self->first_decoded_pts, frame->pts);
      if (decoder->input_segment.rate && decoder->input_segment.rate != 1.0)
        time_diff /= decoder->input_segment.rate;
      expire_time = MAX (expire_time, self->first_decoded_time + time_diff);
    }
  } else {
    expire_time = start_time + max_decode;
  }
  return expire_time;
}

static GstFlowReturn
gst_omx_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXVideoDec *self;
  GstOMXVideoDecClass *klass;
  GstOMXPort *port;
  GstOMXBuffer *buf;
  GstBuffer *codec_data = NULL;
  guint offset = 0, size;
  GstClockTime timestamp, duration;
  GstClockTime start_time, expire_time;
  GstClockTimeDiff max_decode;
  OMX_ERRORTYPE err;

  self = GST_OMX_VIDEO_DEC (decoder);
  klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Handling frame");
  self->decoded_size += gst_buffer_get_size (frame->input_buffer);

  if (self->eos) {
    GST_WARNING_OBJECT (self, "Got frame after EOS");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_EOS;
  }

  if (self->skip_to_i && !self->started
      && !GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame)) {
    GST_WARNING_OBJECT (self, "Drop non-I frame before start");
    gst_video_decoder_release_frame (GST_VIDEO_DECODER (self), frame);
    return GST_FLOW_OK;
  }

  if (gst_buffer_get_size (frame->input_buffer) >
      self->dec_in_port->port_def.nBufferSize) {
    GST_WARNING_OBJECT (self, "Drop frame with size %u > %u",
        gst_buffer_get_size (frame->input_buffer),
        (unsigned int) self->dec_in_port->port_def.nBufferSize);
    gst_video_decoder_release_frame (GST_VIDEO_DECODER (self), frame);
    return GST_FLOW_OK;
  }

  max_decode =
      gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (self), frame);
  start_time = gst_util_get_timestamp ();
  expire_time = calculate_expire_time (self, frame, start_time, max_decode);

  if (self->trick_mode == TRICK_I && self->input_drop) {
    /* Drop all P/B frames during I-only mode */
    if (!GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame)) {
      gst_video_decoder_release_frame (GST_VIDEO_DECODER (self), frame);
      return GST_FLOW_OK;
    }
  }

  timestamp = self->use_dts ? frame->dts : frame->pts;
  duration = frame->duration;

  if (self->downstream_flow_ret != GST_FLOW_OK) {
    gst_video_codec_frame_unref (frame);
    GST_WARNING_OBJECT (self, "downstream_flow_ret is not OK");
    return self->downstream_flow_ret;
  }

  if (klass->prepare_frame) {
    GstFlowReturn ret;

    ret = klass->prepare_frame (self, frame);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Preparing frame failed: %s",
          gst_flow_get_name (ret));
      gst_video_codec_frame_unref (frame);
      return ret;
    }
  }

  port = self->dec_in_port;

  size = gst_buffer_get_size (frame->input_buffer);
  while (offset < size) {
    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    acq_ret = gst_omx_port_acquire_buffer (port, &buf);

    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_ERROR) {
      GST_VIDEO_DECODER_STREAM_LOCK (self);
      goto component_error;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      GST_VIDEO_DECODER_STREAM_LOCK (self);
      goto flushing;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      /* Reallocate all buffers */
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_allocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone) {
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      /* Now get a new buffer and fill it */
      GST_VIDEO_DECODER_STREAM_LOCK (self);
      continue;
    }
    GST_VIDEO_DECODER_STREAM_LOCK (self);

    g_assert (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

    if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <= 0) {
      gst_omx_port_release_buffer (port, buf);
      goto full_buffer;
    }

    if (self->downstream_flow_ret != GST_FLOW_OK) {
      gst_omx_port_release_buffer (port, buf);
      goto flow_error;
    }

    if (self->codec_data) {
      GST_DEBUG_OBJECT (self, "Passing codec data to the component");

      codec_data = self->codec_data;

      if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <
          gst_buffer_get_size (codec_data)) {
        gst_omx_port_release_buffer (port, buf);
        goto too_large_codec_data;
      }

      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

      if (self->stream_format == GST_H264_PARSE_FORMAT_AVC ||
          self->stream_format == GST_H264_PARSE_FORMAT_AVC3) {
        buf->omx_buf->nFilledLen =
            gst_omx_video_dec_parse_h264_codec_data (self, codec_data,
            buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
            buf->omx_buf->nAllocLen);
      } else if (self->stream_format == GST_H265_PARSE_FORMAT_HVC1
          || self->stream_format == GST_H265_PARSE_FORMAT_HEV1) {
        buf->omx_buf->nFilledLen =
            gst_omx_video_dec_parse_h265_codec_data (self, codec_data,
            buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
            buf->omx_buf->nAllocLen);
      } else {
        buf->omx_buf->nFilledLen = gst_buffer_get_size (codec_data);
        gst_buffer_extract (codec_data, 0,
            buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
            buf->omx_buf->nFilledLen);
      }

      if (GST_CLOCK_TIME_IS_VALID (timestamp))
        buf->omx_buf->nTimeStamp =
            gst_util_uint64_scale (timestamp, OMX_TICKS_PER_SECOND, GST_SECOND);
      else
        buf->omx_buf->nTimeStamp = -1;
      buf->omx_buf->nTickCount = 0;

      self->started = TRUE;
      err = gst_omx_port_release_buffer (port, buf);
      gst_buffer_replace (&self->codec_data, NULL);
      if (err != OMX_ErrorNone)
        goto release_error;
      /* Acquire new buffer for the actual frame */
      continue;
    }

    /* Now handle the frame */
    GST_DEBUG_OBJECT (self, "Passing frame offset %d to the component", offset);

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    if (self->stream_format == GST_H264_PARSE_FORMAT_AVC ||
        self->stream_format == GST_H264_PARSE_FORMAT_AVC3 ||
        self->stream_format == GST_H265_PARSE_FORMAT_HVC1 ||
        self->stream_format == GST_H265_PARSE_FORMAT_HEV1) {
      buf->omx_buf->nFilledLen = gst_omx_video_dec_parse_frame_data (self,
          frame->input_buffer, buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nAllocLen);
      if (!buf->omx_buf->nFilledLen) {
        GST_WARNING_OBJECT (self, "Invalid AVC/HEVC packet, dropping");
        gst_omx_port_release_buffer (port, buf);
        goto flow_error;
      }
    } else {
      buf->omx_buf->nFilledLen =
          MIN (size - offset, buf->omx_buf->nAllocLen - buf->omx_buf->nOffset);
      gst_buffer_extract (frame->input_buffer, offset,
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);
    }

    if (timestamp != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTimeStamp =
          gst_util_uint64_scale (timestamp, OMX_TICKS_PER_SECOND, GST_SECOND);
      self->last_upstream_ts = timestamp;
    } else {
      buf->omx_buf->nTimeStamp = -1;
    }

    if (duration != GST_CLOCK_TIME_NONE && offset == 0) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (buf->omx_buf->nFilledLen, duration, size);
      self->last_upstream_ts += duration;
    } else {
      buf->omx_buf->nTickCount = 0;
    }

    if (offset == 0) {
      BufferIdentification *id = g_slice_new0 (BufferIdentification);

      if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame))
        buf->omx_buf->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;

      id->timestamp = buf->omx_buf->nTimeStamp;
      id->start_time = start_time;
      id->expire_time = GST_CLOCK_TIME_NONE;

      if (decoder->input_segment.rate < 0.0 && !self->direct_rewind_push
          && GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame)) {
        GST_DEBUG_OBJECT (self, "Set rewind blocking");
        self->rewind_blocking = TRUE;
        id->expire_time = expire_time;
      }

      gst_video_codec_frame_set_user_data (frame, id,
          (GDestroyNotify) buffer_identification_free);
    }

    /* TODO: Set flags
     *   - OMX_BUFFERFLAG_DECODEONLY for buffers that are outside
     *     the segment
     */

    offset += buf->omx_buf->nFilledLen;

    if (self->stream_format == GST_H264_PARSE_FORMAT_AVC ||
        self->stream_format == GST_H264_PARSE_FORMAT_AVC3 ||
        self->stream_format == GST_H265_PARSE_FORMAT_HVC1 ||
        self->stream_format == GST_H265_PARSE_FORMAT_HEV1)
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
    else if (offset == size)
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    self->started = TRUE;
    err = gst_omx_port_release_buffer (port, buf);
    if (err != OMX_ErrorNone)
      goto release_error;
  }

  if (decoder->input_segment.rate < 0.0 && self->direct_rewind_push) {
    gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
  } else {
    if (self->rewind_blocking) {
      wait_rewind_unblocking (self, expire_time);
      self->rewind_blocking = FALSE;
    }
    gst_video_codec_frame_unref (frame);
  }

  GST_DEBUG_OBJECT (self, "Passed frame to component");

  return self->downstream_flow_ret;

full_buffer:
  {
    self->rewind_blocking = FALSE;
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Got OpenMAX buffer with no free space (%p, %u/%u)", buf,
            (guint) buf->omx_buf->nOffset, (guint) buf->omx_buf->nAllocLen));
    return GST_FLOW_ERROR;
  }

flow_error:
  {
    GST_DEBUG_OBJECT (self, "flow error");
    self->rewind_blocking = FALSE;
    gst_video_codec_frame_unref (frame);

    return self->downstream_flow_ret;
  }

too_large_codec_data:
  {
    self->rewind_blocking = FALSE;
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
        ("codec_data larger than supported by OpenMAX port (%u > %u)",
            (guint) gst_buffer_get_size (codec_data),
            (guint) self->dec_in_port->port_def.nBufferSize));
    return GST_FLOW_ERROR;
  }

component_error:
  {
    self->rewind_blocking = FALSE;
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->dec),
            gst_omx_component_get_last_error (self->dec)));
    return GST_FLOW_ERROR;
  }

flushing:
  {
    self->rewind_blocking = FALSE;
    gst_video_codec_frame_unref (frame);
    GST_DEBUG_OBJECT (self, "Flushing -- returning FLUSHING");
    return GST_FLOW_FLUSHING;
  }
reconfigure_error:
  {
    self->rewind_blocking = FALSE;
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure input port"));
    return GST_FLOW_ERROR;
  }
release_error:
  {
    self->rewind_blocking = FALSE;
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase input buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_omx_video_dec_finish (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  return gst_omx_video_dec_drain (self, TRUE);
}

static GstFlowReturn
gst_omx_video_dec_drain (GstOMXVideoDec * self, gboolean is_eos)
{
  GstOMXVideoDecClass *klass;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (self, "Draining component");

  klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Component not started yet");
    return GST_FLOW_OK;
  }
  self->started = FALSE;

  /* Don't send EOS buffer twice, this doesn't work */
  if (self->eos) {
    GST_DEBUG_OBJECT (self, "Component is EOS already");
    return GST_FLOW_OK;
  }
  if (is_eos)
    self->eos = TRUE;

  if ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER)) {
    GST_WARNING_OBJECT (self, "Component does not support empty EOS buffers");
    return GST_FLOW_OK;
  }

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->dec_in_port, &buf);
  if (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    GST_VIDEO_DECODER_STREAM_LOCK (self);
    GST_ERROR_OBJECT (self, "Failed to acquire buffer for draining: %d",
        acq_ret);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&self->drain_lock);
  self->draining = TRUE;
  buf->omx_buf->nFilledLen = 0;
  buf->omx_buf->nTimeStamp = 0;
  buf->omx_buf->nTickCount = 0;
  buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
  err = gst_omx_port_release_buffer (self->dec_in_port, buf);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to drain component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    g_mutex_unlock (&self->drain_lock);
    GST_VIDEO_DECODER_STREAM_LOCK (self);
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (self, "Waiting until component is drained");

  if (G_UNLIKELY (self->dec->hacks & GST_OMX_HACK_DRAIN_MAY_NOT_RETURN)) {
    gint64 wait_until = g_get_monotonic_time () + G_TIME_SPAN_SECOND / 2;

    if (!g_cond_wait_until (&self->drain_cond, &self->drain_lock, wait_until))
      GST_WARNING_OBJECT (self, "Drain timed out");
    else
      GST_DEBUG_OBJECT (self, "Drained component");

  } else {
    g_cond_wait (&self->drain_cond, &self->drain_lock);
    GST_DEBUG_OBJECT (self, "Drained component");
  }

  g_mutex_unlock (&self->drain_lock);
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  self->started = FALSE;

  return GST_FLOW_OK;
}

static gboolean
gst_omx_video_dec_decide_allocation (GstVideoDecoder * bdec, GstQuery * query)
{
  GstBufferPool *pool;
  GstStructure *config;

  if (!GST_VIDEO_DECODER_CLASS
      (gst_omx_video_dec_parent_class)->decide_allocation (bdec, query))
    return FALSE;

  g_assert (gst_query_get_n_allocation_pools (query) > 0);
  gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);
  g_assert (pool != NULL);

  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  gst_buffer_pool_set_config (pool, config);
  gst_object_unref (pool);

  return TRUE;
}

static void
push_3DType_event (GstOMXVideoDec * self, const GValue * val)
{
  GstEvent *eventP;
  GstStructure *s;

  GST_DEBUG_OBJECT (self, "push 3DType event");
  GstEventType eventType = GST_EVENT_CUSTOM_DOWNSTREAM;

  s = gst_structure_new ("typeof3D", 0, (char *) 0);
  if (s == NULL)
    GST_LOG_OBJECT (self, "gst_structure_new returned NULL");

  gst_structure_set_value (s, "3DTypeValue", val);

  eventP = gst_event_new_custom (eventType, s);
  if (eventP == NULL)
    GST_DEBUG_OBJECT (self, "can't construct ");
  if (!gst_element_send_event (GST_ELEMENT (self), eventP))
    GST_DEBUG_OBJECT (self, "error in sending 3DType info event");
}

static void
push_buffer_underrun_event (GstOMXVideoDec * self)
{
  GstEvent *eventP;
  GstStructure *s;
  GstState state;
  GstStateChangeReturn ret;

  ret =
      gst_element_get_state (GST_ELEMENT (self), &state, NULL,
      100 * GST_MSECOND);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    GST_WARNING_OBJECT (self, "get state error %s",
        gst_element_state_change_return_get_name (ret));
    return;
  }
  GST_LOG_OBJECT (self, "push buffer underrun event, do push %d, state %s",
      state == GST_STATE_PLAYING, gst_element_state_get_name (state));
  if (state != GST_STATE_PLAYING)
    return;

  s = gst_structure_new ("BUFFER_UNDERRUN", 0, (char *) 0);
  if (s == NULL)
    GST_LOG_OBJECT (self, "gst_structure_new returned NULL");

  eventP = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);
  if (eventP == NULL)
    GST_LOG_OBJECT (self, "can't construct");
  if (!gst_element_send_event (GST_ELEMENT (self), eventP))
    GST_LOG_OBJECT (self, "error in sending under run event");
}
