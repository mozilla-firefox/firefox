# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import errno
import io
import json
import logging
import os
import subprocess
import sys
from pathlib import Path

import mozpack.path as mozpath
import six
from mach.mixin.process import ProcessExecutionMixin
from mozboot.mozconfig import MozconfigFindException
from mozfile import which
from mozversioncontrol import (
    GitRepository,
    HgRepository,
    InvalidRepoPath,
    MissingConfigureInfo,
    MissingVCSTool,
    get_repository_from_build_config,
    get_repository_object,
)

from .backend.configenvironment import ConfigEnvironment, ConfigStatusFailure
from .configure import ConfigureSandbox
from .controller.clobber import Clobberer
from .mozconfig import MozconfigLoader, MozconfigLoadException
from .util import cpu_count, memoize, memoized_property

try:
    import psutil
except Exception:
    psutil = None


class BadEnvironmentException(Exception):
    """Base class for errors raised when the build environment is not sane."""


class BuildEnvironmentNotFoundException(BadEnvironmentException, AttributeError):
    """Raised when we could not find a build environment."""


class ObjdirMismatchException(BadEnvironmentException):
    """Raised when the current dir is an objdir and doesn't match the mozconfig."""

    def __init__(self, objdir1, objdir2):
        self.objdir1 = objdir1
        self.objdir2 = objdir2

    def __str__(self):
        return "Objdir mismatch: %s != %s" % (self.objdir1, self.objdir2)


class BinaryNotFoundException(Exception):
    """Raised when the binary is not found in the expected location."""

    def __init__(self, path):
        self.path = path

    def __str__(self):
        return f"Binary expected at {self.path} does not exist."

    def help(self):
        return "It looks like your program isn't built. You can run |./mach build| to build it."


