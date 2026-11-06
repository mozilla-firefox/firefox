# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Compose Cargo commands and environments for Rust build edges."""

from dataclasses import asdict, dataclass, fields

from mozshellutil import quote as shell_quote
from mozshellutil import split as shell_split


def _as_str(v):
    if v is None:
        return ""
    if isinstance(v, (list, tuple)):
        return " ".join(v)
    return str(v)


def _as_list(v):
    if v is None:
        return []
    if isinstance(v, (list, tuple)):
        return list(v)
    if isinstance(v, str):
        return v.split()
    return [str(v)]


def _as_args(v):
    """Split a configured command fragment into arguments.

    A ``nargs=1`` option holds the whole fragment in one element, so the value
    is joined before it is split.
    """
    return shell_split(_as_str(v))


def _bool(v):
    if v is None:
        return False
    if isinstance(v, bool):
        return v
    return str(v).strip() not in ("", "0")


# Every configure substitution the Cargo commands read, and how each one is read.
# A spec carries these and nothing else, CargoConfig converts every value it
# holds once, and it rejects a read of anything absent from here.
CARGO_CONFIG_KEYS = {
    "AR": _as_str,
    "BINDGEN_EXTRA_CLANG_ARGS": _as_list,
    "CARGO": _as_str,
    "CARGOFLAGS": _as_args,
    "CARGO_INCREMENTAL": _as_str,
    "CARGO_PROFILE_DEV_BUILD_OVERRIDE_TRIM_PATHS": _as_str,
    "CARGO_PROFILE_DEV_OPT_LEVEL": _as_str,
    "CARGO_PROFILE_RELEASE_BUILD_OVERRIDE_TRIM_PATHS": _as_str,
    "CARGO_PROFILE_RELEASE_OPT_LEVEL": _as_str,
    "CC_KNOWN_WRAPPER_CUSTOM": _as_str,
    "DLL_PREFIX": _as_str,
    "DLL_SUFFIX": _as_str,
    "GLEAN_PARSER_VENV": _as_str,
    "HOST_AR": _as_str,
    "IPHONEOS_SDK_DIR": _as_str,
    "MOZ_ASAN": _bool,
    "MOZ_CARGO_BUILD_STD_ARGS": _as_list,
    "MOZ_CARGO_CC": _as_list,
    "MOZ_CARGO_CC_ENV_SUFFIX": _as_str,
    "MOZ_CARGO_CFLAGS_BASE": _as_list,
    "MOZ_CARGO_CFLAGS_FILTER": _as_list,
    "MOZ_CARGO_CLIPPY_WRAPPER": _as_str,
    "MOZ_CARGO_CXX": _as_list,
    "MOZ_CARGO_CXXFLAGS_BASE": _as_list,
    "MOZ_CARGO_CXXFLAGS_FILTER": _as_list,
    "MOZ_CARGO_DEFAULT_PROFILE_ARGS": _as_list,
    "MOZ_CARGO_DEFAULT_PROFILE_DIR": _as_str,
    "MOZ_CARGO_FROZEN_ARGS": _as_list,
    "MOZ_CARGO_HOST_CC": _as_list,
    "MOZ_CARGO_HOST_CC_ENV_SUFFIX": _as_str,
    "MOZ_CARGO_HOST_CFLAGS_BASE": _as_list,
    "MOZ_CARGO_HOST_CFLAGS_FILTER": _as_list,
    "MOZ_CARGO_HOST_CXX": _as_list,
    "MOZ_CARGO_HOST_CXXFLAGS_BASE": _as_list,
    "MOZ_CARGO_HOST_CXXFLAGS_FILTER": _as_list,
    "MOZ_CARGO_HOST_LD": _as_list,
    "MOZ_CARGO_HOST_LDFLAGS": _as_list,
    "MOZ_CARGO_HOST_LD_CXX": _as_list,
    "MOZ_CARGO_HOST_LINKER": _as_str,
    "MOZ_CARGO_HOST_LINKER_ENV_VAR": _as_str,
    "MOZ_CARGO_HOST_TARGET_ARGS": _as_list,
    "MOZ_CARGO_LD": _as_list,
    "MOZ_CARGO_LDFLAGS_FILTER_OUT": _as_list,
    "MOZ_CARGO_LD_CXX": _as_list,
    "MOZ_CARGO_LINKER": _as_str,
    "MOZ_CARGO_LINKER_ENV_VAR": _as_str,
    "MOZ_CARGO_PROFILE_PREFIX": _as_str,
    "MOZ_CARGO_PROGRAM_LDFLAGS_FILTER_OUT": _as_list,
    "MOZ_CARGO_TARGET_ARGS": _as_list,
    "MOZ_CLANG_NEWER_THAN_RUSTC_LLVM": _as_str,
    "MOZ_CLANG_PATH": _as_str,
    "MOZ_FOLD_LIBS": _as_str,
    "MOZ_LIBCLANG_PATH": _as_str,
    "MOZ_LTO_LDFLAGS": _as_list,
    "MOZ_LTO_OBJECT_PATH": _bool,
    "MOZ_LTO_RUST_CROSS": _bool,
    "MOZ_PROFILE_GENERATE": _bool,
    "MOZ_RUSTC_BOOTSTRAP_DEFAULT": _as_str,
    "MOZ_RUSTC_BOOTSTRAP_FORCE": _as_str,
    "MOZ_RUSTC_WRAPPER": _as_str,
    "MOZ_RUSTFLAGS_AFTER_EXTRA": _as_list,
    "MOZ_RUSTFLAGS_CODEGEN": _as_list,
    "MOZ_RUSTFLAGS_DEFAULT_LINKER_LIBRARIES": _as_list,
    "MOZ_RUSTFLAGS_TARGET_COMMON": _as_list,
    "MOZ_RUSTFLAGS_TARGET_LTOABLE": _as_list,
    "MOZ_RUST_COREAUDIO_SDK_PATH": _as_str,
    "MOZ_RUST_DEFAULT_FLAGS": _as_list,
    "MOZ_RUST_LIBRARY_RUSTCFLAGS": _as_list,
    "MOZ_RUST_SANITIZER_OPTION_VARS": _as_list,
    "MOZ_TSAN": _bool,
    "MOZ_UBSAN": _bool,
    "OS_ARCH": _as_str,
    "PKG_CONFIG": _as_str,
    "PKG_CONFIG_LIBDIR": _as_str,
    "PKG_CONFIG_PATH": _as_str,
    "PKG_CONFIG_SYSROOT_DIR": _as_str,
    "PYTHON3": _as_str,
    "RUSTC": _as_str,
    "RUSTDOC": _as_str,
    "RUSTDOCFLAGS": _as_str,
    "RUSTFLAGS": _as_list,
    "RUSTFMT": _as_str,
    "RUST_LTO_CFLAGS": _as_list,
    "RUST_LTO_ELIGIBLE": _bool,
    "RUST_PGO_CFLAGS": _as_list,
    "RUST_PGO_LDFLAGS": _as_list,
    "RUST_SANCOV_FLAGS": _as_list,
    "RUST_TARGET": _as_str,
}

