# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
from collections import OrderedDict
from collections.abc import Iterable
from pathlib import Path
from types import ModuleType

import mozpack.path as mozpath

from mozbuild.shellutil import quote as shell_quote
from mozbuild.util import (
    FileAvoidWrite,
    ReadOnlyDict,
    memoized_property,
    system_encoding,
)


class ConfigStatusFailure(Exception):
    """Error loading config.status"""


class BuildConfig:
    """Represents the output of configure."""

    _CODE_CACHE = {}

    def __init__(self):
        self.topsrcdir = None
        self.topobjdir = None
        self.defines = {}
        self.substs = {}
        self.files = []
        self.mozconfig = None

    @classmethod
    def from_config_status(cls, path):
        """Create an instance from a config.status file."""
        code_cache = cls._CODE_CACHE
        mtime = os.path.getmtime(path)

        # cache the compiled code as it can be reused
        # we cache it the first time, or if the file changed
        if path not in code_cache or code_cache[path][0] != mtime:
            # Add config.status manually to sys.modules so it gets picked up by
            # iter_modules_in_path() for automatic dependencies.
            mod = ModuleType("config.status")
            mod.__file__ = path
            sys.modules["config.status"] = mod

            with open(path) as fh:
                source = fh.read()
                code_cache[path] = (
                    mtime,
                    compile(source, path, "exec", dont_inherit=1),
                )

        g = {"__builtins__": __builtins__, "__file__": path}
        l = {}
        try:
            exec(code_cache[path][1], g, l)
        except Exception:
            raise ConfigStatusFailure()

        config = BuildConfig()

        for name in l["__all__"]:
            setattr(config, name, l[name])

        return config


class ConfigEnvironment:
    """Perform actions associated with a configured but bare objdir.

    The purpose of this class is to preprocess files from the source directory
    and output results in the object directory.

    There are two types of files: config files and config headers,
    each treated through a different member function.

    Creating a ConfigEnvironment requires a few arguments:
      - topsrcdir and topobjdir are, respectively, the top source and
        the top object directory.
      - defines is a dict filled from AC_DEFINE and AC_DEFINE_UNQUOTED in autoconf.
      - substs is a dict filled from AC_SUBST in autoconf.

    ConfigEnvironment automatically defines one additional substs variable
    from all the defines:
      - ACDEFINES contains the defines in the form -DNAME=VALUE, for use on
        preprocessor command lines. The order in which defines were given
        when creating the ConfigEnvironment is preserved.

    and two other additional subst variables from all the other substs:
      - ALLSUBSTS contains the substs in the form NAME = VALUE, in sorted
        order, for use in autoconf.mk. It includes ACDEFINES.
        Only substs with a VALUE are included, such that the resulting file
        doesn't change when new empty substs are added.
        This results in less invalidation of build dependencies in the case
        of autoconf.mk..
      - ALLEMPTYSUBSTS contains the substs with an empty value, in the form NAME =.

    ConfigEnvironment expects a "top_srcdir" subst to be set with the top
    source directory, in msys format on windows. It is used to derive a
    "srcdir" subst when treating config files. It can either be an absolute
    path or a path relative to the topobjdir.
    """

    def __init__(
        self,
        topsrcdir,
        topobjdir,
        defines=None,
        substs=None,
        source=None,
        mozconfig=None,
    ):
        if not source:
            source = mozpath.join(topobjdir, "config.status")
        self.source = source
        self.defines = ReadOnlyDict(defines or {})
        self.substs = dict(substs or {})
        self.topsrcdir = mozpath.abspath(topsrcdir)
        self.topobjdir = mozpath.abspath(topobjdir)
        self.mozconfig = mozpath.abspath(mozconfig) if mozconfig else None
        self.lib_prefix = self.substs.get("LIB_PREFIX", "")
        if "LIB_SUFFIX" in self.substs:
            self.lib_suffix = ".%s" % self.substs["LIB_SUFFIX"]
        self.dll_prefix = self.substs.get("DLL_PREFIX", "")
        self.dll_suffix = self.substs.get("DLL_SUFFIX", "")
        self.host_dll_prefix = self.substs.get("HOST_DLL_PREFIX", "")
        self.host_dll_suffix = self.substs.get("HOST_DLL_SUFFIX", "")
        if self.substs.get("IMPORT_LIB_SUFFIX"):
            self.import_prefix = self.lib_prefix
            self.import_suffix = ".%s" % self.substs["IMPORT_LIB_SUFFIX"]
        else:
            self.import_prefix = self.dll_prefix
            self.import_suffix = self.dll_suffix
        if self.substs.get("HOST_IMPORT_LIB_SUFFIX"):
            self.host_import_prefix = self.substs.get("HOST_LIB_PREFIX", "")
            self.host_import_suffix = ".%s" % self.substs["HOST_IMPORT_LIB_SUFFIX"]
        else:
            self.host_import_prefix = self.host_dll_prefix
            self.host_import_suffix = self.host_dll_suffix
        self.bin_suffix = self.substs.get("BIN_SUFFIX", "")

        global_defines = [name for name in self.defines]
        self.substs["ACDEFINES"] = " ".join(
            [
                "-D%s=%s" % (name, shell_quote(self.defines[name]).replace("$", "$$"))
                for name in sorted(global_defines)
            ]
        )

        def serialize(name, obj):
            if isinstance(obj, str):
                return obj
            if isinstance(obj, Iterable):
                return " ".join(obj)
            raise Exception("Unhandled type %s for %s", type(obj), str(name))

        self.substs["ALLSUBSTS"] = "\n".join(
            sorted(
                [
                    "%s = %s" % (name, serialize(name, self.substs[name]))
                    for name in self.substs
                    if self.substs[name]
                ]
            )
        )
        self.substs["ALLEMPTYSUBSTS"] = "\n".join(
            sorted(["%s =" % name for name in self.substs if not self.substs[name]])
        )

        self.substs = ReadOnlyDict(self.substs)

    @property
    def is_artifact_build(self):
        return self.substs.get("MOZ_ARTIFACT_BUILDS", False)

    @memoized_property
    def acdefines(self):
        acdefines = dict((name, self.defines[name]) for name in self.defines)
        return ReadOnlyDict(acdefines)

    @staticmethod
    def from_config_status(path):
        config = BuildConfig.from_config_status(path)

        return ConfigEnvironment(
            config.topsrcdir, config.topobjdir, config.defines, config.substs, path
        )