class MozbuildObject(ProcessExecutionMixin):
    """Base class providing basic functionality useful to many modules.

    Modules in this package typically require common functionality such as
    accessing the current config, getting the location of the source directory,
    running processes, etc. This classes provides that functionality. Other
    modules can inherit from this class to obtain this functionality easily.
    """

    def __init__(
        self,
        topsrcdir,
        settings,
        log_manager,
        topobjdir=None,
        mozconfig=MozconfigLoader.AUTODETECT,
        virtualenv_name=None,
    ):
        """Create a new Mozbuild object instance.

        Instances are bound to a source directory, a ConfigSettings instance,
        and a LogManager instance. The topobjdir may be passed in as well. If
        it isn't, it will be calculated from the active mozconfig.
        """
        self.topsrcdir = mozpath.realpath(topsrcdir)
        self.settings = settings

        self.populate_logger()
        self.log_manager = log_manager

        self._make = None
        self._topobjdir = mozpath.realpath(topobjdir) if topobjdir else topobjdir
        self._mozconfig = mozconfig
        self._config_environment = None
        self._virtualenv_name = virtualenv_name or "common"
        self._virtualenv_manager = None

    @classmethod
    def from_environment(cls, cwd=None, detect_virtualenv_mozinfo=True, **kwargs):
        """Create a MozbuildObject by detecting the proper one from the env.

        This examines environment state like the current working directory and
        creates a MozbuildObject from the found source directory, mozconfig, etc.

        The role of this function is to identify a topsrcdir, topobjdir, and
        mozconfig file.

        If the current working directory is inside a known objdir, we always
        use the topsrcdir and mozconfig associated with that objdir.

        If the current working directory is inside a known srcdir, we use that
        topsrcdir and look for mozconfigs using the default mechanism, which
        looks inside environment variables.

        If the current Python interpreter is running from a virtualenv inside
        an objdir, we use that as our objdir.

        If we're not inside a srcdir or objdir, an exception is raised.

        detect_virtualenv_mozinfo determines whether we should look for a
        mozinfo.json file relative to the virtualenv directory. This was
        added to facilitate testing. Callers likely shouldn't change the
        default.
        """

        cwd = os.path.realpath(cwd or os.getcwd())
        topsrcdir = None
        topobjdir = None
        mozconfig = MozconfigLoader.AUTODETECT

        def load_mozinfo(path):
            info = json.load(open(path, encoding="utf-8"))
            topsrcdir = info.get("topsrcdir")
            topobjdir = os.path.dirname(path)
            mozconfig = info.get("mozconfig")
            return topsrcdir, topobjdir, mozconfig

        for dir_path in [str(path) for path in [cwd] + list(Path(cwd).parents)]:
            # If we find a mozinfo.json, we are in the objdir.
            mozinfo_path = os.path.join(dir_path, "mozinfo.json")
            if os.path.isfile(mozinfo_path):
                topsrcdir, topobjdir, mozconfig = load_mozinfo(mozinfo_path)
                break

        if not topsrcdir:
            # See if we're running from a Python virtualenv that's inside an objdir.
            # sys.prefix would look like "$objdir/_virtualenvs/$virtualenv/".
            # Note that virtualenv-based objdir detection work for instrumented builds,
            # because they aren't created in the scoped "instrumentated" objdir.
            # However, working-directory-ancestor-based objdir resolution should fully
            # cover that case.
            mozinfo_path = os.path.join(sys.prefix, "..", "..", "mozinfo.json")
            if detect_virtualenv_mozinfo and os.path.isfile(mozinfo_path):
                topsrcdir, topobjdir, mozconfig = load_mozinfo(mozinfo_path)

        if not topsrcdir:
            topsrcdir = str(Path(__file__).parent.parent.parent.parent.resolve())

        topsrcdir = mozpath.realpath(topsrcdir)
        if topobjdir:
            topobjdir = mozpath.realpath(topobjdir)

            if topsrcdir == topobjdir:
                raise BadEnvironmentException(
                    "The object directory appears "
                    "to be the same as your source directory (%s). This build "
                    "configuration is not supported." % topsrcdir
                )

        # If we can't resolve topobjdir, oh well. We'll figure out when we need
        # one.
        return cls(
            topsrcdir, None, None, topobjdir=topobjdir, mozconfig=mozconfig, **kwargs
        )

    def resolve_mozconfig_topobjdir(self, default=None):
        topobjdir = self.mozconfig.get("topobjdir") or default
        if not topobjdir:
            return None

        if "@CONFIG_GUESS@" in topobjdir:
            topobjdir = topobjdir.replace("@CONFIG_GUESS@", self.resolve_config_guess())

        if not os.path.isabs(topobjdir):
            topobjdir = os.path.abspath(os.path.join(self.topsrcdir, topobjdir))

        return mozpath.normsep(os.path.normpath(topobjdir))

    def build_out_of_date(self, output, dep_file):
        if not os.path.isfile(output):
            print(" Output reference file not found: %s" % output)
            return True
        if not os.path.isfile(dep_file):
            print(" Dependency file not found: %s" % dep_file)
            return True

        deps = []
        with open(dep_file, encoding="utf-8", newline="\n") as fh:
            deps = fh.read().splitlines()

        mtime = os.path.getmtime(output)
        for f in deps:
            try:
                dep_mtime = os.path.getmtime(f)
            except OSError as e:
                if e.errno == errno.ENOENT:
                    print(" Input not found: %s" % f)
                    return True
                raise
            if dep_mtime > mtime:
                print(" %s is out of date with respect to %s" % (output, f))
                return True
        return False

    def backend_out_of_date(self, backend_file):
        if not os.path.isfile(backend_file):
            return True

        # Check if any of our output files have been removed since
        # we last built the backend, re-generate the backend if
        # so.
        outputs = []
        with open(backend_file, encoding="utf-8", newline="\n") as fh:
            outputs = fh.read().splitlines()
        for output in outputs:
            if not os.path.isfile(mozpath.join(self.topobjdir, output)):
                return True

        dep_file = "%s.in" % backend_file
        return self.build_out_of_date(backend_file, dep_file)

    @property
    def topobjdir(self):
        if self._topobjdir is None:
            self._topobjdir = self.resolve_mozconfig_topobjdir(
                default="obj-@CONFIG_GUESS@"
            )

        return self._topobjdir

    @property
    def virtualenv_manager(self):
        from mach.site import CommandSiteManager
        from mach.util import get_state_dir, get_virtualenv_base_dir

        if self._virtualenv_manager is None:
            self._virtualenv_manager = CommandSiteManager.from_environment(
                self.topsrcdir,
                lambda: get_state_dir(
                    specific_to_topsrcdir=True, topsrcdir=self.topsrcdir
                ),
                self._virtualenv_name,
                get_virtualenv_base_dir(self.topsrcdir),
            )

        return self._virtualenv_manager

    @virtualenv_manager.setter
    def virtualenv_manager(self, command_site_manager):
        self._virtualenv_manager = command_site_manager

    @staticmethod
    @memoize
    def get_base_mozconfig_info(topsrcdir, path, env_mozconfig):
        # env_mozconfig is only useful for unittests, which change the value of
        # the environment variable, which has an impact on autodetection (when
        # path is MozconfigLoader.AUTODETECT), and memoization wouldn't account
        # for it without the explicit (unused) argument.
        out = six.StringIO()
        env = os.environ
        if path and path != MozconfigLoader.AUTODETECT:
            env = dict(env)
            env["MOZCONFIG"] = path

        # We use python configure to get mozconfig content and the value for
        # --target (from mozconfig if necessary, guessed otherwise).

        # Modified configure sandbox that replaces '--help' dependencies with
        # `always`, such that depends functions with a '--help' dependency are
        # not automatically executed when including files. We don't want all of
        # those from init.configure to execute, only a subset.
        class ReducedConfigureSandbox(ConfigureSandbox):
            def depends_impl(self, *args, **kwargs):
                args = tuple(
                    (
                        a
                        if not isinstance(a, str) or a != "--help"
                        else self._always.sandboxed
                    )
                    for a in args
                )
                return super(ReducedConfigureSandbox, self).depends_impl(
                    *args, **kwargs
                )

        # This may be called recursively from configure itself for $reasons,
        # so avoid logging to the same logger (configure uses "moz.configure")
        logger = logging.getLogger("moz.configure.reduced")
        handler = logging.StreamHandler(out)
        logger.addHandler(handler)
        # If this were true, logging would still propagate to "moz.configure".
        logger.propagate = False
        sandbox = ReducedConfigureSandbox(
            {},
            environ=env,
            argv=["mach"],
            logger=logger,
        )
        base_dir = os.path.join(topsrcdir, "build", "moz.configure")
        try:
            sandbox.include_file(os.path.join(base_dir, "init.configure"))
            # Force mozconfig options injection before getting the target.
            sandbox._value_for(sandbox["mozconfig_options"])
            return {
                "mozconfig": sandbox._value_for(sandbox["mozconfig"]),
                "target": sandbox._value_for(sandbox["real_target"]),
                "project": sandbox._value_for(sandbox._options["project"]),
                "artifact-builds": sandbox._value_for(
                    sandbox._options["artifact-builds"]
                ),
            }
        except SystemExit:
            print(out.getvalue())
            raise

    @property
    def base_mozconfig_info(self):
        return self.get_base_mozconfig_info(
            self.topsrcdir, self._mozconfig, os.environ.get("MOZCONFIG")
        )

    @property
    def mozconfig(self):
        """Returns information about the current mozconfig file.

        This a dict as returned by MozconfigLoader.read_mozconfig()
        """
        return self.base_mozconfig_info["mozconfig"]

    @property
    def config_environment(self):
        """Returns the ConfigEnvironment for the current build configuration.

        This property is only available once configure has executed.

        If configure's output is not available, this will raise.
        """
        if self._config_environment:
            return self._config_environment

        config_status = os.path.join(self.topobjdir, "config.status")

        if not os.path.exists(config_status) or not os.path.getsize(config_status):
            raise BuildEnvironmentNotFoundException(
                "config.status not available. Run configure."
            )

        try:
            self._config_environment = ConfigEnvironment.from_config_status(
                config_status
            )
        except ConfigStatusFailure as e:
            raise BuildEnvironmentNotFoundException(
                "config.status is outdated or broken. Run configure."
            ) from e

        return self._config_environment

    @property
    def defines(self):
        return self.config_environment.defines

    @property
    def substs(self):
        return self.config_environment.substs

    @property
    def distdir(self):
        return os.path.join(self.topobjdir, "dist")

    @property
    def bindir(self):
        return os.path.join(self.topobjdir, "dist", "bin")

    @property
    def includedir(self):
        return os.path.join(self.topobjdir, "dist", "include")

    @property
    def statedir(self):
        return os.path.join(self.topobjdir, ".mozbuild")

    @property
    def platform(self):
        """Returns current platform and architecture name"""
        import mozinfo

        platform_name = None
        bits = str(mozinfo.info["bits"])
        if mozinfo.isLinux:
            platform_name = "linux" + bits
        elif mozinfo.isWin:
            platform_name = "win" + bits
        elif mozinfo.isMac:
            platform_name = "macosx" + bits

        return platform_name, bits + "bit"

    @memoized_property
    def repository(self):
        """Get a `mozversioncontrol.Repository` object for the
        top source directory."""
        # We try to obtain a repo using the configured VCS info first.
        # If we don't have a configure context, fall back to auto-detection.
        try:
            return get_repository_from_build_config(self)
        except (
            BuildEnvironmentNotFoundException,
            MissingConfigureInfo,
            MissingVCSTool,
        ):
            pass

        return get_repository_object(self.topsrcdir)

    def reload_config_environment(self):
        """Force config.status to be re-read and return the new value
        of ``self.config_environment``.
        """
        self._config_environment = None
        return self.config_environment

    def mozbuild_reader(
        self, config_mode="build", vcs_revision=None, vcs_check_clean=True
    ):
        """Obtain a ``BuildReader`` for evaluating moz.build files.

        Given arguments, returns a ``mozbuild.frontend.reader.BuildReader``
        that can be used to evaluate moz.build files for this repo.

        ``config_mode`` is either ``build`` or ``empty``. If ``build``,
        ``self.config_environment`` is used. This requires a configured build
        system to work. If ``empty``, an empty config is used. ``empty`` is
        appropriate for file-based traversal mode where ``Files`` metadata is
        read.

        If ``vcs_revision`` is defined, it specifies a version control revision
        to use to obtain files content. The default is to use the filesystem.
        This mode is only supported with Mercurial repositories.

        If ``vcs_revision`` is not defined and the version control checkout is
        sparse, this implies ``vcs_revision='.'``.

        If ``vcs_revision`` is ``.`` (denotes the parent of the working
        directory), we will verify that the working directory is clean unless
        ``vcs_check_clean`` is False. This prevents confusion due to uncommitted
        file changes not being reflected in the reader.
        """
        from mozpack.files import MercurialRevisionFinder

        from mozbuild.frontend.reader import BuildReader, EmptyConfig, default_finder

        if config_mode == "build":
            config = self.config_environment
        elif config_mode == "empty":
            config = EmptyConfig(self.topsrcdir)
        else:
            raise ValueError("unknown config_mode value: %s" % config_mode)

        try:
            repo = self.repository
        except InvalidRepoPath:
            repo = None

        if (
            repo
            and repo != "SOURCE"
            and not vcs_revision
            and repo.sparse_checkout_present()
        ):
            vcs_revision = "."

        if vcs_revision is None:
            finder = default_finder
        else:
            # If we failed to detect the repo prior, check again to raise its
            # exception.
            if not repo:
                self.repository
                assert False

            if repo.name != "hg":
                raise Exception("do not support VCS reading mode for %s" % repo.name)

            if vcs_revision == "." and vcs_check_clean:
                with repo:
                    if not repo.working_directory_clean():
                        raise Exception(
                            "working directory is not clean; "
                            "refusing to use a VCS-based finder"
                        )

            finder = MercurialRevisionFinder(
                self.topsrcdir, rev=vcs_revision, recognize_repo_paths=True
            )

        return BuildReader(config, finder=finder)

    def is_clobber_needed(self):
        if not os.path.exists(self.topobjdir):
            return False
        return Clobberer(self.topsrcdir, self.topobjdir).clobber_needed()

    def get_binary_path(self, what="app", validate_exists=True, where="default"):
        """Obtain the path to a compiled binary for this build configuration.

        The what argument is the program or tool being sought after. See the
        code implementation for supported values.

        If validate_exists is True (the default), we will ensure the found path
        exists before returning, raising an exception if it doesn't.

        If where is 'staged-package', we will return the path to the binary in
        the package staging directory.

        If no arguments are specified, we will return the main binary for the
        configured XUL application.
        """

        if where not in ("default", "staged-package"):
            raise Exception("Don't know location %s" % where)

        substs = self.substs

        stem = self.distdir
        if where == "staged-package":
            stem = os.path.join(stem, substs["MOZ_APP_NAME"])

        if substs["OS_ARCH"] == "Darwin" and "MOZ_MACBUNDLE_NAME" in substs:
            stem = os.path.join(stem, substs["MOZ_MACBUNDLE_NAME"], "Contents", "MacOS")
        elif where == "default":
            stem = os.path.join(stem, "bin")

        leaf = None

        leaf = (substs["MOZ_APP_NAME"] if what == "app" else what) + substs[
            "BIN_SUFFIX"
        ]
        path = os.path.join(stem, leaf)

        if validate_exists and not os.path.exists(path):
            raise BinaryNotFoundException(path)

        return path

    def resolve_config_guess(self):
        return self.base_mozconfig_info["target"].alias

    def notify(self, msg):
        """Show a desktop notification with the supplied message

        On Linux and Mac, this will show a desktop notification with the message,
        but on Windows we can only flash the screen.
        """
        if "MOZ_NOSPAM" in os.environ or "MOZ_AUTOMATION" in os.environ:
            return

        try:
            if sys.platform.startswith("darwin"):
                notifier = which("terminal-notifier")
                if not notifier:
                    raise Exception(
                        "Install terminal-notifier to get "
                        "a notification when the build finishes."
                    )
                self.run_process(
                    [
                        notifier,
                        "-title",
                        "Mozilla Build System",
                        "-group",
                        "mozbuild",
                        "-message",
                        msg,
                    ],
                    ensure_exit_code=False,
                )
            elif sys.platform.startswith("win"):
                from ctypes import POINTER, WINFUNCTYPE, Structure, sizeof, windll
                from ctypes.wintypes import BOOL, DWORD, HANDLE, UINT

                class FLASHWINDOW(Structure):
                    _fields_ = [
                        ("cbSize", UINT),
                        ("hwnd", HANDLE),
                        ("dwFlags", DWORD),
                        ("uCount", UINT),
                        ("dwTimeout", DWORD),
                    ]

                FlashWindowExProto = WINFUNCTYPE(BOOL, POINTER(FLASHWINDOW))
                FlashWindowEx = FlashWindowExProto(("FlashWindowEx", windll.user32))
                FLASHW_CAPTION = 0x01
                FLASHW_TRAY = 0x02
                FLASHW_TIMERNOFG = 0x0C

                # GetConsoleWindows returns NULL if no console is attached. We
                # can't flash nothing.
                console = windll.kernel32.GetConsoleWindow()
                if not console:
                    return

                params = FLASHWINDOW(
                    sizeof(FLASHWINDOW),
                    console,
                    FLASHW_CAPTION | FLASHW_TRAY | FLASHW_TIMERNOFG,
                    3,
                    0,
                )
                FlashWindowEx(params)
            else:
                notifier = which("notify-send")
                if not notifier:
                    raise Exception(
                        "Install notify-send (usually part of "
                        "the libnotify package) to get a notification when "
                        "the build finishes."
                    )
                self.run_process(
                    [
                        notifier,
                        "--app-name=Mozilla Build System",
                        "Mozilla Build System",
                        msg,
                    ],
                    ensure_exit_code=False,
                )
        except Exception as e:
            self.log(
                logging.WARNING,
                "notifier-failed",
                {"error": str(e)},
                "Notification center failed: {error}",
            )

    def _ensure_objdir_exists(self):
        if os.path.isdir(self.statedir):
            return

        os.makedirs(self.statedir)

    def _ensure_state_subdir_exists(self, subdir):
        path = os.path.join(self.statedir, subdir)

        if os.path.isdir(path):
            return

        os.makedirs(path)

    def _get_state_filename(self, filename, subdir=None):
        path = self.statedir

        if subdir:
            path = os.path.join(path, subdir)

        return os.path.join(path, filename)

    def _wrap_path_argument(self, arg):
        return PathArgument(arg, self.topsrcdir, self.topobjdir)

    def _run_make(
        self,
        directory=None,
        filename=None,
        target=None,
        log=True,
        srcdir=False,
        line_handler=None,
        append_env=None,
        explicit_env=None,
        ignore_errors=False,
        ensure_exit_code=0,
        silent=True,
        print_directory=True,
        pass_thru=False,
        num_jobs=0,
        job_size=0,
        keep_going=False,
    ):
        """Invoke make.

        directory -- Relative directory to look for Makefile in.
        filename -- Explicit makefile to run.
        target -- Makefile target(s) to make. Can be a string or iterable of
            strings.
        srcdir -- If True, invoke make from the source directory tree.
            Otherwise, make will be invoked from the object directory.
        silent -- If True (the default), run make in silent mode.
        print_directory -- If True (the default), have make print directories
        while doing traversal.
        """
        self._ensure_objdir_exists()

        args = [self.substs["GMAKE"]]

        if directory:
            args.extend(["-C", directory.replace(os.sep, "/")])

        if filename:
            args.extend(["-f", filename])

        if num_jobs == 0 and self.mozconfig["make_flags"]:
            flags = iter(self.mozconfig["make_flags"])
            for flag in flags:
                if flag == "-j":
                    try:
                        flag = flags.next()
                    except StopIteration:
                        break
                    try:
                        num_jobs = int(flag)
                    except ValueError:
                        args.append(flag)
                elif flag.startswith("-j"):
                    try:
                        num_jobs = int(flag[2:])
                    except (ValueError, IndexError):
                        break
                else:
                    args.append(flag)

        if num_jobs == 0:
            if job_size == 0:
                job_size = 2.0 if self.substs.get("CC_TYPE") == "gcc" else 1.0  # GiB

            cpus = cpu_count()
            if not psutil or not job_size:
                num_jobs = cpus
            else:
                mem_gb = psutil.virtual_memory().total / 1024**3
                from_mem = round(mem_gb / job_size)
                num_jobs = max(1, min(cpus, from_mem))
                print(
                    "  Parallelism determined by memory: using %d jobs for %d cores "
                    "based on %.1f GiB RAM and estimated job size of %.1f GiB"
                    % (num_jobs, cpus, mem_gb, job_size)
                )

        args.append("-j%d" % num_jobs)

        if ignore_errors:
            args.append("-k")

        if silent:
            args.append("-s")

        # Print entering/leaving directory messages. Some consumers look at
        # these to measure progress.
        if print_directory:
            args.append("-w")

        if keep_going:
            args.append("-k")

        if isinstance(target, list):
            args.extend(target)
        elif target:
            args.append(target)

        fn = self._run_command_in_objdir

        if srcdir:
            fn = self._run_command_in_srcdir

        append_env = dict(append_env or ())
        append_env["MACH"] = "1"

        params = {
            "args": args,
            "line_handler": line_handler,
            "append_env": append_env,
            "explicit_env": explicit_env,
            "log_level": logging.INFO,
            "require_unix_environment": False,
            "ensure_exit_code": ensure_exit_code,
            "pass_thru": pass_thru,
            # Make manages its children, so mozprocess doesn't need to bother.
            # Having mozprocess manage children can also have side-effects when
            # building on Windows. See bug 796840.
            "ignore_children": True,
        }

        if log:
            params["log_name"] = "make"

        return fn(**params)

    def _run_command_in_srcdir(self, **args):
        return self.run_process(cwd=self.topsrcdir, **args)

    def _run_command_in_objdir(self, **args):
        return self.run_process(cwd=self.topobjdir, **args)

    def _is_windows(self):
        return os.name in ("nt", "ce")

    def _is_osx(self):
        return "darwin" in str(sys.platform).lower()

    def _spawn(self, cls):
        """Create a new MozbuildObject-derived class instance from ourselves.

        This is used as a convenience method to create other
        MozbuildObject-derived class instances. It can only be used on
        classes that have the same constructor arguments as us.
        """

        return cls(
            self.topsrcdir, self.settings, self.log_manager, topobjdir=self.topobjdir
        )

    def activate_virtualenv(self):
        self.virtualenv_manager.activate()

    def _set_log_level(self, verbose):
        self.log_manager.terminal_handler.setLevel(
            logging.INFO if not verbose else logging.DEBUG
        )

    def _ensure_zstd(self):
        try:
            import zstandard  # noqa: F401
        except (ImportError, AttributeError):
            self.activate_virtualenv()
            self.virtualenv_manager.install_pip_requirements(
                os.path.join(self.topsrcdir, "build", "zstandard_requirements.txt")
            )


