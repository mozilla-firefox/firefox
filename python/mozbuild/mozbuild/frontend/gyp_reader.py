# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import time
from collections.abc import Iterable

import gyp
import gyp.msvs_emulation
import mozpack.path as mozpath
import six
from mozpack.files import FileFinder

from mozbuild import shellutil
from mozbuild.util import expand_variables

from .context import VARIABLES, ObjDirPath, SourcePath, TemplateContext
from .sandbox import alphabetical_sorted

# Define this module as gyp.generator.mozbuild so that gyp can use it
# as a generator under the name "mozbuild".
sys.modules["gyp.generator.mozbuild"] = sys.modules[__name__]

# build/gyp_chromium does this:
#   script_dir = os.path.dirname(os.path.realpath(__file__))
#   chrome_src = os.path.abspath(os.path.join(script_dir, os.pardir))
#   sys.path.insert(0, os.path.join(chrome_src, 'tools', 'gyp', 'pylib'))
# We're not importing gyp_chromium, but we want both script_dir and
# chrome_src for the default includes, so go backwards from the pylib
# directory, which is the parent directory of gyp module.
chrome_src = mozpath.abspath(
    mozpath.join(mozpath.dirname(gyp.__file__), "../../../../..")
)
script_dir = mozpath.join(chrome_src, "build")


# Default variables gyp uses when evaluating gyp files.
generator_default_variables = {}
for dirname in [
    "INTERMEDIATE_DIR",
    "SHARED_INTERMEDIATE_DIR",
    "PRODUCT_DIR",
    "LIB_DIR",
    "SHARED_LIB_DIR",
]:
    # Some gyp steps fail if these are empty(!).
    generator_default_variables[dirname] = "$" + dirname

for unused in [
    "RULE_INPUT_PATH",
    "RULE_INPUT_ROOT",
    "RULE_INPUT_NAME",
    "RULE_INPUT_DIRNAME",
    "RULE_INPUT_EXT",
    "EXECUTABLE_PREFIX",
    "EXECUTABLE_SUFFIX",
    "STATIC_LIB_PREFIX",
    "STATIC_LIB_SUFFIX",
    "SHARED_LIB_PREFIX",
    "SHARED_LIB_SUFFIX",
    "LINKER_SUPPORTS_ICF",
]:
    generator_default_variables[unused] = ""


class GypContext(TemplateContext):
    """Specialized Context for use with data extracted from Gyp.

    config is the ConfigEnvironment for this context.
    relobjdir is the object directory that will be used for this context,
    relative to the topobjdir defined in the ConfigEnvironment.
    """

    def __init__(self, config, relobjdir):
        self._relobjdir = relobjdir
        TemplateContext.__init__(
            self, template="Gyp", allowed_variables=VARIABLES, config=config
        )


def handle_actions(actions, context, action_overrides):
    idir = "$INTERMEDIATE_DIR/"
    for action in actions:
        name = action["action_name"]
        if name not in action_overrides:
            raise RuntimeError("GYP action %s not listed in action_overrides" % name)
        outputs = action["outputs"]
        if len(outputs) > 1:
            raise NotImplementedError(
                "GYP actions with more than one output not supported: %s" % name
            )
        output = outputs[0]
        if not output.startswith(idir):
            raise NotImplementedError(
                "GYP actions outputting to somewhere other than "
                "<(INTERMEDIATE_DIR) not supported: %s" % output
            )
        output = output[len(idir) :]
        context["GENERATED_FILES"] += [output]
        g = context["GENERATED_FILES"][output]
        g.script = action_overrides[name]
        g.inputs = action["inputs"]


def handle_copies(copies, context):
    dist = "$PRODUCT_DIR/dist/"
    for copy in copies:
        dest = copy["destination"]
        if not dest.startswith(dist):
            raise NotImplementedError(
                "GYP copies to somewhere other than <(PRODUCT_DIR)/dist not supported: %s"
                % dest
            )
        dest_paths = dest[len(dist) :].split("/")
        exports = context["EXPORTS"]
        while dest_paths:
            exports = getattr(exports, dest_paths.pop(0))
        exports += sorted(copy["files"], key=lambda x: x.lower())


