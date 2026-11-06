#! gmake
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

CORE_DEPTH = ..
DEPTH      = ..

include $(CORE_DEPTH)/coreconf/config.mk

# This "jungle" of conditionals evaluates which DIRS to build!
# The main reason for it is AFAIK /automation/taskcluster/scripts/split.sh
#
# The fundamental organisation is:
# if NSS_BUILD_UTIL_ONLY
# else
#   if NSS_BUILD_SOFTOKEN_ONLY
#   else # build "all" the rest of NSS
# endif
#
# But there is even some build (NSS_BUILD_WITHOUT_SOFTOKEN), which
# depends on a previous partial build!
#
# FWIW, the dependencies are sorted and "compacted" instead of single
# lines for smaller patches, as these probably won't change much and
# it's easier to read this way.

# Building softoken (and freebl) only requires that the paths
# to the locations where the util headers and libraries were
# previously installed by a prior util-only build - likely in
# in a system location that varies with the distribution. This
# cannot be addressed here and requires that downstream package
# mantainers add suitable modifications. Building full nss will
# not have that problem as everything is available.

ifndef USE_SYSTEM_ZLIB
ZLIB_SRCDIR = zlib
endif

ifndef MOZILLA_CLIENT
ifndef NSS_USE_SYSTEM_SQLITE
SQLITE_SRCDIR = sqlite
endif

ifeq ($(OS_ARCH),Linux)
SYSINIT_SRCDIR = sysinit
endif
endif # MOZILLA_CLIENT

ifndef NSS_DISABLE_DBM
DBM_SRCDIR = dbm
endif

ifndef NSS_DISABLE_LIBPKIX
LIBPKIX_SRCDIR = libpkix
endif

ifeq ($(NSS_BUILD_UTIL_ONLY),1)
  UTIL_SRCDIR = util

else
  ifeq ($(NSS_BUILD_SOFTOKEN_ONLY),1)
    FREEBL_SRCDIR = freebl
    SOFTOKEN_SRCDIR = softoken
  else
    # Not included when building nss without softoken
    # This build type uses the build results of the prior
    # NSS_BUILD_UTIL_ONLY and NSS_BUILD_SOFTOKEN_ONLY builds
    ifneq ($(NSS_BUILD_WITHOUT_SOFTOKEN),1)
      FREEBL_SRCDIR = freebl
      SOFTOKEN_SRCDIR = softoken
      UTIL_SRCDIR = util
    endif
  endif

  SOFTOKEN_SRCDIRS = \
	$(DBM_SRCDIR) $(FREEBL_SRCDIR) $(SOFTOKEN_SRCDIR) \
	$(SQLITE_SRCDIR) $(NULL)

  ifneq (,$(FREEBL_SRCDIR))
    $(FREEBL_SRCDIR): $(UTIL_SRCDIR)
  endif

  ifneq (,$(SOFTOKEN_SRCDIR))
    $(SOFTOKEN_SRCDIR): $(DBM_SRCDIR) $(FREEBL_SRCDIR) $(SQLITE_SRCDIR) $(UTIL_SRCDIR)
  endif

  ifndef NSS_BUILD_SOFTOKEN_ONLY
    NSS_SRCDIRS = \
	base certdb certhigh ckfw cryptohi dev $(LIBPKIX_SRCDIR) mozpkix \
	nss pk11wrap pkcs12 pkcs7 pki smime ssl $(SYSINIT_SRCDIR) $(ZLIB_SRCDIR)

    ifneq (,$(SYSINIT_SRCDIR))
      $(SYSINIT_SRCDIR): $(UTIL_SRCDIR)
    endif

    nss: base certdb certhigh cryptohi dev $(LIBPKIX_SRCDIR) pk11wrap pki $(UTIL_SRCDIR)
    smime: nss pkcs7 pkcs12
    ssl: nss mozpkix
    ckfw: nss

  endif # !NSS_BUILD_SOFTOKEN_ONLY
endif # !NSS_BUILD_UTIL_ONLY

DIRS = \
	$(NSS_SRCDIRS) $(SOFTOKEN_SRCDIRS) $(UTIL_SRCDIR) $(NULL)

include $(CORE_DEPTH)/coreconf/rules.mk