class MachCommandBase(MozbuildObject):
    """Base class for mach command providers that wish to be MozbuildObjects.

    This provides a level of indirection so MozbuildObject can be refactored
    without having to change everything that inherits from it.
    """

    def __init__(self, context, virtualenv_name=None, metrics=None, no_auto_log=False):
        # Attempt to discover topobjdir through environment detection, as it is
        # more reliable than mozconfig when cwd is inside an objdir.
        topsrcdir = context.topdir
        topobjdir = None
        detect_virtualenv_mozinfo = True
        if hasattr(context, "detect_virtualenv_mozinfo"):
            detect_virtualenv_mozinfo = getattr(context, "detect_virtualenv_mozinfo")
        try:
            dummy = MozbuildObject.from_environment(
                cwd=context.cwd, detect_virtualenv_mozinfo=detect_virtualenv_mozinfo
            )
            topsrcdir = dummy.topsrcdir
            topobjdir = dummy._topobjdir
            if topobjdir:
                # If we're inside a objdir and the found mozconfig resolves to
                # another objdir, we abort. The reasoning here is that if you
                # are inside an objdir you probably want to perform actions on
                # that objdir, not another one. This prevents accidental usage
                # of the wrong objdir when the current objdir is ambiguous.
                config_topobjdir = dummy.resolve_mozconfig_topobjdir()

                if config_topobjdir and not Path(topobjdir).samefile(
                    Path(config_topobjdir)
                ):
                    raise ObjdirMismatchException(topobjdir, config_topobjdir)
        except BuildEnvironmentNotFoundException:
            pass
        except ObjdirMismatchException as e:
            print(
                "Ambiguous object directory detected. We detected that "
                "both %s and %s could be object directories. This is "
                "typically caused by having a mozconfig pointing to a "
                "different object directory from the current working "
                "directory. To solve this problem, ensure you do not have a "
                "default mozconfig in searched paths." % (e.objdir1, e.objdir2)
            )
            sys.exit(1)

        except MozconfigLoadException as e:
            print(e)
            sys.exit(1)

        MozbuildObject.__init__(
            self,
            topsrcdir,
            context.settings,
            context.log_manager,
            topobjdir=topobjdir,
            virtualenv_name=virtualenv_name,
        )

        self._mach_context = context
        self.metrics = metrics

        # Incur mozconfig processing so we have unified error handling for
        # errors. Otherwise, the exceptions could bubble back to mach's error
        # handler.
        try:
            self.mozconfig

        except MozconfigFindException as e:
            print(e)
            sys.exit(1)

        except MozconfigLoadException as e:
            print(e)
            sys.exit(1)

        # Always keep a log of the last command, but don't do that for mach
        # invokations from scripts (especially not the ones done by the build
        # system itself).
        try:
            fileno = getattr(sys.stdout, "fileno", lambda: None)()
        except io.UnsupportedOperation:
            fileno = None
        if fileno and os.isatty(fileno) and not no_auto_log:
            self._ensure_state_subdir_exists(".")
            logfile = self._get_state_filename("last_log.json")
            try:
                fd = open(logfile, "w")
                self.log_manager.add_json_handler(fd)
            except Exception as e:
                self.log(
                    logging.WARNING,
                    "mach",
                    {"error": str(e)},
                    "Log will not be kept for this command: {error}.",
                )

    def _sub_mach(self, argv):
        return subprocess.call(
            [sys.executable, os.path.join(self.topsrcdir, "mach")] + argv
        )