def process_gyp_result(
    gyp_result,
    gyp_dir_attrs,
    path,
    config,
    output,
    non_unified_sources,
    action_overrides,
):
    flat_list, targets, data = gyp_result
    no_chromium = gyp_dir_attrs.no_chromium
    no_unified = gyp_dir_attrs.no_unified

    # Process all targets from the given gyp files and its dependencies.
    # The path given to AllTargets needs to use os.sep, while the frontend code
    # gives us paths normalized with forward slash separator.
    for target in sorted(
        gyp.common.AllTargets(flat_list, targets, path.replace("/", os.sep))
    ):
        build_file, target_name, toolset = gyp.common.ParseQualifiedTarget(target)

        # Each target is given its own objdir. The base of that objdir
        # is derived from the relative path from the root gyp file path
        # to the current build_file, placed under the given output
        # directory. Since several targets can be in a given build_file,
        # separate them in subdirectories using the build_file basename
        # and the target_name.
        reldir = mozpath.relpath(mozpath.dirname(build_file), mozpath.dirname(path))
        subdir = "%s_%s" % (
            mozpath.splitext(mozpath.basename(build_file))[0],
            target_name,
        )
        # Emit a context for each target.
        context = GypContext(
            config,
            mozpath.relpath(mozpath.join(output, reldir, subdir), config.topobjdir),
        )
        context.add_source(mozpath.abspath(build_file))
        # The list of included files returned by gyp are relative to build_file
        for f in data[build_file]["included_files"]:
            context.add_source(
                mozpath.abspath(mozpath.join(mozpath.dirname(build_file), f))
            )

        spec = targets[target]

        # Derive which gyp configuration to use based on MOZ_DEBUG.
        c = "Debug" if config.substs.get("MOZ_DEBUG") else "Release"
        if c not in spec["configurations"]:
            raise RuntimeError(
                "Missing %s gyp configuration for target %s "
                "in %s" % (c, target_name, build_file)
            )
        target_conf = spec["configurations"][c]

        if "actions" in spec:
            handle_actions(spec["actions"], context, action_overrides)
        if "copies" in spec:
            handle_copies(spec["copies"], context)

        use_libs = []
        libs = []

        def add_deps(s):
            for t in s.get("dependencies", []) + s.get("dependencies_original", []):
                ty = targets[t]["type"]
                if ty in ("static_library", "shared_library"):
                    l = targets[t]["target_name"]
                    if l not in use_libs:
                        use_libs.append(l)
                # Manually expand out transitive dependencies--
                # gyp won't do this for static libs or none targets.
                if ty in ("static_library", "none"):
                    add_deps(targets[t])
            libs.extend(spec.get("libraries", []))

        # XXX: this sucks, but webrtc breaks with this right now because
        # it builds a library called 'gtest' and we just get lucky
        # that it isn't in USE_LIBS by that name anywhere.
        if no_chromium:
            add_deps(spec)

        os_libs = []
        for l in libs:
            if l.startswith("-"):
                if l.startswith("-l"):
                    # Remove "-l" for consumption in OS_LIBS. Other flags
                    # are passed through unchanged.
                    l = l[2:]
                if l not in os_libs:
                    os_libs.append(l)
            elif l.endswith(".lib"):
                l = l[:-4]
                if l not in os_libs:
                    os_libs.append(l)
            elif l:
                # For library names passed in from moz.build.
                l = os.path.basename(l)
                if l not in use_libs:
                    use_libs.append(l)

        if spec["type"] == "none":
            if not ("actions" in spec or "copies" in spec):
                continue
        elif spec["type"] in ("static_library", "shared_library", "executable"):
            # Remove leading 'lib' from the target_name if any, and use as
            # library name.
            name = six.ensure_text(spec["target_name"])
            if spec["type"] in ("static_library", "shared_library"):
                if name.startswith("lib"):
                    name = name[3:]
                context["LIBRARY_NAME"] = name
            else:
                context["PROGRAM"] = name
            if spec["type"] == "shared_library":
                context["FORCE_SHARED_LIB"] = True
            elif (
                spec["type"] == "static_library"
                and spec.get("variables", {}).get("no_expand_libs", "0") == "1"
            ):
                # PSM links a NSS static library, but our folded libnss
                # doesn't actually export everything that all of the
                # objects within would need, so that one library
                # should be built as a real static library.
                context["NO_EXPAND_LIBS"] = True
            if use_libs:
                context["USE_LIBS"] = sorted(use_libs, key=lambda s: s.lower())
            if os_libs:
                context["OS_LIBS"] = os_libs
            # gyp files contain headers and asm sources in sources lists.
            sources = []
            unified_sources = []
            extensions = set()
            use_defines_in_asflags = False
            for f in spec.get("sources", []):
                ext = mozpath.splitext(f)[-1]
                extensions.add(ext)
                if f.startswith("$INTERMEDIATE_DIR/"):
                    s = ObjDirPath(context, f.replace("$INTERMEDIATE_DIR/", "!"))
                else:
                    s = SourcePath(context, f)
                if ext == ".h":
                    continue
                if ext == ".def":
                    context["SYMBOLS_FILE"] = s
                elif ext != ".S" and not no_unified and s not in non_unified_sources:
                    unified_sources.append(s)
                else:
                    sources.append(s)
                # The Mozilla build system doesn't use DEFINES for building
                # ASFILES.
                if ext == ".s":
                    use_defines_in_asflags = True

            # The context expects alphabetical order when adding sources
            context["SOURCES"] = alphabetical_sorted(sources)
            context["UNIFIED_SOURCES"] = alphabetical_sorted(unified_sources)

            defines = target_conf.get("defines", [])
            if config.substs["CC_TYPE"] == "clang-cl" and no_chromium:
                msvs_settings = gyp.msvs_emulation.MsvsSettings(spec, {})
                # Hack: MsvsSettings._TargetConfig tries to compare a str to an int,
                # so convert manually.
                msvs_settings.vs_version.short_name = int(
                    msvs_settings.vs_version.short_name
                )
                defines.extend(msvs_settings.GetComputedDefines(c))
            for define in defines:
                if "=" in define:
                    name, value = define.split("=", 1)
                    context["DEFINES"][name] = value
                else:
                    context["DEFINES"][define] = True

            product_dir_dist = "$PRODUCT_DIR/dist/"
            for include in target_conf.get("include_dirs", []):
                if include.startswith(product_dir_dist):
                    # special-case includes of <(PRODUCT_DIR)/dist/ to match
                    # handle_copies above. This is used for NSS' exports.
                    include = "!/dist/include/" + include[len(product_dir_dist) :]
                elif include.startswith(config.topobjdir):
                    # NSPR_INCLUDE_DIR gets passed into the NSS build this way.
                    include = "!/" + mozpath.relpath(include, config.topobjdir)
                else:
                    # moz.build expects all LOCAL_INCLUDES to exist, so ensure they do.
                    #
                    # NB: gyp files sometimes have actual absolute paths (e.g.
                    # /usr/include32) and sometimes paths that moz.build considers
                    # absolute, i.e. starting from topsrcdir. There's no good way
                    # to tell them apart here, and the actual absolute paths are
                    # likely bogus. In any event, actual absolute paths will be
                    # filtered out by trying to find them in topsrcdir.
                    #
                    # We do allow !- and %-prefixed paths, assuming they come
                    # from moz.build and will be handled the same way as if they
                    # were given to LOCAL_INCLUDES in moz.build.
                    if include.startswith("/"):
                        resolved = mozpath.abspath(
                            mozpath.join(config.topsrcdir, include[1:])
                        )
                    elif not include.startswith(("!", "%")):
                        resolved = mozpath.abspath(
                            mozpath.join(mozpath.dirname(build_file), include)
                        )
                    if not include.startswith(("!", "%")) and not os.path.exists(
                        resolved
                    ):
                        continue
                context["LOCAL_INCLUDES"] += [include]

            context["ASFLAGS"] = target_conf.get("asflags_mozilla", [])
            if use_defines_in_asflags and defines:
                context["ASFLAGS"] += ["-D" + d for d in defines]
            if config.substs["OS_TARGET"] == "SunOS":
                context["LDFLAGS"] = target_conf.get("ldflags", [])
            flags = target_conf.get("cflags_mozilla", [])
            if flags:
                suffix_map = {
                    ".c": "CFLAGS",
                    ".cpp": "CXXFLAGS",
                    ".cc": "CXXFLAGS",
                    ".m": "CMFLAGS",
                    ".mm": "CMMFLAGS",
                }
                variables = (suffix_map[e] for e in extensions if e in suffix_map)
                for var in variables:
                    for f in flags:
                        # We may be getting make variable references out of the
                        # gyp data, and we don't want those in emitted data, so
                        # substitute them with their actual value.
                        f = expand_variables(f, config.substs).split()
                        if not f:
                            continue
                        # the result may be a string or a list.
                        if isinstance(f, str):
                            context[var].append(f)
                        else:
                            context[var].extend(f)
        else:
            # Ignore other types because we don't have
            # anything using them, and we're not testing them. They can be
            # added when that becomes necessary.
            raise NotImplementedError("Unsupported gyp target type: %s" % spec["type"])

        if not no_chromium:
            # Add some features to all contexts. Put here in case LOCAL_INCLUDES
            # order matters.
            context["LOCAL_INCLUDES"] += [
                "!/ipc/ipdl/_ipdlheaders",
                "/ipc/chromium/src",
            ]
            # These get set via VC project file settings for normal GYP builds.
            if config.substs["OS_TARGET"] == "WINNT":
                context["DEFINES"]["UNICODE"] = True
                context["DEFINES"]["_UNICODE"] = True
        context["COMPILE_FLAGS"]["OS_INCLUDES"] = []

        for key, value in gyp_dir_attrs.sandbox_vars.items():
            if context.get(key) and isinstance(context[key], list):
                # If we have a key from sanbox_vars that's also been
                # populated here we use the value from sandbox_vars as our
                # basis rather than overriding outright.
                context[key] = value + context[key]
            elif context.get(key) and isinstance(context[key], dict):
                context[key].update(value)
            else:
                context[key] = value

        yield context


