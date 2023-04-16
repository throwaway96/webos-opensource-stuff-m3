##
#  LinuxBuildEngine.mk
#
#  This file is intended to be included by the Makefiles for building
#  linux executable and shared library to use inside and outside of PARTS.
#  It might not fit the need yet for building every linux
#  Linux-based applications.  It is __NOT__ for use in building
#  kernel modules.
#
#  Check CommonBuildRules.mk for more information
#
#  This system supports various "BUILD MODELS".  See below for variables
#  relating to each different BUILD MODEL.
#
#  Generally required to be set by the including makefile:
#   * BUILD_MODEL - Which type of build this is.  See below.
#                   Example is 'BUILD_MODEL := ElfModelModule'
#
#  For 'BUILD_MODEL = LinuxSharedLibrary': (build a .so shared library)
#   * <nothing extra>
#
#  For 'BUILD_MODEL = LinuxExe': (build a linux executable)
#   * <nothing extra>
#
#  History:
#    1.0 <AlexRoux> first revision based on CommonBuildRules.mk

##
# Include common utilities, which defines things like HUSH,
# SHOW_BANNER, and FILTERED_MAKEFILE_LIST.
include $(MAKEFILES_DIR)/PalmUtils.mk

# Add support for local developer makefile to override makefile rules
include $(MAKEFILES_DIR)/SupportForUserLocalDevRules.mk

# We don't have standard header here
BUILD_MODEL_HEADER_DIRS    :=
# Nor do We have standard link flags to add
BUILD_MODEL_LINKFLAGS      :=
# No additional build result
BUILD_MODEL_RESULTS        :=
# No additional build script
BUILD_MODEL_INSTALL_SCRIPT :=

##
# The two build models PartsElfModelModule and PartsElfLibrary
# are pretty much the same, so we set a generic variable that
# we can use to check for either of them.
BUILD_MODEL_IS_SO_FILE := no
BUILD_MODEL_BANNER := LinuxExe
ifeq ("$(BUILD_MODEL)", "LinuxSharedLibrary")
    BUILD_MODEL_IS_SO_FILE := yes
    BUILD_MODEL_BANNER := LinuxShLib
endif

##
# Common include utilities used for Parts and non-Parts component
include $(MAKEFILES_DIR)/CommonBuildRules.mk
