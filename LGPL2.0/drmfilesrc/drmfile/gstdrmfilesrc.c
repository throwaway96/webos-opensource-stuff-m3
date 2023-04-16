
/**
 * SECTION:element-drmfile
 *
 * FIXME:Describe drmfile here.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include "gstdrmfilesrc.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef G_OS_WIN32
#include <io.h>                 /* lseek, open, close, read */
/* On win32, stat* default to 32 bit; we need the 64-bit
 * variants, so explicitly define it that way. */
#undef stat
#define stat __stat64
#undef fstat
#define fstat _fstat64
#undef lseek
#define lseek _lseeki64
#undef off_t
#define off_t guint64
/* Prevent stat.h from defining the stat* functions as
 * _stat*, since we're explicitly overriding that */
#undef _INC_STAT_INL
#endif
#include <fcntl.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <errno.h>
#include <string.h>

//#include "../../gst/gst-i18n-lib.h"

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* FIXME we should be using glib for this */
#ifndef S_ISREG
define S_ISREG(mode) ((mode)&_S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode) ((mode)&_S_IFDIR)
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(x) (0)
#endif
#ifndef O_BINARY
#define O_BINARY (0)
#endif

#define DRM_INCLUDE
#ifdef DRM_INCLUDE

#include <dlfcn.h>

static struct DrmFileApi{

void *  dlHandle;
void *  drmHandle;
void *  (*drm_load)(const char * drmType, const char * cid, const char * url);
int     (*drm_release)(void * dHandle);
int     (*drm_open)(const char *filename, int oflag, int pmode);
int     (*drm_read)(int handle,void * buffer,unsigned int count);
long    (*drm_lseek)(int handle, long offser, int origin);
long    (*drm_fstat)(int handle, void * statBuf);
void    (*drm_close)(int handle);
int     (*drm_getFileSize)(int handle, unsigned int * nConetentsSize);

}DrmFileApi;

#endif


/* Copy of glib's g_open due to win32 libc/cross-DLL brokenness: we can't
 * use the 'file descriptor' opened in glib (and returned from this function)
 * in this library, as they may have unrelated C runtimes. */
static int
gst_open (const gchar * filename, int flags, int mode)
{
#ifdef G_OS_WIN32
  wchar_t *wfilename = g_utf8_to_utf16 (filename, -1, NULL, NULL, NULL);
  int retval;
  int save_errno;

  if (wfilename == NULL) {
    errno = EINVAL;
    return -1;
  }

  retval = _wopen (wfilename, flags, mode);
  save_errno = errno;

  g_free (wfilename);

  errno = save_errno;
  return retval;

#else
#ifdef DRM_INCLUDE
  return  DrmFileApi.drm_open(filename, flags, mode);
#else
  return open (filename, flags, mode);
#endif

#endif
}

GST_DEBUG_CATEGORY_STATIC (gst_drm_file_src_debug);
#define GST_CAT_DEFAULT gst_drm_file_src_debug

/* FileSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_BLOCKSIZE       4*1024

enum
{
  PROP_0,
  PROP_LOCATION
};

static void gst_drm_file_src_finalize (GObject * object);

static void gst_drm_file_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_drm_file_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_drm_file_src_start (const GstBaseSrc * const basesrc);
static gboolean gst_drm_file_src_stop (GstBaseSrc * basesrc);

static gboolean gst_drm_file_src_is_seekable (GstBaseSrc * src);
static gboolean gst_drm_file_src_get_size (GstBaseSrc * src, guint64 * size);
static GstFlowReturn gst_drm_file_src_fill (GstBaseSrc * src, guint64 offset,
    guint length, GstBuffer * buf);

static void gst_drm_file_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

#define _do_init \
  G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_drm_file_src_uri_handler_init); \
  GST_DEBUG_CATEGORY_INIT (gst_drm_file_src_debug, "drmfilesrc", 0, "drmfilesrc element");
#define gst_drm_file_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDrmFileSrc, gst_drm_file_src, GST_TYPE_BASE_SRC, _do_init);

static void
gst_drm_file_src_class_init (GstDrmFileSrcClass * klass)
{

  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_drm_file_src_set_property;
  gobject_class->get_property = gst_drm_file_src_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to read", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gobject_class->finalize = gst_drm_file_src_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "DRM File Source",
      "Source/File",
      "Read from arbitrary point in a DRM file",
      "Jeeseung Jung <jeeseung.jung@lge.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_drm_file_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_drm_file_src_stop);
  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_drm_file_src_is_seekable);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_drm_file_src_get_size);
  gstbasesrc_class->fill = GST_DEBUG_FUNCPTR (gst_drm_file_src_fill);

  if (sizeof (off_t) < 8) {
    GST_LOG ("No large file support, sizeof (off_t) = %" G_GSIZE_FORMAT "!",sizeof (off_t));
  }

}

static void
gst_drm_file_src_init (GstDrmFileSrc * src)
{

#ifdef DRM_INCLUDE
  gchar * error = NULL;
  DrmFileApi.dlHandle = NULL;
  DrmFileApi.drmHandle = NULL;
  DrmFileApi.dlHandle = dlopen("/usr/lib/libdrmcontroller.so.1",RTLD_NOW);
  if(!DrmFileApi.dlHandle)
    goto init_failed;
  DrmFileApi.drm_load  =  dlsym(DrmFileApi.dlHandle,"API_DRM_Load");
  if(((error = dlerror()) != NULL) || (DrmFileApi.drm_load == NULL))goto init_failed;
  DrmFileApi.drm_release  =  dlsym(DrmFileApi.dlHandle,"API_DRM_Release");
  if(((error = dlerror()) != NULL) || (DrmFileApi.drm_release == NULL))goto init_failed;
  DrmFileApi.drm_open  =  dlsym(DrmFileApi.dlHandle,"API_DRM_FileOpen");
  if(((error = dlerror()) != NULL) || (DrmFileApi.drm_open == NULL))goto init_failed;
  DrmFileApi.drm_read  =  dlsym(DrmFileApi.dlHandle,"API_DRM_FileRead");
  if(((error = dlerror()) != NULL) || (DrmFileApi.drm_read == NULL))goto init_failed;
  DrmFileApi.drm_lseek  =  dlsym(DrmFileApi.dlHandle,"API_DRM_FileLseek");
  if(((error = dlerror()) != NULL) || (DrmFileApi.drm_lseek == NULL))goto init_failed;
  DrmFileApi.drm_fstat  =  dlsym(DrmFileApi.dlHandle,"API_DRM_FileFstat");
  if(((error = dlerror()) != NULL) || (DrmFileApi.drm_fstat == NULL))goto init_failed;
  DrmFileApi.drm_close  =  dlsym(DrmFileApi.dlHandle,"API_DRM_FileClose");
  if(((error = dlerror()) != NULL) || (DrmFileApi.drm_close == NULL))goto init_failed;
  DrmFileApi.drm_getFileSize  =  dlsym(DrmFileApi.dlHandle,"API_DRM_GetFileSize");
  if(((error = dlerror()) != NULL) || (DrmFileApi.drm_getFileSize == NULL))goto init_failed;


  if(src->drmtype == INKA_DRM ){
     GST_INFO_OBJECT (src, "DRM Component Load");
     DrmFileApi.drmHandle = DrmFileApi.drm_load("inka","","");
   }
  if(DrmFileApi.drmHandle == NULL)
    goto init_failed;

#endif
  src->filename = NULL;
  src->fd = 0;
  src->uri = NULL;

  src->is_regular = FALSE;


  gst_base_src_set_blocksize (GST_BASE_SRC (src), DEFAULT_BLOCKSIZE);
  return;

init_failed:
  GST_LOG ("DRM FILE SRC INIT FAIL");

}

static void
gst_drm_file_src_finalize (GObject * object)
{
  GstDrmFileSrc *src;

  src = GST_DRM_FILE_SRC (object);

  g_free (src->filename);
  g_free (src->uri);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_drm_file_src_set_location (GstDrmFileSrc * src, const gchar * location)
{
  GstState state;

  /* the element must be stopped in order to do this */
  GST_OBJECT_LOCK (src);
  state = GST_STATE (src);
  if (state != GST_STATE_READY && state != GST_STATE_NULL)
    goto wrong_state;
  GST_OBJECT_UNLOCK (src);

  g_free (src->filename);
  g_free (src->uri);

  /* clear the filename if we get a NULL (is that possible?) */
  if (location == NULL) {
    src->filename = NULL;
    src->uri = NULL;
  } else {
    /* we store the filename as received by the application. On Windows this
     * should be UTF8 */
    src->filename = g_strdup (location+11);
    src->uri = g_strdup (location);
    GST_INFO ("filename : %s", src->filename);
    GST_INFO ("uri      : %s", src->uri);
  }
  g_object_notify (G_OBJECT (src), "location");
  /* FIXME 0.11: notify "uri" property once there is one */

  return TRUE;

  /* ERROR */
