#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
#

ifndef topsrcdir
$(error topsrcdir was not set))
endif

# Define an include-at-most-once flag
ifdef INCLUDED_RULES_MK
$(error Do not include rules.mk twice!)
endif
INCLUDED_RULES_MK = 1

ifndef INCLUDED_CONFIG_MK
include $(topsrcdir)/config/config.mk
endif

USE_AUTOTARGETS_MK = 1
include $(MOZILLA_DIR)/config/makefiles/makeutils.mk

ifdef REBUILD_CHECK
REPORT_BUILD = $(info $(shell $(PYTHON3) $(MOZILLA_DIR)/config/rebuild_check.py $@ $?))
REPORT_BUILD_VERBOSE = $(REPORT_BUILD)
else
REPORT_BUILD = $(info $(relativesrcdir)/$(notdir $@))

ifdef BUILD_VERBOSE_LOG
REPORT_BUILD_VERBOSE = $(REPORT_BUILD)
else
REPORT_BUILD_VERBOSE = $(call BUILDSTATUS,BUILD_VERBOSE $(relativesrcdir))
endif

endif

EXEC			= exec

################################################################################
# Testing frameworks support
################################################################################

ifdef ENABLE_TESTS
ifdef CPP_UNIT_TESTS
ifdef COMPILE_ENVIRONMENT

# Compile the tests to $(DIST)/bin.  Make lots of niceties available by default
# through TestHarness.h, by modifying the list of includes and the libs against
# which stuff links.
SIMPLE_PROGRAMS += $(CPP_UNIT_TESTS)

ifndef MOZ_PROFILE_GENERATE
CPP_UNIT_TESTS_FILES = $(CPP_UNIT_TESTS)
CPP_UNIT_TESTS_DEST = $(DIST)/cppunittests
CPP_UNIT_TESTS_TARGET = target
INSTALL_TARGETS += CPP_UNIT_TESTS
endif

endif # COMPILE_ENVIRONMENT
endif # CPP_UNIT_TESTS
endif # ENABLE_TESTS


#
# Library rules
#

ifdef FORCE_SHARED_LIB
ifdef MKSHLIB

ifdef LIB_IS_C_ONLY
MKSHLIB			= $(MKCSHLIB)
endif

endif # MKSHLIB
endif # FORCE_SHARED_LIB

ifeq ($(OS_ARCH),WINNT)

#
# This next line captures both the default (non-MOZ_COPY_PDBS)
# case as well as the MOZ_COPY_PDBS-for-mingwclang case.
#
# For the default case, placing the pdb in the build
# directory is needed.
#
# For the MOZ_COPY_PDBS, non-mingwclang case - we need to
# build the pdb next to the executable (handled in the if
# statement immediately below.)
#
# For the MOZ_COPY_PDBS, mingwclang case - we also need to
# build the pdb next to the executable, but this macro doesn't
# work for jsapi-tests which is a little special, so we specify
# the output directory below with MOZ_PROGRAM_LDFLAGS.
#
LINK_PDBFILE ?= $(basename $(@F)).pdb

ifdef MOZ_COPY_PDBS
ifneq ($(CC_TYPE),clang)
LINK_PDBFILE = $(basename $@).pdb
endif
endif

ifeq ($(CC_TYPE),clang-cl)

ifdef SIMPLE_PROGRAMS
COMPILE_PDB_FLAG ?= -Fd$(basename $(@F)).pdb
COMPILE_CFLAGS += $(COMPILE_PDB_FLAG)
COMPILE_CXXFLAGS += $(COMPILE_PDB_FLAG)
endif

ifdef MOZ_DEBUG
CODFILE=$(basename $(@F)).cod
endif

endif # CC_TYPE == clang-cl
endif # WINNT

ifeq ($(OS_ARCH),WINNT)
ifeq ($(CC_TYPE),clang)
MOZ_PROGRAM_LDFLAGS += -Wl,-pdb,$(dir $@)/$(LINK_PDBFILE)
endif
endif

ifeq ($(HOST_OS_ARCH),WINNT)
HOST_PDBFILE=$(basename $(@F)).pdb
HOST_PDB_FLAG ?= -PDB:$(HOST_PDBFILE)
HOST_C_LDFLAGS += -DEBUG $(HOST_PDB_FLAG)
HOST_CXX_LDFLAGS += -DEBUG $(HOST_PDB_FLAG)
endif

# Don't build SIMPLE_PROGRAMS during the MOZ_PROFILE_GENERATE pass, and do not
# attempt to install them
ifdef MOZ_PROFILE_GENERATE
$(foreach category,$(INSTALL_TARGETS),\
  $(eval $(category)_FILES := $(foreach file,$($(category)_FILES),$(if $(filter $(SIMPLE_PROGRAMS),$(notdir $(file))),,$(file)))))
SIMPLE_PROGRAMS :=
endif

ifdef COMPILE_ENVIRONMENT
ifndef TARGETS
TARGETS			= $(LIBRARY) $(SHARED_LIBRARY) $(PROGRAM) $(SIMPLE_PROGRAMS) $(HOST_PROGRAM) $(HOST_SIMPLE_PROGRAMS) $(HOST_SHARED_LIBRARY)
endif

COBJS = $(notdir $(CSRCS:.c=.$(OBJ_SUFFIX)))
CWASMOBJS = $(notdir $(WASM_CSRCS:.c=.$(WASM_OBJ_SUFFIX)))
SOBJS = $(notdir $(SSRCS:.S=.$(OBJ_SUFFIX)))
# CPPSRCS can have different extensions (eg: .cpp, .cc)
CPPOBJS = $(notdir $(addsuffix .$(OBJ_SUFFIX),$(basename $(CPPSRCS))))
CPPWASMOBJS = $(notdir $(addsuffix .$(WASM_OBJ_SUFFIX),$(basename $(WASM_CPPSRCS))))
CMOBJS = $(notdir $(CMSRCS:.m=.$(OBJ_SUFFIX)))
CMMOBJS = $(notdir $(CMMSRCS:.mm=.$(OBJ_SUFFIX)))
# ASFILES can have different extensions (.s, .asm)
ASOBJS = $(notdir $(addsuffix .$(OBJ_SUFFIX),$(basename $(ASFILES))))
ifndef OBJS
_OBJS = $(COBJS) $(SOBJS) $(CPPOBJS) $(CMOBJS) $(CMMOBJS) $(ASOBJS) $(CWASMOBJS) $(CPPWASMOBJS)
OBJS = $(strip $(_OBJS))
endif

