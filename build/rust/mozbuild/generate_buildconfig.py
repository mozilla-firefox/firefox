# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import string
import textwrap

import buildconfig
import mozpack.path as mozpath
from mozbuild.nss_libs import nss_link_dylibs


def generate_bool(name):
    value = buildconfig.substs.get(name)
    return f"pub const {name}: bool = {'true' if value else 'false'};\n"


def generate_string_array(name):
    value = buildconfig.substs.get(name) or []
    return (
        f"pub const {name}: [&str; {len(value)}] = ["
        + ",".join(map(escape_rust_string, value))
        + "];\n"
    )


def generate_string_array_literal(name, values):
    return (
        f"pub const {name}: [&str; {len(values)}] = ["
        + ",".join(map(escape_rust_string, values))
        + "];\n"
    )


def generate_objdir_flags(name):
    """Like generate_string_array, but with the objdir resolved at runtime from MOZ_TOPOBJDIR."""
    topobjdir = mozpath.normsep(buildconfig.topobjdir)

    def flag_expr(flag):
        prefix, sep, suffix = mozpath.normsep(flag).partition(topobjdir)
        if not sep or suffix[:1] not in ("", "/"):
            return escape_rust_string(flag)
        return f"crate::objdir_flag({escape_rust_string(prefix)}, {escape_rust_string(suffix)})"

    flags = ", ".join(map(flag_expr, buildconfig.substs.get(name) or []))
    return f"pub static {name}: crate::Flags = crate::Flags::new(|| vec![{flags}]);\n"


def generate_string(name):
    value = buildconfig.substs.get(name) or ""
    return f"pub const {name}: &str = {escape_rust_string(value)};\n"


def escape_rust_string(value):
    """escape the string into a Rust literal"""
    # This could be more generous, but we're only escaping paths with it.
    unescaped = string.ascii_letters + string.digits + "/$+-_~ "
    result = ""
    for ch in str(value):
        if ch in unescaped:
            result += ch
        elif ch == "\r":
            result += "\\r"
        elif ch == "\n":
            result += "\\n"
        elif ch == "\\":
            result += "\\\\"
        elif ch == '"':
            result += '\\"'
        else:
            result += "\\u{%x}" % ord(ch)
    return '"%s"' % result


def generate(output):
    # Write out a macro which can be used within `include!`-like methods to
    # reference the topobjdir.
    output.write(
        textwrap.dedent(
            """
            /// Macro used to name a path in the objdir for use with macros like `include!`
            #[macro_export]
            macro_rules! objdir_path {
                ($path:expr) => {
                    concat!(env!("MOZ_TOPOBJDIR"), "/", $path)
                }
            }

            /// Macro used to name a path in the srcdir for use with macros like `include!`
            #[macro_export]
            macro_rules! srcdir_path {
                ($path:expr) => {
                    concat!(env!("MOZ_TOPSRCDIR"), "/", $path)
                }
            }

            """
        )
    )

    windows_rs_dir = buildconfig.substs.get("MOZ_WINDOWS_RS_DIR")
    if windows_rs_dir:
        output.write(
            textwrap.dedent(
                f"""
                /// Macro used to name a path in the srcdir for use with macros like `include!`
                #[macro_export]
                macro_rules! windows_rs_path {{
                    ($path:literal) => {{
                        concat!({escape_rust_string(windows_rs_dir + "/")}, $path)
                    }}
                }}

                /// Macro used to re-export windows-rs's public items
                #[macro_export]
                macro_rules! windows_rs_lib {{
                    () => {{
                        #[path = {escape_rust_string(windows_rs_dir + "/src/lib.rs")}]
                        mod lib;
                        pub use lib::*;
                    }}
                }}
                """
            )
        )

    # Write out some useful strings from the buildconfig.
    output.write(generate_string("MOZ_BACKGROUNDTASK_ACTIVATABLE_CLASS_ID"))
    output.write(generate_string("MOZ_MACBUNDLE_ID"))
    output.write(generate_string("MOZ_APP_BASENAME"))
    output.write(generate_string("MOZ_APP_NAME"))
    output.write(generate_string("MOZ_APP_VENDOR"))
    output.write(generate_string("MOZ_APP_VERSION"))

    # Write out some useful booleans from the buildconfig.
    output.write(generate_bool("MOZ_FOLD_LIBS"))
    output.write(generate_bool("MOZ_GLUE_IN_PROGRAM"))
    output.write(generate_bool("NIGHTLY_BUILD"))
    output.write(generate_bool("RELEASE_OR_BETA"))
    output.write(generate_bool("EARLY_BETA_OR_EARLIER"))
    output.write(generate_bool("MOZ_DEV_EDITION"))
    output.write(generate_bool("MOZ_ESR"))
    output.write(generate_bool("MOZ_DIAGNOSTIC_ASSERT_ENABLED"))
    output.write(generate_bool("MOZ_CODE_COVERAGE"))

    # Used by toolkit/crashreporter/client
    output.write(generate_bool("MOZ_CRASHREPORTER_MOCK"))
    output.write(generate_string_array("BINDGEN_SYSTEM_FLAGS"))
    output.write(generate_string_array("MOZ_GTK3_CFLAGS"))
    output.write(generate_string_array("MOZ_GTK3_LIBS"))
    output.write(generate_objdir_flags("NSPR_CFLAGS"))
    output.write(generate_objdir_flags("NSS_CFLAGS"))
    output.write(generate_string_array("MOZ_PIXMAN_CFLAGS"))
    output.write(generate_string_array("MOZ_ICU_CFLAGS"))

    # Used by toolkit/crashreporter/crash_helper_common
    output.write(generate_string("MOZ_APP_ID"))
    output.write(generate_string("MOZ_CRASHREPORTER_URL"))
    output.write(generate_string("MOZ_UPDATE_CHANNEL"))

    # NSS shared libraries the megazord and rusttests link against. The list lives
    # in mozbuild.nss_libs so that the libsForTests packaging action can use the
    # same definition.
    output.write(
        generate_string_array_literal(
            "NSS_LINK_DYLIBS",
            nss_link_dylibs(buildconfig.substs, buildconfig.defines),
        )
    )
