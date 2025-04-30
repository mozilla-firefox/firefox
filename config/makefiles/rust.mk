# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# /!\ In this file, we export multiple variables globally via make rather than
# in recipes via the `env` command to avoid round-trips to msys on Windows, which
# tend to break environment variable values in interesting ways.

# /!\ Avoid the use of double-quotes in this file, so that the cargo
# commands can be executed directly by make, without doing a round-trip
# through a shell.

cargo_host_flag := --target=$(RUST_HOST_TARGET)
cargo_target_flag := --target=$(RUST_TARGET)

# Permit users to pass flags to cargo from their mozconfigs (e.g. --color=always).
cargo_build_flags = $(CARGOFLAGS)
ifndef MOZ_DEBUG_RUST
cargo_build_flags += --release
endif

# The Spidermonkey library can be built from a package tarball outside the
# tree, so we want to let Cargo create lock files in this case. When built
# within a tree, the Rust dependencies have been vendored in so Cargo won't
# touch the lock file.
ifndef JS_STANDALONE
cargo_build_flags += --frozen
endif

cargo_build_flags += --manifest-path $(CARGO_FILE)
ifdef BUILD_VERBOSE_LOG
cargo_build_flags += -vv
endif

ifneq (,$(USE_CARGO_JSON_MESSAGE_FORMAT))
cargo_build_flags += --message-format=json
endif

# Enable color output if original stdout was a TTY and color settings
# aren't already present. This essentially restores the default behavior
# of cargo when running via `mach`.
ifdef MACH_STDOUT_ISATTY
ifeq (,$(findstring --color,$(cargo_build_flags)))
ifdef NO_ANSI
cargo_build_flags += --color=never
else
cargo_build_flags += --color=always
endif
endif
endif

# Without -j > 1, make will not pass jobserver info down to cargo. Force
# one job when requested as a special case.
cargo_build_flags += $(filter -j1,$(MAKEFLAGS))

# We also need to rebuild the rust stdlib so that it's instrumented. Because
# build-std is still pretty experimental, we need to explicitly request
# the panic_abort crate for `panic = "abort"` support.
ifdef MOZ_TSAN
cargo_build_flags += -Zbuild-std=std,panic_abort
RUSTFLAGS += -Zsanitizer=thread
endif

rustflags_sancov =
ifndef MOZ_TSAN
ifndef FUZZING_JS_FUZZILLI
ifdef LIBFUZZER
# These options should match what is implicitly enabled for `clang -fsanitize=fuzzer`
#   here: https://github.com/llvm/llvm-project/blob/release/13.x/clang/lib/Driver/SanitizerArgs.cpp#L422
#
#  -sanitizer-coverage-inline-8bit-counters      Increments 8-bit counter for every edge.
#  -sanitizer-coverage-level=4                   Enable coverage for all blocks, critical edges, and indirect calls.
#  -sanitizer-coverage-trace-compares            Tracing of CMP and similar instructions.
#  -sanitizer-coverage-pc-table                  Create a static PC table.
#
# In TSan builds, we must not pass any of these, because sanitizer coverage is incompatible with TSan.
rustflags_sancov += -Cpasses=sancov-module -Cllvm-args=-sanitizer-coverage-inline-8bit-counters -Cllvm-args=-sanitizer-coverage-level=4 -Cllvm-args=-sanitizer-coverage-trace-compares -Cllvm-args=-sanitizer-coverage-pc-table
else
ifdef AFLFUZZ
# Use the same flags as afl-cc, specified here:
# https://github.com/AFLplusplus/AFLplusplus/blob/4eaacfb095ac164afaaf9c10b8112f98d8ad7c2a/src/afl-cc.c#L2101
#  -sanitizer-coverage-level=3                   Enable coverage for all blocks, critical edges. Implied by clang as default.
#  -sanitizer-coverage-pc-table                  Create a static PC table.
#  -sanitizer-coverage-trace-pc-guard            Adds guard_variable (uint32_t) to every edge
rustflags_sancov += -Cpasses=sancov-module -Cllvm-args=-sanitizer-coverage-level=3  -Cllvm-args=-sanitizer-coverage-pc-table -Cllvm-args=-sanitizer-coverage-trace-pc-guard
endif
endif
endif
endif