HOST_COBJS = $(addprefix host_,$(notdir $(HOST_CSRCS:.c=.$(OBJ_SUFFIX))))
# HOST_CPPOBJS can have different extensions (eg: .cpp, .cc)
HOST_CPPOBJS = $(addprefix host_,$(notdir $(addsuffix .$(OBJ_SUFFIX),$(basename $(HOST_CPPSRCS)))))
ifndef HOST_OBJS
_HOST_OBJS = $(HOST_COBJS) $(HOST_CPPOBJS)
HOST_OBJS = $(strip $(_HOST_OBJS))
endif
else
LIBRARY :=
SHARED_LIBRARY :=
IMPORT_LIBRARY :=
REAL_LIBRARY :=
PROGRAM :=
SIMPLE_PROGRAMS :=
HOST_SHARED_LIBRARY :=
HOST_PROGRAM :=
HOST_SIMPLE_PROGRAMS :=
endif

define SUBMAKE # $(call SUBMAKE,target,directory,static)
+@$(MAKE) $(if $(2),-C $(2)) $(1)

endef # The extra line is important here! don't delete it

define TIER_DIR_SUBMAKE
$(call SUBMAKE,$(4),$(3),$(5))

endef # Ths empty line is important.

ifneq (,$(strip $(DIRS)))
LOOP_OVER_DIRS = \
  $(foreach dir,$(DIRS),$(call SUBMAKE,$@,$(dir)))
endif

#
# Now we can differentiate between objects used to build a library, and
# objects used to build an executable in the same directory.
#
ifndef PROGOBJS
PROGOBJS		= $(OBJS)
endif

ifndef HOST_PROGOBJS
HOST_PROGOBJS		= $(HOST_OBJS)
endif

#
# Tags: emacs (etags), vi (ctags)
# TAG_PROGRAM := ctags -L -
#
TAG_PROGRAM		= xargs etags -a

#
# MacOS X specific stuff
#

ifeq ($(OS_ARCH),Darwin)
ifneq (,$(SHARED_LIBRARY))
_LOADER_PATH := @rpath
EXTRA_DSO_LDOPTS	+= -dynamiclib -install_name $(_LOADER_PATH)/$(@F) -compatibility_version 1 -current_version 1
endif
endif

#
# MINGW32
#
ifeq ($(OS_ARCH),WINNT)
ifneq ($(CC_TYPE),clang-cl)
DSO_LDOPTS += -Wl,--out-implib -Wl,$(IMPORT_LIBRARY)
endif
endif

ifeq ($(USE_TVFS),1)
IFLAGS1 = -rb
IFLAGS2 = -rb
else
IFLAGS1 = -m 644
IFLAGS2 = -m 755
endif

ifeq (clang-cl_WINNT,$(CC_TYPE)_$(OS_ARCH))
OUTOPTION = -Fo# eol
else
OUTOPTION = -o # eol
endif # WINNT && clang-cl

ifeq (,$(CROSS_COMPILE))
HOST_OUTOPTION = $(OUTOPTION)
else
# Windows-to-Windows cross compiles should always use MSVC-style options for
# host compiles.
ifeq (WINNT_WINNT,$(HOST_OS_ARCH)_$(OS_ARCH))
ifneq (,$(filter-out clang-cl,$(HOST_CC_TYPE)))
$(error MSVC-style compilers should be used for host compilations!)
endif
HOST_OUTOPTION = -Fo# eol
else
HOST_OUTOPTION = -o # eol
endif
endif
################################################################################

# Ensure the build config is up to date. This is done automatically when builds
# are performed through |mach build|. The check here is to catch people not
# using mach. If we ever enforce builds through mach, this code can be removed.
ifndef MOZBUILD_BACKEND_CHECKED
ifndef MACH
ifndef TOPLEVEL_BUILD
BUILD_BACKEND_FILES := $(addprefix $(DEPTH)/backend.,$(addsuffix Backend,$(BUILD_BACKENDS)))
$(DEPTH)/backend.%Backend:
	$(error Build configuration changed. Build with |mach build| or run |mach build-backend| to regenerate build config)

define build_backend_rule
$(1): $$(shell cat $(1).in)

endef
$(foreach file,$(BUILD_BACKEND_FILES),$(eval $(call build_backend_rule,$(file))))

default:: $(BUILD_BACKEND_FILES)

export MOZBUILD_BACKEND_CHECKED=1
endif
endif
endif

# The root makefile doesn't want to do a plain export/libs, because
# of the tiers and because of libxul. Suppress the default rules in favor
# of something else. Makefiles which use this var *must* provide a sensible
# default rule before including rules.mk
default all::
	$(foreach tier,$(TIERS),$(call SUBMAKE,$(tier)))

ifdef BUILD_VERBOSE_LOG
ECHO := echo
QUIET :=
else
ECHO := true
QUIET := -q
endif

# Dependencies which, if modified, should cause everything to rebuild
GLOBAL_DEPS += Makefile $(addprefix $(DEPTH)/config/,$(INCLUDED_AUTOCONF_MK)) $(MOZILLA_DIR)/config/config.mk

ifeq ($(MOZ_WIDGET_TOOLKIT),windows)
# We always build .res files for programs and shared libraries
resfile = $(notdir $1).res
# We also build .res files for simple programs if a corresponding manifest
# exists. We'll generate a .rc file that includes the manifest.
resfile_for_manifest = $(if $(wildcard $(srcdir)/$(notdir $1).manifest),$(call resfile,$1))
else
resfile =
resfile_for_manifest =
endif

##############################################
ifdef COMPILE_ENVIRONMENT
compile:: host target

host:: $(HOST_OBJS) $(HOST_PROGRAM) $(HOST_SIMPLE_PROGRAMS) $(HOST_RUST_PROGRAMS) $(HOST_SHARED_LIBRARY)

target:: $(filter-out $(MOZBUILD_NON_DEFAULT_TARGETS),$(LIBRARY) $(SHARED_LIBRARY) $(PROGRAM) $(SIMPLE_PROGRAMS) $(RUST_PROGRAMS))

ifndef LIBRARY
ifdef OBJS
target:: $(OBJS)
endif
endif

target-objects: $(OBJS) $(PROGOBJS) $(filter-out $(MOZBUILD_NON_DEFAULT_TARGETS),$(RUST_LIBRARY_FILE))
host-objects: $(HOST_OBJS) $(HOST_PROGOBJS) $(HOST_RUST_LIBRARY_FILE)

syms::

include $(MOZILLA_DIR)/config/makefiles/target_binaries.mk
endif

alltags:
	$(RM) TAGS
	find $(topsrcdir) -name dist -prune -o \( -name '*.[hc]' -o -name '*.cp' -o -name '*.cpp' -o -name '*.idl' \) -print | $(TAG_PROGRAM)

