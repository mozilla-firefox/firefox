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

#######################################################################
# (3) Include "component" configuration information. (OPTIONAL)       #
#######################################################################



#######################################################################
# (4) Include "local" platform-dependent assignments (OPTIONAL).      #
#######################################################################

MOZPKIX_HEADER_SRCDIR = $(CORE_DEPTH)/lib/mozpkix/include/pkix
MOZPKIX_HEADER_DESTDIR = $(SOURCE_XPHEADERS_DIR)/mozpkix

MOZPKIX_HEADERS = \
	Input.h \
	Time.h \
	Result.h \
	pkix.h \
	pkixc.h \
	pkixnss.h \
	pkixtypes.h \
	pkixutil.h \
	pkixcheck.h \
	pkixder.h \
	$(NULL)

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

export: mozpkix_export_headers

mozpkix_export_headers: | $(MOZPKIX_HEADER_DESTDIR)/d
	$(INSTALL) -m 444 $(addprefix $(MOZPKIX_HEADER_SRCDIR)/,$(MOZPKIX_HEADERS)) \
		$(MOZPKIX_HEADER_DESTDIR)
	$(INSTALL) -m 444 $(CORE_DEPTH)/cpputil/nss_scoped_ptrs.h \
		$(MOZPKIX_HEADER_DESTDIR)
