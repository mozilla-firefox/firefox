#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#
# config.mk
#
# Determines the platform and builds the macros needed to load the
# appropriate platform-specific .mk file, then defines all (most?)
# of the generic macros.
#

varize = $(subst -,_,$(subst a,A,$(subst b,B,$(subst c,C,$(subst d,D,$(subst e,E,$(subst f,F,$(subst g,G,$(subst h,H,$(subst i,I,$(subst j,J,$(subst k,K,$(subst l,L,$(subst m,M,$(subst n,N,$(subst o,O,$(subst p,P,$(subst q,Q,$(subst r,R,$(subst s,S,$(subst t,T,$(subst u,U,$(subst v,V,$(subst w,W,$(subst x,X,$(subst y,Y,$(subst z,Z,$1)))))))))))))))))))))))))))

# Define an include-at-most-once flag
ifdef INCLUDED_CONFIG_MK
$(error Do not include config.mk twice!)
endif
INCLUDED_CONFIG_MK = 1

EXIT_ON_ERROR = set -e; # Shell loops continue past errors without this.

ifndef topsrcdir
topsrcdir	= $(DEPTH)
endif

ifndef INCLUDED_AUTOCONF_MK
include $(DEPTH)/config/autoconf.mk
endif

-include $(DEPTH)/.mozconfig.mk

# MDDEPDIR is the subdirectory where dependency files are stored
MDDEPDIR := .deps

ifndef EXTERNALLY_MANAGED_MAKE_FILE
# Import the automatically generated backend file. If this file doesn't exist,
# the backend hasn't been properly configured. We want this to be a fatal
# error, hence not using "-include".
ifndef STANDALONE_MAKEFILE
GLOBAL_DEPS += backend.mk
include backend.mk

# Add e.g. `export:: $(EXPORT_TARGETS)` rules. The *_TARGETS variables are defined
# in backend.mk.
$(foreach tier,$(RUNNABLE_TIERS),$(eval $(if $(filter .,$(DEPTH)),recurse_$(tier):,$(tier)::) $($(call varize,$(tier))_TARGETS)))
endif

endif

space = $(NULL) $(NULL)

# Include defs.mk files that can be found in $(srcdir)/$(DEPTH),
# $(srcdir)/$(DEPTH-1), $(srcdir)/$(DEPTH-2), etc., and $(srcdir)
# where $(DEPTH-1) is one level less of depth, $(DEPTH-2), two, etc.
# i.e. for DEPTH=../../.., DEPTH-1 is ../.. and DEPTH-2 is ..
# These defs.mk files are used to define variables in a directory
# and all its subdirectories, recursively.
__depth := $(subst /, ,$(DEPTH))
ifeq (.,$(__depth))
__depth :=
endif
$(foreach __d,$(__depth) .,$(eval __depth = $(wordlist 2,$(words $(__depth)),$(__depth))$(eval -include $(subst $(space),/,$(strip $(srcdir) $(__depth) defs.mk)))))

# Sanity check some variables
CHECK_VARS := \
 LIBRARY_NAME \
 MODULE \
 DEPTH \
 SHARED_LIBRARY_NAME \
 SONAME \
 STATIC_LIBRARY_NAME \
 $(NULL)

# checks for internal spaces or trailing spaces in the variable
# named by $x
check-variable = $(if $(filter-out 0 1,$(words $($(x))z)),$(error Spaces are not allowed in $(x)))

$(foreach x,$(CHECK_VARS),$(check-variable))

ifndef INCLUDED_FUNCTIONS_MK
include $(MOZILLA_DIR)/config/makefiles/functions.mk
endif

RM = rm -f

# FINAL_TARGET specifies the location into which we copy end-user-shipped
# build products (typelibs, components, chrome). It may already be specified by
# a moz.build file.
#
# If DIST_SUBDIR is set, the files will be shipped to a $(DIST_SUBDIR)
# subdirectory of $(DIST)/bin.
FINAL_TARGET ?= $(DIST)/bin$(DIST_SUBDIR:%=/%)
# Override the stored value for the check to make sure that the variable is not
# redefined in the Makefile.in value.
FINAL_TARGET_FROZEN := '$(FINAL_TARGET)'

MKDIR ?= mkdir
SLEEP ?= sleep
TOUCH ?= touch

# Overloaded by comm builds to refer to $(commtopsrcdir), so that
# `mail` resolves in en-US builds and in repacks.
LOCALE_TOPDIR ?= $(topsrcdir)

TAR_CREATE_FLAGS = -chf

#
# Default command macros; can be overridden in <arch>.mk.
#
CCC = $(CXX)

INCLUDES = \
  -I$(srcdir) \
  -I$(CURDIR) \
  $(LOCAL_INCLUDES) \
  -I$(ABS_DIST)/include \
  $(NULL)

include $(MOZILLA_DIR)/config/static-checking-config.mk

LDFLAGS		= $(COMPUTED_LDFLAGS)

COMPILE_CFLAGS	= $(COMPUTED_CFLAGS) $(COMPUTED_PGO_CFLAGS) $(_DEPEND_CFLAGS)
COMPILE_CXXFLAGS = $(COMPUTED_CXXFLAGS) $(COMPUTED_PGO_CFLAGS) $(_DEPEND_CFLAGS)
COMPILE_CMFLAGS = $(COMPUTED_CMFLAGS)
COMPILE_CMMFLAGS = $(COMPUTED_CMMFLAGS)
ASFLAGS = $(COMPUTED_ASFLAGS)
SFLAGS = $(COMPUTED_SFLAGS)