define EXPAND_CC_OR_CXX
$(if $(PROG_IS_C_ONLY_$(notdir $(1))),$(CC),$(CCC))
endef

#
# PROGRAM = Foo
# creates OBJS, links with LIBS to create Foo
#
$(PROGRAM): $(PROGOBJS) $(STATIC_LIBS) $(call resfile,$(PROGRAM)) $(GLOBAL_DEPS) $(call mkdir_deps,$(FINAL_TARGET))
	$(REPORT_BUILD)
	$(call BUILDSTATUS,START_Program $(@F))
ifeq (clang-cl_WINNT,$(CC_TYPE)_$(OS_ARCH))
	$(LINKER) -OUT:$@ -PDB:$(LINK_PDBFILE) -IMPLIB:$(basename $(@F)).lib $(WIN32_EXE_LDFLAGS) $(COMPUTED_PROGRAM_LDFLAGS) $(LDFLAGS) $(MOZ_PROGRAM_LDFLAGS) $($(notdir $@)_OBJS) $(filter %.res,$^) $(STATIC_LIBS) $(SHARED_LIBS) $(OS_LIBS)
else # !WINNT || !clang-cl
	$(call EXPAND_CC_OR_CXX,$@) -o $@ $(COMPUTED_CXX_LDFLAGS) $($(notdir $@)_OBJS) $(filter %.res,$^) $(WIN32_EXE_LDFLAGS) $(LDFLAGS) $(STATIC_LIBS) $(COMPUTED_PROGRAM_LDFLAGS) $(MOZ_PROGRAM_LDFLAGS) $(SHARED_LIBS) $(OS_LIBS)
	$(call py_action,check_binary $(@F),$@)
endif # WINNT && clang-cl

ifdef ENABLE_STRIP
	$(STRIP) $(STRIP_FLAGS) $@
endif
	$(call BUILDSTATUS,END_Program $(@F))

$(HOST_PROGRAM): $(HOST_PROGOBJS) $(HOST_LIBS) $(GLOBAL_DEPS) $(call mkdir_deps,$(DEPTH)/dist/host/bin)
	$(REPORT_BUILD)
	$(call BUILDSTATUS,START_Program $(@F))
ifeq (clang-cl_WINNT,$(HOST_CC_TYPE)_$(HOST_OS_ARCH))
	$(HOST_LINKER) -OUT:$@ -PDB:$(HOST_PDBFILE) $($(notdir $@)_OBJS) $(WIN32_EXE_LDFLAGS) $(HOST_LDFLAGS) $(HOST_LINKER_LIBPATHS) $(HOST_LIBS) $(HOST_EXTRA_LIBS)
else
ifeq ($(HOST_CPP_PROG_LINK),1)
	$(HOST_CXX) -o $@ $(HOST_CXX_LDFLAGS) $(HOST_LDFLAGS) $($(notdir $@)_OBJS) $(HOST_LIBS) $(HOST_EXTRA_LIBS)
else
	$(HOST_CC) -o $@ $(HOST_C_LDFLAGS) $(HOST_LDFLAGS) $($(notdir $@)_OBJS) $(HOST_LIBS) $(HOST_EXTRA_LIBS)
endif # HOST_CPP_PROG_LINK
endif
	$(call BUILDSTATUS,END_Program $(@F))

#
# This is an attempt to support generation of multiple binaries
# in one directory, it assumes everything to compile Foo is in
# Foo.o (from either Foo.c or Foo.cpp).
#
# SIMPLE_PROGRAMS = Foo Bar
# creates Foo.o Bar.o, links with LIBS to create Foo, Bar.
#
define simple_program_deps
$1: $(1:$(BIN_SUFFIX)=.$(OBJ_SUFFIX)) $(STATIC_LIBS) $(call resfile_for_manifest,$1) $(GLOBAL_DEPS)
endef
$(foreach p,$(SIMPLE_PROGRAMS),$(eval $(call simple_program_deps,$(p))))

$(SIMPLE_PROGRAMS):
	$(REPORT_BUILD)
	$(call BUILDSTATUS,START_Program $(@F))
ifeq (clang-cl_WINNT,$(CC_TYPE)_$(OS_ARCH))
	$(LINKER) -out:$@ -pdb:$(LINK_PDBFILE) $($@_OBJS) $(filter %.res,$^) $(WIN32_EXE_LDFLAGS) $(COMPUTED_PROGRAM_LDFLAGS) $(LDFLAGS) $(MOZ_PROGRAM_LDFLAGS) $(STATIC_LIBS) $(SHARED_LIBS) $(OS_LIBS)
else
	$(call EXPAND_CC_OR_CXX,$@) $(COMPUTED_CXX_LDFLAGS) -o $@ $($@_OBJS) $(filter %.res,$^) $(WIN32_EXE_LDFLAGS) $(LDFLAGS) $(STATIC_LIBS) $(COMPUTED_PROGRAM_LDFLAGS) $(MOZ_PROGRAM_LDFLAGS) $(SHARED_LIBS) $(OS_LIBS)
	$(call py_action,check_binary $(@F),$@)
endif # WINNT && clang-cl

ifdef ENABLE_STRIP
	$(STRIP) $(STRIP_FLAGS) $@
endif
	$(call BUILDSTATUS,END_Program $(@F))

$(HOST_SIMPLE_PROGRAMS): host_%$(HOST_BIN_SUFFIX): $(HOST_LIBS) $(GLOBAL_DEPS)
	$(REPORT_BUILD)
	$(call BUILDSTATUS,START_Program $(@F))
ifeq (WINNT_clang-cl,$(HOST_OS_ARCH)_$(HOST_CC_TYPE))
	$(HOST_LINKER) -OUT:$@ -PDB:$(HOST_PDBFILE) $($(notdir $@)_OBJS) $(WIN32_EXE_LDFLAGS) $(HOST_LDFLAGS) $(HOST_LINKER_LIBPATHS) $(HOST_LIBS) $(HOST_EXTRA_LIBS)
else
ifeq ($(HOST_CPP_PROG_LINK),1)
	$(HOST_CXX) $(HOST_OUTOPTION)$@ $(HOST_CXX_LDFLAGS) $(HOST_LDFLAGS) $($(notdir $@)_OBJS) $(HOST_LIBS) $(HOST_EXTRA_LIBS)
else
	$(HOST_CC) $(HOST_OUTOPTION)$@ $(HOST_C_LDFLAGS) $(HOST_LDFLAGS) $($(notdir $@)_OBJS) $(HOST_LIBS) $(HOST_EXTRA_LIBS)