# These flags are passed via `cargo rustc` and only apply to the final rustc
# invocation (i.e., only the top-level crate, not its dependencies).
cargo_rustc_flags = $(CARGO_RUSTCFLAGS)
ifndef DEVELOPER_OPTIONS
ifndef MOZ_DEBUG_RUST
# Enable link-time optimization for release builds, but not when linking
# gkrust_gtest. And not when doing cross-language LTO.
ifndef MOZ_LTO_RUST_CROSS
# Never enable when sancov is enabled to work around https://github.com/rust-lang/rust/issues/90300.
ifndef rustflags_sancov
# Never enable when coverage is enabled to work around https://github.com/rust-lang/rust/issues/90045.
ifndef MOZ_CODE_COVERAGE
ifeq (,$(findstring gkrust_gtest,$(RUST_LIBRARY_FILE)))
cargo_rustc_flags += -Clto$(if $(filter full,$(MOZ_LTO_RUST_CROSS)),=fat)
endif
# We need -Cembed-bitcode=yes for all crates when using -Clto.
RUSTFLAGS += -Cembed-bitcode=yes
endif
endif
endif
endif
endif

ifdef CARGO_INCREMENTAL
export CARGO_INCREMENTAL
endif

rustflags_neon =
ifeq (neon,$(MOZ_FPU))
ifneq (,$(filter thumbv7neon-,$(RUST_TARGET)))
# Enable neon and disable restriction to 16 FPU registers when neon is enabled
# but we're not using a thumbv7neon target, where it's already the default.
# (CPUs with neon have 32 FPU registers available)
rustflags_neon += -C target_feature=+neon,-d16
endif
endif

rustflags_override = $(MOZ_RUST_DEFAULT_FLAGS) $(rustflags_neon)

ifdef DEVELOPER_OPTIONS
# By default the Rust compiler will perform a limited kind of ThinLTO on each
# crate. For local builds this additional optimization is not worth the
# increase in compile time so we opt out of it.
rustflags_override += -Clto=off
endif

ifdef MOZ_USING_SCCACHE
export RUSTC_WRAPPER=$(CCACHE)
endif

ifndef CROSS_COMPILE
ifdef MOZ_TSAN
PASS_ONLY_BASE_CFLAGS_TO_RUST=1
else
ifneq (,$(MOZ_ASAN)$(MOZ_UBSAN))
ifneq ($(OS_ARCH), Linux)
PASS_ONLY_BASE_CFLAGS_TO_RUST=1
endif # !Linux
endif # MOZ_ASAN || MOZ_UBSAN
endif # MOZ_TSAN
endif # !CROSS_COMPILE

ifeq (WINNT,$(HOST_OS_ARCH))
ifdef MOZ_CODE_COVERAGE
PASS_ONLY_BASE_CFLAGS_TO_RUST=1
endif # MOZ_CODE_COVERAGE
endif # WINNT

