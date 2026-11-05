# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

ifndef INCLUDED_RULES_MK
include $(topsrcdir)/config/rules.mk
endif

# The traditional model of directory traversal with make is as follows:
#   make -C foo
#     Entering foo
#     make -C bar
#       Entering foo/bar
#     make -C baz
#       Entering foo/baz
#   make -C qux
#     Entering qux
#
# Pseudo derecurse transforms the above into:
#   make -C foo
#   make -C foo/bar
#   make -C foo/baz
#   make -C qux

ifeq (.,$(DEPTH))

include root.mk

# Main rules (export, compile, libs and tools) call recurse_* rules.
# This wrapping is only really useful for build status.
$(RUNNABLE_TIERS)::
	$(if $(filter $@,$(MAKECMDGOALS)),$(call BUILDSTATUS,TIERS $@),)
	$(call BUILDSTATUS,TIER_START $@)
	+$(MAKE) recurse_$@
	$(call BUILDSTATUS,TIER_FINISH $@)

# Special rule that does install-manifests (cf. Makefile.in) + compile
binaries::
	+$(MAKE) recurse_compile
# On macOS, `mach run` launches the binaries from the application bundle
# (.app/Contents/MacOS), which holds separate copies of what is linked into
# dist/bin. Those copies are refreshed by the tools tier, so run it after the
# compile tier to ensure `mach build binaries` produces a runnable bundle.
ifdef MOZ_MACBUNDLE_NAME
	+$(MAKE) recurse_tools
endif

# Get current tier and corresponding subtiers from the data in root.mk.
CURRENT_TIER := $(filter $(foreach tier,$(RUNNABLE_TIERS) $(non_default_tiers),recurse_$(tier) $(tier)-deps),$(MAKECMDGOALS))
ifneq (,$(filter-out 0 1,$(words $(CURRENT_TIER))))
$(error $(CURRENT_TIER) not supported on the same make command line)
endif
CURRENT_TIER := $(subst recurse_,,$(CURRENT_TIER:-deps=))

# The rules here are doing directory traversal, so we don't want further
# recursion to happen when running make -C subdir $tier. But some make files
# further call make -C something else, and sometimes expect recursion to
# happen in that case.
# Conveniently, every invocation of make increases MAKELEVEL, so only stop
# recursion from happening at current MAKELEVEL + 1.
ifdef CURRENT_TIER
ifeq (0,$(MAKELEVEL))
export NO_RECURSE_MAKELEVEL=1
else
export NO_RECURSE_MAKELEVEL=$(word $(MAKELEVEL),2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20)
endif
endif

RECURSE = $(if $(RECURSE_TRACE_ONLY),@echo $2/$1,$(call SUBMAKE,$1,$2))

# Use the $(*_dirs) variables available in root.mk
CURRENT_DIRS := $($(CURRENT_TIER)_dirs)

# Need a list of compile targets because we can't use pattern rules:
# https://savannah.gnu.org/bugs/index.php?42833
# Only recurse the paths starting with RECURSE_BASE_DIR when provided.
.PHONY: $(pre_compile_targets) $(compile_targets) $(syms_targets)
$(pre_compile_targets) $(compile_targets) $(syms_targets):
	$(if $(filter $(RECURSE_BASE_DIR)%,$@),$(call RECURSE,$(@F),$(@D)))

$(syms_targets): %/syms: %/target

# Only hook symbols targets into the main compile graph in automation.
ifdef MOZ_AUTOMATION
ifeq (1,$(MOZ_AUTOMATION_BUILD_SYMBOLS))
recurse_compile: $(syms_targets)
endif
endif

# Create a separate rule that depends on every 'syms' target so that
# symbols can be dumped on demand locally.
.PHONY: recurse_syms
recurse_syms: $(syms_targets)

# The compile tier has different rules from other tiers.
ifneq ($(CURRENT_TIER),compile)

# Recursion rule for all directories traversed for all subtiers in the
# current tier, except the ones the rule above already covers.
current_tier_targets := $(filter-out $(pre_compile_targets) $(compile_targets) $(syms_targets),$(addsuffix /$(CURRENT_TIER),$(CURRENT_DIRS)))