endif
endif
	$(call BUILDSTATUS,END_Program $(@F))

$(LIBRARY): $(OBJS) $(STATIC_LIBS) $(GLOBAL_DEPS)
	$(REPORT_BUILD)
	$(call BUILDSTATUS,START_StaticLib $@)
	$(RM) $(REAL_LIBRARY)
	$(AR) $(AR_FLAGS) $($@_OBJS)
	$(call BUILDSTATUS,END_StaticLib $@)

$(WASM_ARCHIVE): $(CWASMOBJS) $(CPPWASMOBJS) $(STATIC_LIBS) $(GLOBAL_DEPS)
	$(REPORT_BUILD_VERBOSE)
	$(call BUILDSTATUS,START_WasmLib $@)
	$(RM) $(WASM_ARCHIVE)
	$(WASM_CXX) -o $@ -Wl,--export-all -Wl,--stack-first -Wl,-z,stack-size=$(if $(MOZ_OPTIMIZE),262144,1048576) -Wl,--no-entry -Wl,--import-memory -Wl,--import-table $(CWASMOBJS) $(CPPWASMOBJS) $(addprefix -l,$(WASM_LIBS))
	$(call BUILDSTATUS,END_WasmLib $@)

ifeq ($(OS_ARCH),WINNT)
# Import libraries are created by the rules creating shared libraries.
# The rules to copy them to $(DIST)/lib depend on $(IMPORT_LIBRARY),
# but make will happily consider the import library before it is refreshed
# when rebuilding the corresponding shared library. Defining an empty recipe
# for import libraries forces make to wait for the shared library recipe to
# have run before considering other targets that depend on the import library.
# See bug 795204.
$(IMPORT_LIBRARY): $(SHARED_LIBRARY) ;
endif

$(HOST_SHARED_LIBRARY): Makefile
	$(REPORT_BUILD)
	$(call BUILDSTATUS,START_SharedLib $@)
	$(RM) $@
ifneq (,$(filter clang-cl,$(HOST_CC_TYPE)))
	$(HOST_LINKER) -DLL -OUT:$@ $($(notdir $@)_OBJS) $(HOST_CXX_LDFLAGS) $(HOST_LDFLAGS) $(HOST_LINKER_LIBPATHS) $(HOST_LIBS) $(HOST_EXTRA_LIBS)
else
	$(HOST_CXX) $(HOST_OUTOPTION)$@ $($(notdir $@)_OBJS) $(HOST_CXX_LDFLAGS) $(HOST_LDFLAGS) $(HOST_LIBS) $(HOST_EXTRA_LIBS)
endif
	$(call BUILDSTATUS,END_SharedLib $@)

# On Darwin (Mac OS X), dwarf2 debugging uses debug info left in .o files,
# so instead of deleting .o files after repacking them into a dylib, we make
# symlinks back to the originals. The symlinks are a no-op for stabs debugging,
# so no need to conditionalize on OS version or debugging format.

$(SHARED_LIBRARY): $(OBJS) $(call resfile,$(SHARED_LIBRARY)) $(STATIC_LIBS) $(GLOBAL_DEPS) $(call mkdir_deps,$(FINAL_TARGET))
	$(REPORT_BUILD)
	$(call BUILDSTATUS,START_SharedLib $@)
	$(RM) $@
	$(MKSHLIB) $(if $(filter clang-cl_WINNT,$(CC_TYPE)_$(OS_ARCH)),-IMPLIB:$(basename $(@F)).lib )$($(notdir $@)_OBJS) $(filter %.res,$^) $(RELRHACK_LDFLAGS) $(LDFLAGS) $(STATIC_LIBS) $(SHARED_LIBS) $(EXTRA_DSO_LDOPTS) $(OS_LIBS)
	$(call py_action,check_binary,$@)

ifeq (clang-cl_WINNT,$(CC_TYPE)_$(OS_ARCH))
endif	# WINNT && !GCC
	chmod +x $@
ifdef ENABLE_STRIP
	$(STRIP) $(STRIP_FLAGS) $@
endif
	$(call BUILDSTATUS,END_SharedLib $@)

# The object file is in the current directory, and the source file can be any
# relative path. This macro adds the dependency obj: src for each source file.
# This dependency must be first for the $< flag to work correctly, and the
# rules that have commands for these targets must not list any other
# prerequisites, or they will override the $< variable.
define src_objdep
$(basename $3$(notdir $1)).$2: $1 $$(call mkdir_deps,$$(MDDEPDIR))
endef
$(foreach f,$(CSRCS) $(SSRCS) $(CPPSRCS) $(CMSRCS) $(CMMSRCS) $(ASFILES),$(eval $(call src_objdep,$(f),$(OBJ_SUFFIX))))
$(foreach f,$(HOST_CSRCS) $(HOST_CPPSRCS),$(eval $(call src_objdep,$(f),$(OBJ_SUFFIX),host_)))
$(foreach f,$(WASM_CSRCS) $(WASM_CPPSRCS),$(eval $(call src_objdep,$(f),wasm)))

$(OBJS) $(HOST_OBJS) $(PROGOBJS) $(HOST_PROGOBJS): $(GLOBAL_DEPS)

# Rules for building native targets must come first because of the host_ prefix
$(HOST_COBJS):
	$(REPORT_BUILD_VERBOSE)
	$(call BUILDSTATUS,OBJECT_FILE $@)
	$(HOST_CC) $(HOST_OUTOPTION)$@ -c $(HOST_CPPFLAGS) $(HOST_CFLAGS) $(NSPR_CFLAGS) $<
	$(call BUILDSTATUS,END_Object $@)

$(HOST_CPPOBJS):
	$(REPORT_BUILD_VERBOSE)
	$(call BUILDSTATUS,OBJECT_FILE $@)
	$(HOST_CXX) $(HOST_OUTOPTION)$@ -c $(HOST_CPPFLAGS) $(HOST_CXXFLAGS) $(NSPR_CFLAGS) $<
	$(call BUILDSTATUS,END_Object $@)

$(COBJS):
	$(REPORT_BUILD_VERBOSE)
	$(call BUILDSTATUS,OBJECT_FILE $@)
	$(CC) $(OUTOPTION)$@ -c $(COMPILE_CFLAGS) $($(notdir $<)_FLAGS) $<
	$(call BUILDSTATUS,END_Object $@)

$(CWASMOBJS):
	$(REPORT_BUILD_VERBOSE)
	$(call BUILDSTATUS,OBJECT_FILE $@)
	$(WASM_CC) -o $@ -c $(WASM_CFLAGS) $($(notdir $<)_FLAGS) $<
	$(call BUILDSTATUS,END_Object $@)

