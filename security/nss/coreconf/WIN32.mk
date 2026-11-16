#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#
# Configuration common to all versions of Windows NT
# and Windows 95
#

DEFAULT_COMPILER = cl

ifdef NS_USE_GCC
	CC           = gcc
	CCC          = g++
	LD           = ld
	AR           = ar
	AR          += cr $@
	RANLIB       = ranlib
	BSDECHO      = echo
	RC           = windres.exe -O coff --use-temp-file
	LINK_DLL      = $(CC) $(OS_DLLFLAGS) $(DLLFLAGS)
else
	CC           = cl
	CCC          = cl
	LD           = link
        LDFLAGS += -nologo
	AR           = lib
	AR          += -nologo -OUT:$@
	RANLIB       = echo
	BSDECHO      = echo
	RC           = rc.exe
	MT           = mt.exe
	# Check for clang-cl
	CLANG_CL    := $(shell expr `$(CC) -? 2>&1 | grep -w clang | wc -l` \> 0)
	# Determine compiler version
	ifeq ($(CLANG_CL),1)
	    # clang-cl pretends to be MSVC 2012.
	    CC_VERSION  := 17.00.00.00
	else
	    CC_VERSION  := $(shell $(CC) 2>&1 | sed -ne \
		's|.* \([0-9]\+\.[0-9]\+\.[0-9]\+\(\.[0-9]\+\)\?\).*|\1|p')
	endif
	# Change the dots to spaces.
	_CC_VERSION_WORDS := $(subst ., ,$(CC_VERSION))
	_CC_VMAJOR  := $(word 1,$(_CC_VERSION_WORDS))
	_CC_VMINOR  := $(word 2,$(_CC_VERSION_WORDS))
	_CC_RELEASE := $(word 3,$(_CC_VERSION_WORDS))
	_CC_BUILD   := $(word 4,$(_CC_VERSION_WORDS))
	_MSC_VER     = $(_CC_VMAJOR)$(_CC_VMINOR)
	_MSC_VER_6   = 1200
	# VC10 (2010) is 16.00.30319.01, VC10SP1 is 16.00.40219.01.
	_MSC_VER_GE_10SP1 := $(shell expr $(_MSC_VER) \> 1600 \| \
		$(_MSC_VER) = 1600 \& $(_CC_RELEASE) \>= 40219)
	# VC11 (2012).
	_MSC_VER_GE_11 := $(shell expr $(_MSC_VER) \>= 1700)
	# VC12 (2013).
	_MSC_VER_GE_12 := $(shell expr $(_MSC_VER) \>= 1800)
	# VC14 (2015).
	_MSC_VER_GE_14 := $(shell expr $(_MSC_VER) \>= 1900)
	ifeq ($(_CC_VMAJOR),14)
	    # -DYNAMICBASE is only supported on VC8SP1 or newer,
	    # so be very specific here!
	    # VC8 is 14.00.50727.42, VC8SP1 is 14.00.50727.762
	    ifeq ($(_CC_RELEASE).$(_CC_BUILD),50727.42)
		USE_DYNAMICBASE =
	    else
	    ifeq ($(_CC_RELEASE).$(_CC_BUILD),50727.762)
		USE_DYNAMICBASE = 1
	    else
		_LOSER := $(error Unknown compiler version $(CC_VERSION))
	    endif
	    endif
	endif
	# if $(_CC_VMAJOR) >= 15
	# NOTE: 'sort' sorts the words in lexical order, so this test works
	# only if $(_CC_VMAJOR) is two digits.
	ifeq ($(firstword $(sort $(_CC_VMAJOR) 15)),15)
	    USE_DYNAMICBASE = 1
	endif
endif

ifdef BUILD_TREE
NSINSTALL_DIR  = $(BUILD_TREE)/nss
else
NSINSTALL_DIR  = $(CORE_DEPTH)/coreconf/nsinstall
endif
NSINSTALL      = nsinstall

INSTALL      = $(NSINSTALL)
GARBAGE     += $(OBJDIR)/vc20.pdb $(OBJDIR)/vc40.pdb
XP_DEFINE   += -DXP_PC
ifdef NS_USE_GCC
LIB_SUFFIX   = a
else
LIB_SUFFIX   = lib
endif
DLL_SUFFIX   = dll

define MAKE_OBJDIR
if test ! -d $(@D); then mkdir -p $(@D); fi
endef