class MachCommandConditions:
    """A series of commonly used condition functions which can be applied to
    mach commands with providers deriving from MachCommandBase.
    """

    @staticmethod
    def is_firefox(cls):
        """Must have a Firefox build."""
        if hasattr(cls, "substs"):
            return cls.substs.get("MOZ_BUILD_APP") == "browser"
        return False

    @staticmethod
    def is_jsshell(cls):
        """Must have a jsshell build."""
        if hasattr(cls, "substs"):
            return cls.substs.get("MOZ_BUILD_APP") == "js"
        return False

    @staticmethod
    def is_thunderbird(cls):
        """Must have a Thunderbird build."""
        if hasattr(cls, "substs"):
            return cls.substs.get("MOZ_BUILD_APP") == "comm/mail"
        return False

    @staticmethod
    def is_firefox_or_thunderbird(cls):
        """Must have a Firefox or Thunderbird build."""
        return MachCommandConditions.is_firefox(
            cls
        ) or MachCommandConditions.is_thunderbird(cls)

    @staticmethod
    def is_android(cls):
        """Must have an Android build."""
        if hasattr(cls, "substs"):
            return cls.substs.get("MOZ_WIDGET_TOOLKIT") == "android"
        return False

    @staticmethod
    def is_not_android(cls):
        """Must not have an Android build."""
        if hasattr(cls, "substs"):
            return cls.substs.get("MOZ_WIDGET_TOOLKIT") != "android"
        return False

    @staticmethod
    def is_android_cpu(cls):
        """Targeting Android CPU."""
        if hasattr(cls, "substs"):
            return "ANDROID_CPU_ARCH" in cls.substs
        return False

    @staticmethod
    def is_firefox_or_android(cls):
        """Must have a Firefox or Android build."""
        return MachCommandConditions.is_firefox(
            cls
        ) or MachCommandConditions.is_android(cls)

    @staticmethod
    def has_build(cls):
        """Must have a build."""
        return MachCommandConditions.is_firefox_or_android(
            cls
        ) or MachCommandConditions.is_thunderbird(cls)

    @staticmethod
    def has_build_or_shell(cls):
        """Must have a build or a shell build."""
        return MachCommandConditions.has_build(cls) or MachCommandConditions.is_jsshell(
            cls
        )

    @staticmethod
    def is_hg(cls):
        """Must have a mercurial source checkout."""
        try:
            return isinstance(cls.repository, HgRepository)
        except InvalidRepoPath:
            return False

    @staticmethod
    def is_git(cls):
        """Must have a git source checkout."""
        try:
            return isinstance(cls.repository, GitRepository)
        except InvalidRepoPath:
            return False

    @staticmethod
    def is_artifact_build(cls):
        """Must be an artifact build."""
        if hasattr(cls, "substs"):
            return getattr(cls, "substs", {}).get("MOZ_ARTIFACT_BUILDS")
        return False

    @staticmethod
    def is_non_artifact_build(cls):
        """Must not be an artifact build."""
        if hasattr(cls, "substs"):
            return not MachCommandConditions.is_artifact_build(cls)
        return False

    @staticmethod
    def is_buildapp_in(cls, apps):
        """Must have a build for one of the given app"""
        for app in apps:
            attr = getattr(MachCommandConditions, f"is_{app}", None)
            if attr and attr(cls):
                return True
        return False