wrong_state:
  {
    g_warning ("Changing the `location' property on drmfilesrc when a file is "
        "open is not supported.");
    GST_OBJECT_UNLOCK (src);
    return FALSE;
  }
}

static void
gst_drm_file_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDrmFileSrc *src;

  g_return_if_fail (GST_IS_DRM_FILE_SRC (object));

  src = GST_DRM_FILE_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      gst_drm_file_src_set_location (src, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_drm_file_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDrmFileSrc *src;

  g_return_if_fail (GST_IS_DRM_FILE_SRC (object));

  src = GST_DRM_FILE_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, src->filename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/***
 * read code below
 * that is to say, you shouldn't read the code below, but the code that reads
 * stuff is below.  Well, you shouldn't not read the code below, feel free
 * to read it of course.  It's just that "read code below" is a pretty crappy
 * documentation string because it sounds like we're expecting you to read
 * the code to understand what it does, which, while true, is really not
 * the sort of attitude we want to be advertising.  No sir.
 *
 */
static GstFlowReturn
gst_drm_file_src_fill (GstBaseSrc * basesrc, guint64 offset, guint length,
    GstBuffer * buf)
{
  GstDrmFileSrc *src;
  guint to_read, bytes_read;
  int ret;
  GstMapInfo info;
  guint8 *data;

  src = GST_DRM_FILE_SRC_CAST (basesrc);

  if (G_UNLIKELY (src->read_position != offset)) {
    off_t res;
#ifdef DRM_INCLUDE
    res = DrmFileApi.drm_lseek(src->fd, offset, SEEK_SET);
#else
    res = lseek (src->fd, offset, SEEK_SET);
#endif

    if (G_UNLIKELY (res < 0 || res != offset))
      goto seek_failed;

    src->read_position = offset;
  }

  gst_buffer_map (buf, &info, GST_MAP_WRITE);
  data = info.data;

  bytes_read = 0;
  to_read = length;
  while (to_read > 0) {
    GST_LOG_OBJECT (src, "Reading %d bytes at offset 0x%" G_GINT64_MODIFIER "x",
        to_read, offset + bytes_read);
    errno = 0;
#ifdef DRM_INCLUDE
    ret = DrmFileApi.drm_read (src->fd, data + bytes_read, to_read);
#else
    ret = read (src->fd, data + bytes_read, to_read);
#endif
    if (G_UNLIKELY (ret < 0)) {
      if (errno == EAGAIN || errno == EINTR)
        continue;
      goto could_not_read;
    }

    /* files should eos if they read 0 and more was requested */
    if (G_UNLIKELY (ret == 0)) {
      /* .. but first we should return any remaining data */
      if (bytes_read > 0)
        break;
      goto eos;
    }

    to_read -= ret;
    bytes_read += ret;

    src->read_position += ret;
  }

  gst_buffer_unmap (buf, &info);
  if (bytes_read != length)
    gst_buffer_resize (buf, 0, bytes_read);

  GST_BUFFER_OFFSET (buf) = offset;
  GST_BUFFER_OFFSET_END (buf) = offset + bytes_read;

  return GST_FLOW_OK;

  /* ERROR */
seek_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return GST_FLOW_ERROR;
  }
could_not_read:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    gst_buffer_unmap (buf, &info);
    gst_buffer_resize (buf, 0, 0);
    return GST_FLOW_ERROR;
  }
