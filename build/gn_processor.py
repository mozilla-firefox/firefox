# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from collections import defaultdict, deque
from concurrent.futures import ProcessPoolExecutor, as_completed
from copy import deepcopy
from itertools import combinations
from pathlib import Path
from shutil import which

import mozpack.path as mozpath
from mozbuild.bootstrap import bootstrap_toolchain
from mozbuild.dirutils import mkdir
from mozbuild.frontend.sandbox import alphabetical_sorted
from mozfile import json as mozfile_json

license_header = """# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""

generated_header = """
  ### This moz.build was AUTOMATICALLY GENERATED from a GN config,  ###
  ### DO NOT edit it by hand.                                       ###
"""

# GN's substitution pattern for the response file path in action args.
RESPONSE_FILE_NAME_FLAG = "{{response_file_name}}"


class FStringLiteral(str):
    """A value which must be written as an f-string when serialized, so that it
    can refer to variables available in the mozbuild sandbox, such as TOPSRCDIR.
    Its contents will become the exact contents of the f-string, meaning any
    literal braces must already be escaped."""

    def __repr__(self):
        # Match mb_serialize()'s usage of json.dumps() for strings
        return "f" + json.dumps(self)


class MozbuildWriter:
    def __init__(self, fh):
        self._fh = fh
        self.indent = ""
        self._indent_increment = 4

        # We need to correlate a small amount of state here to figure out which
        # library template to use ("Library()" or "GeckoSharedLibrary()")
        self._library_name = None
        self._shared_library = None

    def mb_serialize(self, v):
        if isinstance(v, list):
            if len(v) <= 1:
                return repr(v)
            # Pretty print a list
            item_indent = self.indent + " " * self._indent_increment
            items = (",\n" + item_indent).join(self.mb_serialize(i) for i in v)
            return f"[\n{item_indent}{items}\n{self.indent}]"
        if isinstance(v, (bool, FStringLiteral)):
            return repr(v)
        return json.dumps(v)

    def finalize(self):
        if self._library_name:
            self.write("\n")
            if self._shared_library:
                self.write_ln(
                    f"GeckoSharedLibrary({self.mb_serialize(self._library_name)})"
                )
            else:
                self.write_ln(f"Library({self.mb_serialize(self._library_name)})")

    def write(self, content):
        self._fh.write(content)

    def write_ln(self, line):
        self.write(self.indent)
        self.write(line)
        self.write("\n")

    def write_attrs(self, context_attrs):
        for k in sorted(context_attrs.keys()):
            v = context_attrs[k]
            if isinstance(v, (list, set)):
                self.write_mozbuild_list(k, v)
            elif isinstance(v, dict):
                self.write_mozbuild_dict(k, v)
            else:
                self.write_mozbuild_value(k, v)

    def write_mozbuild_list(self, key, value):
        if value:
            if key == "GeneratedFiles":
                for generated_file in value:
                    self.write("\n")
                    self.write_ln("GeneratedFile(")
                    self.indent += " " * self._indent_increment
                    for o in generated_file["outputs"]:
                        self.write_ln(f"{self.mb_serialize(o)},")
                    for k, v in sorted(generated_file.items()):
                        if k == "outputs":
                            continue
                        self.write_ln(f"{k}={self.mb_serialize(v)},")
                    self.indent = self.indent[self._indent_increment :]
                    self.write_ln(")")
                return

            self.write("\n")
            self.write(self.indent + key)
            self.write(" += [\n    " + self.indent)
            self.write(
                (",\n    " + self.indent).join(
                    alphabetical_sorted(self.mb_serialize(v) for v in value)
                )
            )
            self.write("\n")
            self.write_ln("]")

    def write_mozbuild_value(self, key, value):
        if value:
            if key == "LIBRARY_NAME":
                self._library_name = value
            elif key == "FORCE_SHARED_LIB":
                self._shared_library = True
            else:
                self.write("\n")
                self.write_ln(f"{key} = {self.mb_serialize(value)}")
                self.write("\n")

    def write_mozbuild_dict(self, key, value):
        # Templates we need to use instead of certain values.
        replacements = (
            (
                ("COMPILE_FLAGS", '"WARNINGS_AS_ERRORS"', "[]"),
                "AllowCompilerWarnings()",
            ),
        )
        if value:
            self.write("\n")
            for k in sorted(value.keys()):
                v = value[k]
                subst_vals = key, self.mb_serialize(k), self.mb_serialize(v)
                wrote_ln = False
                for flags, tmpl in replacements:
                    if subst_vals == flags:
                        self.write_ln(tmpl)
                        wrote_ln = True

                if not wrote_ln:
                    self.write_ln(
                        f"{key}[{self.mb_serialize(k)}] = {self.mb_serialize(v)}"
                    )

    def write_condition(self, values):
        def mk_condition(k, v):
            if not v:
                return f'not CONFIG["{k}"]'
            return f'CONFIG["{k}"] == {self.mb_serialize(v)}'

        self.write("\n")
        self.write("if ")
        self.write(
            " and ".join(mk_condition(k, values[k]) for k in sorted(values.keys()))
        )
        self.write(":\n")
        self.indent += " " * self._indent_increment

    def terminate_condition(self):
        assert len(self.indent) >= self._indent_increment
        self.indent = self.indent[self._indent_increment :]


def select_gn_target(gn_target_config, target_os):
    if isinstance(gn_target_config, str):
        return gn_target_config

    if target_os in gn_target_config:
        return gn_target_config[target_os]

    if "*" in gn_target_config:
        return gn_target_config["*"]

    raise Exception(
        f'No gn_target configured for target_os="{target_os}". '
        'Expected a string or a dict containing "*" and target_os overrides.'
    )


def find_deps(all_targets, target):
    all_deps = set()
    queue = deque([target])
    while queue:
        item = queue.popleft()
        all_deps.add(item)
        for dep in all_targets[item]["deps"]:
            if dep not in all_deps:
                queue.append(dep)
    return all_deps


def filter_gn_config(
    path, gn_result, sandbox_vars, input_vars, gn_target, moz_config_gn_args
):
    gen_path = path / "gen"
    # Translates the raw output of gn into just what we'll need to generate a
    # mozbuild configuration.
    gn_out = {
        "targets": {},
        "sandbox_vars": sandbox_vars,
        "gen_input_files": gn_result["build_settings"]["gen_input_files"],
    }

    cpus = {
        "arm64": "aarch64",
        "x64": "x86_64",
        "mipsel": "mips32",
        "mips64el": "mips64",
        "loong64": "loongarch64",
    }
    os_targets = {
        "android": "Android",
        "linux": "Linux",
        "mac": "Darwin",
        "ios": "Darwin",
        "openbsd": "OpenBSD",
        "win": "WINNT",
    }
    # TARGET_OS is used in addition to OS_TARGET so we can distinguish between
    # macOS and iOS (which both have OS_TARGET == "Darwin").
    target_oses = {
        "android": "Android",
        "linux": "GNU",
        "mac": "OSX",
        "ios": "iOS",
        "openbsd": "OpenBSD",
        "win": "WINNT",
    }

    mozbuild_args = {
        "MOZ_DEBUG": "1" if input_vars.get("is_debug") else None,
        "OS_TARGET": os_targets[input_vars["target_os"]],
        "TARGET_OS": target_oses[input_vars["target_os"]],
        "TARGET_CPU": cpus.get(input_vars["target_cpu"], input_vars["target_cpu"]),
    }
    if "ozone_platform_x11" in input_vars:
        mozbuild_args["MOZ_X11"] = "1" if input_vars["ozone_platform_x11"] else None
    for config_name, gn_arg in moz_config_gn_args.items():
        mozbuild_args[config_name] = "1" if input_vars[gn_arg] else None

    gn_out["mozbuild_args"] = mozbuild_args
    all_deps = find_deps(gn_result["targets"], gn_target)

    for target_fullname in all_deps:
        raw_spec = gn_result["targets"][target_fullname]

        if raw_spec["type"] == "action":
            # Special handling for the action type to avoid putting empty
            # arrays of args, script and outputs on all other types in `spec`.
            spec = {}
            for spec_attr in (
                "type",
                "args",
                "inputs",
                "sources",
                "script",
                "outputs",
                "response_file_contents",
            ):
                spec[spec_attr] = raw_spec.get(spec_attr, [])
                if spec_attr == "outputs":
                    # Rebase outputs from an absolute path in the temp dir to a
                    # path relative to the target dir.
                    spec[spec_attr] = [
                        mozpath.relpath(d, path) for d in spec[spec_attr]
                    ]
                elif spec_attr in ("inputs", "sources"):
                    spec[spec_attr] = [
                        "!//" + mozpath.relpath(d, path)
                        if mozpath.basedir(d, [str(path)])
                        else d
                        for d in spec[spec_attr]
                    ]
            gn_out["targets"][target_fullname] = spec
            continue

        if raw_spec["type"] == "group":
            gn_out["targets"][target_fullname] = {
                "type": "group",
                "deps": raw_spec.get("deps", []),
            }
            continue

        # TODO: 'executable' will need to be handled here at some point as well.
        if raw_spec["type"] not in ("static_library", "shared_library", "source_set"):
            continue

        spec = {}
        for spec_attr in (
            "type",
            "sources",
            "defines",
            "include_dirs",
            "cflags",
            "cflags_c",
            "cflags_cc",
            "cflags_objc",
            "cflags_objcc",
            "deps",
            "libs",
            "frameworks",
            "output_name",
        ):
            spec[spec_attr] = raw_spec.get(spec_attr, [])
            if spec_attr == "defines":
                spec[spec_attr] = [
                    d
                    for d in spec[spec_attr]
                    if "CR_XCODE_VERSION" not in d
                    and "CR_SYSROOT_HASH" not in d
                    and "CR_SYSROOT_KEY" not in d
                    and "_FORTIFY_SOURCE" not in d
                ]
            if spec_attr == "include_dirs":
                # Rebase outputs from an absolute path in the temp dir to a path
                # relative to the target dir.
                spec[spec_attr] = [
                    d if gen_path != Path(d) else "!//gen" for d in spec[spec_attr]
                ]

        gn_out["targets"][target_fullname] = spec

    return gn_out


def process_gn_config(
    gn_config,
    gn_config_dir,
    topsrcdir,
    srcdir,
    non_unified_sources,
    sandbox_vars,
    mozilla_flags,
    mozilla_add_override_dir,
):
    # Translates a json gn config into attributes that can be used to write out
    # moz.build files for this configuration.

    # Much of this code is based on similar functionality in `gyp_reader.py`.

    mozbuild_attrs = {"mozbuild_args": gn_config.get("mozbuild_args", None), "dirs": {}}

    targets = gn_config["targets"]

    project_relsrcdir = mozpath.relpath(srcdir, topsrcdir)

    non_unified_sources = set([mozpath.normpath(s) for s in non_unified_sources])

    def target_info(fullname):
        path, name = fullname.split(":")
        # Stripping '//' gives us a path relative to the project root,
        # adding a suffix avoids name collisions with libraries already
        # in the tree (like "webrtc").
        return path.lstrip("//"), name + "_gn"

    def get_lib_name(target_name):
        if target_name.startswith("lib"):
            return target_name[3:]
        return target_name

    # Return list of library dependencies for a target, recursing through any group's dependencies.
    def find_lib_deps(fullname):
        libs = []
        seen_targets = set()

        def visit(dep):
            if dep in seen_targets:
                return
            seen_targets.add(dep)

            dep_spec = targets.get(dep)
            if not dep_spec:
                return

            dep_type = dep_spec["type"]

            if dep_type in ("static_library", "shared_library", "source_set"):
                _, name = target_info(dep)
                libs.append(get_lib_name(name))
                return

            if dep_type == "group":
                for child in dep_spec.get("deps", []):
                    visit(child)

        spec = targets[fullname]
        for dep in spec.get("deps", []):
            visit(dep)

        return libs

    def resolve_path(path):
        # GN will have resolved all these paths relative to the root of the
        # project indicated by "//".
        if path.startswith("//"):
            path = path[2:]
        if not path.startswith("/"):
            path = f"/{project_relsrcdir}/{path}"
        return path

    # If an arg contains a path to a vendored source file, rewrite it as an
    # f-string referring to TOPSRCDIR. Args that do not contain paths to
    # vendored source files are returned unchanged. This is required because
    # actions are executed with a cwd in the objdir, and at vendor time we
    # cannot know neither the absolute path of the source tree nor its relative
    # path from the objdir.
    def maybe_encode_srcdir_path_arg(arg):
        # The arg may contain a prefix before the srcdir path. We know that any
        # srcdir path will be a relative path from the tempdir to the source
        # tree, and must therefore start with "../", so find the first
        # occurrence of that string in order to find the path section. Args
        # containing paths followed by a suffix are not currently supported.
        idx = arg.find("../")
        if idx == -1:
            return arg
        prefix, path = arg[:idx], arg[idx:]
        path = mozpath.normpath(mozpath.join(str(gn_config_dir), path))
        if not os.path.exists(path) or not mozpath.basedir(path, [str(topsrcdir)]):
            return arg

        def escape_braces(s):
            return s.replace("{", "{{").replace("}", "}}")

        relpath = mozpath.relpath(path, str(topsrcdir))
        return FStringLiteral(
            escape_braces(prefix) + "{TOPSRCDIR}/" + escape_braces(relpath)
        )

    # Encodes a file path used in a response file contents as a mozbuild-style
    # path, e.g. prefixed with "/" to denote topsrcdir-relative, or "!/" to
    # denote objdir-relative.
    def encode_response_file_content_path(path):
        path = mozpath.normpath(mozpath.join(str(gn_config_dir), path))
        if mozpath.basedir(path, [str(topsrcdir)]):
            return "/" + mozpath.relpath(path, str(topsrcdir))
        if mozpath.basedir(path, [str(gn_config_dir)]):
            return "!/" + mozpath.join(
                project_relsrcdir, mozpath.relpath(path, str(gn_config_dir))
            )
        raise Exception(
            f'Path "{path}" is not under topsrcdir "{topsrcdir}" '
            f'or GN config dir "{gn_config_dir}"'
        )

    # Process all targets from the given gn project and its dependencies.
    for target_fullname, spec in targets.items():
        target_path, target_name = target_info(target_fullname)
        target_relsrcdir = mozpath.join(project_relsrcdir, target_path, target_name)
        context_attrs = {}
        extra_files = {}

        if spec["type"] in ("static_library", "shared_library", "source_set", "action"):
            # Remove leading 'lib' from the target_name if any, and use as
            # library name.
            context_attrs["LIBRARY_NAME"] = get_lib_name(target_name)
        elif spec["type"] == "group":
            # groups are only used to propagate deps, so should be skipped here.
            continue
        else:
            raise Exception(
                "The following GN target type is not currently "
                f'consumed by moz.build: "{spec["type"]}". It may need to be '
                "added, or you may need to re-run the "
                "`GnConfigGen` step."
            )

        # GN static_library targets are intended to produce real archives,
        # whereas mozbuild static libraries are expanded into their object files
        # by default, which is closer to GN source_set semantics. Using
        # `NO_EXPAND_LIBS = True` preserves that archive boundary.
        #
        # Note that we only set NO_EXPAND_LIBS when FINAL_LIBRARY is not
        # set. When FINAL_LIBRARY is set, we intentionally do not emit USE_LIBS
        # for the targets' dependencies. Combined with NO_EXPAND_LIBS, this can
        # cause mozbuild to attempt to produce real archives for GN targets that
        # have no object files of their own, resulting in link failures.
        if spec["type"] == "static_library" and "FINAL_LIBRARY" not in sandbox_vars:
            context_attrs["NO_EXPAND_LIBS"] = True

        if spec["type"] == "shared_library":
            context_attrs["FORCE_SHARED_LIB"] = True

            # Target names are suffixed with "_gn" to avoid collisions, and the
            # target name by default gets used as the library name. For shared
            # libraries we should instead use the `output_name` derived from GN
            # so that consumers can load the expected library names.
            output_name = spec.get("output_name")
            if output_name:
                # `output_name` may already be prefixed with "lib". Mozbuild,
                # however, will add a "lib" prefix to `SHARED_LIBRARY_NAME` on
                # non-Windows platforms. Remove the prefix here to avoid a
                # resulting "liblibX" library name.
                if gn_config["mozbuild_args"]["OS_TARGET"] != "WINNT":
                    output_name = get_lib_name(output_name)
                context_attrs["SHARED_LIBRARY_NAME"] = output_name

        if spec["type"] == "action" and "script" in spec:
            generated_files = []

            extra_deps = [
                "!" + resolve_path(path[1:])
                if path.startswith("!")
                else resolve_path(path)
                for attr in ("inputs", "sources")
                for path in spec.get(attr, [])
            ]

            if spec.get("response_file_contents"):
                # GN's response_file_contents field allows passing an unlimited
                # amount of data to a script via a temporary file, avoiding
                # maximum command-line length limits.
                #
                # In the only case we currently need to support, the response
                # file contents is a list of files in the source tree, relative
                # to the action's cwd. At vendor time, we cannot know the
                # location of the source tree relative to the action's cwd
                # (located in the objdir), nor the absolute location of the
                # source tree, so we cannot write them directly into a response
                # file.
                #
                # Instead, we encode the paths as mozbuild-style paths (i.e.
                # "/"-prefixed denoting topsrcdir-relative, "!/"-prefixed
                # denoting topobjdir-relative) and write this to a .rsp.in
                # file alongside the generated moz.build files.
                #
                # We then emit a GeneratedFile that will at build time convert
                # the .rsp.in file into the response file expected by the
                # script: converting the mozbuild-style paths into concrete
                # paths now that these are known, and shell-quoting as specified
                # by GN's response file format.
                #
                # Finally, we emit another GeneratedFile to perform the action,
                # ensuring we substitute the path to the generated response file
                # in place of the {{response_file_name}} placeholder arg.
                #
                # Note that if we ever need to support response file contents
                # that are not lists of paths then some of this behaviour must
                # be made optional.

                if RESPONSE_FILE_NAME_FLAG not in spec.get("args", []):
                    raise Exception(
                        f"{target_fullname} has response_file_contents without "
                        f"{RESPONSE_FILE_NAME_FLAG} in args."
                    )

                input_response_file_contents = "\n".join(
                    encode_response_file_content_path(d)
                    for d in spec["response_file_contents"]
                )

                # Response file contents can vary by build configuration.
                # A hash of the contents is included in the filename so that
                # each distinct set of contents gets a unique .rsp.in file,
                # allowing multiple configurations to coexist in-tree.
                digest = hashlib.sha256(
                    input_response_file_contents.encode("utf-8")
                ).hexdigest()[:16]
                input_response_file = (
                    f"{mozpath.basename(spec['outputs'][0])}-{digest}.rsp.in"
                )
                extra_files[input_response_file] = input_response_file_contents

                output_response_file = resolve_path(f"{spec['outputs'][0]}.rsp")

                # Generate the response file from the .rsp.in file, resolving
                # the mozbuild-style paths to absolute paths at build time now
                # these are known.
                # Because the .rsp.in filename includes a hash of its contents
                # and may vary between configurations, this GeneratedFile entry
                # may differ between configurations. Where entries differ, the
                # moz.build writer will wrap them in conditional statements,
                # ensuring each configuration uses the correct .rsp.in file.
                generated_files.append({
                    "script": "/python/mozbuild/mozbuild/action/write_path_list.py",
                    "entry_point": "write_paths",
                    "outputs": [output_response_file],
                    "inputs": [input_response_file],
                })

                args = [
                    f"{spec['outputs'][0]}.rsp"
                    if arg == RESPONSE_FILE_NAME_FLAG
                    else maybe_encode_srcdir_path_arg(arg)
                    for arg in spec.get("args", [])
                ]
                flags = [
                    resolve_path(spec["script"]),
                    resolve_path(""),
                ] + args
                generated_files.append({
                    "script": "/python/mozbuild/mozbuild/action/file_generate_wrapper.py",
                    "entry_point": "action",
                    "outputs": [resolve_path(f) for f in spec["outputs"]],
                    "extra_deps": ["!" + output_response_file] + extra_deps,
                    "flags": flags,
                })

            else:
                flags = [
                    resolve_path(spec["script"]),
                    resolve_path(""),
                ] + [maybe_encode_srcdir_path_arg(arg) for arg in spec.get("args", [])]
                generated_file = {
                    "script": "/python/mozbuild/mozbuild/action/file_generate_wrapper.py",
                    "entry_point": "action",
                    "outputs": [resolve_path(f) for f in spec["outputs"]],
                    "flags": flags,
                }
                if extra_deps:
                    generated_file["extra_deps"] = extra_deps
                generated_files.append(generated_file)

            context_attrs["GeneratedFiles"] = generated_files

        sources = []
        unified_sources = []
        extensions = set()
        use_defines_in_asflags = False

        if spec["type"] != "action":
            for f in [item.lstrip("//") for item in spec.get("sources", [])]:
                ext = mozpath.splitext(f)[-1]
                extensions.add(ext)
                src = f"{project_relsrcdir}/{f}"
                if ext in {".h", ".hpp", ".inc"}:
                    continue
                elif ext == ".def":
                    context_attrs["DEFFILE"] = f"/{src}"
                elif ext == ".rc":
                    context_attrs["RCFILE"] = f"/{src}"
                elif ext != ".S" and src not in non_unified_sources:
                    unified_sources.append(f"/{src}")
                else:
                    sources.append(f"/{src}")
                # The Mozilla build system doesn't use DEFINES for building
                # ASFILES.
                if ext == ".s":
                    use_defines_in_asflags = True

        context_attrs["SOURCES"] = sources
        context_attrs["UNIFIED_SOURCES"] = unified_sources

        context_attrs["DEFINES"] = {}
        for define in spec.get("defines", []):
            if "=" in define:
                name, value = define.split("=", 1)
                context_attrs["DEFINES"][name] = value
            else:
                context_attrs["DEFINES"][define] = True

        context_attrs["LOCAL_INCLUDES"] = []
        for include in spec.get("include_dirs", []):
            if include.startswith("!"):
                include = "!" + resolve_path(include[1:])
            else:
                include = resolve_path(include)
                # moz.build expects all LOCAL_INCLUDES to exist, so ensure they do.
                resolved = mozpath.abspath(mozpath.join(topsrcdir, include[1:]))
                if not os.path.exists(resolved):
                    # GN files may refer to include dirs that are outside of the
                    # tree or we simply didn't vendor. Print a warning in this case.
                    if not resolved.endswith("gn-output/gen"):
                        print(
                            f"Included path: '{resolved}' does not exist, dropping include from GN "
                            "configuration.",
                            file=sys.stderr,
                        )
                    continue
            if include in context_attrs["LOCAL_INCLUDES"]:
                continue
            context_attrs["LOCAL_INCLUDES"] += [include]

        context_attrs["ASFLAGS"] = spec.get("asflags_mozilla", [])
        if use_defines_in_asflags and context_attrs["DEFINES"]:
            context_attrs["ASFLAGS"] += ["-D" + d for d in context_attrs["DEFINES"]]
        suffix_map = {
            ".c": ("CFLAGS", ["cflags", "cflags_c"]),
            ".cpp": ("CXXFLAGS", ["cflags", "cflags_cc"]),
            ".cc": ("CXXFLAGS", ["cflags", "cflags_cc"]),
            ".m": ("CMFLAGS", ["cflags", "cflags_objc"]),
            ".mm": ("CMMFLAGS", ["cflags", "cflags_objcc"]),
        }
        variables = (suffix_map[e] for e in extensions if e in suffix_map)
        for var, flag_keys in variables:
            flags = [
                _f for _k in flag_keys for _f in spec.get(_k, []) if _f in mozilla_flags
            ]
            for f in flags:
                # the result may be a string or a list.
                if isinstance(f, str):
                    context_attrs.setdefault(var, []).append(f)
                else:
                    context_attrs.setdefault(var, []).extend(f)

        if "FINAL_LIBRARY" not in sandbox_vars:
            context_attrs["USE_LIBS"] = find_lib_deps(target_fullname)

        context_attrs["OS_LIBS"] = []
        for lib in spec.get("libs", []) + spec.get("frameworks", []):
            lib_name = os.path.splitext(lib)[0]
            if lib.endswith(".framework"):
                context_attrs["OS_LIBS"] += ["-framework " + lib_name]
            else:
                context_attrs["OS_LIBS"] += [lib_name]

        # Add some features to all contexts. Put here in case LOCAL_INCLUDES
        # order matters.
        if mozilla_add_override_dir != "":
            context_attrs["LOCAL_INCLUDES"] += [mozilla_add_override_dir]

        context_attrs["LOCAL_INCLUDES"] += [
            "!/ipc/ipdl/_ipdlheaders",
            "/ipc/chromium/src",
            "/tools/profiler/public",
        ]
        # These get set via VC project file settings for normal GYP builds.
        # TODO: Determine if these defines are needed for GN builds.
        if gn_config["mozbuild_args"]["OS_TARGET"] == "WINNT":
            context_attrs["DEFINES"]["UNICODE"] = True
            context_attrs["DEFINES"]["_UNICODE"] = True

        context_attrs["COMPILE_FLAGS"] = {"OS_INCLUDES": []}

        for key, value in sandbox_vars.items():
            if context_attrs.get(key) and isinstance(context_attrs[key], list):
                # If we have a key from sandbox_vars that's also been
                # populated here we use the value from sandbox_vars as our
                # basis rather than overriding outright.
                context_attrs[key] = value + context_attrs[key]
            elif context_attrs.get(key) and isinstance(context_attrs[key], dict):
                context_attrs[key].update(value)
            else:
                context_attrs[key] = value

        mozbuild_attrs["dirs"][target_relsrcdir] = (context_attrs, extra_files)

    return mozbuild_attrs


def find_common_attrs(config_attributes):
    # Returns the intersection of the given configs and prunes the inputs
    # to no longer contain these common attributes.

    common_attrs = deepcopy(config_attributes[0])

    def make_intersection(reference, input_attrs):
        # Modifies `reference` so that after calling this function it only
        # contains parts it had in common with in `input_attrs`.

        for k, input_value in input_attrs.items():
            # Anything in `input_attrs` must match what's already in
            # `reference`.
            common_value = reference.get(k)
            if common_value:
                if isinstance(input_value, list):
                    reference[k] = [
                        i
                        for i in common_value
                        if input_value.count(i) == common_value.count(i)
                    ]
                elif isinstance(input_value, dict):
                    reference[k] = {
                        key: value
                        for key, value in common_value.items()
                        if key in input_value and value == input_value[key]
                    }
                elif input_value != common_value:
                    del reference[k]
            elif k in reference:
                del reference[k]

        # Additionally, any keys in `reference` that aren't in `input_attrs`
        # must be deleted.
        for k in set(reference.keys()) - set(input_attrs.keys()):
            del reference[k]

    def make_difference(reference, input_attrs):
        # Modifies `input_attrs` so that after calling this function it contains
        # no parts it has in common with in `reference`.
        for k, input_value in list(input_attrs.items()):
            common_value = reference.get(k)
            if common_value:
                if isinstance(input_value, list):
                    input_attrs[k] = [
                        i
                        for i in input_value
                        if common_value.count(i) != input_value.count(i)
                    ]
                elif isinstance(input_value, dict):
                    input_attrs[k] = {
                        key: value
                        for key, value in input_value.items()
                        if key not in common_value
                    }
                else:
                    del input_attrs[k]

    for config_attr_set in config_attributes[1:]:
        make_intersection(common_attrs, config_attr_set)

    for config_attr_set in config_attributes:
        make_difference(common_attrs, config_attr_set)

    return common_attrs


# Order in which keys of mozbuild_args are combined into conditions. This
# doesn't affect correctness, but keys tried first claim the attributes they
# have in common, so putting coarse keys before the keys that refine them
# keeps the generated moz.build files compact. Unlisted keys come last.
CONDITION_KEY_ORDER = ("MOZ_DEBUG", "OS_TARGET", "TARGET_OS", "MOZ_X11", "TARGET_CPU")


def condition_key_combinations(all_args):
    # Returns every combination of the keys in `all_args`, from least to most
    # specific: the empty combination, then each single key, then each pair,
    # and so on, with combinations of the same size ordered by
    # CONDITION_KEY_ORDER. Trying broader combinations first means narrower
    # ones only see what the broader ones couldn't express, and ending with
    # all the keys ensures every configuration can be told apart.
    keys = sorted(
        {k for args in all_args for k in args},
        key=lambda k: (
            CONDITION_KEY_ORDER.index(k)
            if k in CONDITION_KEY_ORDER
            else len(CONDITION_KEY_ORDER),
            k,
        ),
    )
    return [c for n in range(len(keys) + 1) for c in combinations(keys, n)]


def write_mozbuild(topsrcdir, write_mozbuild_variables, relsrcdir, configs):
    target_srcdir = mozpath.join(topsrcdir, relsrcdir)
    mkdir(target_srcdir)

    target_mozbuild = mozpath.join(target_srcdir, "moz.build")
    with open(target_mozbuild, "w") as fh:
        mb = MozbuildWriter(fh)
        mb.write(license_header)
        mb.write("\n")
        mb.write(generated_header)

        try:
            if relsrcdir in write_mozbuild_variables["INCLUDE_TK_CFLAGS_DIRS"]:
                mb.write('if CONFIG["MOZ_WIDGET_TOOLKIT"] == "gtk":\n')
                mb.write('    CXXFLAGS += CONFIG["MOZ_GTK3_CFLAGS"]\n')
        except KeyError:
            pass
        try:
            if relsrcdir in write_mozbuild_variables["INCLUDE_SYSTEM_GBM_HANDLING"]:
                mb.write('CXXFLAGS += CONFIG["MOZ_GBM_CFLAGS"]\n')
                mb.write('if not CONFIG["MOZ_SYSTEM_GBM"]:\n')
                mb.write('    LOCAL_INCLUDES += [ "/third_party/gbm/gbm/" ]\n')
        except KeyError:
            pass
        try:
            if relsrcdir in write_mozbuild_variables["INCLUDE_SYSTEM_LIBDRM_HANDLING"]:
                mb.write('CXXFLAGS += CONFIG["MOZ_LIBDRM_CFLAGS"]\n')
                mb.write('if not CONFIG["MOZ_SYSTEM_LIBDRM"]:\n')
                mb.write('    LOCAL_INCLUDES += [ "/third_party/drm/drm/",\n')
                mb.write('                        "/third_party/drm/drm/include/",\n')
                mb.write(
                    '                        "/third_party/drm/drm/include/libdrm" ]\n'
                )
        except KeyError:
            pass
        try:
            if (
                relsrcdir
                in write_mozbuild_variables["INCLUDE_SYSTEM_PIPEWIRE_HANDLING"]
            ):
                mb.write('CXXFLAGS += CONFIG["MOZ_PIPEWIRE_CFLAGS"]\n')
                mb.write('if not CONFIG["MOZ_SYSTEM_PIPEWIRE"]:\n')
                mb.write('    LOCAL_INCLUDES += [ "/third_party/pipewire/" ]\n')
        except KeyError:
            pass
        try:
            if relsrcdir in write_mozbuild_variables["INCLUDE_SYSTEM_LIBVPX_HANDLING"]:
                mb.write('if not CONFIG["MOZ_SYSTEM_LIBVPX"]:\n')
                mb.write('    LOCAL_INCLUDES += [ "/media/libvpx/libvpx/" ]\n')
                mb.write('    CXXFLAGS += CONFIG["MOZ_LIBVPX_CFLAGS"]\n')
        except KeyError:
            pass
        try:
            if relsrcdir in write_mozbuild_variables["INCLUDE_SYSTEM_DAV1D_HANDLING"]:
                mb.write('if CONFIG["MOZ_SYSTEM_AV1"]:\n')
                mb.write('    CXXFLAGS += CONFIG["MOZ_SYSTEM_DAV1D_CFLAGS"]\n')
                mb.write('    CXXFLAGS += CONFIG["MOZ_SYSTEM_LIBAOM_CFLAGS"]\n')
        except KeyError:
            pass
        try:
            if relsrcdir in write_mozbuild_variables["INCLUDE_GECKO_ZLIB_HANDLING"]:
                mb.write('USE_LIBS += [ "zlib" ]\n')
        except KeyError:
            pass

        all_args = [args for args, _ in configs]

        # Start with attributes that will be a part of the mozconfig
        # for every configuration, then factor by increasingly specific
        # combinations.
        for attrs in condition_key_combinations(all_args):
            # Each pass prunes what it writes from `configs`, so once nothing
            # is left there is nothing more to write.
            if not any(any(a.values()) for _, a in configs):
                break

            conditions = set()
            for args in all_args:
                cond = tuple((k, args.get(k) or "") for k in attrs)
                conditions.add(cond)

            for cond in sorted(conditions):
                matching = [
                    attrs
                    for args, attrs in configs
                    if all((args.get(k) or "") == v for k, v in cond)
                ]
                if not any(any(a.values()) for a in matching):
                    continue
                common_attrs = find_common_attrs(matching)
                if any(common_attrs.values()):
                    if cond:
                        mb.write_condition(dict(cond))
                    mb.write_attrs(common_attrs)
                    if cond:
                        mb.terminate_condition()

        if any(any(a.values()) for _, a in configs):
            raise Exception(f"Could not express the configurations of {relsrcdir}")

        mb.finalize()
    return target_mozbuild


def write_mozbuild_files(
    topsrcdir,
    srcdir,
    all_mozbuild_results,
    write_mozbuild_variables,
):
    print("Writing moz.build files")

    # Translate {config -> {dirs -> build info}} into
    #           {dirs -> [(config, build_info)]}
    configs_by_dir = defaultdict(list)
    extra_files_by_dir = defaultdict(dict)
    for config_attrs in all_mozbuild_results:
        mozbuild_args = config_attrs["mozbuild_args"]
        dirs = config_attrs["dirs"]
        for d, (build_data, dir_extra_files) in dirs.items():
            configs_by_dir[d].append((mozbuild_args, build_data))
            for filename, contents in dir_extra_files.items():
                existing = extra_files_by_dir[d].get(filename)
                if existing is not None and existing != contents:
                    raise Exception(f"Conflicting contents for {d}/{filename}")
                extra_files_by_dir[d][filename] = contents

    mozbuilds = set()
    extra_files = set()

    # threading this section did not produce noticeable speed gains
    for relsrcdir, configs in sorted(configs_by_dir.items()):
        mozbuilds.add(
            write_mozbuild(topsrcdir, write_mozbuild_variables, relsrcdir, configs)
        )

    for relsrcdir, files in sorted(extra_files_by_dir.items()):
        for filename, contents in sorted(files.items()):
            path = mozpath.join(topsrcdir, relsrcdir, filename)
            extra_files.add(path)
            with open(path, "w") as fh:
                fh.write(contents)

    # write the project moz.build file
    dirs_mozbuild = mozpath.join(srcdir, "moz.build")
    mozbuilds.add(dirs_mozbuild)
    with open(dirs_mozbuild, "w") as fh:
        mb = MozbuildWriter(fh)
        mb.write(license_header)
        mb.write("\n")
        mb.write(generated_header)

        # Not every srcdir is present for every config, which needs to be
        # reflected in the generated root moz.build.
        dirs_by_config = {
            tuple(v["mozbuild_args"].items()): set(v["dirs"].keys())
            for v in all_mozbuild_results
        }

        for attrs in condition_key_combinations(
            v["mozbuild_args"] for v in all_mozbuild_results
        ):
            if not any(dirs_by_config.values()):
                break

            conditions = set()
            for args in dirs_by_config.keys():
                cond = tuple((k, dict(args).get(k) or "") for k in attrs)
                conditions.add(cond)

            for cond in sorted(conditions):
                common_dirs = None
                for args, dir_set in dirs_by_config.items():
                    if all((dict(args).get(k) or "") == v for k, v in cond):
                        if common_dirs is None:
                            common_dirs = deepcopy(dir_set)
                        else:
                            common_dirs &= dir_set

                for args, dir_set in dirs_by_config.items():
                    if all((dict(args).get(k) or "") == v for k, v in cond):
                        dir_set -= common_dirs

                if common_dirs:
                    if cond:
                        mb.write_condition(dict(cond))
                    mb.write_mozbuild_list("DIRS", [f"/{d}" for d in common_dirs])
                    if cond:
                        mb.terminate_condition()

        if any(dirs_by_config.values()):
            raise Exception(f"Could not express the DIRS of {srcdir}")

    # Remove stale moz.builds and extra files.
    for root, dirs, files in os.walk(srcdir):
        if mozpath.normpath(root) == mozpath.normpath(srcdir):
            # No need to remove anything from the project's root srcdir
            continue
        if "moz.build" not in files:
            # This dir never contained moz.build or extra files, so we don't
            # want to remove any of the project's actual files.
            continue

        for filename in files:
            file = mozpath.join(root, filename)
            if file not in mozbuilds | extra_files:
                os.unlink(file)


def generate_gn_config(
    topsrcdir,
    build_root_dir,
    target_dir,
    gn_binary,
    input_variables,
    sandbox_variables,
    gn_target_config,
    moz_build_flag,
    non_unified_sources,
    mozilla_flags,
    mozilla_add_override_dir,
    moz_config_gn_args,
):
    def str_for_arg(v):
        if v in (True, False):
            return str(v).lower()
        return f'"{v}"'

    build_root_dir = topsrcdir / build_root_dir
    srcdir = build_root_dir / target_dir
    gn_target = select_gn_target(gn_target_config, input_variables["target_os"])

    gn_input_variables = input_variables.copy()
    gn_input_variables.update({
        f"{moz_build_flag}": True,
        "concurrent_links": 1,
        "action_pool_depth": 1,
    })

    if input_variables["target_os"] == "win":
        gn_input_variables.update({
            "visual_studio_path": "/",
            "visual_studio_version": 2015,
            "wdk_path": "/",
            "windows_sdk_version": "n/a",
        })
    if input_variables["target_os"] in ("ios", "mac"):
        gn_input_variables.update({
            "mac_sdk_path": "/",
        })

    # Running `gn gen` for an ios target doesn't succeed, so we use a mac target
    # instead and hope the result is similar enough that iOS can still build
    # successfully.
    if input_variables["target_os"] == "ios":
        gn_input_variables["target_os"] = "mac"

    gn_args = f"--args={' '.join([f'{k}={str_for_arg(v)}' for k, v in gn_input_variables.items()])}"
    with tempfile.TemporaryDirectory() as tempdir:
        # On Mac, `tempdir` starts with /var which is a symlink to /private/var.
        # We resolve the symlinks in `tempdir` here so later usage with
        # relpath() does not lead to unexpected results, should it be used
        # together with another path that has symlinks resolved.
        resolved_tempdir = Path(tempdir).resolve()
        gen_args = [
            gn_binary,
            "gen",
            str(resolved_tempdir),
            gn_args,
            "--ide=json",
            "--root=./",  # must find the google build directory in this directory
            f"--dotfile={target_dir}/.gn",
        ]
        print(f'Running "{" ".join(gen_args)}"', file=sys.stderr)
        subprocess.check_call(gen_args, cwd=build_root_dir, stderr=subprocess.STDOUT)

        gn_config_file = resolved_tempdir / "project.json"
        with open(gn_config_file) as fh:
            raw_json = fh.read()
            raw_json = raw_json.replace(f"//{target_dir}/", "//")
            raw_json = raw_json.replace(f"//{target_dir}:", "//:")
            raw_json = raw_json.replace(f"gen/{target_dir}", "gen")
            gn_config = mozfile_json.loads(raw_json)
            gn_config = filter_gn_config(
                resolved_tempdir,
                gn_config,
                sandbox_variables,
                input_variables,
                gn_target,
                moz_config_gn_args,
            )
            mozbuild_result = process_gn_config(
                gn_config,
                resolved_tempdir,
                topsrcdir,
                srcdir,
                non_unified_sources,
                gn_config["sandbox_vars"],
                mozilla_flags,
                mozilla_add_override_dir,
            )
            return gn_config, mozbuild_result


def generate_gn_configs(topsrcdir, config):
    gn_binary = bootstrap_toolchain("gn/gn") or which("gn")
    if not gn_binary:
        raise Exception("The GN program must be present to generate GN configs.")

    vars_set = []
    for is_debug in (True, False):
        for target_os in ("android", "ios", "linux", "mac", "openbsd", "win"):
            target_cpus = ["x64"]
            if target_os in ("android", "ios", "linux", "mac", "win", "openbsd"):
                target_cpus.append("arm64")
            if target_os in ("android", "linux"):
                target_cpus.append("arm")
            if target_os in ("android", "linux", "win"):
                target_cpus.append("x86")
            if target_os in ("linux", "openbsd"):
                target_cpus.append("riscv64")
            if target_os == "linux":
                target_cpus.extend(["loong64", "ppc64", "mipsel", "mips64el"])
            for target_cpu in target_cpus:
                vars = {
                    "host_cpu": "x64",
                    "is_debug": is_debug,
                    "target_cpu": target_cpu,
                    "target_os": target_os,
                }

                config_args = config.get("gn_args", {})
                vars.update(config_args.get("*", {}))
                vars.update(config_args.get(target_os, {}))

                if target_os == "linux":
                    for enable_x11 in (True, False):
                        vars["ozone_platform_x11"] = enable_x11
                        vars_set.append(vars.copy())
                else:
                    if target_os == "openbsd":
                        vars["ozone_platform_x11"] = True
                    vars_set.append(vars)

    moz_config_gn_args = config.get("moz_config_gn_args", {})
    for gn_arg in moz_config_gn_args.values():
        # Replace each configuration in vars_set with two copies, with gn_arg
        # enabled and disabled.
        new_vars_set = []
        for vars in vars_set:
            if gn_arg in vars:
                raise ValueError(f"GN arg {gn_arg!r} is already set")
            new_vars_set.extend({**vars, gn_arg: enabled} for enabled in (True, False))
        vars_set = new_vars_set

    gn_configs = []
    NUM_WORKERS = 5
    with ProcessPoolExecutor(max_workers=NUM_WORKERS) as executor:
        # Submit tasks to the executor
        futures = {
            executor.submit(
                generate_gn_config,
                topsrcdir,
                config["build_root_dir"],
                config["target_dir"],
                gn_binary,
                vars,
                config["gn_sandbox_variables"],
                config["gn_target"],
                config["moz_build_flag"],
                config["non_unified_sources"],
                config["mozilla_flags"],
                config["mozilla_add_override_dir"],
                moz_config_gn_args,
            ): vars
            for vars in vars_set
        }

        # Process completed tasks as they finish
        error_generating_configs = False
        for future in as_completed(futures):
            try:
                gn_configs.append(future.result())
            except Exception as e:
                print(f"[Task] Task failed with exception: {e}")
                error_generating_configs = True

        if error_generating_configs:
            print("\nGenerating configs failed.  See errors above.\n", file=sys.stderr)
            sys.exit(1)

        print("All generation tasks have been processed.")

    return gn_configs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("config", help="configuration in json format")
    args = parser.parse_args()

    with open(args.config) as fh:
        config = mozfile_json.load(fh)

    topsrcdir = Path(__file__).parent.parent.resolve()

    _, mozbuild_results = zip(*generate_gn_configs(topsrcdir, config))

    write_mozbuild_files(
        topsrcdir,
        topsrcdir / config["build_root_dir"] / config["target_dir"],
        mozbuild_results,
        config["write_mozbuild_variables"],
    )


if __name__ == "__main__":
    main()
