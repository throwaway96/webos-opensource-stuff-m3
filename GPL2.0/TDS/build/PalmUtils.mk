##
#  PalmUtils.mk
#
#  Some handy utilities for use when building Makefiles.  These
#  could be used in almost any Makefile.
#
#  In general, utilities that are here should be useful in _any_
#  Makefile.  They shouldn't depend on any specific build
#  environment, shouldn't know about our special tools, etc.
#  You should be able to pick this Makefile up and use it anywhere.
#
#  PLEASE DON'T ADD TOO MUCH HERE.  REMEMBER: keep complexity low!
#
#  What's here:
#  * Definitions of some tools, just so you don't need to put the
#    bare name in your makefiles.
#    - $(AWK)
#    - $(GREP)
#    - $(SED)
#  * $(FILTERED_MAKEFILE_LIST) - A version of "MAKEFILE_LIST" that
#    doesn't contain any makefiles with ".d" in them.  This is
#    useful for making things depend on any non-automatically
#    generated makefiles.
#  * SHOW_BANNER macro - See the macro itself for details, but
#    is nice for prettifying build output.
#  * $(HUSH) - Defined to either be "@" or "" depending on the
#    value of the "VERBOSE" variable
#  * showvar_% rule - Handy rule for debugging.  Doing a
#    'make showvar_xyzzy' will show you the value of the variable
#    xyzzy.
#  * A template for defining a rule to create a symlink
#    automatically by just depending on it: DirSymlinkRuleTemplate
#  * The SubDirs and SubFiles variables, which can be used to
#    get a list of directories and/or files under a directory.
#  * .shell rule - Making this target will start a shell inside
#    of the make environment.  Also useful for debugging.
#

#################################################################
# Utilities
#
# These are just general make tricks, macros, etc.  These should
# probably be abstracted to a common makefile.
#################################################################

# Prevent recursion
ifndef __PALMUTIL_MK__
__PALMUTIL_MK__ := 1

##
# Yell if someone's trying to build make 3.80 because of issues
# it has with 'eval'.  You'll get errors like "Virtual memory
# exhausted", which is make bug 1517

ifeq ("$(MAKE_VERSION)", "3.80")
  $(error Version 3.80 version of Make is unsupported.  Update your tools.)
endif

##
# Names for common tools...
AWK  := awk
GREP := grep
SED  := sed

##
# Filter out the list of makefiles that have been run.
#
# ...we take out all ".d" makefiles, since those are dependency
# makefiles and are dynamically generated.
#
# Note: we must use = not := here so we really get all the
# Makefiles, not just the ones that showed up till now.
FILTERED_MAKEFILE_LIST = $(filter-out %.d, $(MAKEFILE_LIST))

##
# SHOW_BANNER macro...
#
# When makefiles have their own rules, this macro should be used
# to display a one-line description of what's being executed.
#
# The makefile should define a 'MakefileName' variable, which may be
# the same as the project name:
#	MakefileName = $(projectName)
#
# Each rule in the makefile should begin with a command that sets
# one or more shell variables then invokes the $(SHOW_BANNER) macro.
#
#	bannerMakefile	- description of which makefile holds the rule
#			  (defaults to value of $(MakefileName), so rarely
#			   used except by the common makefiles which can't
#			   use $(MakefileName))
#
#	bannerCommand	- name of the command being executed
#			  (or an abbreviation) [default = $?]
#
#	bannerTarget	- the source or result of the file being processed
#			  (defaults to the filename only of the first dependency)
#
# example:
#	myfile.o: mysource.c
#		bannerCommand=cc; $(SHOW_BANNER)
#		$(CC) $(CFLAGS) -o $@ $<

# define SHOW_BANNER
#     printf "[%-12.12s] %-12.12s: %s\n" $${bannerMakefile:-$(MakefileName)} $${bannerCommand:-?} $${bannerTarget:-$(<F)}
# endef

##
# The "HUSH" variable, which makes it easy to be more/less verbose.
# Do a 'make VERBOSE=1' to get verbose output...  See below for usage.
# ifeq ("$(VERBOSE)", "1")
#     HUSH :=
# else
#     HUSH := @
# endif