eos:
  {
    GST_DEBUG ("EOS");
    gst_buffer_unmap (buf, &info);
    gst_buffer_resize (buf, 0, 0);
    return GST_FLOW_EOS;
  }
}

static gboolean
gst_drm_file_src_is_seekable (GstBaseSrc * basesrc)
{
  GstDrmFileSrc *src = GST_DRM_FILE_SRC (basesrc);

  return src->seekable;
}

static gboolean
gst_drm_file_src_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  struct stat stat_results;
  GstDrmFileSrc *src;
#ifdef DRM_INCLUDE
  guint drmFileSize;
#endif
  src = GST_DRM_FILE_SRC (basesrc);

  if (!src->seekable) {
    /* If it isn't seekable, we won't know the length (but fstat will still
     * succeed, and wrongly say our length is zero. */
    return FALSE;
  }
#ifdef DRM_INCLUDE
  if (DrmFileApi.drm_getFileSize(src->fd, &drmFileSize ) != 0)
#else
  if (fstat (src->fd, &stat_results) < 0)
 #endif
    goto could_not_stat;


#ifdef DRM_INCLUDE
  *size = drmFileSize;
#else
  *size = stat_results.st_size;
 #endif
  return TRUE;

  /* ERROR */
could_not_stat:
  {
    return FALSE;
  }
}

/* open the file, necessary to go to READY state */
static gboolean
gst_drm_file_src_start (const GstBaseSrc * const basesrc)
{
  GstDrmFileSrc *src = GST_DRM_FILE_SRC (basesrc);
  struct stat stat_results;

  if (src->filename == NULL || src->filename[0] == '\0')
    goto no_filename;
  GST_INFO_OBJECT (src, "opening file %s", src->filename);

#ifdef DRM_INCLUDE
  if(src->drmtype == INKA_DRM ){
    GST_INFO_OBJECT (src, "DRM Component Load");
    DrmFileApi.drmHandle = DrmFileApi.drm_load("inka","","");
  }
  else
    goto load_failed;
#endif

  /* open the file */
  src->fd = gst_open (src->filename, O_RDONLY | O_BINARY, 0);

  if (src->fd < 0)
    goto open_failed;

  /* check if it is a regular file, otherwise bail out */

#ifdef DRM_INCLUDE
  if (DrmFileApi.drm_fstat (src->fd,(void *)&stat_results) < 0)
#else
  if (fstat (src->fd, &stat_results) < 0)
#endif
    goto no_stat;

  if (S_ISDIR (stat_results.st_mode))
    goto was_directory;

  if (S_ISSOCK (stat_results.st_mode))
    goto was_socket;

  src->read_position = 0;

  /* record if it's a regular (hence seekable and lengthable) file */
  if (S_ISREG (stat_results.st_mode))
    src->is_regular = TRUE;

 /* We need to check if the underlying file is seekable. */

  {
#ifdef DRM_INCLUDE
    off_t res  = DrmFileApi.drm_lseek(src->fd, 0, SEEK_END);
#else
    off_t res = lseek (src->fd, 0, SEEK_END);
#endif
    if (res < 0) {
      GST_LOG_OBJECT (src, "disabling seeking, lseek failed: %s",
          g_strerror (errno));
      src->seekable = FALSE;
    } else {
      src->seekable = TRUE;
    }
#ifdef DRM_INCLUDE
    DrmFileApi.drm_lseek (src->fd, 0, SEEK_SET);
#else
    lseek (src->fd, 0, SEEK_SET);
#endif
  }

  /* We can only really do seeking on regular files - for other file types, we
   * don't know their length, so seeking isn't useful/meaningful */
  src->seekable = src->seekable && src->is_regular;
  gst_base_src_set_dynamic_size (basesrc, src->seekable);

  return TRUE;


  /* ERROR */
no_filename:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("No file name specified for reading."), (NULL));
    goto error_exit;
  }
