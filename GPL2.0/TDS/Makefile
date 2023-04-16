##
#  Component TelephonyDataService
#
#  Makefile using LinuxBuildEngine.mk
##

##
# COMPONENT is the name of your component.

COMPONENT            := TelephonyDataService

DEPENDS_ON_PUBLIC    := PmLogLib
DEPENDS_ON_PROTECTED := PmWirelessSystemFramework

INCLUDE_DIRS         := ${INC}
WORK_DIRS            := ./
EXTRA_HEADER_DIRS    := ${INCLUDE_DIRS}/glib-2.0

BUILD_MODEL          := LinuxExe
LD := "${LD}"

VPATH= \
	Src/ \
	Src/gdbus \

# Object files
OBJS_GDBUS := \
		mainloop.o \
		object.o \
		watch.o \
		polkit.o \

OBJS_IFDBUS := \
		dbus.o \
		manager.o \
		gprs.o \
		modem.o \

OBJS :=	$(OBJS_GDBUS) $(OBJS_IFDBUS)\
		main.o \
		luna_helper.o \
		luna_svc_mgr.o \
		til_handler.o\

COMPONENT_CXXFLAGS := -fvisibility=hidden \
		    -I${WORK_DIRS}/Src/ -I$(WORK_DIRS)/Src/gdbus \
		    $(shell pkg-config --cflags glib-2.0 glibmm-2.4 PmLogLib) \
		    $(shell pkg-config --cflags dbus-1) \

LIB_DIR =${LIB}

COMPONENT_LINKFLAGS := \
	-Wl,--export-dynamic \
	-L$(LIB_DIR) \
	-lstdc++ \
	-ldl \
	$(shell pkg-config --libs glib-2.0 glibmm-2.4 dbus-1 PmLogLib) \
	-llunaservice \
	-lpbnjson_c -lpbnjson_cpp

export MAKEFILES_DIR=build

export INSTALL_DIR=${WORK_DIRS}

include $(MAKEFILES_DIR)/LinuxBuildEngine.mk