WINEWRAP = $(if $(and $(filter %.exe,$1),$(WINE)),$(WINE) $1,$1)

# Windows program run via Wine don't like Unix absolute paths (they look
# like command line arguments). So when needed, create relative paths
# from absolute paths. We start with $(DEPTH), which gets us to topobjdir,
# then add "/.." for each component of topobjdir, which gets us to /.
# then we can add the absolute path after that and we have a relative path,
# albeit longer than it could be.
ifdef WINE
relativize = $(if $(filter /%,$1),$(DEPTH)$(subst $(space),,$(foreach d,$(subst /, ,$(topobjdir)),/..))$1,$1)
else
relativize = $1
endif

ifdef WINE
# asmarm64 needs a library that can be found in $PATH but for some reason,
# wine wants its path in $WINEPATH, so fill that to make it happy.
$(ASOBJS) $(SOBJS): export WINEPATH=$(subst :,;,$(PATH))
endif

ifdef ASFILES
# The AS_DASH_C_FLAG is needed cause not all assemblers (Solaris) accept
# a '-c' flag.
$(ASOBJS):
	$(REPORT_BUILD_VERBOSE)
	$(call BUILDSTATUS,OBJECT_FILE $@)
	$(call WINEWRAP,$(AS)) $(ASOUTOPTION)$@ $(ASFLAGS) $($(notdir $<)_FLAGS) $(AS_DASH_C_FLAG) $(call relativize,$<)
	$(call BUILDSTATUS,END_Object $@)
endif

define syms_template
$(2): $(1)
ifdef MOZ_CRASHREPORTER
	$$(call py_action,dumpsymbols $$@,$$(abspath $$<) $$(abspath $$@) $$(DUMP_SYMBOLS_FLAGS))
ifeq ($(OS_ARCH),WINNT)
ifdef WINCHECKSEC
	$$(PYTHON3) $$(topsrcdir)/build/win32/autowinchecksec.py $$<
endif # WINCHECKSEC
endif # WINNT
endif
endef

# Binaries installed into dist/bin/gtest (the gtest libxul and the libraries
# its tests load) are only used by gtest runs. Keep their symbols out of the
# main symbols archive, so that other tasks don't have to download them.
ifneq (,$(filter $(DIST)/bin/gtest%,$(FINAL_TARGET)))
DUMP_SYMBOLS_FLAGS += --symbols-dir=crashreporter-symbols-gtest
endif

ifneq (,$(filter $(DIST)/bin%,$(FINAL_TARGET)))
DUMP_SYMS_TARGETS := $(filter-out $(MOZBUILD_NON_DEFAULT_TARGETS),$(SHARED_LIBRARY) $(PROGRAM) $(SIMPLE_PROGRAMS) $(RUST_PROGRAMS))
endif

ifdef MOZ_AUTOMATION
ifeq (,$(filter 1,$(MOZ_AUTOMATION_BUILD_SYMBOLS)))
DUMP_SYMS_TARGETS :=
endif
endif

ifdef MOZ_COPY_PDBS
MAIN_PDB_FILES = $(addsuffix .pdb,$(basename $(DUMP_SYMS_TARGETS)))
MAIN_PDB_DEST ?= $(FINAL_TARGET)
MAIN_PDB_TARGET = syms
INSTALL_TARGETS += MAIN_PDB

ifdef CPP_UNIT_TESTS
CPP_UNIT_TESTS_PDB_FILES = $(addsuffix .pdb,$(basename $(CPP_UNIT_TESTS)))
CPP_UNIT_TESTS_PDB_DEST = $(DIST)/cppunittests
CPP_UNIT_TESTS_PDB_TARGET = syms
INSTALL_TARGETS += CPP_UNIT_TESTS_PDB
endif

else ifdef MOZ_CRASHREPORTER
$(foreach file,$(DUMP_SYMS_TARGETS),$(eval $(call syms_template,$(file),$(notdir $(file))_syms.track)))
syms:: $(foreach file,$(DUMP_SYMS_TARGETS),$(notdir $(file))_syms.track)
endif

ifneq (,$(RUST_TESTS)$(RUST_LIBRARY_FILE)$(HOST_RUST_LIBRARY_FILE)$(RUST_PROGRAMS)$(HOST_RUST_PROGRAMS))
include $(MOZILLA_DIR)/config/makefiles/rust.mk
endif

$(SOBJS):
	$(REPORT_BUILD)
	$(call WINEWRAP,$(AS)) $(ASOUTOPTION)$@ $(SFLAGS) $($(notdir $<)_FLAGS) -c $(call relativize,$<)

$(CPPOBJS):
	$(REPORT_BUILD_VERBOSE)
	$(call BUILDSTATUS,OBJECT_FILE $@)
	$(CCC) $(OUTOPTION)$@ -c $(COMPILE_CXXFLAGS) $($(notdir $<)_FLAGS) $<
	$(call BUILDSTATUS,END_Object $@)

$(CPPWASMOBJS):
	$(REPORT_BUILD_VERBOSE)
	$(call BUILDSTATUS,OBJECT_FILE $@)
	$(WASM_CXX) -o $@ -c $(WASM_CXXFLAGS) $($(notdir $<)_FLAGS) $<
	$(call BUILDSTATUS,END_Object $@)

$(CMMOBJS):
	$(REPORT_BUILD_VERBOSE)
	$(call BUILDSTATUS,OBJECT_FILE $@)
	$(CCC) -o $@ -c $(COMPILE_CXXFLAGS) $(COMPILE_CMMFLAGS) $($(notdir $<)_FLAGS) $<
	$(call BUILDSTATUS,END_Object $@)

$(CMOBJS):
	$(REPORT_BUILD_VERBOSE)
	$(call BUILDSTATUS,OBJECT_FILE $@)
	$(CC) -o $@ -c $(COMPILE_CFLAGS) $(COMPILE_CMFLAGS) $($(notdir $<)_FLAGS) $<
	$(call BUILDSTATUS,END_Object $@)

$(filter %.s,$(CPPSRCS:%.cpp=%.s)): %.s: %.cpp $(call mkdir_deps,$(MDDEPDIR))
	$(REPORT_BUILD_VERBOSE)
	$(CCC) -S $(COMPILE_CXXFLAGS) $($(notdir $<)_FLAGS) $<

$(filter %.s,$(CPPSRCS:%.cc=%.s)): %.s: %.cc $(call mkdir_deps,$(MDDEPDIR))
	$(REPORT_BUILD_VERBOSE)
	$(CCC) -S $(COMPILE_CXXFLAGS) $($(notdir $<)_FLAGS) $<