# Serialized command filename for each Rust edge kind.
CARGO_SPEC_FILES = {
    "library": ".cargo-library-spec.json",
    "host-library": ".cargo-host-library-spec.json",
    "program": ".cargo-program-spec.json",
    "host-program": ".cargo-host-program-spec.json",
    "test": ".cargo-tests-spec.json",
}

# The Cargo environment adds its own LTO and PGO flags around the computed
# flags, and Cargo host builds never received `NSPR_CFLAGS`, so these categories
# are left out of the flags passed to Cargo.
RUST_EXCLUDED_FLAG_CATEGORIES = ("LTO", "NSPR", "PGO", "PGO_DEFINES")


@dataclass
class CargoCommand:
    """Declarative metadata for one Cargo build edge.

    ``names`` select what Cargo builds, interpreted by ``kind``: the library
    file name, the program binary names, or the test package names.
    ``rustflags`` and ``rustc_flags`` hold the edge's own additions to
    ``RUSTFLAGS`` and to the flags after ``--``. ``lto`` is whether a library
    uses link time optimization in a build that enables it for Rust libraries.
    ``outputs`` are the library or program files Cargo leaves in its target
    directory, relative to the object directory.
    """

    kind: str
    manifest_path: str
    working_directory: str
    names: tuple = ()
    outputs: tuple = ()
    features: tuple = ()
    cargo_profile_suffix: str = ""
    cargo_crate_type: str = ""
    lto: bool = True
    computed_cflags: tuple = ()
    computed_cxxflags: tuple = ()
    computed_host_cflags: tuple = ()
    computed_host_cxxflags: tuple = ()
    link_flags: tuple = ()
    rustflags: tuple = ()
    rustc_flags: tuple = ()

    def __post_init__(self):
        if self.kind not in CARGO_SPEC_FILES:
            raise ValueError(f"Unknown Rust build kind: {self.kind!r}")
        if not self.working_directory:
            raise ValueError(f"Rust build edge {self.kind!r} has no working directory")
        for field in fields(self):
            if field.type is tuple:
                setattr(self, field.name, tuple(getattr(self, field.name)))