class PartialConfigDict:
    """Facilitates mapping the config.statusd defines & substs with dict-like access.

    This allows a buildconfig client to use buildconfig.defines['FOO'] (and
    similar for substs), where the value of FOO is delay-loaded until it is
    needed.
    """

    def __init__(self, config_statusd, typ, environ_override=False):
        self._dict = {}
        self._datadir = mozpath.join(config_statusd, typ)
        self._config_track = mozpath.join(self._datadir, "config.track")
        self._files = set()
        self._environ_override = environ_override

    def _load_config_track(self):
        existing_files = set()
        try:
            with open(self._config_track) as fh:
                existing_files.update(fh.read().splitlines())
        except OSError:
            pass
        return existing_files

    def _write_file(self, key, value):
        filename = mozpath.join(self._datadir, key)
        with FileAvoidWrite(filename) as fh:
            to_write = json.dumps(value, indent=4)
            fh.write(to_write.encode(system_encoding))
        return filename

    def _fill_group(self, values):
        # Clear out any cached values. This is mostly for tests that will check
        # the environment, write out a new set of variables, and then check the
        # environment again. Normally only configure ends up calling this
        # function, and other consumers create their own
        # PartialConfigEnvironments in new python processes.
        self._dict = {}

        existing_files = self._load_config_track()
        existing_files = {Path(f) for f in existing_files}

        new_files = set()
        for k, v in values.items():
            new_files.add(Path(self._write_file(k, v)))

        for filename in existing_files - new_files:
            # We can't actually os.remove() here, since make would not see that the
            # file has been removed and that the target needs to be updated. Instead
            # we just overwrite the file with a value of None, which is equivalent
            # to a non-existing file.
            with FileAvoidWrite(filename) as fh:
                json.dump(None, fh)

        with FileAvoidWrite(self._config_track) as fh:
            for f in sorted(new_files):
                fh.write("%s\n" % f)

    def __getitem__(self, key):
        if self._environ_override:
            if (key not in ("CPP", "CXXCPP", "SHELL")) and (key in os.environ):
                return os.environ[key]

        if key not in self._dict:
            data = None
            try:
                filename = mozpath.join(self._datadir, key)
                self._files.add(filename)
                with open(filename) as f:
                    data = json.load(f)
            except OSError:
                pass
            self._dict[key] = data

        if self._dict[key] is None:
            raise KeyError("'%s'" % key)
        return self._dict[key]

    def __setitem__(self, key, value):
        self._dict[key] = value

    def get(self, key, default=None):
        return self[key] if key in self else default

    def __contains__(self, key):
        try:
            return self[key] is not None
        except KeyError:
            return False

    def iteritems(self):
        existing_files = self._load_config_track()
        for f in existing_files:
            # The track file contains filenames, and the basename is the
            # variable name.
            var = mozpath.basename(f)
            yield var, self[var]


class PartialConfigEnvironment:
    """Allows access to individual config.status items via config.statusd/* files.

    This class is similar to the full ConfigEnvironment, which uses
    config.status, except this allows access and tracks dependencies to
    individual configure values. It is intended to be used during the build
    process to handle things like GENERATED_FILES, CONFIGURE_DEFINE_FILES, and
    anything else that may need to access specific substs or defines.

    Creating a PartialConfigEnvironment requires only the topobjdir, which is
    needed to distinguish between the top-level environment and the js/src
    environment.

    The PartialConfigEnvironment automatically defines one additional subst variable
    from all the defines:

      - ACDEFINES contains the defines in the form -DNAME=VALUE, for use on
        preprocessor command lines. The order in which defines were given
        when creating the ConfigEnvironment is preserved.

    and one additional define from all the defines as a dictionary:

      - ALLDEFINES contains all of the global defines as a dictionary. This is
        intended to be used instead of the defines structure from config.status so
        that scripts can depend directly on its value.
    """

    def __init__(self, topobjdir):
        config_statusd = mozpath.join(topobjdir, "config.statusd")
        self.substs = PartialConfigDict(config_statusd, "substs", environ_override=True)
        self.defines = PartialConfigDict(config_statusd, "defines")
        self.topobjdir = topobjdir

    def write_vars(self, config):
        substs = config["substs"].copy()
        defines = config["defines"].copy()

        global_defines = [name for name in config["defines"]]
        acdefines = " ".join(
            [
                "-D%s=%s"
                % (name, shell_quote(config["defines"][name]).replace("$", "$$"))
                for name in sorted(global_defines)
            ]
        )
        substs["ACDEFINES"] = acdefines

        all_defines = OrderedDict()
        for k in global_defines:
            all_defines[k] = config["defines"][k]
        defines["ALLDEFINES"] = all_defines

        self.substs._fill_group(substs)
        self.defines._fill_group(defines)

    def get_dependencies(self):
        return ["$(wildcard %s)" % f for f in self.substs._files | self.defines._files]
