#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

ifeq ($(BUILD_SUN_PKG), 1)

# set RPATH-type linker instructions here so they can be used in the shared
# version and in the mixed (static nss libs/shared NSPR libs) version.

ifeq ($(OS_ARCH), SunOS) 
ifeq ($(USE_64), 1)
EXTRA_SHARED_LIBS += -R '$$ORIGIN/../lib:/usr/lib/mps/secv1/64:/usr/lib/mps/64'
else
EXTRA_SHARED_LIBS += -R '$$ORIGIN/../lib:/usr/lib/mps/secv1:/usr/lib/mps'
endif
endif

ifeq ($(OS_ARCH), Linux)
ifeq ($(USE_64), 1)
EXTRA_SHARED_LIBS += -Wl,-rpath,'$$ORIGIN/../lib64:/opt/sun/private/lib64:$$ORIGIN/../lib'
else
EXTRA_SHARED_LIBS += -Wl,-rpath,'$$ORIGIN/../lib:/opt/sun/private/lib'
endif
endif

endif # BUILD_SUN_PKG

ifdef NSS_DISABLE_DBM
DBMLIB = $(NULL)
else
DBMLIB = $(DIST)/lib/$(LIB_PREFIX)dbm.$(LIB_SUFFIX) 
endif

ifeq ($(NSS_BUILD_UTIL_ONLY),1)
SECTOOL_LIB = $(NULL)
else
SECTOOL_LIB = $(DIST)/lib/$(LIB_PREFIX)sectool.$(LIB_SUFFIX)
endif

ifdef USE_STATIC_LIBS

DEFINES += -DNSS_USE_STATIC_LIBS
# $(PROGRAM) has explicit dependencies on $(EXTRA_LIBS)
ifndef NSS_USE_SYSTEM_FREEBL
CRYPTOLIB=$(DIST)/lib/$(LIB_PREFIX)freebl.$(LIB_SUFFIX)
SOFTOKENLIB=$(DIST)/lib/$(LIB_PREFIX)softokn.$(LIB_SUFFIX)
else
# Use the system installed freebl static library and set softoken one to empty.
# Some tools need to link statically with freebl but none with softoken. Only
# the softoken shared library, not the static one, is installed in the system.
CRYPTOLIB=$(FREEBL_LIB_DIR)/$(LIB_PREFIX)freebl.$(LIB_SUFFIX)
SOFTOKENLIB=
EXTRA_SHARED_LIBS += \
	-L$(SOFTOKEN_LIB_DIR) \
	-lsoftokn3 \
	$(NULL)
endif

ifndef NSS_DISABLE_LIBPKIX
ifndef NSS_BUILD_SOFTOKEN_ONLY
PKIXLIB = \
	$(DIST)/lib/$(LIB_PREFIX)pkixtop.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixutil.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixsystem.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixcrlsel.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixmodule.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixstore.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixparams.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixchecker.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixpki.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixtop.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixresults.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkixcertsel.$(LIB_SUFFIX)
endif
endif

NSS_LIBS_1=
NSS_LIBS_2=
NSS_LIBS_3=
NSS_LIBS_4=

ifneq ($(NSS_BUILD_SOFTOKEN_ONLY),1)
ifeq ($(OS_ARCH), WINNT)
# breakdown for windows
NSS_LIBS_1 = \
	$(DIST)/lib/$(LIB_PREFIX)smime.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)ssl.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)mozpkix.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)nss.$(LIB_SUFFIX) \
	$(NULL)
NSS_LIBS_2 = \
	$(DIST)/lib/$(LIB_PREFIX)pkcs12.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkcs7.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)certhi.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)cryptohi.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pk11wrap.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)certdb.$(LIB_SUFFIX) \
	$(NULL)
NSS_LIBS_3 = \
	$(DIST)/lib/$(LIB_PREFIX)nsspki.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)nssdev.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)nssb.$(LIB_SUFFIX) \
	$(PKIXLIB) \
	$(DBMLIB) \
	$(NULL)
NSS_LIBS_4 = \
	$(SQLITE_LIB_DIR)/$(LIB_PREFIX)$(SQLITE_LIB_NAME).$(LIB_SUFFIX) \
	$(NSSUTIL_LIB_DIR)/$(LIB_PREFIX)nssutil3.$(LIB_SUFFIX) \
	$(NSPR_LIB_DIR)/$(NSPR31_LIB_PREFIX)plc4.$(LIB_SUFFIX) \
	$(NSPR_LIB_DIR)/$(NSPR31_LIB_PREFIX)plds4.$(LIB_SUFFIX) \
	$(NSPR_LIB_DIR)/$(NSPR31_LIB_PREFIX)nspr4.$(LIB_SUFFIX) \
	$(NULL)
else
# breakdown for others
NSS_LIBS_1 = \
	$(DIST)/lib/$(LIB_PREFIX)smime.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)ssl.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)mozpkix.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)nss.$(LIB_SUFFIX) \
	$(NULL)
NSS_LIBS_2 = \
	$(DIST)/lib/$(LIB_PREFIX)pkcs12.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pkcs7.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)certhi.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)cryptohi.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pk11wrap.$(LIB_SUFFIX) \
	$(NULL)
NSS_LIBS_3 = \
	$(DIST)/lib/$(LIB_PREFIX)certdb.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)nsspki.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)nssdev.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)nssb.$(LIB_SUFFIX) \
	$(NULL)
