##
#  SupportForUserLocalDevRules.mk
#
#  Rules for support user local development makefile
#  In such makefile, developer can override some definition for easier
#  debugging
#
#  This file is included by LinuxBuildEngine.mk
#
#  To work simply create a file in your own component directory
#  Build/UserLocalDevRules.mk
#
#  content example:
#  COMPONENT_CFLAGS += -O0
#

ifneq ($(strip $(wildcard Build/UserLocalDevRules.mk)),)
    # -include Build/UserLocalDevRules.mk could have been used instead of the
    # check, however the goal here is to generate a warning
    include Build/UserLocalDevRules.mk
    $(warning Build/UserLocalDevRules.mk used. Make sure you don't submit such file and that the build works without it. This is for development/debugging only)
endif
