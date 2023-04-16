##
#  CompileRules.mk
#
#  Set of rules for compiling assembly, C, and C++ source
#  code.  These rules are really very similar to the rules that are
#  present in make by default...
#
#
#  IMPORTANT:
#    * The including engine must include PalmUtils.mk manually.  This    *
#    * makefile makes use of things in PalmUtils.mk, but doesn't include *
#    * it (since it should be included at the top of the main engine).   *
#
#
#  The differences between this and the default CC/AS rules are:
#  * Uses $(SHOW_BANNER) and $(HUSH) to make the build pretty
#  * Generates ".d" dependency files properly in a way that is happy
#    with our build system (using -MMD, -MP, and -MF).
#  * Uses the C compiler to compile assembly (!), passing CFLAGS
#  * Supports both .s and .S for assembly.
#
#  Things that must be defined by the including makefile (just like
#  for the standard Make rules):
#  * CC       - The C compiler
#  * CXX      - The C++ compiler
#  * CFLAGS   - Flags to pass to the C compiler.
#  * CXXFLAGS - Flags to pass to the C++ compiler.

##
# Generic 'cc' and 'as' rules.
#
# CC and AS are identical other than the banner (can we combine in some way?).
# We actually use gcc to compile our assembler files.  I forgot why, but I
# think there was a good reason at some point.  It certainly doesn't
# seem to hurt...
#
# We generate dependencies using a scheme described on a nice web page
# I found at <http://make.paulandlesley.org/autodep.html> titled
# "Advanced Auto-Dependency Generation".  Changes that I do over the web
# page:
# - I place things in .d files, since it's more traditional than .P files.
# - I use -MP instead of all the sed-ing.  It looks like gcc has caught up
#   to what it needs to do so we don't need the extra processing.
# - I use -MMD, not -MD.  It seems safe to not generate dependencies on
#   system stuff, I think.  We can revisit if needed.
#
# ...in other places (like the .xrd area), we have to do things manually.
# In those places, I use a bunch of temp files so that I can create the ".d"
# file atomically (no partially created .d files).  I assume that's not needed
# here.
#
# TODO: Standardize on ".s" or ".S".  gcc seems to do slightly different
# things with them (and likes ".s" better?), at least it generates different
# ".d" files.
#
# Copyright 2010 Palm, Inc. All rights reserved.
#

%.o: %.c
	@bannerMakefile=CompileRules.mk; bannerCommand=CC; $(SHOW_BANNER)
	$(HUSH) $(CC) -MMD -MP -MF $*.d $(CFLAGS) -c $<

%.o: %.cpp
	@bannerMakefile=$(BUILD_MODEL_BANNER); bannerCommand=CXX; $(SHOW_BANNER)
	$(HUSH) $(CXX) -MMD -MP -MF $*.d $(CXXFLAGS) -c $<

%.o: %.s
	@bannerMakefile=CompileRules.mk; bannerCommand=AS; $(SHOW_BANNER)
	$(HUSH) $(CC) -MMD -MP -MF $*.d $(CFLAGS) -c $<

%.o: %.S
	@bannerMakefile=CompileRules.mk; bannerCommand=AS; $(SHOW_BANNER)
	$(HUSH) $(CC) -MMD -MP -MF $*.d $(CFLAGS) -c $<