ifdef NS_USE_GCC
    OS_CFLAGS += -mwindows
    _GEN_IMPORT_LIB=-Wl,--out-implib,$(IMPORT_LIBRARY)
    DLLFLAGS  += -mwindows -o $@ -shared -Wl,--export-all-symbols $(if $(IMPORT_LIBRARY),$(_GEN_IMPORT_LIB))
    ifdef BUILD_OPT
	ifeq (11,$(ALLOW_OPT_CODE_SIZE)$(OPT_CODE_SIZE))
		OPTIMIZER += -Os
	else
		OPTIMIZER += -O2
	endif
	DEFINES    += -UDEBUG -DNDEBUG
    else
	OPTIMIZER  += -g
	DEFINES    += -DDEBUG -UNDEBUG
    endif
else # !NS_USE_GCC
    WARNING_CFLAGS = -W3 -nologo -D_CRT_SECURE_NO_WARNINGS \
                      -D_CRT_NONSTDC_NO_WARNINGS
    OS_DLLFLAGS += -nologo -DLL -SUBSYSTEM:WINDOWS
    ifndef NSS_ENABLE_WERROR
        NSS_ENABLE_WERROR = 1
    endif
    ifeq ($(NSS_ENABLE_WERROR),1)
        WARNING_CFLAGS += -WX
    endif
    ifeq ($(_MSC_VER),$(_MSC_VER_6))
    ifndef MOZ_DEBUG_SYMBOLS
	OS_DLLFLAGS += -PDB:NONE
    endif
    endif
    ifdef USE_DYNAMICBASE
	OS_DLLFLAGS += -DYNAMICBASE
    endif
    #
    # Define USE_DEBUG_RTL if you want to use the debug runtime library
    # (RTL) in the debug build.
    # Define USE_STATIC_RTL if you want to use the static RTL.
    #
    ifdef USE_DEBUG_RTL
	ifdef USE_STATIC_RTL
		OS_CFLAGS += -MTd
	else
		OS_CFLAGS += -MDd
	endif
	OS_CFLAGS += -D_CRTDBG_MAP_ALLOC
    else
	ifdef USE_STATIC_RTL
		OS_CFLAGS += -MT
	else
		OS_CFLAGS += -MD
	endif
    endif
    ifdef BUILD_OPT
	ifeq (11,$(ALLOW_OPT_CODE_SIZE)$(OPT_CODE_SIZE))
		OPTIMIZER += -O1
	else
		OPTIMIZER += -O2
	endif
	DEFINES    += -UDEBUG -DNDEBUG
	DLLFLAGS   += -OUT:$@
	ifdef MOZ_DEBUG_SYMBOLS
		ifdef MOZ_DEBUG_FLAGS
			OPTIMIZER += $(MOZ_DEBUG_FLAGS) -Fd$(OBJDIR)/
		else
			OPTIMIZER += -Zi -Fd$(OBJDIR)/
		endif
		DLLFLAGS += -DEBUG -OPT:REF
		LDFLAGS += -DEBUG -OPT:REF
	endif
    else
	OPTIMIZER += -Zi -Fd$(OBJDIR)/ -Od
	NULLSTRING :=
	DEFINES    += -DDEBUG -UNDEBUG
	DLLFLAGS   += -DEBUG -OUT:$@
	LDFLAGS    += -DEBUG 
ifeq ($(_MSC_VER),$(_MSC_VER_6))
ifndef MOZ_DEBUG_SYMBOLS
	LDFLAGS    += -PDB:NONE 
endif
endif
	# Purify requires /FIXED:NO when linking EXEs.
	LDFLAGS    += /FIXED:NO
	# So the linker will find main in the gtestutil library
	LDFLAGS    += -SUBSYSTEM:CONSOLE
    endif
ifneq ($(_MSC_VER),$(_MSC_VER_6))
    # NSS has too many of these to fix, downgrade the warning
    # Disable C4267: conversion from 'size_t' to 'type', possible loss of data
    # Disable C4244: conversion from 'type1' to 'type2', possible loss of data
    # Disable C4018: 'expression' : signed/unsigned mismatch
    # Disable C4312: 'type cast': conversion from 'type1' to 'type2' of greater size
    OS_CFLAGS += -w44267 -w44244 -w44018 -w44312
    ifeq ($(_MSC_VER_GE_12),1)
	OS_CFLAGS += -FS
    endif
endif # !MSVC6
endif # NS_USE_GCC

ifdef USE_64
DEFINES += -DWIN64
else
DEFINES += -DWIN32
endif

ifeq (,$(filter-out x386 x86_64,$(CPU_ARCH)))
ifdef USE_64
	DEFINES += -D_AMD64_
	# Use subsystem 5.02 to allow running on Windows XP.
	ifeq ($(_MSC_VER_GE_11),1)
		LDFLAGS += -SUBSYSTEM:CONSOLE,5.02
	endif
	CPU_ARCH = x86_64
