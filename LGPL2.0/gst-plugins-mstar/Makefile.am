DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc
SUBDIRS = gst m4 common

DIST_SUBDIRS = gst m4 common

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
