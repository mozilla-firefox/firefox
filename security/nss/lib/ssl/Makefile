#! gmake
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#######################################################################
# (1) Include initial platform-independent assignments (MANDATORY).   #
#######################################################################

include manifest.mn

#######################################################################
# (2) Include "global" configuration information. (OPTIONAL)          #
#######################################################################

include $(CORE_DEPTH)/coreconf/config.mk

include $(CORE_DEPTH)/lib/mozpkix/mozpkix_cxx.mk

#######################################################################
# (3) Include "component" configuration information. (OPTIONAL)       #
######################################################################
#


#######################################################################
# (4) Include "local" platform-dependent assignments (OPTIONAL).      #
#######################################################################

include config.mk

ifeq (,$(filter-out WIN%,$(OS_TARGET)))
CSRCS	+= win32err.c
DEFINES += -DIN_LIBSSL
else
CSRCS	+= unix_err.c
endif

# Enable key logging by default in debug builds, but not opt builds.
# Logging still needs to be enabled at runtime through env vars.
NSS_ALLOW_SSLKEYLOGFILE ?= $(if $(BUILD_OPT),0,1)
ifeq (1,$(NSS_ALLOW_SSLKEYLOGFILE))
DEFINES += -DNSS_ALLOW_SSLKEYLOGFILE=1
endif

#######################################################################
# (5) Execute "global" rules. (OPTIONAL)                              #
#######################################################################

include $(CORE_DEPTH)/coreconf/rules.mk

#######################################################################
# (6) Execute "component" rules. (OPTIONAL)                           #
#######################################################################



#######################################################################
# (7) Execute "local" rules. (OPTIONAL).                              #
#######################################################################
