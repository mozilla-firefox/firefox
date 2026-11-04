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

include config.mk

# default for all platforms
# unset this on those that have multiple freebl libraries
FREEBL_BUILD_SINGLE_SHLIB = 1

ifdef USE_64
	DEFINES += -DNSS_USE_64
endif

ifdef USE_ABI32_FPU
	DEFINES += -DNSS_USE_ABI32_FPU
endif

ifeq ($(FREEBL_NO_DEPEND),1)
	DEFINES += -DFREEBL_NO_DEPEND
	STUBS_SRCS = stubs.c
endif

ifeq ($(FREEBL_LOWHASH),1)
	DEFINES += -DFREEBL_LOWHASH
	LOWHASH_SRCS = nsslowhash.c
	LOWHASH_EXPORTS = nsslowhash.h
	MAPFILE_SOURCE = freebl_hash_vector.def
	NEED_STUB_BUILD = 1
else
	MAPFILE_SOURCE = freebl.def
endif

ifdef USE_STUB_BUILD
	CSRCS           = lowhash_vector.c
	SIMPLE_OBJS     = $(CSRCS:.c=$(OBJ_SUFFIX))
	OBJS            = $(addprefix $(OBJDIR)/$(PROG_PREFIX), $(SIMPLE_OBJS))
	ALL_TRASH :=    $(TARGETS) $(OBJS) $(OBJDIR) LOGS TAGS $(GARBAGE) \
                $(NOSUCHFILE) so_locations
	MAPFILE_SOURCE = freebl_hash.def
endif

# FREEBL_USE_PRELINK
#
# Most modern version of Linux support a speed optimization scheme where an
# application called prelink modifies programs and shared libraries to quickly
# load if they fit into an already designed address space. In short, prelink
# scans the list of programs and libraries on your system, assigns them a
# predefined space in the the address space, then provides the fixups to the
# library.
#
# The modification of the shared library is correctly detected by the freebl
# FIPS checksum scheme where we check a signed hash of the library against the
# library itself.
#
# The prelink command itself can reverse the process of modification and output
# the prestine shared library as it was before prelink made it's changes.
# This option tells Freebl could use prelink to output the original copy of
# the shared library before prelink modified it.
#
# FREEBL_PRELINK_COMMAND
#
# This is an optional environment variable which can override the default
# prelink command. It could be used on systems that did something similiar to
# prelink but used a different command and syntax. The only requirement is the
# program must take the library as the last argument, the program must output
# the original library to standard out, and the program does not need to take
# any quoted or imbedded spaces in its arguments (except the path to the
# library itself, which can have imbedded spaces or special characters).
#
ifdef FREEBL_USE_PRELINK
	DEFINES += -DFREEBL_USE_PRELINK
ifdef LINUX
	DEFINES += -D__GNU_SOURCE=1
endif
endif
ifdef NSS_NO_INIT_SUPPORT
    DEFINES += -DNSS_NO_INIT_SUPPORT
endif
ifdef NSS_STRICT_INTEGRITY
    DEFINES += -DNSS_STRICT_INTEGRITY_
endif

ifdef FREEBL_PRELINK_COMMAND
	DEFINES +=-DFREEBL_PRELINK_COMMAND=\"$(FREEBL_PRELINK_COMMAND)\"
endif
# NSS_X86 means the target is a 32-bits x86 CPU architecture
# NSS_X64 means the target is a 64-bits 64 CPU architecture
# NSS_X86_OR_X64 means the target is either x86 or x64
ifeq (,$(filter-out i386 x386 x86 x86_64,$(CPU_ARCH)))
        DEFINES += -DNSS_X86_OR_X64 -DHAVE_PLATFORM_GHASH
        EXTRA_SRCS += ghash-x86.c aes-x86.c
$(OBJDIR)/ghash-x86.o: CFLAGS += -mpclmul -maes
$(OBJDIR)/aes-x86.o: CFLAGS += -mpclmul -maes
ifneq (,$(USE_64)$(USE_X32))
        DEFINES += -DNSS_X64
else
        DEFINES += -DNSS_X86
endif
    ifdef CC_IS_CLANG
        EXTRA_SRCS += sha256-x86.c
        DEFINES += -DUSE_HW_SHA2
    else ifeq (1,$(CC_IS_GCC))
        # Old compiler doesn't support Intel SHA extension
        ifneq (,$(filter 4.9,$(word 1,$(GCC_VERSION)).$(word 2,$(GCC_VERSION))))
            EXTRA_SRCS += sha256-x86.c
            DEFINES += -DUSE_HW_SHA2
        endif
        ifeq (,$(filter 0 1 2 3 4,$(word 1,$(GCC_VERSION))))
            EXTRA_SRCS += sha256-x86.c
            DEFINES += -DUSE_HW_SHA2
        endif
    endif