else
	DEFINES += -D_X86_
	# VS2012 defaults to -arch:SSE2. Use -arch:IA32 to avoid requiring
	# SSE2. Clang-cl gets confused by -arch:IA32, so don't add it.
	# (See https://llvm.org/bugs/show_bug.cgi?id=24335)
	# Use subsystem 5.01 to allow running on Windows XP.
	ifeq ($(_MSC_VER_GE_11),1)
		ifneq ($(CLANG_CL),1)
			OS_CFLAGS += -arch:IA32
		endif
		LDFLAGS += -SUBSYSTEM:CONSOLE,5.01
	endif
	CPU_ARCH = x386
endif
endif
ifeq ($(CPU_ARCH), ALPHA)
	DEFINES += -D_ALPHA_=1
endif

ifdef MAPFILE
ifndef NS_USE_GCC
DLLFLAGS += -DEF:$(MAPFILE)
endif
endif
# Change PROCESS to put the mapfile in the correct format for this platform
PROCESS_MAP_FILE = cp $< $@


#
#  The following is NOT needed for the NSPR 2.0 library.
#

DEFINES += -D_WINDOWS

# override default, which is ASFLAGS = CFLAGS
ifdef NS_USE_GCC
	AS	= $(CC)
	ASFLAGS = $(INCLUDES)
else
ifdef USE_64
	AS	= ml64.exe
	ASFLAGS = -nologo -Cp -Sn -Zi $(INCLUDES)
else
	AS	= ml.exe
	ASFLAGS = -nologo -Cp -Sn -Zi -coff -safeseh $(INCLUDES)
endif
endif

# Unless we're using GCC, use the compiler's default C standard, and give the
# C++ standard in MSVC syntax (which clang-cl also accepts)
ifndef NS_USE_GCC
        CSTD    =
        CXXSTD  = -std:c++17
endif

#
# override the definitions of RELEASE_TREE found in tree.mk
#
ifndef RELEASE_TREE
    ifdef BUILD_SHIP
	ifdef USE_SHIPS
	    RELEASE_TREE = $(NTBUILD_SHIP)
	else
	    RELEASE_TREE = //redbuild/components
	endif
    else
	RELEASE_TREE = //redbuild/components
    endif
endif

#
# override the definitions of IMPORT_LIB_PREFIX, LIB_PREFIX, and
# DLL_PREFIX in prefix.mk
#

ifndef IMPORT_LIB_PREFIX
    ifdef NS_USE_GCC
	IMPORT_LIB_PREFIX = lib
    else
	IMPORT_LIB_PREFIX = $(NULL)
    endif
endif

ifndef LIB_PREFIX
    ifdef NS_USE_GCC
	LIB_PREFIX = lib
    else
	LIB_PREFIX = $(NULL)
    endif
endif

ifndef DLL_PREFIX
    DLL_PREFIX =  $(NULL)
endif

#
# override the definitions of various _SUFFIX symbols in suffix.mk
#

#
# Object suffixes
#
ifndef OBJ_SUFFIX
    ifdef NS_USE_GCC
	OBJ_SUFFIX = .o
    else
	OBJ_SUFFIX = .obj
    endif
endif

#
# Assembler source suffixes
#
ifndef ASM_SUFFIX
    ifdef NS_USE_GCC
	ASM_SUFFIX = .s
    else
	ASM_SUFFIX = .asm
    endif
endif

#
# Library suffixes
#

ifndef IMPORT_LIB_SUFFIX
    IMPORT_LIB_SUFFIX = .$(LIB_SUFFIX)
endif

ifndef DYNAMIC_LIB_SUFFIX_FOR_LINKING
    DYNAMIC_LIB_SUFFIX_FOR_LINKING = $(IMPORT_LIB_SUFFIX)
endif

#
# Program suffixes
#
ifndef PROG_SUFFIX
    PROG_SUFFIX = .exe
endif

#
# When the processor is NOT 386-based on Windows NT, override the
# value of $(CPU_TAG).  For WinNT, 95, 16, not CE.
#
ifneq ($(CPU_ARCH),x386)
    CPU_TAG = _$(CPU_ARCH)
endif

#
# override the TARGETS defined in ruleset.mk, adding IMPORT_LIBRARY
#
ifndef TARGETS
    TARGETS = $(LIBRARY) $(SHARED_LIBRARY) $(IMPORT_LIBRARY) $(PROGRAM)
endif