HOST_CFLAGS = $(COMPUTED_HOST_CFLAGS) $(_HOST_DEPEND_CFLAGS)
HOST_CXXFLAGS = $(COMPUTED_HOST_CXXFLAGS) $(_HOST_DEPEND_CFLAGS)
HOST_C_LDFLAGS = $(COMPUTED_HOST_LDFLAGS) $(COMPUTED_HOST_C_LDFLAGS)
HOST_CXX_LDFLAGS = $(COMPUTED_HOST_LDFLAGS) $(COMPUTED_HOST_CXX_LDFLAGS)

WASM_CFLAGS = $(COMPUTED_WASM_CFLAGS) $(_DEPEND_CFLAGS)
WASM_CXXFLAGS = $(COMPUTED_WASM_CXXFLAGS) $(_DEPEND_CFLAGS)

# We only add color flags if neither the flag to disable color
# (e.g. "-fno-color-diagnostics" nor a flag to control color
# (e.g. "-fcolor-diagnostics=never") is present.
define colorize_flags
ifeq (,$(filter $(COLOR_CFLAGS:-f%=-fno-%),$$(1))$(findstring $(COLOR_CFLAGS),$$(1)))
$(1) += $(COLOR_CFLAGS)
endif
endef

color_flags_vars := \
  COMPILE_CFLAGS \
  COMPILE_CXXFLAGS \
  COMPILE_CMFLAGS \
  COMPILE_CMMFLAGS \
  WASM_CFLAGS \
  WASM_CXXFLAGS \
  $(NULL)

ifdef MACH_STDOUT_ISATTY
ifdef COLOR_CFLAGS
# TODO Bug 1319166 - iTerm2 interprets some bytes  sequences as a
# request to show a print dialog. Don't enable color on iTerm2 until
# a workaround is in place.
ifneq ($(TERM_PROGRAM),iTerm.app)
$(foreach var,$(color_flags_vars),$(eval $(call colorize_flags,$(var))))
endif
endif
endif

#
# Name of the binary code directories
#
# Override defaults

DEPENDENCIES	= .md

# Export to propagate to link.exe and submake for third-party code.
# Eventually, we'll want to just use -LIBPATH.
ifdef LIB
export LIB
endif

# Export so sccache (used as a compiler wrapper) rewrites paths under these
# base directories to relative ones, sharing cache hits across worktrees.
ifdef SCCACHE_BASEDIRS
export SCCACHE_BASEDIRS
endif

ifdef SCCACHE_MAX_FRAME_LENGTH
export SCCACHE_MAX_FRAME_LENGTH
endif

ifdef SCCACHE_CLIENT_SIDE
export SCCACHE_CLIENT_SIDE
endif

-include $(topsrcdir)/$(MOZ_BUILD_APP)/app-config.mk

######################################################################

ifeq ($(OS_ARCH),Darwin)
PWD := $(CURDIR)
endif

NSINSTALL_PY := $(PYTHON3) $(abspath $(MOZILLA_DIR)/config/nsinstall.py)
ifneq (,$(or $(filter WINNT,$(HOST_OS_ARCH)),$(if $(COMPILE_ENVIRONMENT),,1)))
NSINSTALL = $(NSINSTALL_PY)
else
NSINSTALL = $(DEPTH)/config/nsinstall$(HOST_BIN_SUFFIX)
endif # WINNT


ifeq (,$(CROSS_COMPILE)$(filter-out WINNT, $(OS_ARCH)))
INSTALL = $(NSINSTALL) -t

else ifeq ($(OS_ARCH),Darwin)
INSTALL = $(NSINSTALL) -L $(PWD)

else
INSTALL = $(NSINSTALL) -R

endif

# The default for install_cmd is simply INSTALL
install_cmd ?= $(INSTALL) $(1)

# Use nsinstall in copy mode to install files on the system
SYSINSTALL	= $(NSINSTALL) -t
# This isn't necessarily true, just here
sysinstall_cmd = install_cmd

#
# Localization build automation
#

# Because you might wish to "make locales AB_CD=ab-CD", we don't hardcode
# MOZ_UI_LOCALE directly, but use an intermediate variable that can be
# overridden by the command line. (Besides, AB_CD is prettier).
AB_CD = $(MOZ_UI_LOCALE)

include $(MOZILLA_DIR)/config/AB_rCD.mk

# Many locales directories want this definition.
ACDEFINES += -DAB_CD=$(AB_CD)

EXPAND_LOCALE_SRCDIR = $(if $(filter en-US,$(AB_CD)),$(LOCALE_TOPDIR)/$(1)/en-US,$(REAL_LOCALE_MERGEDIR)/$(subst /locales,,$(1)))

ifdef relativesrcdir
LOCALE_RELATIVEDIR ?= $(relativesrcdir)
endif

ifdef LOCALE_RELATIVEDIR
LOCALE_SRCDIR ?= $(call EXPAND_LOCALE_SRCDIR,$(LOCALE_RELATIVEDIR))
endif

MERGE_RELATIVE_FILE = $(call EXPAND_LOCALE_SRCDIR,$(2))/$(1)

# Enable verbose logs when not using `make -s`
ifeq (,$(findstring s, $(filter-out --%, $(MAKEFLAGS))))
export BUILD_VERBOSE_LOG = 1
endif