ifeq (WINNT,$(HOST_OS_ARCH))
# //?/ is the long path prefix which seems to confuse make, so we remove it
# (things should work without it).
normalize_sep = $(patsubst //?/%,%,$(subst \,/,$(1)))
else
normalize_sep = $(1)
endif

# We start with host variables because the rust host and the rust target might be the same,
# in which case we want the latter to take priority.

# We're passing these for consumption by the `cc` crate, which doesn't use the same
# convention as cargo itself:
# https://github.com/alexcrichton/cc-rs/blob/baa71c0e298d9ad7ac30f0ad78f20b4b3b3a8fb2/src/lib.rs#L1715
rust_host_cc_env_name := $(subst -,_,$(RUST_HOST_TARGET))

# HOST_CC/HOST_CXX/CC/CXX usually contain base flags for e.g. the build target.
# We want to pass those through CFLAGS_*/CXXFLAGS_* instead, so that they end up
# after whatever cc-rs adds to the compiler command line, so that they win.
# Ideally, we'd use CRATE_CC_NO_DEFAULTS=1, but that causes other problems at the
# moment.
export CC_$(rust_host_cc_env_name)=$(filter-out $(HOST_CC_BASE_FLAGS),$(HOST_CC))
export CXX_$(rust_host_cc_env_name)=$(filter-out $(HOST_CXX_BASE_FLAGS),$(HOST_CXX))
export AR_$(rust_host_cc_env_name)=$(HOST_AR)

rust_cc_env_name := $(subst -,_,$(RUST_TARGET))

export CC_$(rust_cc_env_name)=$(filter-out $(CC_BASE_FLAGS),$(CC))
export CXX_$(rust_cc_env_name)=$(filter-out $(CXX_BASE_FLAGS),$(CXX))
export AR_$(rust_cc_env_name)=$(AR)

ifeq (WINNT,$(HOST_OS_ARCH))
HOST_CC_BASE_FLAGS += -DUNICODE
HOST_CXX_BASE_FLAGS += -DUNICODE
endif
ifeq (WINNT,$(OS_ARCH))
CC_BASE_FLAGS += -DUNICODE
CXX_BASE_FLAGS += -DUNICODE
endif

ifneq (1,$(PASS_ONLY_BASE_CFLAGS_TO_RUST))
# -DMOZILLA_CONFIG_H is added to prevent mozilla-config.h from injecting anything
# in C/C++ compiles from rust. That's not needed in the other branch because the
# base flags don't force-include mozilla-config.h.
export CFLAGS_$(rust_host_cc_env_name)=$(HOST_CC_BASE_FLAGS) $(COMPUTED_HOST_CFLAGS) -DMOZILLA_CONFIG_H
export CXXFLAGS_$(rust_host_cc_env_name)=$(HOST_CXX_BASE_FLAGS) $(COMPUTED_HOST_CXXFLAGS) -DMOZILLA_CONFIG_H
# We exclude -fprofile-generate from the PGO flags because on non-cross compiles,
# that affects build scripts, and they fail to link because the linker flags are
# not adequate, and also, we don't want to run instrumented build scripts.
# The cc crate will fill in for those flags anyways, but we do need the PGO and
# LTO flags to fill in for what the cc crate doesn't handle
# (e.g. -pgo-temporal-instrumentation)
# We can't use LTO flags with GCC, though: https://github.com/rust-lang/rust/issues/138681
ifneq (,$(filter clang%,$(CC_TYPE)))
RUST_LTO_CFLAGS=$(MOZ_LTO_CFLAGS)
endif
export CFLAGS_$(rust_cc_env_name)=$(CC_BASE_FLAGS) $(RUST_LTO_CFLAGS) $(COMPUTED_CFLAGS) $(filter-out -fprofile-generate%,$(PGO_CFLAGS)) -DMOZILLA_CONFIG_H
export CXXFLAGS_$(rust_cc_env_name)=$(CXX_BASE_FLAGS) $(RUST_LTO_CFLAGS) $(COMPUTED_CXXFLAGS) $(filter-out -fprofile-generate%,$(PGO_CFLAGS)) -DMOZILLA_CONFIG_H
else
# Because cargo doesn't allow to distinguish builds happening for build
# scripts/procedural macros vs. those happening for the rust target,
# we can't blindly pass all our flags down for cc-rs to use them, because of the
# side effects they can have on what otherwise should be host builds.
# So for sanitizer and coverage builds, we only pass the base compiler flags.
# This means C code built by rust is not going to be covered by sanitizers
# and coverage. But at least we control what compiler is being used,
# rather than relying on cc-rs guesses, which, sometimes fail us.
# -fno-sized-deallocation is important, though, as -fsized-deallocation may be the
# compiler default and we don't want it to be used
# (see build/moz.configure/flags.configure). Likewise with -fno-aligned-new.
export CFLAGS_$(rust_host_cc_env_name)=$(HOST_CC_BASE_FLAGS)
export CXXFLAGS_$(rust_host_cc_env_name)=$(HOST_CXX_BASE_FLAGS)
export CFLAGS_$(rust_cc_env_name)=$(CC_BASE_FLAGS)
export CXXFLAGS_$(rust_cc_env_name)=$(CXX_BASE_FLAGS) $(filter -fno-aligned-new -fno-sized-deallocation,$(COMPUTED_CXXFLAGS))
endif

# When host == target, cargo will compile build scripts with sanitizers enabled
# if sanitizers are enabled, which may randomly fail when they execute
# because of https://github.com/google/sanitizers/issues/1322.
# Work around by disabling __tls_get_addr interception (bug 1635327).
ifeq ($(RUST_TARGET),$(RUST_HOST_TARGET))
define sanitizer_options
ifdef MOZ_$1
export $1_OPTIONS:=$$($1_OPTIONS:%=%:)intercept_tls_get_addr=0
endif
endef
$(foreach san,ASAN TSAN UBSAN,$(eval $(call sanitizer_options,$(san))))
endif

# Force the target down to all bindgen callers, even those that may not
# read BINDGEN_SYSTEM_FLAGS some way or another.
export BINDGEN_EXTRA_CLANG_ARGS:=$(filter --target=%,$(BINDGEN_SYSTEM_FLAGS))
export CARGO_TARGET_DIR
export RUSTFLAGS
export RUSTC
export RUSTDOC
export RUSTDOCFLAGS
export RUSTFMT
export LIBCLANG_PATH=$(MOZ_LIBCLANG_PATH)
export CLANG_PATH=$(MOZ_CLANG_PATH)
export PKG_CONFIG
export PKG_CONFIG_ALLOW_CROSS=1
export PKG_CONFIG_PATH
ifneq (,$(PKG_CONFIG_SYSROOT_DIR))
export PKG_CONFIG_SYSROOT_DIR
endif
ifneq (,$(PKG_CONFIG_LIBDIR))
export PKG_CONFIG_LIBDIR
endif
export RUST_BACKTRACE=full
export MOZ_TOPOBJDIR=$(topobjdir)
export MOZ_FOLD_LIBS
export PYTHON3
export CARGO_PROFILE_RELEASE_OPT_LEVEL
export CARGO_PROFILE_DEV_OPT_LEVEL

# Set COREAUDIO_SDK_PATH for third_party/rust/coreaudio-sys/build.rs
ifeq ($(OS_ARCH), Darwin)
ifdef MACOS_SDK_DIR
export COREAUDIO_SDK_PATH=$(MACOS_SDK_DIR)
endif
ifdef IPHONEOS_SDK_DIR
export COREAUDIO_SDK_PATH=$(IPHONEOS_SDK_DIR)
# export for build/macosx/xcrun
export IPHONEOS_SDK_DIR
PATH := $(topsrcdir)/build/macosx:$(PATH)
endif
endif
# Use the same prefix as set through modules/zlib/src/mozzconf.h
# for libz-rs-sys, since we still use the headers from there.
export LIBZ_RS_SYS_PREFIX=MOZ_Z_

ifndef RUSTC_BOOTSTRAP
RUSTC_BOOTSTRAP := mozglue_static,qcms
ifdef MOZ_RUST_SIMD
RUSTC_BOOTSTRAP := $(RUSTC_BOOTSTRAP),encoding_rs,any_all_workaround
endif
export RUSTC_BOOTSTRAP
endif

target_rust_ltoable := force-cargo-library-build $(ADD_RUST_LTOABLE)
target_rust_nonltoable := force-cargo-test-run force-cargo-program-build

ifdef MOZ_PGO_RUST
ifdef MOZ_PROFILE_GENERATE
rust_pgo_flags := -C profile-generate=$(topobjdir)
ifeq (1,$(words $(filter 5.% 6.% 7.% 8.% 9.% 10.% 11.%,$(CC_VERSION) $(RUSTC_LLVM_VERSION))))
# Disable value profiling when:
# (RUSTC_LLVM_VERSION < 12 and CC_VERSION >= 12) or (RUSTC_LLVM_VERSION >= 12 and CC_VERSION < 12)
rust_pgo_flags += -C llvm-args=--disable-vp=true
endif
# The C compiler may be passed extra llvm flags for PGO that we also want to pass to rust as well.
# In PROFILE_GEN_CFLAGS, they look like "-mllvm foo", and we want "-C llvm-args=foo", so first turn
# "-mllvm foo" into "-mllvm:foo" so that it becomes a unique argument, that we can then filter for,
# excluding other flags, and then turn into the right string.
rust_pgo_flags += $(patsubst -mllvm:%,-C llvm-args=%,$(filter -mllvm:%,$(subst -mllvm ,-mllvm:,$(PROFILE_GEN_CFLAGS))))
else # MOZ_PROFILE_USE
rust_pgo_flags := -C profile-use=$(PGO_PROFILE_PATH)
endif
endif

# Work around https://github.com/rust-lang/rust/issues/112480
ifdef MOZ_DEBUG_RUST
ifneq (,$(filter i686-pc-windows-%,$(RUST_TARGET)))
RUSTFLAGS += -Zmir-enable-passes=-CheckAlignment
RUSTC_BOOTSTRAP := 1
endif
endif

$(target_rust_ltoable): RUSTFLAGS:=$(rustflags_override) $(rustflags_sancov) $(RUSTFLAGS) $(rust_pgo_flags) \
								$(if $(MOZ_LTO_RUST_CROSS),\
								    -Clinker-plugin-lto \
									,)
$(target_rust_nonltoable): RUSTFLAGS:=$(rustflags_override) $(rustflags_sancov) $(RUSTFLAGS)

TARGET_RECIPES := $(target_rust_ltoable) $(target_rust_nonltoable)

HOST_RECIPES := \
  $(foreach a,library program,$(foreach b,build check udeps clippy,force-cargo-host-$(a)-$(b)))

$(HOST_RECIPES): RUSTFLAGS:=$(rustflags_override)

# If this is a release build we want rustc to generate one codegen unit per
# crate. This results in better optimization and less code duplication at the
# cost of longer compile times.
ifndef DEVELOPER_OPTIONS
$(TARGET_RECIPES) $(HOST_RECIPES): RUSTFLAGS += -C codegen-units=1
endif

# We use the + prefix to pass down the jobserver fds to cargo, but we
# don't use the prefix when make -n is used, so that cargo doesn't run
# in that case)
define RUN_CARGO_INNER
$(if $(findstring n,$(filter-out --%, $(MAKEFLAGS))),,+)$(CARGO) $(1) $(cargo_build_flags) $(CARGO_EXTRA_FLAGS) $(cargo_extra_cli_flags)
endef

ifdef CARGO_CONTINUE_ON_ERROR
define RUN_CARGO
-$(RUN_CARGO_INNER)
endef
else
define RUN_CARGO
$(RUN_CARGO_INNER)
endef
endif

# This function is intended to be called by:
#
#   $(call CARGO_BUILD,EXTRA_ENV_VAR1=X EXTRA_ENV_VAR2=Y ...)
#
# but, given the idiosyncracies of make, can also be called without arguments:
#
#   $(call CARGO_BUILD)
define CARGO_BUILD
$(call RUN_CARGO,rustc$(if $(BUILDSTATUS), --timings)$(if $(findstring k,$(filter-out --%, $(MAKEFLAGS))), --keep-going))
endef

cargo_host_linker_env_var := CARGO_TARGET_$(call varize,$(RUST_HOST_TARGET))_LINKER
cargo_linker_env_var := CARGO_TARGET_$(call varize,$(RUST_TARGET))_LINKER

export MOZ_CLANG_NEWER_THAN_RUSTC_LLVM
export MOZ_CARGO_WRAP_LDFLAGS
export MOZ_CARGO_WRAP_LD
export MOZ_CARGO_WRAP_LD_CXX
export MOZ_CARGO_WRAP_HOST_LDFLAGS
export MOZ_CARGO_WRAP_HOST_LD
export MOZ_CARGO_WRAP_HOST_LD_CXX
# Exporting from make always exports a value. Setting a value per-recipe
# would export an empty value for the host recipes. When not doing a
# cross-compile, the --target for those is the same, and cargo will use
# CARGO_TARGET_*_LINKER for its linker, so we always pass the
# cargo-linker wrapper, and fill MOZ_CARGO_WRAP_{HOST_,}LD* more or less
# appropriately for all recipes.
ifeq (WINNT,$(HOST_OS_ARCH))
# Use .bat wrapping on Windows hosts, and shell wrapping on other hosts.
# Like for CC/C*FLAGS, we want the target values to trump the host values when
# both variables are the same.
export $(cargo_host_linker_env_var):=$(topsrcdir)/build/cargo-host-linker.bat
export $(cargo_linker_env_var):=$(topsrcdir)/build/cargo-linker.bat
WRAP_HOST_LINKER_LIBPATHS:=$(HOST_LINKER_LIBPATHS_BAT)
else
export $(cargo_host_linker_env_var):=$(topsrcdir)/build/cargo-host-linker
export $(cargo_linker_env_var):=$(topsrcdir)/build/cargo-linker
WRAP_HOST_LINKER_LIBPATHS:=$(HOST_LINKER_LIBPATHS)
endif

# Cargo needs the same linker flags as the C/C++ compiler,
# but not the final libraries. Filter those out because they
# cause problems on macOS 10.7; see bug 1365993 for details.
# Also, we don't want to pass PGO flags until cargo supports them.
# Finally, we also remove the -Wl,--build-id=uuid flag when it's in
# the LDFLAGS. The flag was chosen over the default (build-id=sha1)
# in developer builds, because for libxul, it's faster. But it's also
# non-deterministic. So when the rust compiler produces procedural
# macros as libraries, they're not reproducible. Those procedural
# macros then end up as dependencies of other crates, and their
# non-reproducibility leads to sccache transitively having cache
# misses.
$(TARGET_RECIPES): MOZ_CARGO_WRAP_LDFLAGS:=$(filter-out -fsanitize=cfi% -framework Cocoa -lobjc AudioToolbox ExceptionHandling -fprofile-% -Wl$(COMMA)--build-id=uuid,$(LDFLAGS))

# When building with sanitizer, rustc links its own runtime, which conflicts
# with the one that passing -fsanitize=* to the linker would add.
# Ideally, we'd always do this filtering, but because the flags may also apply
# to build scripts because cargo doesn't allow the distinction, we only filter
# when building programs, except when using thread sanitizer where we filter
# everywhere.
ifneq (,$(filter -Zsanitizer=%,$(RUSTFLAGS)))
$(if $(filter -Zsanitizer=thread,$(RUSTFLAGS)),$(TARGET_RECIPES),force-cargo-program-build): MOZ_CARGO_WRAP_LDFLAGS:=$(filter-out -fsanitize=%,$(MOZ_CARGO_WRAP_LDFLAGS))
endif

# Rustc assumes that *-windows-gnu targets build with mingw-gcc and manually
# add runtime libraries that don't exist with mingw-clang. We created dummy
# libraries in $(topobjdir)/build/win32, but that's not enough, because some
# of the wanted symbols that come from these libraries are available in a
# different library, that we add manually. We also need to avoid rustc
# passing -nodefaultlibs to clang so that it adds clang_rt.
ifeq (WINNT_clang,$(OS_ARCH)_$(CC_TYPE))
force-cargo-program-build: MOZ_CARGO_WRAP_LDFLAGS+=-L$(topobjdir)/build/win32 -lunwind
force-cargo-program-build: CARGO_RUSTCFLAGS += -C default-linker-libraries=yes
endif

# Rustc passes -nodefaultlibs to the linker (clang) on mac, which prevents
# clang from adding the necessary sanitizer runtimes when building with
# C/C++ sanitizer but without rust sanitizer.
ifeq (Darwin,$(OS_ARCH))
ifeq (,$(filter -Zsanitizer=%,$(RUSTFLAGS)))
ifneq (,$(filter -fsanitize=%,$(LDFLAGS)))
$(TARGET_RECIPES): RUSTFLAGS += -C default-linker-libraries=yes
endif
endif
endif

$(HOST_RECIPES): MOZ_CARGO_WRAP_LDFLAGS:=$(HOST_LDFLAGS) $(WRAP_HOST_LINKER_LIBPATHS)
$(TARGET_RECIPES) $(HOST_RECIPES): MOZ_CARGO_WRAP_HOST_LDFLAGS:=$(HOST_LDFLAGS) $(WRAP_HOST_LINKER_LIBPATHS)

ifeq (,$(filter clang-cl,$(CC_TYPE)))
$(TARGET_RECIPES): MOZ_CARGO_WRAP_LD:=$(CC)
$(TARGET_RECIPES): MOZ_CARGO_WRAP_LD_CXX:=$(CXX)
else
$(TARGET_RECIPES): MOZ_CARGO_WRAP_LD:=$(LINKER)
$(TARGET_RECIPES): MOZ_CARGO_WRAP_LD_CXX:=$(LINKER)
endif

ifeq (,$(filter clang-cl,$(HOST_CC_TYPE)))
$(HOST_RECIPES): MOZ_CARGO_WRAP_LD:=$(HOST_CC)
$(HOST_RECIPES): MOZ_CARGO_WRAP_LD_CXX:=$(HOST_CXX)
$(TARGET_RECIPES) $(HOST_RECIPES): MOZ_CARGO_WRAP_HOST_LD:=$(HOST_CC)
$(TARGET_RECIPES) $(HOST_RECIPES): MOZ_CARGO_WRAP_HOST_LD_CXX:=$(HOST_CXX)
else
$(HOST_RECIPES): MOZ_CARGO_WRAP_LD:=$(HOST_LINKER)
$(HOST_RECIPES): MOZ_CARGO_WRAP_LD_CXX:=$(HOST_LINKER)
$(TARGET_RECIPES) $(HOST_RECIPES): MOZ_CARGO_WRAP_HOST_LD:=$(HOST_LINKER)
$(TARGET_RECIPES) $(HOST_RECIPES): MOZ_CARGO_WRAP_HOST_LD_CXX:=$(HOST_LINKER)
endif

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
# need to be preserved, but $(foreach) splits on spaces, so we replace escaped
# spaces with some unlikely string for the foreach, and replace them back in the
# loop itself.
define make_cargo_rule
$(notdir $(1))_deps := $$(call normalize_sep,$$(wordlist 2, 10000000, $$(if $$(wildcard $(basename $(1)).d),$$(shell cat $(basename $(1)).d))))
$(1): $(CARGO_FILE) $(3) $(topsrcdir)/Cargo.lock $$(if $$($(notdir $(1))_deps),$$($(notdir $(1))_deps),$(2))
	$$(REPORT_BUILD)
	$$(if $$($(notdir $(1))_deps),+$(MAKE) $(2),:)

$$(foreach dep, $$(subst \ ,_^_^_^_,$$($(notdir $(1))_deps)),$$(eval $$(call make_default_rule,$$(subst _^_^_^_,\ ,$$(dep)))))
endef

ifdef RUST_LIBRARY_FILE

rust_features_flag := --features '$(if $(RUST_LIBRARY_FEATURES),$(RUST_LIBRARY_FEATURES) )mozilla-central-workspace-hack'

ifeq (WASI,$(OS_ARCH))
# The rust wasi target defaults to statically link the wasi crt, but when we
# build static libraries from rust and link them with C/C++ code, we also link
# a wasi crt, which may conflict with rust's.
force-cargo-library-build: CARGO_RUSTCFLAGS += -C target-feature=-crt-static
endif

# Assume any system libraries rustc links against are already in the target's LIBS.
#
# We need to run cargo unconditionally, because cargo is the only thing that
# has full visibility into how changes in Rust sources might affect the final
# build.
force-cargo-library-build:
	$(call BUILDSTATUS,START_Rust $(notdir $(RUST_LIBRARY_FILE)))
	$(call CARGO_BUILD) --lib $(cargo_target_flag) $(rust_features_flag) -- $(cargo_rustc_flags)
	$(call BUILDSTATUS,END_Rust $(notdir $(RUST_LIBRARY_FILE)))
# When we are building in --enable-release mode; we add an additional check to confirm
# that we are not importing any networking-related functions in rust code. This reduces
# the chance of proxy bypasses originating from rust code.
# The check only works when rust code is built with -Clto but without MOZ_LTO_RUST_CROSS.
# Sanitizers and sancov also fail because compiler-rt hooks network functions.
ifndef MOZ_PROFILE_GENERATE
ifeq ($(OS_ARCH), Linux)
ifeq (,$(rustflags_sancov)$(MOZ_ASAN)$(MOZ_TSAN)$(MOZ_UBSAN))
ifndef MOZ_LTO_RUST_CROSS
ifneq (,$(filter -Clto,$(cargo_rustc_flags)))
	$(call py_action,check_binary $(@F),--networking $(RUST_LIBRARY_FILE))
endif
endif
endif
endif
endif

$(eval $(call make_cargo_rule,$(RUST_LIBRARY_FILE),force-cargo-library-build))

SUGGEST_INSTALL_ON_FAILURE = (ret=$$?; if [ $$ret = 101 ]; then echo If $1 is not installed, install it using: cargo install $1; fi; exit $$ret)

ifndef CARGO_NO_AUTO_ARG
force-cargo-library-%:
	$(call RUN_CARGO,$*) --lib $(cargo_target_flag) $(rust_features_flag) || $(call SUGGEST_INSTALL_ON_FAILURE,cargo-$*)
else
force-cargo-library-%:
	$(call RUN_CARGO,$*) || $(call SUGGEST_INSTALL_ON_FAILURE,cargo-$*)
endif

else
force-cargo-library-%:
	@true

endif # RUST_LIBRARY_FILE

ifdef RUST_TESTS

rust_test_options := $(foreach test,$(RUST_TESTS),-p $(test))

rust_test_features_flag := --features '$(if $(RUST_TEST_FEATURES),$(RUST_TEST_FEATURES) )mozilla-central-workspace-hack'

# Don't stop at the first failure. We want to list all failures together.
rust_test_flag := --no-fail-fast

force-cargo-test-run:
	$(call RUN_CARGO,test $(cargo_target_flag) $(rust_test_flag) $(rust_test_options) $(rust_test_features_flag))

endif # RUST_TESTS

ifdef HOST_RUST_LIBRARY_FILE

host_rust_features_flag := --features '$(if $(HOST_RUST_LIBRARY_FEATURES),$(HOST_RUST_LIBRARY_FEATURES) )mozilla-central-workspace-hack'

force-cargo-host-library-build:
	$(call BUILDSTATUS,START_Rust $(notdir $(HOST_RUST_LIBRARY_FILE)))
	$(call CARGO_BUILD) --lib $(cargo_host_flag) $(host_rust_features_flag)
	$(call BUILDSTATUS,END_Rust $(notdir $(HOST_RUST_LIBRARY_FILE)))

$(eval $(call make_cargo_rule,$(HOST_RUST_LIBRARY_FILE),force-cargo-host-library-build))

ifndef CARGO_NO_AUTO_ARG
force-cargo-host-library-%:
	$(call RUN_CARGO,$*) --lib $(cargo_host_flag) $(host_rust_features_flag)
else
force-cargo-host-library-%:
	$(call RUN_CARGO,$*) --lib $(filter-out --release $(cargo_host_flag)) $(host_rust_features_flag)
endif

else
force-cargo-host-library-%:
	@true
endif # HOST_RUST_LIBRARY_FILE

ifdef RUST_PROGRAMS

program_features_flag := --features mozilla-central-workspace-hack

force-cargo-program-build: $(call resfile,module)
	$(call BUILDSTATUS,START_Rust $(RUST_CARGO_PROGRAMS))
	$(call CARGO_BUILD) $(addprefix --bin ,$(RUST_CARGO_PROGRAMS)) $(cargo_target_flag) $(program_features_flag) -- $(addprefix -C link-arg=$(CURDIR)/,$(call resfile,module)) $(CARGO_RUSTCFLAGS)
	$(call BUILDSTATUS,END_Rust $(RUST_CARGO_PROGRAMS))

$(foreach RUST_PROGRAM,$(RUST_PROGRAMS), $(eval $(call make_cargo_rule,$(RUST_PROGRAM),force-cargo-program-build,$(call resfile,module))))

ifndef CARGO_NO_AUTO_ARG
force-cargo-program-%:
	$(call RUN_CARGO,$*) $(addprefix --bin ,$(RUST_CARGO_PROGRAMS)) $(cargo_target_flag) $(program_features_flag)
else
force-cargo-program-%:
	$(call RUN_CARGO,$*)
endif

else
force-cargo-program-%:
	@true
endif # RUST_PROGRAMS
ifdef HOST_RUST_PROGRAMS

host_program_features_flag := --features mozilla-central-workspace-hack

force-cargo-host-program-build:
	$(call BUILDSTATUS,START_Rust $(HOST_RUST_CARGO_PROGRAMS))
	$(call CARGO_BUILD) $(addprefix --bin ,$(HOST_RUST_CARGO_PROGRAMS)) $(cargo_host_flag) $(host_program_features_flag)
	$(call BUILDSTATUS,END_Rust $(HOST_RUST_CARGO_PROGRAMS))

$(foreach HOST_RUST_PROGRAM,$(HOST_RUST_PROGRAMS), $(eval $(call make_cargo_rule,$(HOST_RUST_PROGRAM),force-cargo-host-program-build)))

ifndef CARGO_NO_AUTO_ARG
force-cargo-host-program-%:
	$(call BUILDSTATUS,START_Rust $(HOST_RUST_CARGO_PROGRAMS))
	$(call RUN_CARGO,$*) $(addprefix --bin ,$(HOST_RUST_CARGO_PROGRAMS)) $(cargo_host_flag) $(host_program_features_flag)
	$(call BUILDSTATUS,END_Rust $(HOST_RUST_CARGO_PROGRAMS))
else
force-cargo-host-program-%:
	$(call RUN_CARGO,$*) $(addprefix --bin ,$(HOST_RUST_CARGO_PROGRAMS)) $(filter-out --release $(cargo_target_flag))
endif

else
force-cargo-host-program-%:
	@true

endif # HOST_RUST_PROGRAMS