endif
ifeq ($(CPU_ARCH),aarch64)
    # CC_IS_GCC covers clang too; the Arm crypto extension intrinsics need one
    # or the other.
    ifeq (1,$(CC_IS_GCC))
        DEFINES += -DUSE_HW_AES -DUSE_HW_SHA1 -DUSE_HW_SHA2
        EXTRA_SRCS += aes-armv8.c sha1-armv8.c sha256-armv8.c
        # The PMULL code is little-endian only. This must agree with the #if
        # in ghash-aarch64.c, or freebl ends up with duplicate or missing
        # gcm_*_hw symbols. See bug 2068010.
        ifeq ($(LITTLE_ENDIAN),1)
            AARCH64_PMULL = 1
        endif
    endif
    ifdef AARCH64_PMULL
        DEFINES += -DHAVE_PLATFORM_GHASH
        EXTRA_SRCS += ghash-aarch64.c
    endif
    # AArch64 AES-GCM implementation from AWS-LC. Not supported on Windows.
    # It bridges to the ghash-aarch64.c hash context, so it needs PMULL too.
    ifndef USE_STUB_BUILD
    ifdef AARCH64_PMULL
    ifneq ($(OS_TARGET),WINNT)
        DEFINES += -DHAVE_PLATFORM_GCM
        EXTRA_SRCS += aarch64-gcm-wrap.c
        AARCH64_GCM = 1
        # The .S assembly files are preprocessed and therefore not handled by
        # the ASFILES suffix substitution (which only matches $(ASM_SUFFIX)),
        # so add their object files to OBJS explicitly. This must happen before
        # coreconf/rules.mk is included so they become link prerequisites. The
        # C wrapper rides along in EXTRA_SRCS, which the parent (loader-only)
        # build discards by resetting CSRCS; the OBJS addition bypasses that, so
        # restrict it to the child build that compiles the real library.
        AARCH64_GCM_ASFILES = \
            aarch64-gcm-slothy/gcm-128-enc.S \
            aarch64-gcm-slothy/gcm-192-enc.S \
            aarch64-gcm-slothy/gcm-256-enc.S \
            aarch64-gcm-slothy/gcm-128-dec.S \
            aarch64-gcm-slothy/gcm-192-dec.S \
            aarch64-gcm-slothy/gcm-256-dec.S \
            $(NULL)
        ifdef FREEBL_CHILD_BUILD
            OBJS += $(addprefix $(OBJDIR)/$(PROG_PREFIX), $(AARCH64_GCM_ASFILES:.S=$(OBJ_SUFFIX)))
        endif
    endif
    endif
    endif
endif
ifeq ($(CPU_ARCH),arm)
ifndef NSS_DISABLE_ARM32_NEON
    ifeq ($(LITTLE_ENDIAN),1)
        DEFINES += -DHAVE_PLATFORM_GHASH
    endif
    EXTRA_SRCS += ghash-arm32-neon.c
endif
    ifdef CC_IS_CLANG
        DEFINES += -DUSE_HW_AES -DUSE_HW_SHA1 -DUSE_HW_SHA2
        EXTRA_SRCS += aes-armv8.c sha1-armv8.c sha256-armv8.c
    else ifeq (1,$(CC_IS_GCC))
        # GCC versions older than 4.9 don't support ARM AES. The check
        # is done in two parts, first allows "major.minor" == "4.9",
        # and then rejects any major versions prior to 5. Note that
        # there has been no GCC 4.10, as it was renamed to GCC 5.
        ifneq (,$(filter 4.9,$(word 1,$(GCC_VERSION)).$(word 2,$(GCC_VERSION))))
            DEFINES += -DUSE_HW_AES -DUSE_HW_SHA1 -DUSE_HW_SHA2
            EXTRA_SRCS += aes-armv8.c sha1-armv8.c sha256-armv8.c
        endif
        ifeq (,$(filter 0 1 2 3 4,$(word 1,$(GCC_VERSION))))
            DEFINES += -DUSE_HW_AES -DUSE_HW_SHA1 -DUSE_HW_SHA2
            EXTRA_SRCS += aes-armv8.c sha1-armv8.c sha256-armv8.c
        endif
    endif
endif

ifeq (,$(filter-out WINNT,$(OS_TARGET)))
ifndef USE_64
# 32-bit Windows
ifdef NS_USE_GCC
# Ideally, we want to use assembler
#     ASFILES  = mpi_x86.s
#     DEFINES += -DMP_ASSEMBLY_MULTIPLY -DMP_ASSEMBLY_SQUARE \
#                -DMP_ASSEMBLY_DIV_2DX1D
# but we haven't figured out how to make it work, so we are not
# using assembler right now.
    ASFILES  =
    DEFINES += -DMP_NO_MP_WORD -DMP_USE_UINT_DIGIT
else
# MSVC
    MPI_SRCS += mpi_x86_asm.c
    DEFINES += -DMP_ASSEMBLY_MULTIPLY -DMP_ASSEMBLY_SQUARE
    DEFINES += -DMP_ASSEMBLY_DIV_2DX1D -DMP_USE_UINT_DIGIT -DMP_NO_MP_WORD
    ifdef BUILD_OPT
	OPTIMIZER += -Ox  # maximum optimization for freebl
    endif
    # The Intel AES assembly code requires Visual C++ 2010.
    # if $(_MSC_VER) >= 1600 (Visual C++ 2010)
    ifeq ($(firstword $(sort $(_MSC_VER) 1600)),1600)
	DEFINES += -DUSE_HW_AES -DHAVE_PLATFORM_GCM
	ASFILES += intel-aes-x86-masm.asm intel-gcm-x86-masm.asm
	EXTRA_SRCS += intel-gcm-wrap.c
	ifeq ($(CLANG_CL),1)
	    INTEL_GCM = 1
	endif
    endif
    # The Intel SHA extenstion requires Visual C++ 2015.
    ifeq ($(_MSC_VER_GE_14),1)
        DEFINES += -DUSE_HW_SHA2
        EXTRA_SRCS += sha256-x86.c
    endif
endif
else
ifdef NS_USE_GCC
# Ideally, we should use amd64 assembly code, but it's not yet mingw-w64
# compatible.
else
# MSVC
    ifdef BUILD_OPT
	OPTIMIZER += -Ox  # maximum optimization for freebl
    endif