class PathArgument:
    """Parse a filesystem path argument and transform it in various ways."""

    def __init__(self, arg, topsrcdir, topobjdir, cwd=None):
        self.arg = arg
        self.topsrcdir = topsrcdir
        self.topobjdir = topobjdir
        self.cwd = os.getcwd() if cwd is None else cwd

    def relpath(self):
        """Return a path relative to the topsrcdir or topobjdir.

        If the argument is a path to a location in one of the base directories
        (topsrcdir or topobjdir), then strip off the base directory part and
        just return the path within the base directory."""

        abspath = os.path.abspath(os.path.join(self.cwd, self.arg))

        # If that path is within topsrcdir or topobjdir, return an equivalent
        # path relative to that base directory.
        for base_dir in [self.topobjdir, self.topsrcdir]:
            if abspath.startswith(os.path.abspath(base_dir)):
                return mozpath.relpath(abspath, base_dir)

        return mozpath.normsep(self.arg)

    def srcdir_path(self):
        return mozpath.join(self.topsrcdir, self.relpath())

    def objdir_path(self):
        return mozpath.join(self.topobjdir, self.relpath())


class ExecutionSummary(dict):
    """Helper for execution summaries."""

    def __init__(self, summary_format, **data):
        self._summary_format = ""
        assert "execution_time" in data
        self.extend(summary_format, **data)

    def extend(self, summary_format, **data):
        self._summary_format += summary_format
        self.update(data)

    def __str__(self):
        return self._summary_format.format(**self)

    def __getattr__(self, key):
        return self[key]
