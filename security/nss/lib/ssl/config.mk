#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

ifdef NISCC_TEST
DEFINES += -DNISCC_TEST
endif

# mozpkixwrappers.cpp uses mozpkix.  Linked as an archive, so only the mozpkix
# objects that are referenced are pulled in.
EXTRA_LIBS += $(DIST)/lib/$(LIB_PREFIX)mozpkix.$(LIB_SUFFIX)

ifeq (,$(filter-out WIN%,$(OS_TARGET)))

ifdef NS_USE_GCC
EXTRA_SHARED_LIBS += \
	-L$(DIST)/lib \
	-lnss3 \
	-L$(NSSUTIL_LIB_DIR) \
	-lnssutil3 \
	-L$(NSPR_LIB_DIR) \
	-lplc4 \
	-lplds4 \
	-lnspr4 \
	$(NULL)
else # ! NS_USE_GCC
EXTRA_SHARED_LIBS += \
	$(DIST)/lib/nss3.lib \
	$(DIST)/lib/nssutil3.lib \
	$(NSPR_LIB_DIR)/$(NSPR31_LIB_PREFIX)plc4.lib \
	$(NSPR_LIB_DIR)/$(NSPR31_LIB_PREFIX)plds4.lib \
	$(NSPR_LIB_DIR)/$(NSPR31_LIB_PREFIX)nspr4.lib \
	$(NULL)
endif # NS_USE_GCC

else

# $(EXTRA_SHARED_LIBS) come before $(OS_LIBS), except on AIX.
EXTRA_SHARED_LIBS += \
	-L$(DIST)/lib \
	-lnss3 \
	-L$(NSSUTIL_LIB_DIR) \
	-lnssutil3 \
	-L$(NSPR_LIB_DIR) \
	-lplc4 \
	-lplds4 \
	-lnspr4 \
	$(NULL)

endif

ifdef NSS_DISABLE_TLS_1_3
DEFINES += -DNSS_DISABLE_TLS_1_3
endif