$(filter %.s,$(CPPSRCS:%.cxx=%.s)): %.s: %.cpp $(call mkdir_deps,$(MDDEPDIR))
	$(REPORT_BUILD_VERBOSE)
	$(CCC) -S $(COMPILE_CXXFLAGS) $($(notdir $<)_FLAGS) $<

$(filter %.s,$(CSRCS:%.c=%.s)): %.s: %.c $(call mkdir_deps,$(MDDEPDIR))
	$(REPORT_BUILD_VERBOSE)
	$(CC) -S $(COMPILE_CFLAGS) $($(notdir $<)_FLAGS) $<

ifneq (,$(filter %.i,$(MAKECMDGOALS)))
# Call as $(call _group_srcs,extension,$(SRCS)) - this will create a list
# of the full sources, as well as the $(notdir) version. So:
#   foo.cpp sub/bar.cpp
# becomes:
#   foo.cpp sub/bar.cpp bar.cpp
#
# This way we can match both 'make sub/bar.i' and 'make bar.i'
_group_srcs = $(sort $(patsubst %.$1,%.i,$(filter %.$1,$2 $(notdir $2))))

define PREPROCESS_RULES
_PREPROCESSED_$1_FILES := $$(call _group_srcs,$1,$$($2))
# Make preprocessed files PHONY so they are always executed, since they are
# manual targets and we don't necessarily write to $@.
.PHONY: $$(_PREPROCESSED_$1_FILES)

# Hack up VPATH so we can reach the sources. Eg: 'make Parser.i' may need to
# reach $(srcdir)/frontend/Parser.i
vpath %.$1 $$(addprefix $$(srcdir)/,$$(sort $$(dir $$($2))))
vpath %.$1 $$(addprefix $$(CURDIR)/,$$(sort $$(dir $$($2))))

$$(_PREPROCESSED_$1_FILES): _DEPEND_CFLAGS=
$$(_PREPROCESSED_$1_FILES): %.i: %.$1
	$$(REPORT_BUILD_VERBOSE)
	$$(addprefix $$(MKDIR) -p ,$$(filter-out .,$$(@D)))
	$$($3) -C $$(PREPROCESS_OPTION)$$@ $(foreach var,$4,$$($(var))) $$($$(notdir $$<)_FLAGS) $$<

endef

$(eval $(call PREPROCESS_RULES,cpp,CPPSRCS,CCC,COMPILE_CXXFLAGS))
$(eval $(call PREPROCESS_RULES,cc,CPPSRCS,CCC,COMPILE_CXXFLAGS))
$(eval $(call PREPROCESS_RULES,cxx,CPPSRCS,CCC,COMPILE_CXXFLAGS))
$(eval $(call PREPROCESS_RULES,c,CSRCS,CC,COMPILE_CFLAGS))
$(eval $(call PREPROCESS_RULES,mm,CMMSRCS,CCC,COMPILE_CXXFLAGS COMPILE_CMMFLAGS))

# Default to pre-processing the actual unified file. This can be overridden
# at the command-line to pre-process only the individual source file.
PP_UNIFIED ?= 1

# PP_REINVOKE gets set on the sub-make to prevent us from going in an
# infinite loop if the filename doesn't exist in the unified source files.
ifndef PP_REINVOKE

MATCH_cpp = \(cpp\|cc|cxx\)
UPPER_c = C
UPPER_cpp = CPP
UPPER_mm = CMM

# When building with PP_UNIFIED=0, we also have to look in the Unified files to
# find a matching pathname.
_get_all_sources = $1 $(if $(filter Unified%,$1),$(shell sed -n 's/\#include "\(.*\)"$$/\1/p' $(filter Unified%,$1)))
all_cpp_sources := $(call _get_all_sources,$(CPPSRCS))
all_mm_sources := $(call _get_all_sources,$(CMMSRCS))
all_c_sources := $(call _get_all_sources,$(CSRCS))
all_sources := $(all_cpp_sources) $(all_cmm_sources) $(all_c_sources)

# The catch-all %.i rule runs when we pass in a .i filename that doesn't match
# one of the *SRCS variables. The two code paths depend on whether or not
# we are requesting a unified file (PP_UNIFIED=1, the default) or not:
#
# PP_UNIFIED=1:
#  - Look for it in any of the Unified files, and re-exec make with
#    Unified_foo0.i as the target. This gets us the full unified preprocessed
#    file.
#
# PP_UNIFIED=0:
#  - If the .i filename is in *SRCS, or in a Unified filename, then we re-exec
#    make with that filename as the target. The *SRCS variables are modified
#    to have the Unified sources appended to them so that the static pattern
#    rules will match.
%.i: FORCE
ifeq ($(PP_UNIFIED),1)
	@$(MAKE) PP_REINVOKE=1 \
	    $(or $(addsuffix .i, \
              $(foreach type,c cpp mm, \
	        $(if $(filter Unified%,$($(UPPER_$(type))SRCS)), \
	          $(shell grep -l '#include "\(.*/\)\?$(basename $@).$(or $(MATCH_$(type)),$(type))"' Unified*.$(type) | sed 's/\.$(type)$$//') \
            ))),$(error "File not found for preprocessing: $@"))
else
	@$(MAKE) PP_REINVOKE=1 $@ \
	    $(foreach type,c cpp mm,$(UPPER_$(type))SRCS="$(all_$(type)_sources)")
endif

endif

endif

%.res: $(or $(RCFILE),%.rc) $(MOZILLA_DIR)/config/create_res.py
	$(REPORT_BUILD)
	$(call BUILDSTATUS,START_Res $@)
	$(PYTHON3) $(MOZILLA_DIR)/config/create_res.py $(DEFINES) $(INCLUDES) -o $@ $<
	$(call BUILDSTATUS,END_Res $@)

$(notdir $(addsuffix .rc,$(PROGRAM) $(SHARED_LIBRARY) $(SIMPLE_PROGRAMS) module)): %.rc: $(RCINCLUDE) $(MOZILLA_DIR)/config/create_rc.py
	$(call BUILDSTATUS,START_Rc $@)
	$(PYTHON3) $(MOZILLA_DIR)/config/create_rc.py '$(if $(filter module,$*),,$*)' --include '$(RCINCLUDE)' --dep-file '$(MDDEPDIR)/$@.d'
	$(call BUILDSTATUS,END_Rc $@)

-include $(addsuffix .rc.d, $(addprefix $(MDDEPDIR)/,$(PROGRAM) $(SHARED_LIBRARY) $(SIMPLE_PROGRAMS) module))

