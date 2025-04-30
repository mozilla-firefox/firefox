# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import json
import os
import subprocess
import sys
import tempfile
from collections import defaultdict, deque
from copy import deepcopy
from importlib import util
from pathlib import Path
from shutil import which

import mozpack.path as mozpath
from mozbuild.bootstrap import bootstrap_toolchain
from mozbuild.dirutils import mkdir
from mozbuild.frontend.sandbox import alphabetical_sorted

license_header = """# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""

generated_header = """
  ### This moz.build was AUTOMATICALLY GENERATED from a GN config,  ###
  ### DO NOT edit it by hand.                                       ###
"""


class MozbuildWriter:
    def __init__(self, fh):
        self._fh = fh
        self.indent = ""
        self._indent_increment = 4

        # We need to correlate a small amount of state here to figure out
        # which library template to use ("Library()" or "SharedLibrary()")
        self._library_name = None
        self._shared_library = None

    def mb_serialize(self, v):
        if isinstance(v, list):
            if len(v) <= 1:
                return repr(v)
            # Pretty print a list
            raw = json.dumps(v, indent=self._indent_increment)
            # Add the indent of the current indentation level
            return raw.replace("\n", "\n" + self.indent)
        if isinstance(v, bool):
            return repr(v)
        return '"%s"' % v

    def finalize(self):
        if self._library_name:
            self.write("\n")
            if self._shared_library:
                self.write_ln(
                    "SharedLibrary(%s)" % self.mb_serialize(self._library_name)
                )
            else:
                self.write_ln("Library(%s)" % self.mb_serialize(self._library_name))

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
                self.write_ln("%s = %s" % (key, self.mb_serialize(value)))
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
            if key == "GeneratedFile":
                self.write_ln("GeneratedFile(")
                self.indent += " " * self._indent_increment
                for o in value["outputs"]:
                    self.write_ln("%s," % (self.mb_serialize(o)))
                for k, v in sorted(value.items()):
                    if k == "outputs":
                        continue
                    self.write_ln("%s=%s," % (k, self.mb_serialize(v)))
                self.indent = self.indent[self._indent_increment :]
                self.write_ln(")")
                return
            for k in sorted(value.keys()):
                v = value[k]
                subst_vals = key, self.mb_serialize(k), self.mb_serialize(v)
                wrote_ln = False
                for flags, tmpl in replacements:
                    if subst_vals == flags:
                        self.write_ln(tmpl)
                        wrote_ln = True

                if not wrote_ln:
                    self.write_ln("%s[%s] = %s" % subst_vals)

    def write_condition(self, values):
        def mk_condition(k, v):
            if not v:
                return 'not CONFIG["%s"]' % k
            return 'CONFIG["%s"] == %s' % (k, self.mb_serialize(v))

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


def filter_gn_config(path, gn_result, sandbox_vars, input_vars, gn_target):
    gen_path = path / "gen"
    # Translates the raw output of gn into just what we'll need to generate a
    # mozbuild configuration.
    gn_out = {"targets": {}, "sandbox_vars": sandbox_vars}

    cpus = {
        "arm64": "aarch64",
        "x64": "x86_64",
        "mipsel": "mips32",
        "mips64el": "mips64",
        "loong64": "loongarch64",
    }
    oses = {
        "android": "Android",
        "linux": "Linux",
        "mac": "Darwin",
        "openbsd": "OpenBSD",
        "win": "WINNT",
    }

    mozbuild_args = {
        "MOZ_DEBUG": "1" if input_vars.get("is_debug") else None,
        "OS_TARGET": oses[input_vars["target_os"]],
        "TARGET_CPU": cpus.get(input_vars["target_cpu"], input_vars["target_cpu"]),
    }
    if "use_x11" in input_vars:
        mozbuild_args["MOZ_X11"] = "1" if input_vars["use_x11"] else None

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
                "script",
                "outputs",
            ):
                spec[spec_attr] = raw_spec.get(spec_attr, [])
                if spec_attr == "outputs":
                    # Rebase outputs from an absolute path in the temp dir to a
                    # path relative to the target dir.
                    spec[spec_attr] = [
                        mozpath.relpath(d, path) for d in spec[spec_attr]
                    ]
            gn_out["targets"][target_fullname] = spec

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
        ):
            spec[spec_attr] = raw_spec.get(spec_attr, [])
            if spec_attr == "defines":
                spec[spec_attr] = [
                    d
                    for d in spec[spec_attr]
                    if "CR_XCODE_VERSION" not in d
                    and "CR_SYSROOT_HASH" not in d
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
    gn_config, topsrcdir, srcdir, non_unified_sources, sandbox_vars, mozilla_flags
):
    # Translates a json gn config into attributes that can be used to write out
    # moz.build files for this configuration.

    # Much of this code is based on similar functionality in `gyp_reader.py`.

    mozbuild_attrs = {"mozbuild_args": gn_config.get("mozbuild_args", None), "dirs": {}}

    targets = gn_config["targets"]

    project_relsrcdir = mozpath.relpath(srcdir, topsrcdir)

    non_unified_sources = set([mozpath.normpath(s) for s in non_unified_sources])

    def target_info(fullname):
        path, name = target_fullname.split(":")
        # Stripping '//' gives us a path relative to the project root,
        # adding a suffix avoids name collisions with libraries already
        # in the tree (like "webrtc").
        return path.lstrip("//"), name + "_gn"

    def resolve_path(path):
        # GN will have resolved all these paths relative to the root of the
        # project indicated by "//".
        if path.startswith("//"):
            path = path[2:]
        if not path.startswith("/"):
            path = "/%s/%s" % (project_relsrcdir, path)
        return path

    # Process all targets from the given gn project and its dependencies.
    for target_fullname, spec in targets.items():
        target_path, target_name = target_info(target_fullname)
        context_attrs = {}

        # Remove leading 'lib' from the target_name if any, and use as
        # library name.
        name = target_name
        if spec["type"] in ("static_library", "shared_library", "source_set", "action"):
            if name.startswith("lib"):
                name = name[3:]
            context_attrs["LIBRARY_NAME"] = str(name)
        else:
            raise Exception(
                "The following GN target type is not currently "
                'consumed by moz.build: "%s". It may need to be '
                "added, or you may need to re-run the "
                "`GnConfigGen` step." % spec["type"]
            )

        if spec["type"] == "shared_library":
            context_attrs["FORCE_SHARED_LIB"] = True

        if spec["type"] == "action" and "script" in spec:
            flags = [
                resolve_path(spec["script"]),
                resolve_path(""),
            ] + spec.get("args", [])
            context_attrs["GeneratedFile"] = {
                "script": "/python/mozbuild/mozbuild/action/file_generate_wrapper.py",
                "entry_point": "action",
                "outputs": [resolve_path(f) for f in spec["outputs"]],
                "flags": flags,
            }

        sources = []
        unified_sources = []
        extensions = set()
        use_defines_in_asflags = False

        for f in spec.get("sources", []):
            f = f.lstrip("//")
            ext = mozpath.splitext(f)[-1]
            extensions.add(ext)
            src = "%s/%s" % (project_relsrcdir, f)
            if ext == ".h" or ext == ".inc":
                continue
            elif ext == ".def":
                context_attrs["SYMBOLS_FILE"] = src
            elif ext != ".S" and src not in non_unified_sources:
                unified_sources.append("/%s" % src)
            else:
                sources.append("/%s" % src)
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
                            "Included path: '%s' does not exist, dropping include from GN "
                            "configuration." % resolved,
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

        context_attrs["OS_LIBS"] = []
        for lib in spec.get("libs", []):
            lib_name = os.path.splitext(lib)[0]
            if lib.endswith(".framework"):
                context_attrs["OS_LIBS"] += ["-framework " + lib_name]
            else:
                context_attrs["OS_LIBS"] += [lib_name]

        # Add some features to all contexts. Put here in case LOCAL_INCLUDES
        # order matters.
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

        target_relsrcdir = mozpath.join(project_relsrcdir, target_path, target_name)
        mozbuild_attrs["dirs"][target_relsrcdir] = context_attrs

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


def write_mozbuild(
    topsrcdir,
    srcdir,
    non_unified_sources,
    gn_configs,
    mozilla_flags,
    write_mozbuild_variables,
):
    all_mozbuild_results = []

    for gn_config in gn_configs:
        mozbuild_attrs = process_gn_config(
            gn_config,
            topsrcdir,
            srcdir,
            non_unified_sources,
            gn_config["sandbox_vars"],
            mozilla_flags,
        )
        all_mozbuild_results.append(mozbuild_attrs)

    # Translate {config -> {dirs -> build info}} into
    #           {dirs -> [(config, build_info)]}
    configs_by_dir = defaultdict(list)
    for config_attrs in all_mozbuild_results:
        mozbuild_args = config_attrs["mozbuild_args"]
        dirs = config_attrs["dirs"]
        for d, build_data in dirs.items():
            configs_by_dir[d].append((mozbuild_args, build_data))

    mozbuilds = set()
    for relsrcdir, configs in sorted(configs_by_dir.items()):
        target_srcdir = mozpath.join(topsrcdir, relsrcdir)
        mkdir(target_srcdir)

        target_mozbuild = mozpath.join(target_srcdir, "moz.build")
        mozbuilds.add(target_mozbuild)
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
                if (
                    relsrcdir
                    in write_mozbuild_variables["INCLUDE_SYSTEM_LIBDRM_HANDLING"]
                ):
                    mb.write('CXXFLAGS += CONFIG["MOZ_LIBDRM_CFLAGS"]\n')
                    mb.write('if not CONFIG["MOZ_SYSTEM_LIBDRM"]:\n')
                    mb.write('    LOCAL_INCLUDES += [ "/third_party/drm/drm/",\n')
                    mb.write(
                        '                        "/third_party/drm/drm/include/",\n'
                    )
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
                if (
                    relsrcdir
                    in write_mozbuild_variables["INCLUDE_SYSTEM_LIBVPX_HANDLING"]
                ):
                    mb.write('if not CONFIG["MOZ_SYSTEM_LIBVPX"]:\n')
                    mb.write('    LOCAL_INCLUDES += [ "/media/libvpx/libvpx/" ]\n')
                    mb.write('    CXXFLAGS += CONFIG["MOZ_LIBVPX_CFLAGS"]\n')
            except KeyError:
                pass
            try:
                if (
                    relsrcdir
                    in write_mozbuild_variables["INCLUDE_SYSTEM_DAV1D_HANDLING"]
                ):
                    mb.write('if CONFIG["MOZ_SYSTEM_AV1"]:\n')
                    mb.write('    CXXFLAGS += CONFIG["MOZ_SYSTEM_DAV1D_CFLAGS"]\n')
                    mb.write('    CXXFLAGS += CONFIG["MOZ_SYSTEM_LIBAOM_CFLAGS"]\n')
            except KeyError:
                pass

            all_args = [args for args, _ in configs]

            # Start with attributes that will be a part of the mozconfig
            # for every configuration, then factor by other potentially useful
            # combinations.
            # FIXME: this is a time-bomb. See bug 1775202.
            for attrs in (
                (),
                ("MOZ_DEBUG",),
                ("OS_TARGET",),
                ("TARGET_CPU",),
                ("MOZ_DEBUG", "OS_TARGET"),
                ("OS_TARGET", "MOZ_X11"),
                ("OS_TARGET", "TARGET_CPU"),
                ("OS_TARGET", "TARGET_CPU", "MOZ_X11"),
                ("OS_TARGET", "TARGET_CPU", "MOZ_DEBUG"),
                ("OS_TARGET", "TARGET_CPU", "MOZ_DEBUG", "MOZ_X11"),
            ):
                conditions = set()
                for args in all_args:
                    cond = tuple((k, args.get(k) or "") for k in attrs)
                    conditions.add(cond)

                for cond in sorted(conditions):
                    common_attrs = find_common_attrs(
                        [
                            attrs
                            for args, attrs in configs
                            if all((args.get(k) or "") == v for k, v in cond)
                        ]
                    )
                    if any(common_attrs.values()):
                        if cond:
                            mb.write_condition(dict(cond))
                        mb.write_attrs(common_attrs)
                        if cond:
                            mb.terminate_condition()

            mb.finalize()

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

        for attrs in (
            (),
            ("OS_TARGET",),
            ("OS_TARGET", "TARGET_CPU"),
            ("OS_TARGET", "TARGET_CPU", "MOZ_X11"),
        ):
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
                    if all(dict(args).get(k) == v for k, v in cond):
                        dir_set -= common_dirs

                if common_dirs:
                    if cond:
                        mb.write_condition(dict(cond))
                    mb.write_mozbuild_list("DIRS", ["/%s" % d for d in common_dirs])
                    if cond:
                        mb.terminate_condition()

    # Remove possibly stale moz.builds
    for root, dirs, files in os.walk(srcdir):
        if "moz.build" in files:
            file = os.path.join(root, "moz.build")
            if file not in mozbuilds:
                os.unlink(file)


def generate_gn_config(
    build_root_dir,
    target_dir,
    gn_binary,
    input_variables,
    sandbox_variables,
    gn_target,
    preprocessor,
    moz_build_flag,
):
    def str_for_arg(v):
        if v in (True, False):
            return str(v).lower()
        return '"%s"' % v

    input_variables = input_variables.copy()
    input_variables.update(
        {
            f"{moz_build_flag}": True,
            "concurrent_links": 1,
            "action_pool_depth": 1,
        }
    )

    if input_variables["target_os"] == "win":
        input_variables.update(
            {
                "visual_studio_path": "/",
                "visual_studio_version": 2015,
                "wdk_path": "/",
            }
        )
    if input_variables["target_os"] == "mac":
        input_variables.update(
            {
                "mac_sdk_path": "/",
                "enable_wmax_tokens": False,
            }
        )

    gn_args = "--args=%s" % " ".join(
        ["%s=%s" % (k, str_for_arg(v)) for k, v in input_variables.items()]
    )
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
        print('Running "%s"' % " ".join(gen_args), file=sys.stderr)
        subprocess.check_call(gen_args, cwd=build_root_dir, stderr=subprocess.STDOUT)

        gn_config_file = resolved_tempdir / "project.json"
        if preprocessor:
            preprocessor.main(gn_config_file)

        with open(gn_config_file) as fh:
            gn_out = json.load(fh)
            gn_out = filter_gn_config(
                resolved_tempdir, gn_out, sandbox_variables, input_variables, gn_target
            )
            return gn_out


def load_preprocessor(script_name):
    if script_name and os.path.isfile(script_name):
        print(f"Loading preprocessor {script_name}")
        spec = util.spec_from_file_location("preprocess", script_name)
        module = util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("config", help="configuration in json format")
    args = parser.parse_args()

    gn_binary = bootstrap_toolchain("gn/gn") or which("gn")
    if not gn_binary:
        raise Exception("The GN program must be present to generate GN configs.")

    with open(args.config) as fh:
        config = json.load(fh)

    topsrcdir = Path(__file__).parent.parent.resolve()

    vars_set = []
    for is_debug in (True, False):
        for target_os in ("android", "linux", "mac", "openbsd", "win"):
            target_cpus = ["x64"]
            if target_os in ("android", "linux", "mac", "win", "openbsd"):
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
                if target_os == "linux":
                    for use_x11 in (True, False):
                        vars["use_x11"] = use_x11
                        vars_set.append(vars.copy())
                else:
                    if target_os == "openbsd":
                        vars["use_x11"] = True
                    vars_set.append(vars)

    preprocessor = load_preprocessor(config.get("preprocessing_script", None))

    gn_configs = []
    for vars in vars_set:
        gn_configs.append(
            generate_gn_config(
                topsrcdir / config["build_root_dir"],
                config["target_dir"],
                gn_binary,
                vars,
                config["gn_sandbox_variables"],
                config["gn_target"],
                preprocessor,
                config["moz_build_flag"],
            )
        )

    print("Writing moz.build files")
    write_mozbuild(
        topsrcdir,
        topsrcdir / config["build_root_dir"] / config["target_dir"],
        config["non_unified_sources"],
        gn_configs,
        config["mozilla_flags"],
        config["write_mozbuild_variables"],
    )


if __name__ == "__main__":
    main()