# Space separated names of the `RUST_TESTS` crates `./mach rusttests` runs.
TEST_SELECTION_ENV = "MOZ_RUST_TEST_SELECTION"


@dataclass
class CargoInvocation:
    """State that varies per Cargo run and belongs to neither an edge nor configure.

    ``color`` is ``always``, ``never``, or empty to leave Cargo's default.
    ``rustc_bootstrap`` replaces the configured ``RUSTC_BOOTSTRAP`` default.
    """

    verbose: bool = False
    json_output: bool = False
    color: str = ""
    extra_rustflags: tuple = ()
    cargo_rustcflags: tuple = ()
    cargo_extra_flags: tuple = ()
    rustc_bootstrap: str = ""
    test_crates: tuple = ()

    @classmethod
    def from_environ(cls, environ):
        color = ""
        if _bool(environ.get("MACH_STDOUT_ISATTY")):
            color = "never" if _bool(environ.get("NO_ANSI")) else "always"
        return cls(
            verbose=_bool(environ.get("BUILD_VERBOSE_LOG")),
            color=color,
            extra_rustflags=tuple((environ.get("extra_rustflags") or "").split()),
            cargo_rustcflags=tuple(_as_args(environ.get("CARGO_RUSTCFLAGS"))),
            cargo_extra_flags=tuple(_as_args(environ.get("CARGO_EXTRA_FLAGS"))),
            rustc_bootstrap=environ.get("RUSTC_BOOTSTRAP") or "",
            test_crates=tuple((environ.get(TEST_SELECTION_ENV) or "").split()),
        )


def _copied(value):
    """A value a caller may extend without reaching back into the config."""
    return list(value) if isinstance(value, list) else value


class CargoConfig:
    """The configure substitutions a Cargo command may read.

    Every value arrives in the representation ``CARGO_CONFIG_KEYS`` declares for
    its key, so a read needs no conversion, and a key the substitutions do not
    carry reads as that representation's empty value. Reading a key that
    ``CARGO_CONFIG_KEYS`` does not declare raises, because a spec only carries
    the declared ones.
    """

    def __init__(self, substs):
        self._values = {
            key: CARGO_CONFIG_KEYS[key](value)
            for key, value in substs.items()
            if key in CARGO_CONFIG_KEYS
        }

    def _check(self, key):
        if key not in CARGO_CONFIG_KEYS:
            raise KeyError(f"{key} is not declared in CARGO_CONFIG_KEYS")

    def __getitem__(self, key):
        self._check(key)
        return _copied(self._values[key])

    def __contains__(self, key):
        self._check(key)
        return key in self._values

    def get(self, key, default=None):
        self._check(key)
        if key not in self._values:
            return CARGO_CONFIG_KEYS[key](default)
        return _copied(self._values[key])