# Cancel GNU make built-in implicit rules
MAKEFLAGS += -r

ifneq (,$(filter WINNT,$(OS_ARCH)))
SEP := ;
else
SEP := :
endif

EMPTY :=
SPACE := $(EMPTY) $(EMPTY)

#############################################################################
# MDDEPDIR is the subdirectory where all the dependency files are placed.
#   This uses a make rule (instead of a macro) to support parallel
#   builds (-jN). If this were done in the LOOP_OVER_DIRS macro, two
#   processes could simultaneously try to create the same directory.
#
#   We use $(CURDIR) in the rule's target to ensure that we don't find
#   a dependency directory in the source tree via VPATH (perhaps from
#   a previous build in the source tree) and thus neglect to create a
#   dependency directory in the object directory, where we really need
#   it.

_MDDEPEND_FILES :=

ifneq (,$(filter target-objects target all default,$(MAKECMDGOALS)))
_MDDEPEND_FILES += $(addsuffix .pp,$(notdir $(sort $(OBJS) $(PROGOBJS))))
endif

ifneq (,$(filter host-objects host all default,$(MAKECMDGOALS)))
_MDDEPEND_FILES += $(addsuffix .pp,$(notdir $(sort $(HOST_OBJS) $(HOST_PROGOBJS))))
endif

MDDEPEND_FILES := $(strip $(wildcard $(addprefix $(MDDEPDIR)/,$(_MDDEPEND_FILES))))
MDDEPEND_FILES += $(EXTRA_MDDEPEND_FILES)

ifneq (,$(MDDEPEND_FILES))
-include $(MDDEPEND_FILES)
endif

################################################################################
# Install/copy rules
#
# The INSTALL_TARGETS variable contains a list of all install target
# categories. Each category defines a list of files and executables, and an
# install destination,
#
# FOO_FILES := foo bar
# FOO_EXECUTABLES := baz
# FOO_DEST := target_path
# INSTALL_TARGETS += FOO
#
# Additionally, a FOO_TARGET variable may be added to indicate the target for
# which the files and executables are installed. Default is "libs".
#
# Finally, a FOO_KEEP_PATH variable may be set to 1 to indicate the paths given
# in FOO_FILES/FOO_EXECUTABLES are to be kept at the destination. That is,
# if FOO_FILES is bar/baz/qux.h, and FOO_DEST is $(DIST)/include, the installed
# file would be $(DIST)/include/bar/baz/qux.h instead of $(DIST)/include/qux.h

# If we're using binary nsinstall and it's not built yet, fallback to python nsinstall.
ifneq (,$(filter $(DEPTH)/config/nsinstall$(HOST_BIN_SUFFIX),$(install_cmd)))
ifeq (,$(wildcard $(DEPTH)/config/nsinstall$(HOST_BIN_SUFFIX)))
nsinstall_is_usable = $(if $(wildcard $(DEPTH)/config/nsinstall$(HOST_BIN_SUFFIX)),yes)

define install_cmd_override
$(1): install_cmd = $$(if $$(nsinstall_is_usable),$$(INSTALL),$$(NSINSTALL_PY) -t) $$(1)
endef
endif
endif

install_target_tier = $(or $($(1)_TARGET),libs)
INSTALL_TARGETS_TIERS := $(sort $(foreach category,$(INSTALL_TARGETS),$(call install_target_tier,$(category))))

install_target_result = $($(1)_DEST:%/=%)/$(if $($(1)_KEEP_PATH),$(2),$(notdir $(2)))
install_target_files = $(foreach file,$($(1)_FILES),$(call install_target_result,$(category),$(file)))
install_target_executables = $(foreach file,$($(1)_EXECUTABLES),$(call install_target_result,$(category),$(file)))

# Work around a GNU make 3.81 bug where it gives $< the wrong value.
# See details in bug 934864.
define create_dependency
$(1): $(2)
$(1): $(2)
endef

define install_target_template
$(call install_cmd_override,$(2))
$(call create_dependency,$(2),$(1))
endef

$(foreach category,$(INSTALL_TARGETS),\
  $(if $($(category)_DEST),,$(error Missing $(category)_DEST)) \
  $(foreach tier,$(call install_target_tier,$(category)),\
    $(eval INSTALL_TARGETS_FILES_$(tier) += $(call install_target_files,$(category))) \
    $(eval INSTALL_TARGETS_EXECUTABLES_$(tier) += $(call install_target_executables,$(category))) \
  ) \
  $(foreach file,$($(category)_FILES) $($(category)_EXECUTABLES), \
    $(eval $(call install_target_template,$(file),$(call install_target_result,$(category),$(file)))) \
  ) \
)

$(foreach tier,$(INSTALL_TARGETS_TIERS), \
  $(eval $(if $(filter .,$(DEPTH)),recurse_$(tier):,$(tier)::) $(INSTALL_TARGETS_FILES_$(tier)) $(INSTALL_TARGETS_EXECUTABLES_$(tier))) \
)

install_targets_sanity = $(if $(filter-out $(notdir $@),$(notdir $(<))),$(error Looks like $@ has an unexpected dependency on $< which breaks INSTALL_TARGETS))

$(sort $(foreach tier,$(INSTALL_TARGETS_TIERS),$(INSTALL_TARGETS_FILES_$(tier)))):
	$(install_targets_sanity)
	$(call BUILDSTATUS,START_Install $@)
	$(call install_cmd,$(IFLAGS1) '$<' '$(@D)')
	$(call BUILDSTATUS,END_Install $@)

$(sort $(foreach tier,$(INSTALL_TARGETS_TIERS),$(INSTALL_TARGETS_EXECUTABLES_$(tier)))):
	$(install_targets_sanity)
	$(call BUILDSTATUS,START_Install $@)
	$(call install_cmd,$(IFLAGS2) '$<' '$(@D)')
	$(call BUILDSTATUS,END_Install $@)