ifeq ($(CPU_ARCH),x86_64)
    ASFILES  = arcfour-amd64-masm.asm mpi_amd64_masm.asm mp_comba_amd64_masm.asm
    DEFINES += -DNSS_BEVAND_ARCFOUR -DMPI_AMD64 -DMP_ASSEMBLY_MULTIPLY
    DEFINES += -DNSS_USE_COMBA
    # The Intel AES assembly code requires Visual C++ 2010 (10.0). The _xgetbv
    # compiler intrinsic function requires Visual C++ 2010 (10.0) SP1.
    ifeq ($(_MSC_VER_GE_10SP1),1)
	DEFINES += -DUSE_HW_AES -DHAVE_PLATFORM_GCM
	ASFILES += intel-aes-x64-masm.asm intel-gcm-x64-masm.asm
	EXTRA_SRCS += intel-gcm-wrap.c
	ifeq ($(CLANG_CL),1)
	    INTEL_GCM = 1
	endif
    endif
    # The Intel SHA extenstion requires Visual C++ 2015.
    ifeq ($(_MSC_VER_GE_14),1)
        DEFINES += -DUSE_HW_SHA2
        EXTRA_SRCS += sha256-x86.c
    endif
    MPI_SRCS += mpi_amd64.c
endif
endif
endif
endif

ifeq ($(OS_TARGET),Darwin)
ifeq ($(CPU_ARCH),x86_64)
    ASFILES  = mpi_amd64_common.s
    DEFINES += -DMPI_AMD64
    DEFINES += -DMP_ASSEMBLY_MULTIPLY -DNSS_USE_COMBA
    MPI_SRCS += mpi_amd64.c mp_comba.c
else ifeq ($(CPU_ARCH),x86)
    ASFILES  = mpi_sse2.s
    DEFINES += -DMP_USE_UINT_DIGIT
    DEFINES += -DMP_ASSEMBLY_MULTIPLY -DMP_ASSEMBLY_SQUARE
    DEFINES += -DMP_ASSEMBLY_DIV_2DX1D
endif
endif # Darwin

ifeq ($(OS_TARGET),Linux)
ifeq ($(CPU_ARCH),x86_64)
    # Lower case s on mpi_amd64_common due to make implicit rules.
    ASFILES  = arcfour-amd64-gas.s mpi_amd64_common.s
    ASFLAGS += -fPIC -Wa,--noexecstack
    DEFINES += -DNSS_BEVAND_ARCFOUR -DMPI_AMD64 -DMP_ASSEMBLY_MULTIPLY
    DEFINES += -DNSS_USE_COMBA
#   DEFINES += -DMPI_AMD64_ADD
    # comment the next four lines to turn off Intel HW acceleration.
    DEFINES += -DUSE_HW_AES -DHAVE_PLATFORM_GCM
    ASFILES += intel-aes.s intel-gcm.s
    EXTRA_SRCS += intel-gcm-wrap.c
    INTEL_GCM = 1
    MPI_SRCS += mpi_amd64.c mp_comba.c
endif
ifeq ($(CPU_ARCH),x86)
    ASFILES  = mpi_x86.s
    DEFINES += -DMP_ASSEMBLY_MULTIPLY -DMP_ASSEMBLY_SQUARE
    DEFINES += -DMP_ASSEMBLY_DIV_2DX1D -DMP_USE_UINT_DIGIT
endif
ifeq ($(CPU_ARCH),arm)
    DEFINES += -DMP_ASSEMBLY_MULTIPLY -DMP_ASSEMBLY_SQUARE
    DEFINES += -DMP_USE_UINT_DIGIT
    DEFINES += -DSHA_NO_LONG_LONG # avoid 64-bit arithmetic in SHA512
    MPI_SRCS += mpi_arm.c
endif
ifeq ($(CPU_ARCH),ppc)
    ifeq ($(LITTLE_ENDIAN),1)
        DEFINES += -DHAVE_PLATFORM_GHASH
    endif
    EXTRA_SRCS += ghash-ppc.c
ifdef USE_64
    DEFINES += -DNSS_NO_INIT_SUPPORT
    PPC_ABI := $(shell $(CC) -dM -E - < /dev/null | awk '$$2 == "_CALL_ELF" {print $$3}')
    ifeq ($(PPC_ABI),2)
        ASFILES += sha512-p8.s
    ifeq ($(OS_TEST),ppc64le)
        EXTRA_SRCS += chacha20poly1305-ppc.c ppc-gcm-wrap.c
        ASFILES += chacha20-ppc64le.s ppc-gcm.s
        DEFINES += -DHAVE_PLATFORM_GCM
    endif # ppc64le
    endif
endif # USE_64
endif # ppc
endif # Linux

ifeq ($(OS_TARGET),AIX)
    DEFINES += -DMP_USE_UINT_DIGIT
    ifndef USE_64
	DEFINES += -DMP_NO_DIV_WORD -DMP_NO_ADD_WORD -DMP_NO_SUB_WORD
    endif
endif # AIX

ifeq ($(OS_TARGET), HP-UX)
ifneq ($(OS_TEST), ia64)
# PA-RISC
ASFILES += ret_cr16.s
ifndef USE_64
    FREEBL_BUILD_SINGLE_SHLIB =
    HAVE_ABI32_INT32 = 1
    HAVE_ABI32_FPU = 1
endif
ifdef FREEBL_CHILD_BUILD
ifdef USE_ABI32_INT32
# build for DA1.1 (HP PA 1.1) 32-bit ABI build with 32-bit arithmetic
    DEFINES  += -DMP_USE_UINT_DIGIT -DMP_NO_MP_WORD
    DEFINES += -DSHA_NO_LONG_LONG # avoid 64-bit arithmetic in SHA512
else
ifdef USE_64
# this builds for DA2.0W (HP PA 2.0 Wide), the LP64 ABI, using 64-bit digits
    MPI_SRCS += mpi_hp.c
    ASFILES  += hpma512.s hppa20.s
    DEFINES  += -DMP_ASSEMBLY_MULTIPLY -DMP_ASSEMBLY_SQUARE