# A version of gyp.Load that doesn't return the generator (because module objects
# aren't Pickle-able, and we don't use it anyway).
def load_gyp(*args):
    _, flat_list, targets, data = gyp.Load(*args)
    return flat_list, targets, data


class GypProcessor:
    """Reads a gyp configuration in the background using the given executor and
    emits GypContexts for the backend to process.

    config is a ConfigEnvironment, path is the path to a root gyp configuration
    file, and output is the base path under which the objdir for the various
    gyp dependencies will be. gyp_dir_attrs are attributes set for the dir
    from moz.build.
    """

    def __init__(
        self,
        config,
        gyp_dir_attrs,
        path,
        output,
        executor,
        action_overrides,
        non_unified_sources,
    ):
        self._path = path
        self._config = config
        self._output = output
        self._non_unified_sources = non_unified_sources
        self._gyp_dir_attrs = gyp_dir_attrs
        self._action_overrides = action_overrides
        self.execution_time = 0.0
        self._results = []

        # gyp expects plain str instead of unicode. The frontend code gives us
        # unicode strings, so convert them.
        if config.substs["CC_TYPE"] == "clang-cl":
            # This isn't actually used anywhere in this generator, but it's needed
            # to override the registry detection of VC++ in gyp.
            os.environ.update(
                {
                    "GYP_MSVS_OVERRIDE_PATH": "fake_path",
                    "GYP_MSVS_VERSION": config.substs["MSVS_VERSION"],
                }
            )

        params = {
            "parallel": False,
            "generator_flags": {},
            "build_files": [path],
            "root_targets": None,
        }
        # The NSS gyp configuration uses CC and CFLAGS to determine the
        # floating-point ABI on arm.
        os.environ.update(
            CC=config.substs["CC"],
            CFLAGS=shellutil.quote(*config.substs["CC_BASE_FLAGS"]),
        )

        if gyp_dir_attrs.no_chromium:
            includes = []
            depth = mozpath.dirname(path)
        else:
            depth = chrome_src
            # Files that gyp_chromium always includes
            includes = [mozpath.join(script_dir, "gyp_includes", "common.gypi")]
            finder = FileFinder(chrome_src)
            includes.extend(
                mozpath.join(chrome_src, name)
                for name, _ in finder.find("*/supplement.gypi")
            )

        # We need to normalize EnumStrings to strings because the gyp code is
        # not guaranteed to like them.
        def normalize(obj):
            if isinstance(obj, dict):
                return {k: normalize(v) for k, v in obj.items()}
            if isinstance(obj, str):  # includes EnumStrings
                return str(obj)
            if isinstance(obj, Iterable):
                return [normalize(o) for o in obj]
            return obj

        str_vars = normalize(gyp_dir_attrs.variables)
        str_vars["python"] = sys.executable
        self._gyp_loader_future = executor.submit(
            load_gyp, [path], "mozbuild", str_vars, includes, depth, params
        )

    @property
    def results(self):
        if self._results:
            for res in self._results:
                yield res
        else:
            # We report our execution time as the time spent blocked in a call
            # to `result`, which is the only case a gyp processor will
            # contribute significantly to total wall time.
            t0 = time.monotonic()
            flat_list, targets, data = self._gyp_loader_future.result()
            self.execution_time += time.monotonic() - t0
            results = []
            for res in process_gyp_result(
                (flat_list, targets, data),
                self._gyp_dir_attrs,
                self._path,
                self._config,
                self._output,
                self._non_unified_sources,
                self._action_overrides,
            ):
                results.append(res)
                yield res
            self._results = results