################################################################################
# Preprocessing rules
#
# The PP_TARGETS variable contains a list of all preprocessing target
# categories. Each category has associated variables listing input files, the
# output directory, extra preprocessor flags, and so on. For example:
#
#   FOO := input-file
#   FOO_PATH := target-directory
#   FOO_FLAGS := -Dsome_flag
#   PP_TARGETS += FOO
#
# If PP_TARGETS lists a category name <C> (like FOO, above), then we consult the
# following make variables to see what to do:
#
# - <C> lists input files to be preprocessed with mozbuild.action.preprocessor.
#   We search VPATH for the names given here. If an input file name ends in
#   '.in', that suffix is omitted from the output file name.
#
# - <C>_PATH names the directory in which to place the preprocessed output
#   files. We create this directory if it does not already exist. Setting
#   this variable is optional; if unset, we install the files in $(CURDIR).
#
# - <C>_FLAGS lists flags to pass to mozbuild.action.preprocessor, in addition
#   to the usual bunch. Setting this variable is optional.
#
# - <C>_TARGET names the 'make' target that should depend on creating the output
#   files. Setting this variable is optional; if unset, we preprocess the
#   files for the 'libs' target.
#
# - <C>_KEEP_PATH may be set to 1 to indicate the paths given in <C> are to be
#   kept under <C>_PATH. That is, if <C> is bar/baz/qux.h.in and <C>_PATH is
#   $(DIST)/include, the preprocessed file would be $(DIST)/include/bar/baz/qux.h
#   instead of $(DIST)/include/qux.h.

pp_target_tier = $(or $($(1)_TARGET),libs)
PP_TARGETS_TIERS := $(sort $(foreach category,$(PP_TARGETS),$(call pp_target_tier,$(category))))

pp_target_result = $(or $($(1)_PATH:%/=%),$(CURDIR))/$(if $($(1)_KEEP_PATH),$(2:.in=),$(notdir $(2:.in=)))
pp_target_results = $(foreach file,$($(1)),$(call pp_target_result,$(category),$(file)))

$(foreach category,$(PP_TARGETS), \
  $(foreach tier,$(call pp_target_tier,$(category)), \
    $(eval PP_TARGETS_RESULTS_$(tier) += $(call pp_target_results,$(category))) \
  ) \
  $(foreach file,$($(category)), \
    $(eval $(call create_dependency,$(call pp_target_result,$(category),$(file)), \
                                    $(file) $(GLOBAL_DEPS) $($(category)_EXTRA_DEPS))) \
  ) \
  $(eval $(call pp_target_results,$(category)): PP_TARGET_FLAGS=$($(category)_FLAGS)) \
)

$(foreach tier,$(PP_TARGETS_TIERS), \
  $(eval $(if $(filter .,$(DEPTH)),recurse_$(tier):,$(tier)::) $(PP_TARGETS_RESULTS_$(tier))) \
)

PP_TARGETS_ALL_RESULTS := $(sort $(foreach tier,$(PP_TARGETS_TIERS),$(PP_TARGETS_RESULTS_$(tier))))
$(PP_TARGETS_ALL_RESULTS):
	$(if $(filter-out $(notdir $@),$(notdir $(<:.in=))),$(error Looks like $@ has an unexpected dependency on $< which breaks PP_TARGETS))
	$(RM) '$@'
	$(call py_action,preprocessor $(@F),--depend $(MDDEPDIR)/$(@F).pp $(PP_TARGET_FLAGS) $(DEFINES) $(ACDEFINES) '$<' -o '$@')

$(filter %.css,$(PP_TARGETS_ALL_RESULTS)): PP_TARGET_FLAGS+=--marker %

# The depfile is based on the filename, and we don't want conflicts. So check
# there's only one occurrence of any given filename in PP_TARGETS_ALL_RESULTS.
PP_TARGETS_ALL_RESULT_NAMES := $(notdir $(PP_TARGETS_ALL_RESULTS))
$(foreach file,$(sort $(PP_TARGETS_ALL_RESULT_NAMES)), \
  $(if $(filter-out 1,$(words $(filter $(file),$(PP_TARGETS_ALL_RESULT_NAMES)))), \
    $(error Multiple preprocessing rules are creating a $(file) file) \
  ) \
)

ifneq (,$(filter $(PP_TARGETS_TIERS) $(PP_TARGETS_ALL_RESULTS),$(MAKECMDGOALS)))
# If the depfile for a preprocessed file doesn't exist, add a dep to force
# re-preprocessing.
$(foreach file,$(PP_TARGETS_ALL_RESULTS), \
  $(if $(wildcard $(MDDEPDIR)/$(notdir $(file)).pp), \
    , \
    $(eval $(file): FORCE) \
  ) \
)

MDDEPEND_FILES := $(strip $(wildcard $(addprefix $(MDDEPDIR)/,$(addsuffix .pp,$(notdir $(PP_TARGETS_ALL_RESULTS))))))

ifneq (,$(MDDEPEND_FILES))
-include $(MDDEPEND_FILES)
endif

endif

# Pull in non-recursive targets if this is a partial tree build.
ifndef TOPLEVEL_BUILD
include $(MOZILLA_DIR)/config/makefiles/nonrecursive.mk
endif

################################################################################
# Special gmake rules.
################################################################################


#
# Re-define the list of default suffixes, so gmake won't have to churn through
# hundreds of built-in suffix rules for stuff we don't need.
#
.SUFFIXES:

#
# Fake targets.  Always run these rules, even if a file/directory with that
# name already exists.
#
.PHONY: all alltags boot export install libs makefiles run_apprunner tools $(DIRS) FORCE

# Used as a dependency to force targets to rebuild
FORCE:

# Delete target if error occurs when building target
.DELETE_ON_ERROR:

tags: TAGS

TAGS: $(CSRCS) $(CPPSRCS) $(wildcard *.h)
	-etags $(CSRCS) $(CPPSRCS) $(wildcard *.h)
	$(LOOP_OVER_DIRS)

ifndef INCLUDED_DEBUGMAKE_MK #{
  ## Only parse when an echo* or show* target is requested
  ifneq (,$(call isTargetStem,echo,show))
    include $(MOZILLA_DIR)/config/makefiles/debugmake.mk
  endif #}
endif #}

FREEZE_VARIABLES = \
  CSRCS \
  CPPSRCS \
  EXPORTS \
  DIRS \
  LIBRARY \
  MODULE \
  $(NULL)

$(foreach var,$(FREEZE_VARIABLES),$(eval $(var)_FROZEN := '$($(var))'))

CHECK_FROZEN_VARIABLES = $(foreach var,$(FREEZE_VARIABLES), \
  $(if $(subst $($(var)_FROZEN),,'$($(var))'),$(error Makefile variable '$(var)' changed value after including rules.mk. Was $($(var)_FROZEN), now $($(var)).)))

libs export::
	$(CHECK_FROZEN_VARIABLES)

.DEFAULT_GOAL := $(or $(OVERRIDE_DEFAULT_GOAL),default)

#############################################################################
# Derived targets and dependencies

include $(MOZILLA_DIR)/config/makefiles/autotargets.mk
ifneq ($(NULL),$(AUTO_DEPS))
  default all libs tools export:: $(AUTO_DEPS)
endif