else
# this builds for DA2.0 (HP PA 2.0 Narrow) ABI32_FPU model
# (the 32-bit ABI with 64-bit registers) using 64-bit digits
    MPI_SRCS += mpi_hp.c
    ASFILES  += hpma512.s hppa20.s
    DEFINES  += -DMP_ASSEMBLY_MULTIPLY -DMP_ASSEMBLY_SQUARE
ifndef NS_USE_GCC
    ARCHFLAG = -Aa +e +DA2.0 +DS2.0
endif
endif
endif
endif
endif
endif

# The blapi functions are defined not only in the freebl shared
# libraries but also in the shared libraries linked with loader.c
# (libsoftokn3.so and libssl3.so).  We need to use GNU ld's
# -Bsymbolic option or the equivalent option for other linkers
# to bind the blapi function references in FREEBLVector vector
# (ldvector.c) to the blapi functions defined in the freebl
# shared libraries.
ifeq (,$(filter-out FreeBSD Linux NetBSD OpenBSD, $(OS_TARGET)))
    MKSHLIB += -Wl,-Bsymbolic
endif

ifeq ($(OS_TARGET),SunOS)

ifdef NS_USE_GCC
    ifdef GCC_USE_GNU_LD
	MKSHLIB += -Wl,-Bsymbolic,-z,now,-z,text
    else
	MKSHLIB += -Wl,-B,symbolic,-z,now,-z,text
    endif # GCC_USE_GNU_LD
else
    MKSHLIB += -B symbolic -z now -z text
endif # NS_USE_GCC

# Sun's WorkShop defines v8, v8plus and v9 architectures.
# gcc on Solaris defines v8 and v9 "cpus".
# gcc's v9 is equivalent to Workshop's v8plus.
# gcc's -m64 is equivalent to Workshop's v9
# We always use Sun's assembler, which uses Sun's naming convention.
ifeq ($(CPU_ARCH),sparc)
    FREEBL_BUILD_SINGLE_SHLIB=
    ifdef USE_64
        HAVE_ABI64_INT = 1
        HAVE_ABI64_FPU = 1
    else
        HAVE_ABI32_FPU = 1
        HAVE_ABI32_INT64 = 1
    endif
    SYSV_SPARC = 1
    SOLARIS_AS = /usr/ccs/bin/as
    #### set arch, asm, c flags
    ifdef NS_USE_GCC
	ifdef USE_ABI32_INT64
	    ARCHFLAG=-mcpu=v9 -Wa,-xarch=v8plus
	    SOLARIS_AS_FLAGS = -xarch=v8plus -K PIC
	endif
	ifdef USE_ABI32_FPU
	    ARCHFLAG=-mcpu=v9 -Wa,-xarch=v8plusa
	    SOLARIS_AS_FLAGS = -xarch=v8plusa -K PIC
	endif # USE_ABI32_FPU
	ifdef USE_ABI64_INT
	    # this builds for Sparc v9a pure 64-bit architecture
	    ARCHFLAG += -mcpu=v9 -Wa,-xarch=v9
	    SOLARIS_AS_FLAGS = -xarch=v9 -K PIC
	endif
	ifdef USE_ABI64_FPU
	    # this builds for Sparc v9a pure 64-bit architecture
	    # It uses floating point, and 32-bit word size
	    ARCHFLAG += -mcpu=v9 -Wa,-xarch=v9a
	    SOLARIS_AS_FLAGS = -xarch=v9a -K PIC
	endif
    else # NS_USE_GCC
	# FPU_TARGET_OPTIMIZER specifies the target processor and cache
	# properties of the ABI32_FPU and ABI64_FPU architectures for use
	# by the optimizer.
	ifeq (,$(findstring Sun WorkShop 6,$(shell $(CC) -V 2>&1)))
	    # if the compiler is not Forte 6
	    FPU_TARGET_OPTIMIZER = -xcache=64/32/4:1024/64/4 -xchip=ultra3
	else
	    # Forte 6 C compiler generates incorrect code for rijndael.c
	    # if -xchip=ultra3 is used (Bugzilla bug 333925).  So we revert
	    # to what we used in NSS 3.10.
	    FPU_TARGET_OPTIMIZER = -xchip=ultra2
	endif
	ifdef USE_ABI32_INT64
	    # this builds for Sparc v8+a ABI32_FPU architecture, 64-bit registers,
	    # 32-bit ABI, it uses 64-bit words, integer arithmetic,
	    # no FPU (non-VIS cpus).
	    # These flags were suggested by the compiler group for building
	    # with SunStudio 10.
	    ifdef BUILD_OPT
                SOL_CFLAGS += -xO4
	    endif
		SOL_CFLAGS += -xtarget=generic
	    ARCHFLAG = -xarch=v8plus
	    SOLARIS_AS_FLAGS = -xarch=v8plus -K PIC
	endif
	ifdef USE_ABI32_FPU
	    # this builds for Sparc v8+a ABI32_FPU architecture, 64-bit registers,
	    # 32-bit ABI, it uses FPU code, and 32-bit word size.
	    # these flags were determined by running cc -### -fast and copying
	    # the generated flag settings
	    SOL_CFLAGS += -fsingle -xmemalign=8s
	    ifdef BUILD_OPT
                SOL_CFLAGS += -D__MATHERR_ERRNO_DONTCARE -fsimple=1
                SOL_CFLAGS += -xalias_level=basic -xbuiltin=%all
                SOL_CFLAGS += $(FPU_TARGET_OPTIMIZER) -xdepend
                SOL_CFLAGS += -xlibmil -xO5
	    endif
	    ARCHFLAG = -xarch=v8plusa
	    SOLARIS_AS_FLAGS = -xarch=v8plusa -K PIC
	endif
	ifdef USE_ABI64_INT
	    # this builds for Sparc v9a pure 64-bit architecture,
	    # no FPU (non-VIS cpus). For building with SunStudio 10.
	    ifdef BUILD_OPT
                SOL_CFLAGS += -xO4
	    endif
		SOL_CFLAGS += -xtarget=generic
	    ARCHFLAG = -xarch=v9
	    SOLARIS_AS_FLAGS = -xarch=v9 -K PIC
	endif
	ifdef USE_ABI64_FPU
	    # this builds for Sparc v9a pure 64-bit architecture
	    # It uses floating point, and 32-bit word size.
	    # See comment for USE_ABI32_FPU.
	    SOL_CFLAGS += -fsingle -xmemalign=8s
	    ifdef BUILD_OPT
                SOL_CFLAGS += -D__MATHERR_ERRNO_DONTCARE -fsimple=1
                SOL_CFLAGS += -xalias_level=basic -xbuiltin=%all
                SOL_CFLAGS += $(FPU_TARGET_OPTIMIZER) -xdepend
                SOL_CFLAGS += -xlibmil -xO5
	    endif
	    ARCHFLAG = -xarch=v9a
	    SOLARIS_AS_FLAGS = -xarch=v9a -K PIC
	endif
    endif # NS_USE_GCC

    ### set flags for both GCC and Sun cc
    ifdef USE_ABI32_INT64
	# this builds for Sparc v8+a ABI32_FPU architecture, 64-bit registers,
	# 32-bit ABI, it uses 64-bit words, integer arithmetic, no FPU
	# best times are with no MP_ flags specified
    endif
    ifdef USE_ABI32_FPU
	# this builds for Sparc v8+a ABI32_FPU architecture, 64-bit registers,
	# 32-bit ABI, it uses FPU code, and 32-bit word size
	MPI_SRCS += mpi_sparc.c
	ASFILES  = mpv_sparcv8.s montmulfv8.s
	DEFINES  += -DMP_NO_MP_WORD -DMP_USE_UINT_DIGIT -DMP_ASSEMBLY_MULTIPLY
	DEFINES  += -DMP_USING_MONT_MULF -DMP_MONT_USE_MP_MUL
    endif
    ifdef USE_ABI64_INT
	# this builds for Sparc v9a pure 64-bit architecture
	# best times are with no MP_ flags specified
    endif
    ifdef USE_ABI64_FPU
	# this builds for Sparc v9a pure 64-bit architecture
	# It uses floating point, and 32-bit word size
	MPI_SRCS += mpi_sparc.c
	ASFILES   = mpv_sparcv9.s montmulfv9.s
	DEFINES  += -DMP_NO_MP_WORD -DMP_USE_UINT_DIGIT -DMP_ASSEMBLY_MULTIPLY
	DEFINES  += -DMP_USING_MONT_MULF -DMP_MONT_USE_MP_MUL
    endif