NSS_LIBS_4 = \
	$(DBMLIB) \
	$(PKIXLIB) \
	$(DIST)/lib/$(LIB_PREFIX)nss.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)pk11wrap.$(LIB_SUFFIX) \
	$(DIST)/lib/$(LIB_PREFIX)certhi.$(LIB_SUFFIX) \
	$(NULL)
endif
endif

# can't do this in manifest.mn because OS_ARCH isn't defined there.
ifeq ($(OS_ARCH), WINNT)

EXTRA_LIBS += \
	$(SECTOOL_LIB) \
	$(NSS_LIBS_1) \
	$(NSS_LIBS_2) \
	$(SOFTOKENLIB) \
	$(CRYPTOLIB) \
	$(NSS_LIBS_3) \
	$(NSS_LIBS_4) \
	$(NULL)

# $(PROGRAM) has NO explicit dependencies on $(OS_LIBS)
#OS_LIBS += \
	wsock32.lib \
	winmm.lib \
	$(NULL)
else

EXTRA_LIBS += \
	$(SECTOOL_LIB) \
	$(NSS_LIBS_1) \
	$(NSS_LIBS_2) \
	$(SOFTOKENLIB) \
	$(NSS_LIBS_3) \
	$(CRYPTOLIB) \
	$(NSS_LIBS_4) \
	$(NULL)

ifeq ($(OS_ARCH), AIX) 
EXTRA_SHARED_LIBS += -brtl 
endif

# $(PROGRAM) has NO explicit dependencies on $(EXTRA_SHARED_LIBS)
# $(EXTRA_SHARED_LIBS) come before $(OS_LIBS), except on AIX.
EXTRA_SHARED_LIBS += \
	-L$(SQLITE_LIB_DIR) \
	-l$(SQLITE_LIB_NAME) \
	-L$(NSSUTIL_LIB_DIR) \
	-lnssutil3 \
	-L$(NSPR_LIB_DIR) \
	-lplc4 \
	-lplds4 \
	-lnspr4 \
	$(NULL)
endif

else # USE_STATIC_LIBS
# can't do this in manifest.mn because OS_ARCH isn't defined there.
ifeq ($(OS_ARCH), WINNT)

# $(PROGRAM) has explicit dependencies on $(EXTRA_LIBS)
EXTRA_LIBS += \
	$(SECTOOL_LIB) \
	$(NSSUTIL_LIB_DIR)/$(IMPORT_LIB_PREFIX)nssutil3$(IMPORT_LIB_SUFFIX) \
	$(DIST)/lib/$(IMPORT_LIB_PREFIX)smime3$(IMPORT_LIB_SUFFIX) \
	$(DIST)/lib/$(IMPORT_LIB_PREFIX)ssl3$(IMPORT_LIB_SUFFIX) \
	$(DIST)/lib/$(IMPORT_LIB_PREFIX)nss3$(IMPORT_LIB_SUFFIX) \
	$(NSPR_LIB_DIR)/$(NSPR31_LIB_PREFIX)plc4$(IMPORT_LIB_SUFFIX) \
	$(NSPR_LIB_DIR)/$(NSPR31_LIB_PREFIX)plds4$(IMPORT_LIB_SUFFIX) \
	$(NSPR_LIB_DIR)/$(NSPR31_LIB_PREFIX)nspr4$(IMPORT_LIB_SUFFIX) \
	$(NULL)

# $(PROGRAM) has NO explicit dependencies on $(OS_LIBS)
#OS_LIBS += \
	wsock32.lib \
	winmm.lib \
	$(NULL)
else

# $(PROGRAM) has explicit dependencies on $(EXTRA_LIBS)
EXTRA_LIBS += \
	$(SECTOOL_LIB) \
	$(NULL)

ifeq ($(OS_ARCH), AIX) 
EXTRA_SHARED_LIBS += -brtl 
endif

# $(PROGRAM) has NO explicit dependencies on $(EXTRA_SHARED_LIBS)
# $(EXTRA_SHARED_LIBS) come before $(OS_LIBS), except on AIX.
EXTRA_SHARED_LIBS += \
	-L$(DIST)/lib \
	-L$(NSSUTIL_LIB_DIR) \
	-lnssutil3 \
	-L$(NSPR_LIB_DIR) \
	-lplc4 \
	-lplds4 \
	-lnspr4 \
	$(NULL)
ifndef NSS_BUILD_UTIL_ONLY
ifndef NSS_BUILD_SOFTOKEN_ONLY
EXTRA_SHARED_LIBS += \
	-lssl3 \
	-lsmime3 \
	-lnss3
endif
endif
endif

ifdef SOFTOKEN_LIB_DIR
ifdef NSS_USE_SYSTEM_FREEBL
EXTRA_SHARED_LIBS += -L$(SOFTOKEN_LIB_DIR) -lsoftokn3
endif
endif

endif # USE_STATIC_LIBS

# If a platform has a system freebl, set USE_SYSTEM_FREEBL to 1 and
# FREEBL_LIBS to the linker command-line arguments for the system nss-util
# (for example, -lfreebl3 on fedora) in the platform's config file in coreconf.
ifdef NSS_USE_SYSTEM_FREEBL
FREEBL_LIBS = $(FREEBL_LIB_DIR)/$(LIB_PREFIX)freebl.$(LIB_SUFFIX)
EXTRA_LIBS += $(FREEBL_LIBS)
endif

