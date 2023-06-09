dnl required version of autoconf
AC_PREREQ([2.62])

dnl TODO: fill in your package name and package version here
AC_INIT(Gstreamer Cool Plug-ins,1.0.0,
https://github.com/altura/gst-plugins-cool,
gst-plugins-cool)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.11 no-dist-gzip dist-xz tar-ustar])

dnl *** required versions of GStreamer stuff ***
GST_REQ=1.2.0
GSTPB_REQ=1.2.0

dnl required versions of gstreamer and plugins-base
GST_REQUIRED=1.0.0
GSTPB_REQUIRED=1.0.0

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/decproxy/gstdecproxy2.c])

dnl define the output header for config
AC_CONFIG_HEADERS([config.h])

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl sets host_* variables
AC_CANONICAL_HOST

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])

dnl GStreamer versioning, this is mostly informational
GST_VERSION_MAJOR=$PACKAGE_VERSION_MAJOR
GST_VERSION_MINOR=$PACKAGE_VERSION_MINOR
GST_VERSION_MICRO=$PACKAGE_VERSION_MICRO
GST_VERSION_NANO=$PACKAGE_VERSION_NANO
AC_SUBST(GST_VERSION_MAJOR)
AC_SUBST(GST_VERSION_MINOR)
AC_SUBST(GST_VERSION_MICRO)
AC_SUBST(GST_VERSION_NANO)

dnl our libraries and install dirs use GST_API_VERSION in the filename
dnl to allow side-by-side installation of different API versions
GST_API_VERSION=1.0
AC_SUBST(GST_API_VERSION)
AC_DEFINE_UNQUOTED(GST_API_VERSION, "$GST_API_VERSION",
  [GStreamer API Version])

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl sets GST_LT_LDFLAGS
AS_LIBTOOL(GST, 9, 0, 9)

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal (keep in sync with Makefile.am)
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

AG_GST_ARG_DEBUG
AG_GST_ARG_PROFILING
AG_GST_ARG_VALGRIND
AG_GST_ARG_GCOV

AG_GST_ARG_EXAMPLES

AG_GST_ARG_WITH_PKG_CONFIG_PATH
AG_GST_ARG_WITH_PACKAGE_NAME
AG_GST_ARG_WITH_PACKAGE_ORIGIN
AG_GST_ARG_WITH_PLUGINS

AG_GST_PKG_CONFIG_PATH

dnl Check for a way to display the function name in debug output
AG_GST_CHECK_FUNCTION

dnl check for tools (compiler etc.)
AC_PROG_CC

dnl required version of libtool
LT_PREREQ([2.2.6])
LT_INIT

dnl give error and exit if we don't have pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
AC_MSG_ERROR([You need to have pkg-config installed!])
])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_BASE($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_CHECK($GST_API_VERSION, [$GST_REQ], no)
AG_GST_CHECK_GST_CONTROLLER($GST_API_VERSION, [$GST_REQ], yes)
AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

AG_GST_CHECK_GST_PLUGINS_BASE($GST_API_VERSION, [$GSTPB_REQ], yes)

dnl check for uninstalled plugin directories for unit tests
AG_GST_CHECK_UNINSTALLED_SETUP([
  AG_GST_CHECK_GST_PLUGINS_GOOD($GST_API_VERSION, [0.11.0])
])

AG_GST_CHECK_PLUGIN(decproxy)
AG_GST_CHECK_PLUGIN(httpextbin)
AG_GST_CHECK_PLUGIN(dynappsrc)
AG_GST_CHECK_PLUGIN(streamiddemux)
AG_GST_CHECK_PLUGIN(tsinkbin)
AG_GST_CHECK_PLUGIN(textbin)
AG_GST_CHECK_PLUGIN(fakedec)

dnl check for plugins to enable or disable
AG_GST_CHECK_PLUGIN_WITH_DEFAULT_FEATURE(reformatter, no)

dnl check for which audio format is used for puppet
AC_ARG_WITH(puppet-audio-format,
  AC_HELP_STRING([--with-puppet-audio-format], [Which audio format is used for puppet, S16LE/S32LE, (default is S32LE)]),
  [
    case "${withval}" in
      yes) AC_MSG_ERROR(bad value ${withval} for --with-puppet-audio-format) ;;
      no)  AC_MSG_ERROR(bad value ${withval} for --with-puppet-audio-format) ;;
      *)   DEFAULT_PUPPET_AUDIO_FORMAT="${withval}" ;;
    esac
  ],
  [
    DEFAULT_PUPPET_AUDIO_FORMAT="S32LE"
  ] dnl Default value
)
AC_MSG_NOTICE(Using $DEFAULT_PUPPET_AUDIO_FORMAT as default audio format for puppet)
AC_SUBST(DEFAULT_PUPPET_AUDIO_FORMAT)
AC_DEFINE_UNQUOTED(DEFAULT_PUPPET_AUDIO_FORMAT, "$DEFAULT_PUPPET_AUDIO_FORMAT",
  [Default audio format for puppet])


dnl check for documentation tools
GTK_DOC_CHECK([1.12])

dnl GTK is optional and only used in examples
HAVE_GTK=no
HAVE_GTK_X11=no