##
# The "showvar" utility.  If you are debugging the make system,
# you might want to use this to print out the value of a variable.
# Do a 'make showvar_HUSH' to show the value of $HUSH
showvar_% showVar_% :
	@echo '$* = "$($*)"'

##
# Runs a bash shell for you.  Why is this useful?  It ends up
# making things a little easier to debug in the WindRiver build
# system.  If you can convince the WindRiver build system to make
# this target (putting a call to it in the rule for "config" works),
# then you can get into an environment where the WindRiver environment
# (including the PATH) is setup properly.
#
# ...this starts with a "." so make won't take it as a default target.
#
# I tend to invoke this with the following, given the ways I've setup
# the "dist" makefiles:
#   cd BUILD/dist
#   rm .stamp/Project.*
#   make Project.configure CONFIG_SHELL=1
.PHONY: .shell
.shell:
	@if [ "$$IN_MAKE_DEBUG" == "1" ] ; then \
		echo "Already in a bash shell for you."; \
	else \
		echo "Running a bash shell for you."; \
		IN_MAKE_DEBUG=1 bash; \
	fi;


##
# This template can be used to create a rule to make sure a symlink to a
# directory exists.
#
# If you wanted to create a symlink called "$(INCLUDE_DIR)/$(COMPONENT)" and
# wanted it to point to the directory "$(INCS_DIR)", you'd use it like:
#   $(eval $(call DirSymlinkRuleTemplate, $(INCLUDE_DIR)/$(COMPONENT), $(INCS_DIR)))
#
# ...the rules will create the symlink's parent directory if needed.
# ...the rules will use 'readlink' to make the destination of the link absolute.
#
#
# This will also create a file called "<symlinkName>.timestamp", which
# tells when the symlink was created.  We use this so that we know if the
# Makefile ever changes, we should recreate the symlink.
#
#
# Gory details:
#   There are some tricks to update the symlink if any makefile ever changes.
#   Make considers the date of the symlink to be the date of the destination
#   directory, which is updated whenever you add a file.  To reliably update
#   the symlink, we add an extra timestamp file.  Here are scenarios:
#   - Files added to directory (directory file date changes): no need to
#     rebuild.  $(1) and $(2) will show up as newer timestamps.
#   - Makefile touched: will rebuild the .timestamp, then re-create the link.  We
#     "touch" the Incs dir to tell make that we don't need to link again.
#   - Makefile touched, then include files added: in this case, the "Incs"
#     dir will be _newer_ than the makefile.  However, we'll still rebuild it
#     because the .timestamp file will be older than the Makefile and the link
#     depends on the .timestamp file.

define DirSymlinkRuleTemplate
$(strip $(1).timestamp): $$(FILTERED_MAKEFILE_LIST)
	$(HUSH) mkdir -p $$(@D)
	$(HUSH) touch $$@

$(1): $(strip $(1)).timestamp
	@bannerMakefile=PalmUtils.mk; bannerCommand=symlink; bannerTarget=$(strip $(2)); $$(SHOW_BANNER)
	$(HUSH) rm -rf $$@
	$(HUSH) ln -s `readlink -f $(2)` $$@
	$(HUSH) if [ -f $(2) ]; then \
	  echo "ERROR:" `readlink -f $(2)` "exists, but isn't a directory."; \
	  echo "       It may have been created by mistake.  If so, delete it."; \
	fi
	$(HUSH) mkdir -p $(2)
	$(HUSH) touch $(2)
endef


##
# You can use these variables to easily get a list of all subdirectories or
# subfiles from a given directory.  So, if you've got the following tree:
#   /tmp/foo/one.txt
#   /tmp/foo/two.txt
#   /tmp/bar/three.txt
#
# ...and you did:
#   $(call SubDirs, /tmp)    ==> /tmp/foo and /tmp/bar
#   $(call SubFiles, /tmp)   ==> /tmp/foo/one.txt, /tmp/foo/two.txt, /tmp/bar/three.txt
#
# This uses the "find" utility, passing -L (so symlinks are followed!)
# Note, we want to omit hidden directories such as ".svn"

SubDirs  = $(shell if [ -e $(1) ]; then find -L $(1) -type d -not -wholename '*/.*' | sort; fi)
SubFiles = $(shell if [ -e $(1) ]; then find -L $(1) -type f | sort; fi)

endif # ifndef __PALMUTIL_MK__
