#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Compiler settings for mozpkix and for the C++ code in NSS libraries that
# link it; the make counterpart of mozpkix_cxx.gypi.

ifeq ($(OS_TARGET),WINNT)
ifndef NS_USE_GCC
# The mozpkix headers use C++ standard library types that require /EHsc
# with MSVC when warnings are treated as errors.
CXXFLAGS += -EHsc -GR-
MOZPKIX_MSVC = 1
endif
endif

ifndef MOZPKIX_MSVC
CXXFLAGS += -fno-exceptions -fno-rtti
endif