$(current_tier_targets): %/$(CURRENT_TIER):
	$(call RECURSE,$(CURRENT_TIER),$*)

.PHONY: $(current_tier_targets)

# Dummy rules for possibly inexisting dependencies for the above tier targets
$(addsuffix /Makefile,$(CURRENT_DIRS)) $(addsuffix /backend.mk,$(CURRENT_DIRS)):

ifeq ($(CURRENT_TIER),export)
# config/export builds the STL and system wrappers. Keep it first until the
# export consumers are audited.
$(addsuffix /$(CURRENT_TIER),$(filter-out config,$(CURRENT_DIRS))): config/$(CURRENT_TIER)

# The export tier requires nsinstall, which is built from config. So every
# subdirectory traversal needs to happen after building nsinstall in config, which
# is done with the config/host target. Note the config/host target only exists if
# nsinstall is actually built, which it is not on Windows, because we use
# nsinstall.py there.
ifdef COMPILE_ENVIRONMENT
ifneq (,$(filter config/host, $(compile_targets)))
$(addsuffix /$(CURRENT_TIER),$(CURRENT_DIRS)): config/host
endif
endif
endif

endif # ifeq ($(CURRENT_TIER),compile)

else

# Don't recurse if MAKELEVEL is NO_RECURSE_MAKELEVEL as defined above
ifeq ($(NO_RECURSE_MAKELEVEL),$(MAKELEVEL))

$(RUNNABLE_TIERS)::

else
#########################
# Tier traversal handling
#########################

define CREATE_SUBTIER_TRAVERSAL_RULE
.PHONY: $(1)

$(1):: $$(SUBMAKEFILES)
	$$(LOOP_OVER_DIRS)

endef

$(foreach subtier,$(filter-out compile,$(RUNNABLE_TIERS)),$(eval $(call CREATE_SUBTIER_TRAVERSAL_RULE,$(subtier))))

ifndef TOPLEVEL_BUILD
ifdef COMPILE_ENVIRONMENT
compile::
	@$(MAKE) -C $(DEPTH) compile RECURSE_BASE_DIR=$(relativesrcdir)/
endif # COMPILE_ENVIRONMENT
endif

endif # ifeq ($(NO_RECURSE_MAKELEVEL),$(MAKELEVEL))

endif # ifeq (.,$(DEPTH))

recurse:
	@$(RECURSED_COMMAND)
	$(LOOP_OVER_DIRS)

ifeq (.,$(DEPTH))

ifdef ENABLE_CLANG_PLUGIN
# Only target rules use the clang plugin.
$(filter %/target %/target-objects,$(filter-out config/export config/host build/unix/stdc++compat/% build/clang-plugin/%,$(compile_targets))) security/rlbox/pre-compile media/libsoundtouch/src/pre-compile: build/clang-plugin/host build/clang-plugin/tests/target-objects
build/clang-plugin/tests/target-objects: build/clang-plugin/host
# clang-plugin tests require js-confdefs.h on js standalone builds and mozilla-config.h on
# other builds, because they are -include'd.
ifdef JS_STANDALONE
# The js/src/export target only exists when CURRENT_TIER is export. If we're in a later tier,
# we can assume js/src/export has happened anyways.
ifeq ($(CURRENT_TIER),export)
build/clang-plugin/tests/target-objects: js/src/export
endif
else
build/clang-plugin/tests/target-objects: mozilla-config.h
endif
endif

# Interdependencies that moz.build world don't know about yet for compilation.
# Note some others are hardcoded or "guessed" in recursivemake.py and emitter.py

# Rust targets, and export targets that run cbindgen need
# $topobjdir/.cargo/config.toml to be preprocessed first. Ideally, we'd only set it
# as a dependency of the rust targets, but unfortunately, that pushes Make to
# execute them much later than we'd like them to be when the file doesn't exist
# prior to Make running. So we also set it as a dependency of pre-export, which
# ensures it exists before recursing the rust targets and the export targets
# that run cbindgen, tricking Make into keeping them early.
$(rust_targets): $(DEPTH)/.cargo/config.toml
ifndef TEST_MOZBUILD
recurse_pre-export: $(DEPTH)/.cargo/config.toml
endif
endif
