#! gmake
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

include $(CORE_DEPTH)/cmd/platlibs.mk

MKPROG = $(CCC)
MKSHLIB	= $(CCC) $(DSO_LDOPTS) $(DARWIN_SDK_SHLIBFLAGS)

# gtests pick up errors with signed/unsigned comparisons on some platforms
# even though we disabled -Wsign-compare.
# This catches that by enabling the warning.
# Only add -Wsign-compare if -Werror is enabled, lest we add it on the wrong
# platform.
ifeq (-Werror,$(filter -Werror -Wsign-compare,$(WARNING_CFLAGS)))
WARNING_CFLAGS += -Wsign-compare
endif
WARNING_CFLAGS := $(filter-out -w44018,$(WARNING_CFLAGS))

ifeq (WINNT,$(OS_ARCH))
    # -EHsc because gtest has exception handlers
    OS_CFLAGS += -EHsc -nologo
    # http://www.suodenjoki.dk/us/archive/2010/min-max.htm
    OS_CFLAGS += -DNOMINMAX

    # Linking to winsock to get htonl
    OS_LIBS += Ws2_32.lib

    # On windows, we need to create the parent directory
    # Needed because we include files from a subdirectory
    MAKE_OBJDIR = $(INSTALL) -D $(dir $@)
endif
