# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

ifeq (WINNT,$(HOST_OS_ARCH))
# //?/ is the long path prefix which seems to confuse make, so we remove it
# (things should work without it).
normalize_sep = $(patsubst //?/%,%,$(subst \,/,$(1)))
else
normalize_sep = $(1)
endif

# The + prefix preserves jobserver file descriptors. Omit it for `make -n`.
# Pass --single-job when Make is limited to one job.
define RUN_CARGO_ACTION
$(if $(findstring n,$(firstword -$(MAKEFLAGS))),,+)$(PYTHON3) -m mozbuild.action.run_cargo --spec $(1) $(if $(filter --jobserver% -j,$(MAKEFLAGS)),,--single-job) $(2)
endef

# Rust build edges forward timing and keep-going signals. Test edges do not.
cargo_action_rustc_flags = $(if $(BUILDSTATUS),--timings) $(if $(findstring k,$(firstword -$(MAKEFLAGS))),--keep-going)

define make_default_rule
$(1):

endef

# make_cargo_rule(target, real-target [, extra-deps])
# Generates a rule suitable to rebuild $(target) only if its dependencies are
# obsolete.
# It relies on the fact that upon build, cargo generates a dependency file named
# `$(target).d'. Unfortunately the lhs of the rule has an absolute path,
# so we extract it under the name $(target)_deps below.
#
# If the dependencies are empty, the file was not created so we force a rebuild.
# Otherwise we add it to the dependency list.
#
# The actual rule is a bit tricky. The `+' prefix allow for recursive parallel
# make, and it's skipped (`:') if we already triggered a rebuild as part of the
# dependency chain.
#
# Another tricky thing: some dependencies may contain escaped spaces, and they
# need to be preserved, but $(wordlist) and $(foreach) split on spaces, so we
# replace escaped spaces with some unlikely string, and replace them back after.
escape_sequence=_^_^_^_
escape_spaces = $(subst \ ,$(escape_sequence),$(1))
unescape_spaces = $(subst $(escape_sequence),\ ,$(1))

define make_cargo_rule
$(notdir $(1))_deps := $$(call unescape_spaces,$$(call normalize_sep,$$(wordlist 2, 10000000, $$(call escape_spaces,$$(if $$(wildcard $(basename $(1)).d),$$(shell cat $(basename $(1)).d))))))
$(1): $(CARGO_FILE) $(3) $(topsrcdir)/Cargo.lock $$(if $$($(notdir $(1))_deps),$$($(notdir $(1))_deps),$(2))
	$$(REPORT_BUILD)
	$$(if $$($(notdir $(1))_deps),+$(MAKE) $(2),:)
	@touch $$@

$$(foreach dep, $$(call escape_spaces,$$($(notdir $(1))_deps)),$$(eval $$(call make_default_rule,$$(call unescape_spaces,$$(dep)))))
endef

ifdef RUST_LIBRARY_FILE

# Assume any system libraries rustc links against are already in the target's LIBS.
#
# We need to run cargo unconditionally, because cargo is the only thing that
# has full visibility into how changes in Rust sources might affect the final
# build.
force-cargo-library-build:
	$(call BUILDSTATUS,START_Rust $(notdir $(RUST_LIBRARY_FILE)))
	$(call RUN_CARGO_ACTION,.cargo-library-spec.json,$(cargo_action_rustc_flags))
	$(call BUILDSTATUS,END_Rust $(notdir $(RUST_LIBRARY_FILE)))

$(eval $(call make_cargo_rule,$(RUST_LIBRARY_FILE),force-cargo-library-build,$(CARGO_LIBRARY_DEPS)))

endif # RUST_LIBRARY_FILE

ifdef RUST_TESTS

force-cargo-test-run:
	$(call RUN_CARGO_ACTION,.cargo-tests-spec.json)

endif # RUST_TESTS

ifdef HOST_RUST_LIBRARY_FILE

force-cargo-host-library-build:
	$(call BUILDSTATUS,START_Rust $(notdir $(HOST_RUST_LIBRARY_FILE)))
	$(call RUN_CARGO_ACTION,.cargo-host-library-spec.json,$(cargo_action_rustc_flags))
	$(call BUILDSTATUS,END_Rust $(notdir $(HOST_RUST_LIBRARY_FILE)))

$(eval $(call make_cargo_rule,$(HOST_RUST_LIBRARY_FILE),force-cargo-host-library-build,$(CARGO_HOST_LIBRARY_DEPS)))

endif # HOST_RUST_LIBRARY_FILE

ifdef RUST_PROGRAMS

force-cargo-program-build: $(CARGO_PROGRAM_DEPS)
	$(call BUILDSTATUS,START_Rust $(RUST_CARGO_PROGRAMS))
	$(call RUN_CARGO_ACTION,.cargo-program-spec.json,$(cargo_action_rustc_flags))
	$(call BUILDSTATUS,END_Rust $(RUST_CARGO_PROGRAMS))

$(foreach RUST_PROGRAM,$(RUST_PROGRAMS), $(eval $(call make_cargo_rule,$(RUST_PROGRAM),force-cargo-program-build,$(CARGO_PROGRAM_DEPS))))

endif # RUST_PROGRAMS

ifdef HOST_RUST_PROGRAMS

force-cargo-host-program-build:
	$(call BUILDSTATUS,START_Rust $(HOST_RUST_CARGO_PROGRAMS))
	$(call RUN_CARGO_ACTION,.cargo-host-program-spec.json,$(cargo_action_rustc_flags))
	$(call BUILDSTATUS,END_Rust $(HOST_RUST_CARGO_PROGRAMS))

$(foreach HOST_RUST_PROGRAM,$(HOST_RUST_PROGRAMS), $(eval $(call make_cargo_rule,$(HOST_RUST_PROGRAM),force-cargo-host-program-build,$(CARGO_HOST_PROGRAM_DEPS))))

endif # HOST_RUST_PROGRAMS