open_failed:
  {
    switch (errno) {
      case ENOENT:
        GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
            ("No such file \"%s\"", src->filename));
        break;
      default:
        GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
            ("Could not open file \"%s\" for reading.", src->filename),
            GST_ERROR_SYSTEM);
        break;
    }
    if( DrmFileApi.drmHandle != NULL ){
      DrmFileApi.drm_release(DrmFileApi.drmHandle);
      DrmFileApi.drmHandle = NULL;
    }
    goto error_exit;
  }
no_stat:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Could not get info on \"%s\".", src->filename), (NULL));
    goto error_close;
  }
was_directory:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("\"%s\" is a directory.", src->filename), (NULL));
    goto error_close;
  }
was_socket:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("File \"%s\" is a socket.", src->filename), (NULL));
    goto error_close;
  }
load_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("drm component load fail. File:\"%s\" ", src->filename), (NULL));
    goto error_close;
  }
error_close:
#ifdef DRM_INCLUDE
  DrmFileApi.drm_close (src->fd);
  if( DrmFileApi.drmHandle != NULL ){
    DrmFileApi.drm_release(DrmFileApi.drmHandle);
    DrmFileApi.drmHandle = NULL;
  }
#else
  close (src->fd);
#endif
error_exit:
  return FALSE;
}

/* unmap and close the file */
static gboolean
gst_drm_file_src_stop (GstBaseSrc * basesrc)
{
  GstDrmFileSrc *src = GST_DRM_FILE_SRC (basesrc);

  /* close the file */
#ifdef DRM_INCLUDE
  DrmFileApi.drm_close (src->fd);
  if( DrmFileApi.drmHandle != NULL ){
    DrmFileApi.drm_release(DrmFileApi.drmHandle);
    DrmFileApi.drmHandle = NULL;
  }
#else
  close (src->fd);
#endif
  /* zero out a lot of our state */
  src->fd = 0;
  src->is_regular = FALSE;

  return TRUE;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_drm_file_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_drm_file_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "inkafile", NULL };

  return protocols;
}

static gchar *
gst_drm_file_src_uri_get_uri (GstURIHandler * handler)
{
  GstDrmFileSrc *src = GST_DRM_FILE_SRC (handler);

  /* FIXME: make thread-safe */
  return g_strdup (src->uri);
}

static gboolean
gst_drm_file_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** err)
{
  gboolean ret = FALSE;
  GstDrmFileSrc *src = GST_DRM_FILE_SRC (handler);
  src->drmtype = NONE_DRM;

  if(strlen (uri) <= 11)
    goto beach;

  if (strncmp (uri, "inkafile://",11) == 0) {
    /* Special case for "inkafile://" as this is used by some applications
     *  to test with gst_element_make_from_uri if there's an element
     *  that supports the URI protocol. */
    src->drmtype = INKA_DRM;
    gst_drm_file_src_set_location (src, uri);
    return TRUE;
  }

beach:
  GST_WARNING_OBJECT (src, "Invalid URI '%s' for drmfilesrc", uri);
  g_set_error (err, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
      "File URI'%s' is invalid for drm ", uri);
  return FALSE;
}

static void
gst_drm_file_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_drm_file_src_uri_get_type;
  iface->get_protocols = gst_drm_file_src_uri_get_protocols;
  iface->get_uri = gst_drm_file_src_uri_get_uri;
  iface->set_uri = gst_drm_file_src_uri_set_uri;
}
