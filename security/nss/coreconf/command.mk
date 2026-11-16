#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#######################################################################
# Master "Core Components" default command macros;                    #
# can be overridden in <arch>.mk                                      #
#######################################################################

AS            = $(CC)
ASFLAGS      += $(CFLAGS)
CCF           = $(CC) $(CFLAGS)
LINK_DLL      = $(LD) $(OS_DLLFLAGS) $(DLLFLAGS) $(XLDFLAGS)
CFLAGS        = $(OPTIMIZER) $(OS_CFLAGS) $(WARNING_CFLAGS) $(XP_DEFINE) \
                $(DEFINES) $(INCLUDES) $(XCFLAGS)
CSTD          = -std=c99
CXXSTD        = -std=c++17
PERL          = perl
RANLIB        = echo
TAR           = /bin/tar
#
# For purify
#
NOMD_CFLAGS  += $(OPTIMIZER) $(NOMD_OS_CFLAGS) $(XP_DEFINE) $(DEFINES) \
		$(INCLUDES) $(XCFLAGS)

# Optimization of code for size
# OPT_CODE_SIZE
# =1: The code can be optimized for size.
#     The code is actually optimized for size only if ALLOW_OPT_CODE_SIZE=1
#     in a given source code directory (in manifest.mn)
# =0: Never optimize the code for size.
#
# Default value = 0 
# Can be overridden from the make command line.
ifndef OPT_CODE_SIZE
OPT_CODE_SIZE = 0
endif

MK_COMMAND = included