def _cargo_config(substs):
    """The substitutions as a ``CargoConfig``, converting them if they are not one."""
    return substs if isinstance(substs, CargoConfig) else CargoConfig(substs)


def cargo_spec(command, substs, topsrcdir, topobjdir):
    """Everything one Rust build edge needs, in serializable form.

    The spec is the only input the ``run_cargo`` action reads, so it carries
    every configure substitution the Cargo commands read.
    """
    if isinstance(substs, CargoConfig):
        raise TypeError(
            "cargo_spec takes the raw substitutions, because loading the spec "
            "converts them and a converted value cannot be converted again"
        )
    return {
        "config": {k: substs[k] for k in sorted(CARGO_CONFIG_KEYS) if k in substs},
        "edge": asdict(command),
        "topobjdir": topobjdir,
        "topsrcdir": topsrcdir,
    }


def load_cargo_spec(data):
    """Return the command, substs, topsrcdir and topobjdir held in a spec."""
    return (
        CargoCommand(**data["edge"]),
        CargoConfig(data["config"]),
        data["topsrcdir"],
        data["topobjdir"],
    )


def _matches(flag, pattern):
    """Whether a Make filter pattern matches, where "%" matches any text."""
    if "%" not in pattern:
        return flag == pattern
    prefix, suffix = pattern.split("%", 1)
    return (
        len(flag) >= len(prefix) + len(suffix)
        and flag.startswith(prefix)
        and flag.endswith(suffix)
    )


def _filter(flags, patterns):
    return [f for f in flags if any(_matches(f, p) for p in patterns)]


def _filter_out(flags, patterns):
    return [f for f in flags if not any(_matches(f, p) for p in patterns)]


def _is_host(cmd):
    return cmd.kind.startswith("host-")


def _lto_object_stem(cmd):
    if _is_host(cmd):
        return ""
    if cmd.kind == "test":
        return "tests"
    return cmd.names[0] if cmd.names else ""


def _subcommand_args(cmd):
    """The Cargo arguments selecting which artifacts the edge builds."""
    args = []
    if cmd.kind == "test":
        for name in cmd.names:
            args += ["-p", name]
    elif cmd.kind in ("program", "host-program"):
        for name in cmd.names:
            args += ["--bin", name]
    return args


def _target_args(cmd, substs):
    if _is_host(cmd):
        return substs.get("MOZ_CARGO_HOST_TARGET_ARGS")
    return substs.get("MOZ_CARGO_TARGET_ARGS")


def _rustflags(cmd, substs, invocation, ltoable):
    """The composed RUSTFLAGS tokens for this edge."""
    flags = substs.get("MOZ_RUST_DEFAULT_FLAGS")

    # Allow tools such as clippy to inject extra driver flags (e.g. -W/-D) via an
    # environment variable. These are folded into RUSTFLAGS here (rather than
    # passed on the cargo CLI), which sidesteps any ordering issue with the `--`
    # separator and applies to both target and host edges.
    flags += invocation.extra_rustflags
    flags += substs.get("MOZ_RUSTFLAGS_AFTER_EXTRA")

    if not _is_host(cmd):
        flags += substs.get("RUST_SANCOV_FLAGS")
        flags += substs.get("RUSTFLAGS")
        flags += substs.get("MOZ_RUSTFLAGS_TARGET_COMMON")
        if ltoable:
            flags += substs.get("MOZ_RUSTFLAGS_TARGET_LTOABLE")

    flags += substs.get("MOZ_RUSTFLAGS_CODEGEN")

    if not _is_host(cmd):
        flags += substs.get("MOZ_RUSTFLAGS_DEFAULT_LINKER_LIBRARIES")

    flags += cmd.rustflags
    return flags