else
    # Solaris for non-sparc family CPUs
    ifdef NS_USE_GCC
	LD = gcc
	AS = gcc
	ASFLAGS = -x assembler-with-cpp
    endif
    ifeq ($(USE_64),1)
	# Solaris for AMD64
	ifdef NS_USE_GCC
	    ASFILES  = arcfour-amd64-gas.s mpi_amd64_common.s
	    ASFLAGS += -march=opteron -m64 -fPIC
	    MPI_SRCS += mp_comba.c
	    # comment the next four lines to turn off Intel HW acceleration
	    ASFILES += intel-gcm.s
	    EXTRA_SRCS += intel-gcm-wrap.c
	    DEFINES += -DHAVE_PLATFORM_GCM
	    INTEL_GCM = 1
	else
	    ASFILES  = arcfour-amd64-sun.s mpi_amd64_sun.s sha-fast-amd64-sun.s
		ASFILES += mp_comba_amd64_sun.s mpcpucache_amd64.s
	    ASFLAGS += -xarch=generic64 -K PIC
            SOL_CFLAGS += -xprefetch=no
	    SHA_SRCS =
		MPCPU_SRCS =
	    # Intel acceleration for GCM does not build currently with Studio
	endif
	DEFINES += -DNSS_BEVAND_ARCFOUR -DMPI_AMD64 -DMP_ASSEMBLY_MULTIPLY
	DEFINES += -DNSS_USE_COMBA
	# comment the next two lines to turn off Intel HW acceleration
	DEFINES += -DUSE_HW_AES
	ASFILES += intel-aes.s
	MPI_SRCS += mpi_amd64.c
    else
	# Solaris x86
	DEFINES += -DMP_USE_UINT_DIGIT
	DEFINES += -DMP_ASSEMBLY_MULTIPLY -DMP_ASSEMBLY_SQUARE
	DEFINES += -DMP_ASSEMBLY_DIV_2DX1D
	ASFILES  = mpi_i86pc.s
	ifndef NS_USE_GCC
		MPCPU_SRCS =
		ASFILES += mpcpucache_x86.s
	endif
    endif
endif # Solaris for non-sparc family CPUs
endif # target == SunO

ifdef USE_64
# no __int128 at least up to lcc 1.23 (pretending to be gcc5)
# NB: CC_NAME is not defined here
ifneq ($(shell $(CC) -? 2>&1 >/dev/null </dev/null | sed -e 's/:.*//;1q'),lcc)
    ifdef CC_IS_CLANG
            HAVE_INT128_SUPPORT = 1
            DEFINES += -DHAVE_INT128_SUPPORT
    else ifeq (1,$(CC_IS_GCC))
        ifneq (,$(filter 4.6 4.7 4.8 4.9,$(word 1,$(GCC_VERSION)).$(word 2,$(GCC_VERSION))))
            HAVE_INT128_SUPPORT = 1
            DEFINES += -DHAVE_INT128_SUPPORT
        endif
        ifneq (,$(filter 0 1 2 3 4,$(word 1,$(GCC_VERSION))))
            NSS_DISABLE_AVX2 = 1
        endif
        ifeq (,$(filter 0 1 2 3 4,$(word 1,$(GCC_VERSION))))
            HAVE_INT128_SUPPORT = 1
            DEFINES += -DHAVE_INT128_SUPPORT
        endif
        ifeq (,$(filter 0 1 2 3 4,$(word 1,$(GCC_VERSION))))
            NSS_DISABLE_SSE3 = 1
            NSS_DISABLE_SSE4_1 = 1
            NSS_DISABLE_SSE4_2 = 1
        endif
    endif