AM_CONDITIONAL(HAVE_GTK, test "x$HAVE_GTK" = "xyes")
AM_CONDITIONAL(HAVE_GTK_X11, test "x$HAVE_GTK_X11" = "xyes")

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl Check for the required version of GStreamer core (and gst-plugins-base)
dnl This will export GST_CFLAGS and GST_LIBS variables for use in Makefile.am
dnl
dnl If you need libraries from gst-plugins-base here, also add:
dnl for libgstaudio-1.0: gstreamer-audio-1.0 >= $GST_REQUIRED
dnl for libgstvideo-1.0: gstreamer-video-1.0 >= $GST_REQUIRED
dnl for libgsttag-1.0: gstreamer-tag-1.0 >= $GST_REQUIRED
dnl for libgstpbutils-1.0: gstreamer-pbutils-1.0 >= $GST_REQUIRED
dnl for libgstfft-1.0: gstreamer-fft-1.0 >= $GST_REQUIRED
dnl for libgstinterfaces-1.0: gstreamer-interfaces-1.0 >= $GST_REQUIRED
dnl for libgstrtp-1.0: gstreamer-rtp-1.0 >= $GST_REQUIRED
dnl for libgstrtsp-1.0: gstreamer-rtsp-1.0 >= $GST_REQUIRED
dnl etc.
PKG_CHECK_MODULES(GST, [
gstreamer-1.0 >= $GST_REQUIRED
gstreamer-base-1.0 >= $GST_REQUIRED
], [
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)
], [
AC_MSG_ERROR([
You need to install or upgrade the GStreamer development
packages on your system. On debian-based systems these are
libgstreamer1.0-dev and libgstreamer-plugins-base1.0-dev.
on RPM-based systems gstreamer1.0-devel, libgstreamer1.0-devel
or similar. The minimum version required is $GST_REQUIRED.
])
])

dnl check if compiler understands -Wall (if yes, add -Wall to GST_CFLAGS)
AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
GST_CFLAGS="$GST_CFLAGS -Wall"
AC_MSG_RESULT([yes])
], [
AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
plugindir="$HOME/.gstreamer-1.0/plugins"
else
plugindir="\$(libdir)/gstreamer-1.0"
fi
AC_SUBST(plugindir)

dnl every flag in GST_OPTION_CFLAGS, GST_OPTION_CXXFLAGS and GST_OPTION_OBJCFLAGS can be overridden
dnl at make time with e.g. make ERROR_CFLAGS=""
GST_OPTION_CFLAGS="\$(WARNING_CFLAGS) \$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
GST_OPTION_CXXFLAGS="\$(WARNING_CXXFLAGS) \$(ERROR_CXXFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
GST_OPTION_OBJCFLAGS="\$(WARNING_OBJCFLAGS) \$(ERROR_OBJCFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)
AC_SUBST(GST_OPTION_CXXFLAGS)
AC_SUBST(GST_OPTION_OBJCFLAGS)

dnl our libraries need to be versioned correctly
AC_SUBST(GST_LT_LDFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
dnl add ERROR_CFLAGS, but overridable
GST_CFLAGS="$GST_CFLAGS -DGST_USE_UNSTABLE_API"
GST_CXXFLAGS="-I\$(top_srcdir)/gst-libs $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CXXFLAGS)"
GST_OBJCFLAGS="-I\$(top_srcdir)/gst-libs $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_OBJCFLAGS)"
GST_CFLAGS="-I\$(top_srcdir)/gst-libs $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_CXXFLAGS)
AC_SUBST(GST_OBJCFLAGS)
AC_SUBST(GST_LIBS)

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc.*' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

AC_ARG_WITH([tmpdir],
  AS_HELP_STRING([--with-tmpdir],[tmp dir]),
  [
    TMPDIR="${withval}"
  ], [
    TMPDIR="/var/tmp"
  ]
)
AC_SUBST(TMPDIR)

AC_MSG_NOTICE([Using ${MACHINE} as a target])
case "${MACHINE}" in
  h15|k2h|k2l|lm15u|m14tv|mtka5lr|rtks2b|m16|m2)
    ;;
  none|*)
    MACHINE=default
    ;;
esac

MACHINE_CONF_DATA="${MACHINE}/gstcool.conf"
AC_SUBST(MACHINE_CONF_DATA)
AC_MSG_NOTICE([machine conf data : ${MACHINE_CONF_DATA}])

AC_CONFIG_FILES(
Makefile
common/Makefile
common/m4/Makefile
config/Makefile
gst/Makefile
gst/decproxy/Makefile
gst/fakedec/Makefile
gst/httpextbin/Makefile
gst/dynappsrc/Makefile
gst/streamiddemux/Makefile
gst/textbin/Makefile
gst/reformatter/Makefile
gst/tsinkbin/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/cool/Makefile
pkgconfig/Makefile
pkgconfig/gstreamer-cool.pc
pkgconfig/gstreamer-cool-uninstalled.pc
tests/Makefile
tests/check/Makefile
tests/examples/Makefile
tests/examples/rm/Makefile
tests/examples/mediainfo/Makefile
tests/examples/textbin/Makefile
tests/examples/dynappsrc/Makefile
m4/Makefile
scripts/registry/gst-registry.conf
scripts/registry/gst-env.conf
)
AC_OUTPUT