def _cargo_build_flags(cmd, substs, invocation, single_job=False):
    # Permit users to pass flags to cargo from their mozconfigs
    # (e.g. --color=always).
    flags = substs.get("CARGOFLAGS")

    if cmd.cargo_profile_suffix:
        flags += ["--profile", cargo_profile_dir(cmd, substs)]
    else:
        flags += substs.get("MOZ_CARGO_DEFAULT_PROFILE_ARGS")

    flags += substs.get("MOZ_CARGO_FROZEN_ARGS")
    flags += ["--manifest-path", cmd.manifest_path]

    if invocation.verbose:
        flags.append("-vv")

    if invocation.json_output:
        flags.append("--message-format=json")

    # Enable color output if original stdout was a TTY and color settings
    # aren't already present. This essentially restores the default behavior
    # of cargo when running via `mach`.
    if invocation.color and not any(f.startswith("--color") for f in flags):
        flags.append(f"--color={invocation.color}")

    # A jobserver with one job is not propagated to Cargo, so pass -j1 explicitly.
    if single_job:
        flags.append("-j1")

    flags += substs.get("MOZ_CARGO_BUILD_STD_ARGS")

    return flags


def cargo_profile_dir(cmd, substs):
    """The directory Cargo writes the edge's profile into, under the target triple."""
    substs = _cargo_config(substs)
    if cmd.cargo_profile_suffix:
        return f"{substs.get('MOZ_CARGO_PROFILE_PREFIX')}-{cmd.cargo_profile_suffix}"
    return substs.get("MOZ_CARGO_DEFAULT_PROFILE_DIR")


def applies_library_lto(kind, lto, substs):
    """Return whether a Rust library edge uses LTO.

    Release builds enable link time optimization unless the library opted out.
    """
    return bool(
        kind == "library" and _cargo_config(substs).get("RUST_LTO_ELIGIBLE") and lto
    )


def _rustc_flags(cmd, substs, invocation):
    """The flags passed after `--` to `cargo rustc`.

    They apply only to the final rustc invocation, so only to the top level
    crate and not to its dependencies.
    """
    flags = list(cmd.rustc_flags)

    if cmd.kind == "library":
        flags += substs.get("MOZ_RUST_LIBRARY_RUSTCFLAGS")

    if applies_library_lto(cmd.kind, cmd.lto, substs):
        flags.append("-Clto")

    flags += invocation.cargo_rustcflags

    return flags


def _compiler_flags(substs, computed, host=False, cxx=False):
    prefix = f"MOZ_CARGO_{'HOST_' if host else ''}{'CXX' if cxx else 'C'}FLAGS"
    flags = list(computed)
    if not host:
        flags = substs.get("RUST_LTO_CFLAGS") + flags + substs.get("RUST_PGO_CFLAGS")
    return substs.get(f"{prefix}_BASE") + _filter(flags, substs.get(f"{prefix}_FILTER"))


def _assembled_target_ldflags(cmd, substs):
    # Assemble global LTO and PGO flags around the per directory linker flags.
    ldflags = (
        substs.get("MOZ_LTO_LDFLAGS")
        + list(cmd.link_flags)
        + substs.get("RUST_PGO_LDFLAGS")
    )
    # Keep macOS LTO objects in a separate directory for each edge so dsymutil
    # can read them without collisions.
    stem = _lto_object_stem(cmd)
    if stem and substs.get("MOZ_LTO_OBJECT_PATH"):
        ldflags.append(f"-Wl,-object_path_lto,{stem}.lto.o/")
    return ldflags


def _cargo_wrap_ldflags(cmd, substs):
    ldflags = _filter_out(
        _assembled_target_ldflags(cmd, substs),
        substs.get("MOZ_CARGO_LDFLAGS_FILTER_OUT"),
    )

    if cmd.kind == "program":
        ldflags = _filter_out(
            ldflags, substs.get("MOZ_CARGO_PROGRAM_LDFLAGS_FILTER_OUT")
        )

    return shell_quote(*ldflags)