endif # lcc
endif # USE_64

ifndef HAVE_INT128_SUPPORT
    DEFINES += -DKRML_VERIFIED_UINT128
endif

ifndef NSS_DISABLE_CHACHAPOLY
    ifeq ($(CPU_ARCH),x86_64)
        ifndef NSS_DISABLE_AVX2
            EXTRA_SRCS += Hacl_Poly1305_256.c Hacl_Chacha20_Vec256.c Hacl_Chacha20Poly1305_256.c
            DEFINES += -DHACL_CAN_COMPILE_VEC256
        endif # NSS_DISABLE_AVX2
        ifndef NSS_DISABLE_SSE3
            EXTRA_SRCS += Hacl_Poly1305_128.c Hacl_Chacha20_Vec128.c Hacl_Chacha20Poly1305_128.c
            DEFINES += -DHACL_CAN_COMPILE_VEC128 
        endif
    endif # x86_64
    ifeq ($(CPU_ARCH),aarch64)
        EXTRA_SRCS += Hacl_Poly1305_128.c Hacl_Chacha20_Vec128.c Hacl_Chacha20Poly1305_128.c
        DEFINES += -DHACL_CAN_COMPILE_VEC128
    endif # aarch64

    VERIFIED_SRCS += Hacl_Poly1305_32.c Hacl_Chacha20.c Hacl_Chacha20Poly1305_32.c
endif # NSS_DISABLE_CHACHAPOLY

VERIFIED_SRCS += Hacl_Hash_SHA3.c Hacl_P256.c Hacl_P384.c Hacl_P521.c
VERIFIED_SRCS += Hacl_Ed25519.c
VERIFIED_SRCS += Hacl_Curve25519_51.c

# Vendored libcrux ML-KEM + ML-DSA combined extraction (see lib/freebl/libcrux/).
# These share one SHA-3/combined_core and are compiled with an isolated include
# scope (see below) so their krml/eurydice headers don't collide with the HACL*
# extraction in verified/.
LIBCRUX_SRCS = combined_core.c libcrux_ct_ops.c libcrux_sha3_portable.c \
               libcrux_mlkem_core.c libcrux_mlkem_portable.c \
               libcrux_mlkem512_portable.c libcrux_mlkem768_portable.c \
               libcrux_mlkem1024_portable.c \
               libcrux_mldsa_core.c libcrux_mldsa_portable.c \
               libcrux_mldsa44_portable.c libcrux_mldsa65_portable.c \
               libcrux_mldsa87_portable.c
# Bug 2067434 - restrict these to the child build that compiles the real
# library. The parent (loader-only) and lowhash stub builds reset CSRCS to their
# single source file, and appending here bypasses that. The stub build shares
# its OBJDIR with the real single-shlib build, so under make -jN both would
# compile these to the same object files at once, producing truncated objects.
ifdef FREEBL_CHILD_BUILD
ifndef USE_STUB_BUILD
CSRCS += $(LIBCRUX_SRCS)
endif
endif

# Bug 1918767 / Bug 1918711 - by setting KRML_MUSTINLINE=inline here, we
# avoid it being defined to `inline __forceinline` (for msvc) or `inline
# __attribute__((always_inline))` (for gcc/clang) in
# verified/karamel/include/krml/internal/target.h. These other
# configurations can cause excessive stack usage.
DEFINES += -DKRML_MUSTINLINE=inline

ifeq (,$(filter-out x86_64 aarch64,$(CPU_ARCH)))
    # All 64-bit architectures get the 64 bit version.
    ECL_SRCS += curve25519_64.c
else
    # All other architectures get the generic 32 bit implementation
    ECL_SRCS += curve25519_32.c
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


rijndael_tables:
	$(CC) -o $(OBJDIR)/make_rijndael_tab rijndael_tables.c \
	         $(DEFINES) $(INCLUDES) $(OBJDIR)/libfreebl.a
	$(OBJDIR)/make_rijndael_tab

vpath %.h mpi ecl verified deprecated
vpath %.c mpi ecl verified deprecated libcrux
vpath %.S mpi ecl
vpath %.s mpi ecl
vpath %.asm mpi ecl
INCLUDES += -Impi -Iecl -Iverified -Iverified/internal -Iverified/karamel/include -Iverified/karamel/krmllib/dist/minimal -Ideprecated

# The vendored libcrux objects and the freebl wrappers that use them (kyber.c,
# ml_dsa.c) are compiled with the libcrux/ include dirs searched FIRST, so their
# krml/eurydice headers resolve to the libcrux copies rather than the HACL*
# copies under verified/.
LIBCRUX_OBJS = $(addprefix $(OBJDIR)/$(PROG_PREFIX),$(LIBCRUX_SRCS:.c=$(OBJ_SUFFIX)))
LIBCRUX_USERS = $(LIBCRUX_OBJS) \
                $(OBJDIR)/$(PROG_PREFIX)kyber$(OBJ_SUFFIX) \
                $(OBJDIR)/$(PROG_PREFIX)ml_dsa$(OBJ_SUFFIX)
$(LIBCRUX_USERS): INCLUDES := -Ilibcrux -Ilibcrux/internal -Ilibcrux/karamel $(INCLUDES)

