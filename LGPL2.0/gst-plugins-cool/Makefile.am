DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc
SUBDIRS = pkgconfig gst-libs gst m4 common tests config

DIST_SUBDIRS = pkgconfig gst-libs gst m4 common tests config

include $(top_srcdir)/common/win32.mak

EXTRA_DIST = autogen.sh

DISTCLEANFILES = _stdint.h

ACLOCAL_AMFLAGS = -I m4 -I common/m4

check-valgrind:
	cd tests/check && make check-valgrind

check: check-exports

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

include $(top_srcdir)/common/coverage/lcov.mak