def compose_env(
    cmd,
    substs,
    environ,
    invocation,
    topsrcdir,
    topobjdir,
    ltoable=None,
    subcommand="build",
):
    substs = _cargo_config(substs)
    env = dict(environ)

    library_search_path = {
        "Linux": ("LD_LIBRARY_PATH", ":"),
        "WINNT": ("PATH", ";"),
    }.get(substs.get("OS_ARCH"))
    if cmd.kind == "test" and library_search_path:
        # Find native libraries without depending on Cargo's output layout.
        var, separator = library_search_path
        libpath = f"{topobjdir}/dist/bin"
        if existing := env.get(var):
            libpath += f"{separator}{existing}"
        env[var] = libpath

    # Set both host and target tool variables for every edge. Only Cargo's
    # --target argument is edge specific.
    #
    # We start with host variables because the rust host and the rust target
    # might be the same, in which case we want the latter to take priority.
    host_suffix = substs.get("MOZ_CARGO_HOST_CC_ENV_SUFFIX")
    target_suffix = substs.get("MOZ_CARGO_CC_ENV_SUFFIX")

    env[f"CC_{host_suffix}"] = " ".join(substs.get("MOZ_CARGO_HOST_CC"))
    env[f"CXX_{host_suffix}"] = " ".join(substs.get("MOZ_CARGO_HOST_CXX"))
    env[f"AR_{host_suffix}"] = substs.get("HOST_AR")
    env[f"CC_{target_suffix}"] = " ".join(substs.get("MOZ_CARGO_CC"))
    env[f"CXX_{target_suffix}"] = " ".join(substs.get("MOZ_CARGO_CXX"))
    env[f"AR_{target_suffix}"] = substs.get("AR")

    # cc-rs may not know whether we are using a compiler wrapper, so explicitly
    # tell it that we do.
    if known_wrapper := substs.get("CC_KNOWN_WRAPPER_CUSTOM"):
        env["CC_KNOWN_WRAPPER_CUSTOM"] = known_wrapper

    env[f"CFLAGS_{host_suffix}"] = " ".join(
        _compiler_flags(substs, cmd.computed_host_cflags, host=True)
    )
    env[f"CXXFLAGS_{host_suffix}"] = " ".join(
        _compiler_flags(substs, cmd.computed_host_cxxflags, host=True, cxx=True)
    )
    env[f"CFLAGS_{target_suffix}"] = " ".join(
        _compiler_flags(substs, cmd.computed_cflags)
    )
    env[f"CXXFLAGS_{target_suffix}"] = " ".join(
        _compiler_flags(substs, cmd.computed_cxxflags, cxx=True)
    )

    if incremental := substs.get("CARGO_INCREMENTAL"):
        env["CARGO_INCREMENTAL"] = incremental

    if rustc_wrapper := substs.get("MOZ_RUSTC_WRAPPER"):
        env["RUSTC_WRAPPER"] = rustc_wrapper
    if subcommand == "clippy":
        env["RUSTC_WRAPPER"] = substs.get("MOZ_CARGO_CLIPPY_WRAPPER")

    env["CARGO_TARGET_DIR"] = topobjdir
    if ltoable is None:
        ltoable = cmd.kind == "library"
    rustflags = _rustflags(cmd, substs, invocation, ltoable)
    env["RUSTFLAGS"] = " ".join(rustflags)
    env["RUSTC"] = substs.get("RUSTC")
    env["RUSTDOC"] = substs.get("RUSTDOC")
    env["RUSTDOCFLAGS"] = substs.get("RUSTDOCFLAGS")
    env["RUSTFMT"] = substs.get("RUSTFMT")
    env["LIBCLANG_PATH"] = substs.get("MOZ_LIBCLANG_PATH")
    env["CLANG_PATH"] = substs.get("MOZ_CLANG_PATH")
    env["PKG_CONFIG_ALLOW_CROSS"] = "1"
    # A configured empty value clears an inherited one, which is how a sysroot
    # build keeps host search paths out of pkg-config.
    if "PKG_CONFIG" in substs:
        env["PKG_CONFIG"] = substs["PKG_CONFIG"]
    if "PKG_CONFIG_PATH" in substs:
        env["PKG_CONFIG_PATH"] = substs["PKG_CONFIG_PATH"]
    if "PKG_CONFIG_SYSROOT_DIR" in substs:
        env["PKG_CONFIG_SYSROOT_DIR"] = substs["PKG_CONFIG_SYSROOT_DIR"]
    if "PKG_CONFIG_LIBDIR" in substs:
        env["PKG_CONFIG_LIBDIR"] = substs["PKG_CONFIG_LIBDIR"]
    env["RUST_BACKTRACE"] = "full"
    env["MOZ_TOPSRCDIR"] = topsrcdir
    env["MOZ_TOPOBJDIR"] = topobjdir
    env["MOZ_FOLD_LIBS"] = substs.get("MOZ_FOLD_LIBS")
    env["GLEAN_PYTHON_VENV_DIR"] = substs.get("GLEAN_PARSER_VENV")
    env["PYTHON3"] = substs.get("PYTHON3")
    env["CARGO_PROFILE_RELEASE_OPT_LEVEL"] = substs.get(
        "CARGO_PROFILE_RELEASE_OPT_LEVEL"
    )
    env["CARGO_PROFILE_DEV_OPT_LEVEL"] = substs.get("CARGO_PROFILE_DEV_OPT_LEVEL")
    for profile in ("RELEASE", "DEV"):
        key = f"CARGO_PROFILE_{profile}_BUILD_OVERRIDE_TRIM_PATHS"
        if trim_paths := substs.get(key):
            env[key] = trim_paths
    env["BINDGEN_EXTRA_CLANG_ARGS"] = " ".join(substs.get("BINDGEN_EXTRA_CLANG_ARGS"))

    if coreaudio_sdk := substs.get("MOZ_RUST_COREAUDIO_SDK_PATH"):
        env["COREAUDIO_SDK_PATH"] = coreaudio_sdk
    if iphoneos_sdk := substs.get("IPHONEOS_SDK_DIR"):
        # For build/macosx/xcrun
        env["IPHONEOS_SDK_DIR"] = iphoneos_sdk

    # Use the same prefix as set through modules/zlib/src/mozzconf.h for
    # libz-rs-sys, since we still use the headers from there.
    env["LIBZ_RS_SYS_PREFIX"] = "MOZ_Z_"

    bootstrap = invocation.rustc_bootstrap or substs.get("MOZ_RUSTC_BOOTSTRAP_DEFAULT")
    if forced := substs.get("MOZ_RUSTC_BOOTSTRAP_FORCE"):
        bootstrap = forced
    env["RUSTC_BOOTSTRAP"] = bootstrap

    env["MOZ_CLANG_NEWER_THAN_RUSTC_LLVM"] = substs.get(
        "MOZ_CLANG_NEWER_THAN_RUSTC_LLVM"
    )

    # When not doing a cross compile, the --target of the host edges is the same
    # as the target one, and cargo will use CARGO_TARGET_*_LINKER for its linker,
    # so we always pass the cargo-linker wrapper and fill
    # MOZ_CARGO_WRAP_{HOST_,}LD* more or less appropriately for all edges. Like
    # for CC/C*FLAGS, we want the target values to trump the host values when
    # both variables are the same.
    if host_linker_var := substs.get("MOZ_CARGO_HOST_LINKER_ENV_VAR"):
        env[host_linker_var] = substs.get("MOZ_CARGO_HOST_LINKER")
    if linker_var := substs.get("MOZ_CARGO_LINKER_ENV_VAR"):
        env[linker_var] = substs.get("MOZ_CARGO_LINKER")

    if _is_host(cmd):
        env["MOZ_CARGO_WRAP_LD"] = " ".join(substs.get("MOZ_CARGO_HOST_LD"))
        env["MOZ_CARGO_WRAP_LD_CXX"] = " ".join(substs.get("MOZ_CARGO_HOST_LD_CXX"))
        env["MOZ_CARGO_WRAP_LDFLAGS"] = " ".join(substs.get("MOZ_CARGO_HOST_LDFLAGS"))
    else:
        env["MOZ_CARGO_WRAP_LD"] = " ".join(substs.get("MOZ_CARGO_LD"))
        env["MOZ_CARGO_WRAP_LD_CXX"] = " ".join(substs.get("MOZ_CARGO_LD_CXX"))
        env["MOZ_CARGO_WRAP_LDFLAGS"] = _cargo_wrap_ldflags(cmd, substs)

    env["MOZ_CARGO_WRAP_HOST_LD"] = " ".join(substs.get("MOZ_CARGO_HOST_LD"))
    env["MOZ_CARGO_WRAP_HOST_LD_CXX"] = " ".join(substs.get("MOZ_CARGO_HOST_LD_CXX"))
    env["MOZ_CARGO_WRAP_HOST_LDFLAGS"] = " ".join(substs.get("MOZ_CARGO_HOST_LDFLAGS"))

    for var in substs.get("MOZ_RUST_SANITIZER_OPTION_VARS"):
        existing = env.get(var, "")
        prefix = f"{existing}:" if existing else ""
        env[var] = f"{prefix}intercept_tls_get_addr=0"

    return env