ifeq ($(OS_TARGET),WINNT)
# C4146: unary minus applied to an unsigned type. eurydice_glue.h does this
# deliberately in its wrapping_neg helpers, where the wrapped result is the
# intended value. The extraction is kept byte-identical to upstream (CI diffs
# it), so silence the warning here rather than patching the header.
$(LIBCRUX_USERS): CFLAGS += -wd4146
endif


DEFINES += -DMP_API_COMPATIBLE

MPI_USERS = secmpi.c dh.c pqg.c dsa.c rsa.c ec.c

MPI_OBJS = $(addprefix $(OBJDIR)/$(PROG_PREFIX), $(MPI_SRCS:.c=$(OBJ_SUFFIX)))
MPI_OBJS += $(addprefix $(OBJDIR)/$(PROG_PREFIX), $(MPI_USERS:.c=$(OBJ_SUFFIX)))

$(MPI_OBJS): $(MPI_HDRS)

ECL_USERS = ec.c

ECL_OBJS = $(addprefix $(OBJDIR)/$(PROG_PREFIX), $(ECL_SRCS:.c=$(OBJ_SUFFIX)) $(ECL_ASM_SRCS:$(ASM_SUFFIX)=$(OBJ_SUFFIX)))
ECL_OBJS += $(addprefix $(OBJDIR)/$(PROG_PREFIX), $(ECL_USERS:.c=$(OBJ_SUFFIX)))

$(ECL_OBJS): $(ECL_HDRS)

$(OBJDIR)/sysrand$(OBJ_SUFFIX): sysrand.c unix_rand.c win_rand.c

$(OBJDIR)/$(PROG_PREFIX)mpprime$(OBJ_SUFFIX): primes.c

$(OBJDIR)/ldvector$(OBJ_SUFFIX) $(OBJDIR)/loader$(OBJ_SUFFIX) : loader.h

ifeq ($(SYSV_SPARC),1)

$(OBJDIR)/mpv_sparcv8.o $(OBJDIR)/mpv_sparcv8x.o $(OBJDIR)/montmulfv8.o : $(OBJDIR)/%.o : %.s | $$(@D)/d
	$(SOLARIS_AS) -o $@ $(SOLARIS_AS_FLAGS) $<

$(OBJDIR)/mpv_sparcv9.o $(OBJDIR)/montmulfv9.o : $(OBJDIR)/%.o : %.s | $$(@D)/d
	$(SOLARIS_AS) -o $@ $(SOLARIS_AS_FLAGS) $<

$(OBJDIR)/mpmontg.o: mpmontg.c montmulf.h

endif

ifndef FREEBL_CHILD_BUILD

# Parent build. This is where we decide which shared libraries to build

# too suppress the SINGLE_SHLIB override warning
FREEBL_OBJDIRS :=

define target_freebl_SHLIB
ifdef $(2)
$(1)_DIR = $$(OBJDIR)/$$(OS_TARGET)_$(1)
ALL_TRASH += $$($(1)_DIR)

ifeq (,$$(filter $$($(1)_DIR)/d,$$(FREEBL_OBJDIRS)))
FREEBL_OBJDIRS += $$($(1)_DIR)/d
endif

release_md:: freebl_$(2)
libs: freebl_$(2)
freebl_$(2): | $$($(1)_DIR)/d
	$$(MAKE) FREEBL_CHILD_BUILD=1 $(3)=1 OBJDIR=$$($(1)_DIR) libs
endif
endef # target_freebl_SHLIB

target_freebl_ABI = $(call target_freebl_SHLIB,$(1),HAVE_$(1),USE_$(1))

$(eval $(call target_freebl_SHLIB,SINGLE_SHLIB,FREEBL_BUILD_SINGLE_SHLIB,NEEDED_DUMMY))
$(eval $(call target_freebl_SHLIB,SINGLE_SHLIB,NEED_STUB_BUILD,USE_STUB_BUILD))

$(eval $(call target_freebl_ABI,ABI32_FPU))
$(eval $(call target_freebl_ABI,ABI32_INT32))
$(eval $(call target_freebl_ABI,ABI32_INT64))
$(eval $(call target_freebl_ABI,ABI64_FPU))
$(eval $(call target_freebl_ABI,ABI64_INT))

endif  # FREEBL_CHILD_BUILD


# Bugzilla Bug 333917: the non-x86 code in desblapi.c seems to violate
# ANSI C's strict aliasing rules.
ifeq ($(OS_TARGET),Linux)
ifneq ($(CPU_ARCH),x86)
$(OBJDIR)/$(PROG_PREFIX)desblapi$(OBJ_SUFFIX): desblapi.c | $$(@D)/d
ifdef NEED_ABSOLUTE_PATH
	$(CC) -o $@ -c $(CFLAGS) -fno-strict-aliasing $(call core_abspath,$<)
else
	$(CC) -o $@ -c $(CFLAGS) -fno-strict-aliasing $<
endif
endif
endif

ifdef INTEL_GCM
#
# GCM binary needs -mssse3
#
$(OBJDIR)/$(PROG_PREFIX)intel-gcm-wrap$(OBJ_SUFFIX): CFLAGS += -mssse3
endif

$(OBJDIR)/$(PROG_PREFIX)sha256-x86$(OBJ_SUFFIX): CFLAGS += -msha -mssse3 -msse4.1