def _features_arg(cmd, workspace_hack=True):
    features = list(cmd.features)
    if workspace_hack:
        features.append("mozilla-central-workspace-hack")
    return ["--features", ",".join(features)] if features else []


def compose_cargo_build_edge_argv(
    cmd,
    substs,
    invocation,
    timings=False,
    keep_going=False,
    single_job=False,
    workspace_hack=True,
):
    substs = _cargo_config(substs)
    cargo = substs.get("CARGO")
    build_flags = _cargo_build_flags(cmd, substs, invocation, single_job)
    extra = list(invocation.cargo_extra_flags)

    if cmd.kind == "test":
        # Keep test arguments before shared build flags so CARGO_EXTRA_FLAGS can
        # override them.
        argv = [cargo, "test"]
        argv.extend(_target_args(cmd, substs))
        # Don't stop at the first failure. We want to list all failures together.
        argv.append("--no-fail-fast")
        argv.extend(_subcommand_args(cmd))
        argv.extend(_features_arg(cmd, workspace_hack))
        argv.extend(build_flags)
        argv.extend(extra)
        return argv

    argv = [cargo, "rustc"]

    if timings:
        argv.append("--timings")
    if keep_going:
        argv.append("--keep-going")

    argv.extend(build_flags)
    argv.extend(extra)
    argv.extend(_subcommand_args(cmd))

    if cmd.kind in ("library", "host-library"):
        argv.append("--lib")

    if cmd.kind == "library" and cmd.cargo_crate_type:
        argv += ["--crate-type", cmd.cargo_crate_type]

    argv.extend(_target_args(cmd, substs))
    argv.extend(_features_arg(cmd))

    if cmd.kind in ("library", "program"):
        argv.append("--")
        argv.extend(_rustc_flags(cmd, substs, invocation))

    return argv


def compose_mach_cargo_argv(
    cmd,
    substs,
    invocation,
    subcommand,
    build_flags_override=(),
    extra_cli_flags=(),
    jobs=0,
    auto_args=True,
):
    substs = _cargo_config(substs)
    argv = [substs.get("CARGO"), subcommand]

    # Plugin commands may not accept Cargo's -j option.
    if build_flags_override:
        return [
            *argv,
            *build_flags_override,
            *invocation.cargo_extra_flags,
            *extra_cli_flags,
        ]

    argv.extend(_cargo_build_flags(cmd, substs, invocation))
    if jobs:
        argv += ["-j", str(jobs)]
    argv.extend(invocation.cargo_extra_flags)
    argv.extend(extra_cli_flags)

    if auto_args:
        if cmd.kind in ("library", "host-library"):
            argv.append("--lib")
        else:
            argv.extend(_subcommand_args(cmd))
        argv.extend(_target_args(cmd, substs))
        argv.extend(_features_arg(cmd))

    return argv