ifeq ($(CPU_ARCH),arm)
# When the compiler uses the softfloat ABI, we want to use the compatible softfp ABI when
# enabling NEON for these objects.
# Confusingly, __SOFTFP__ is the name of the define for the softfloat ABI, not for the softfp ABI.
USES_SOFTFLOAT_ABI := $(shell $(CC) -o - -E -dM - $(CFLAGS) < /dev/null | grep __SOFTFP__ > /dev/null && echo 1)
$(OBJDIR)/$(PROG_PREFIX)aes-armv8$(OBJ_SUFFIX): CFLAGS += -march=armv8-a -mfpu=crypto-neon-fp-armv8$(if $(USES_SOFTFLOAT_ABI), -mfloat-abi=softfp)
$(OBJDIR)/$(PROG_PREFIX)sha1-armv8$(OBJ_SUFFIX): CFLAGS += -march=armv8-a -mfpu=crypto-neon-fp-armv8$(if $(USES_SOFTFLOAT_ABI), -mfloat-abi=softfp)
$(OBJDIR)/$(PROG_PREFIX)sha256-armv8$(OBJ_SUFFIX): CFLAGS += -march=armv8-a -mfpu=crypto-neon-fp-armv8$(if $(USES_SOFTFLOAT_ABI), -mfloat-abi=softfp)
ifndef NSS_DISABLE_ARM32_NEON
$(OBJDIR)/$(PROG_PREFIX)ghash-arm32-neon$(OBJ_SUFFIX): CFLAGS += -mfpu=neon$(if $(USES_SOFTFLOAT_ABI), -mfloat-abi=softfp)
endif
endif

ifeq ($(CPU_ARCH),aarch64)
$(OBJDIR)/$(PROG_PREFIX)aes-armv8$(OBJ_SUFFIX): CFLAGS += -march=armv8-a+crypto
$(OBJDIR)/$(PROG_PREFIX)ghash-aarch64$(OBJ_SUFFIX): CFLAGS += -march=armv8-a+crypto
$(OBJDIR)/$(PROG_PREFIX)sha1-armv8$(OBJ_SUFFIX): CFLAGS += -march=armv8-a+crypto
$(OBJDIR)/$(PROG_PREFIX)sha256-armv8$(OBJ_SUFFIX): CFLAGS += -march=armv8-a+crypto
endif

ifdef AARCH64_GCM
# Compile the preprocessed .S assembly with an explicit rule. The generic
# pattern rules for .s and .S share the same target pattern, so leaving these
# to implicit-rule resolution lets make chain through a generated .s file and
# (with some make versions) delete the .S source as an intermediate.
$(addprefix $(OBJDIR)/$(PROG_PREFIX), $(AARCH64_GCM_ASFILES:.S=$(OBJ_SUFFIX))): $(OBJDIR)/$(PROG_PREFIX)%$(OBJ_SUFFIX): %.S | $$(@D)/d
	$(CC) -o $@ -c $(CFLAGS) -march=armv8-a+crypto $<
$(OBJDIR)/$(PROG_PREFIX)aarch64-gcm-wrap$(OBJ_SUFFIX): CFLAGS += -march=armv8-a+crypto
endif

ifeq ($(CPU_ARCH),ppc)
$(OBJDIR)/$(PROG_PREFIX)sha512$(OBJ_SUFFIX): CFLAGS += -funroll-loops -fpeel-loops
ifneq ($(NSS_DISABLE_ALTIVEC),1)
$(OBJDIR)/$(PROG_PREFIX)ghash-ppc$(OBJ_SUFFIX): CFLAGS += -maltivec
$(OBJDIR)/$(PROG_PREFIX)gcm$(OBJ_SUFFIX): CFLAGS += -maltivec
$(OBJDIR)/$(PROG_PREFIX)rijndael$(OBJ_SUFFIX): CFLAGS += -maltivec
$(OBJDIR)/$(PROG_PREFIX)sha512$(OBJ_SUFFIX): CFLAGS += -maltivec
$(OBJDIR)/$(PROG_PREFIX)chacha20poly1305-ppc$(OBJ_SUFFIX): CFLAGS += -maltivec
endif
ifneq ($(NSS_DISABLE_CRYPTO_VSX),1)
$(OBJDIR)/$(PROG_PREFIX)ghash-ppc$(OBJ_SUFFIX): CFLAGS += -mcrypto -mvsx
$(OBJDIR)/$(PROG_PREFIX)gcm$(OBJ_SUFFIX): CFLAGS += -mcrypto -mvsx
$(OBJDIR)/$(PROG_PREFIX)rijndael$(OBJ_SUFFIX): CFLAGS += -mcrypto -mvsx
$(OBJDIR)/$(PROG_PREFIX)sha512$(OBJ_SUFFIX): CFLAGS += -mcrypto -mvsx
$(OBJDIR)/$(PROG_PREFIX)chacha20poly1305-ppc$(OBJ_SUFFIX): CFLAGS += -mcrypto -mvsx
endif
endif

ifeq ($(CPU_ARCH),x86_64)
$(OBJDIR)/$(PROG_PREFIX)Hacl_Chacha20_Vec128$(OBJ_SUFFIX): CFLAGS += -mssse3 -msse4.1 -msse4.2 -mavx
$(OBJDIR)/$(PROG_PREFIX)Hacl_Chacha20Poly1305_128$(OBJ_SUFFIX): CFLAGS += -mssse3 -msse4.1 -msse4.2 -mavx
$(OBJDIR)/$(PROG_PREFIX)Hacl_Poly1305_128$(OBJ_SUFFIX): CFLAGS += -mssse3 -msse4.1 -msse4.2 -mavx

ifndef NSS_DISABLE_AVX2
$(OBJDIR)/$(PROG_PREFIX)Hacl_Chacha20Poly1305_256$(OBJ_SUFFIX): CFLAGS += -mssse3 -msse4.1 -msse4.2 -mavx2
$(OBJDIR)/$(PROG_PREFIX)Hacl_Chacha20_Vec256$(OBJ_SUFFIX): CFLAGS += -mssse3 -msse4.1 -msse4.2 -mavx -mavx2
$(OBJDIR)/$(PROG_PREFIX)Hacl_Poly1305_256$(OBJ_SUFFIX): CFLAGS += -mssse3 -msse4.1 -msse4.2 -mavx -mavx2
endif
endif # x86_64
