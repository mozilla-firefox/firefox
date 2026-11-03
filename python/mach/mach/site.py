# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This file contains code for managing the Python import scope for Mach. This
# generally involves populating a Python virtualenv.

import ast
import enum
import functools
import glob
import os
import platform
import shutil
import site
import subprocess
import sys
import sysconfig
import tempfile
import textwrap
import warnings
from contextlib import contextmanager
from pathlib import Path
from typing import Callable, Optional

from mozfile import json
from packaging.specifiers import SpecifierSet
from packaging.utils import canonicalize_name

from mach.filelock import FileLock, Timeout
from mach.requirements import (
    MachEnvRequirements,
    UnexpectedFlexibleRequirementException,
)
from mach.util import resource_marker


class SiteLockTimeout(Exception):
    def __init__(self, lock_file, site_name, timeout):
        self.lock_file = lock_file
        super().__init__(
            f"Could not acquire the lock at {lock_file} for the {site_name} site after {timeout} seconds"
        )


PTH_FILENAME = "mach.pth"
METADATA_FILENAME = "moz_virtualenv_metadata.json"
# The following virtualenvs *may* be used in a context where they aren't allowed to
# install pip packages over the network. In such a case, they must access unvendored
# python packages via the system environment.
PIP_NETWORK_INSTALL_RESTRICTED_VIRTUALENVS = ("mach", "build", "common")

_is_windows = sys.platform == "cygwin" or (sys.platform == "win32" and os.sep == "\\")


@functools.cache
def use_uv():
    return (
        os.environ.get("MACH_NO_UV", "").lower()
        not in (
            "1",
            "true",
        )
        and get_uv_executable()
    )


@functools.cache
def get_uv_executable():
    return shutil.which("uv")


@functools.cache
def show_pip_output():
    return os.environ.get("MACH_SHOW_PIP_OUTPUT", "").lower() in ("1", "true")


def pip_command(*, python_executable, subcommand=None, args=None, non_uv_args=None):
    if use_uv():
        uv_executable = get_uv_executable()
        command = [uv_executable, "pip"]
        if subcommand:
            command.append(subcommand)
            python_root = Path(python_executable).parent.parent
            command.append(f"--python={python_root}")
        full_command = command + (args or [])
    else:
        command = [python_executable, "-m", "pip"]
        if subcommand:
            command.append(subcommand)
        full_command = command + (non_uv_args or []) + (args or [])

    return full_command


class VenvModuleNotFoundException(Exception):
    def __init__(self):
        msg = (
            'Mach was unable to find the "venv" module, which is needed '
            "to create virtual environments in Python. You may need to "
            "install it manually using the package manager for your system."
        )
        super(Exception, self).__init__(msg)


class VirtualenvOutOfDateException(Exception):
    pass


class MozSiteMetadataOutOfDateError(Exception):
    pass


class InstallPipRequirementsException(Exception):
    pass


class SiteUpToDateResult:
    def __init__(self, is_up_to_date, reason=None):
        self.is_up_to_date = is_up_to_date
        self.reason = reason


class SitePackagesSource(enum.Enum):
    NONE = "none"
    SYSTEM = "system"
    VENV = "pip"

    @classmethod
    def for_mach(cls):
        source = os.environ.get("MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE", "").lower()
        if source == "system":
            source = SitePackagesSource.SYSTEM
        elif source == "none":
            source = SitePackagesSource.NONE
        elif source == "pip":
            source = SitePackagesSource.VENV
        elif source:
            raise Exception(
                "Unexpected MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE value, expected one "
                'of "system", "pip", "none", or to not be set'
            )

        mach_use_system_python = bool(os.environ.get("MACH_USE_SYSTEM_PYTHON"))
        if source:
            if mach_use_system_python:
                raise Exception(
                    "The MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE environment variable is "
                    "set, so the MACH_USE_SYSTEM_PYTHON variable is redundant and "
                    "should be unset."
                )
            return source

        # Only print this warning once for the Mach site, so we don't spam it every
        # time a site handle is created.
        if mach_use_system_python:
            print(
                'The "MACH_USE_SYSTEM_PYTHON" environment variable is deprecated, '
                "please unset it or replace it with either "
                '"MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE=system" or '
                '"MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE=none"'
            )

        return (
            SitePackagesSource.NONE
            if (mach_use_system_python or os.environ.get("MOZ_AUTOMATION"))
            else SitePackagesSource.VENV
        )


class MozSiteMetadata:  # noqa PLW1641
    """Details about a Moz-managed python site

    When a Moz-managed site is active, its associated metadata is available
    at "MozSiteMetadata.current".

    Sites that have associated virtualenvs (so, those that aren't strictly leaning on
    the external python packages) will have their metadata written to
    <prefix>/moz_virtualenv_metadata.json.
    """

    # Used to track which which virtualenv has been activated in-process.
    current: Optional["MozSiteMetadata"] = None

    def __init__(
        self,
        hex_version: int,
        site_name: str,
        mach_site_packages_source: SitePackagesSource,
        original_python: "ExternalPythonSite",
        prefix: str,
    ):
        """
        Args:
            hex_version: The python version number from sys.hexversion
            site_name: The name of the site this metadata is associated with
            site_packages_source: Where this site imports its
                pip-installed dependencies from
            mach_site_packages_source: Where the Mach site imports
                its pip-installed dependencies from
            original_python: The external Python site that was
                used to invoke Mach. Usually the system Python, such as /usr/bin/python3
            prefix: The same value as "sys.prefix" is when running within the
                associated Python site. The same thing as the "virtualenv root".
        """

        self.hex_version = hex_version
        self.site_name = site_name
        self.mach_site_packages_source = mach_site_packages_source
        # original_python is needed for commands that tweak the system, such
        # as "./mach install-moz-phab".
        self.original_python = original_python
        self.prefix = prefix

    def write(self, is_finalized):
        raw = {
            "hex_version": self.hex_version,
            "virtualenv_name": self.site_name,
            "mach_site_packages_source": self.mach_site_packages_source.name,
            "original_python_executable": self.original_python.python_path,
            "is_finalized": is_finalized,
        }
        with open(os.path.join(self.prefix, METADATA_FILENAME), "w") as file:
            json.dump(raw, file)

    def __eq__(self, other):
        return (
            type(self) is type(other)
            and self.hex_version == other.hex_version
            and self.site_name == other.site_name
            and self.mach_site_packages_source == other.mach_site_packages_source
            # On Windows, execution environment can lead to different cases.  Normalize.
            and Path(self.original_python.python_path)
            == Path(other.original_python.python_path)
        )

    @classmethod
    def from_runtime(cls):
        if cls.current:
            return cls.current

        return cls.from_path(sys.prefix)

    @classmethod
    def from_path(cls, prefix):
        metadata_path = os.path.join(prefix, METADATA_FILENAME)
        out_of_date_exception = MozSiteMetadataOutOfDateError(
            f'The virtualenv at "{prefix}" is out-of-date.'
        )
        try:
            with open(metadata_path) as file:
                raw = json.load(file)

            if not raw.get("is_finalized", False):
                raise out_of_date_exception

            return cls(
                raw["hex_version"],
                raw["virtualenv_name"],
                SitePackagesSource[raw["mach_site_packages_source"]],
                ExternalPythonSite(raw["original_python_executable"]),
                metadata_path,
            )
        except FileNotFoundError:
            return None
        except KeyError:
            raise out_of_date_exception

    @contextmanager
    def update_current_site(self, executable):
        """Updates necessary global state when a site is activated

        Due to needing to fetch some state before the actual activation happens, this
        is represented as a context manager and should be used as follows:

        with metadata.update_current_site(executable):
            # Perform the actual implementation of changing the site, whether that is
            # by exec-ing "activate_this.py" in a virtualenv, modifying the sys.path
            # directly, or some other means
            ...
        """

        yield
        MozSiteMetadata.current = self

        sys.executable = executable


class MachSiteManager:
    """Represents the activate-able "import scope" Mach needs

    Whether running independently, using the system packages, or automatically managing
    dependencies with "pip install", this class provides an easy handle to verify
    that the "site" is up-to-date (whether than means that system packages don't
    collide with vendored packages, or that the on-disk virtualenv needs rebuilding).

    Note that, this is a *virtual* site: an on-disk Python virtualenv
    is only created if there will be "pip installs" into the Mach site.
    """

    def __init__(
        self,
        topsrcdir: str,
        virtualenv_root: Optional[str],
        requirements: MachEnvRequirements,
        original_python: "ExternalPythonSite",
        site_packages_source: SitePackagesSource,
        quiet: bool = False,
    ):
        """
        Args:
            topsrcdir: The path to the Firefox repo
            virtualenv_root: The path to the the associated Mach virtualenv,
                if any
            requirements: The requirements associated with the Mach site, parsed from
                the file at python/sites/mach.txt
            original_python: The external Python site that was used to invoke Mach.
                If Mach invocations are nested, then "original_python" refers to
                Python site that was used to start Mach first.
                Usually the system Python, such as /usr/bin/python3.
            site_packages_source: Where the Mach site will import its pip-installed
                dependencies from
        """
        self._topsrcdir = topsrcdir
        self._site_packages_source = site_packages_source
        self._requirements = requirements
        self._virtualenv_root = virtualenv_root
        self._metadata = MozSiteMetadata(
            sys.hexversion,
            "mach",
            site_packages_source,
            original_python,
            self._virtualenv_root,
        )
        self._quiet = quiet

    def _log(self, message: str):
        if not self._quiet:
            print(message)

    @classmethod
    def from_environment(
        cls, topsrcdir: str, get_state_dir: Callable[[], str], quiet: bool = False
    ):
        """
        Args:
            topsrcdir: The path to the Firefox repo
            get_state_dir: A function that resolves the path to the checkout-scoped
                state_dir, generally ~/.mozbuild/srcdirs/<checkout-based-dir>/
        """

        requirements = resolve_requirements(topsrcdir, "mach")

        # Mach needs to operate in environments in which no pip packages are installed
        # yet, and the system isn't guaranteed to have the packages we need. For example,
        # "./mach bootstrap" can't have any dependencies.
        # So, all external dependencies of Mach's must be optional.
        assert not requirements.pypi_requirements, (
            "Mach pip package requirements must be optional."
        )

        # external_python is the Python interpreter that invoked Mach for this process.
        external_python = ExternalPythonSite(sys.executable)

        # original_python is the first Python interpreter that invoked the top-level
        # Mach process. This is different from "external_python" when there's nested
        # Mach invocations.
        active_metadata = MozSiteMetadata.from_runtime()
        if active_metadata:
            original_python = active_metadata.original_python
        else:
            original_python = external_python

        source = SitePackagesSource.for_mach()
        virtualenv_root = (
            _mach_virtualenv_root(get_state_dir())
            if source == SitePackagesSource.VENV
            else None
        )
        return cls(
            topsrcdir,
            virtualenv_root,
            requirements,
            original_python,
            source,
            quiet,
        )

    def _up_to_date(self):
        if self._site_packages_source == SitePackagesSource.NONE:
            return SiteUpToDateResult(True)
        elif self._site_packages_source == SitePackagesSource.SYSTEM:
            _assert_pip_check(self._sys_path(), "mach", self._requirements)
            return SiteUpToDateResult(True)
        elif self._site_packages_source == SitePackagesSource.VENV:
            environment = self._virtualenv()
            return _is_venv_up_to_date(
                environment,
                self._pthfile_lines(environment),
                self._requirements,
                self._metadata,
                self._topsrcdir,
            )

    def ensure(self, *, force=False):
        root = None
        if self._virtualenv_root:
            root = self._virtualenv_root
        else:
            workspace = os.environ.get("WORKSPACE")
            if os.environ.get("MOZ_AUTOMATION") and workspace:
                # In CI, put Mach virtualenv in the $WORKSPACE dir, which
                # should be cleaned between jobs.
                root = os.path.join(workspace, "mach_virtualenv")

        # Although `root` should never be `None` here, let's guard against
        # that edge case by skipping the FileLock step if it is.
        if root:
            lock_file = Path(root).with_suffix(".lock")
            timeout = 60

            # In the scenario where multiple processes try to create a mach site that does not yet
            # exist, they will trample each other when attempting to create it. To resolve this, we
            # use a file lock. The first process to reach the lock will create it and ensure it is up
            # to date, while the other(s) wait(s). Once the first releases the lock, the others will
            # continue one-by-one and determine it's up-to-date.
            try:
                with FileLock(lock_file, timeout=timeout):
                    self._ensure(force=force)
            except Timeout:
                raise SiteLockTimeout(lock_file, "mach", timeout)
        else:
            self._ensure(force=force)

    def _ensure(self, force=False):
        result = self._up_to_date()
        if force or not result.is_up_to_date:
            if Path(sys.prefix) == Path(self._metadata.prefix):
                # If the Mach virtualenv is already activated, then the changes caused
                # by rebuilding the virtualenv won't take effect until the next time
                # Mach is used, which can lead to confusing one-off errors.
                # Instead, request that the user resolve the out-of-date situation,
                # *then* come back and run the intended command.
                raise VirtualenvOutOfDateException(result.reason)
            self._build()

    def attempt_populate_optional_packages(self):
        if self._site_packages_source != SitePackagesSource.VENV:
            return

        self._virtualenv().install_optional_packages(
            self._requirements.pypi_optional_requirements
        )

    def activate(self):
        assert not MozSiteMetadata.current

        self.ensure()
        with self._metadata.update_current_site(
            (
                self._virtualenv().python_path
                if self._site_packages_source == SitePackagesSource.VENV
                else sys.executable
            ),
        ):
            # Reset the sys.path to insulate ourselves from the environment.
            # This should be safe to do, since activation of the Mach site happens so
            # early in the Mach lifecycle that no packages should have been imported
            # from external sources yet.
            sys.path = self._sys_path()
            if self._site_packages_source == SitePackagesSource.VENV:
                # Activate the Mach virtualenv in the current Python context. This
                # automatically adds the virtualenv's "site-packages" to our scope, in
                # addition to our first-party/vendored modules since they're specified
                # in the "mach.pth" file.
                activate_virtualenv(self._virtualenv())

    def _build(self):
        self._log(f"Creating the 'mach' site at {self._virtualenv_root}")
        if self._site_packages_source != SitePackagesSource.VENV:
            # The Mach virtualenv doesn't have a physical virtualenv on-disk if it won't
            # be "pip install"-ing. So, there's no build work to do.
            return

        environment = self._virtualenv()
        _create_venv_with_pthfile(
            environment,
            self._pthfile_lines(environment),
            True,
            self._requirements,
            self._metadata,
        )

    def _sys_path(self):
        if self._site_packages_source == SitePackagesSource.SYSTEM:
            stdlib_paths, system_site_paths = self._metadata.original_python.sys_path()
            return [
                *stdlib_paths,
                *self._requirements.pths_as_absolute(self._topsrcdir),
                *system_site_paths,
                *self._requirements.pths_fallback_as_absolute(self._topsrcdir),
            ]
        elif self._site_packages_source == SitePackagesSource.NONE:
            stdlib_paths = self._metadata.original_python.sys_path_stdlib()
            return [
                *stdlib_paths,
                *self._requirements.pths_as_absolute(self._topsrcdir),
            ]
        elif self._site_packages_source == SitePackagesSource.VENV:
            stdlib_paths = self._metadata.original_python.sys_path_stdlib()
            return [
                *stdlib_paths,
                # self._requirements will be added as part of the virtualenv activation.
            ]

    def _pthfile_lines(self, environment):
        return [
            # Prioritize vendored and first-party modules first.
            *self._requirements.pths_as_absolute(self._topsrcdir),
            # Then, include the virtualenv's site-packages.
            *_deprioritize_venv_packages(
                environment, self._site_packages_source == SitePackagesSource.VENV
            ),
        ]

    def _virtualenv(self):
        assert self._site_packages_source == SitePackagesSource.VENV
        return PythonVirtualenv(self._metadata.prefix, self._quiet)


class CommandSiteManager:
    """Activate sites and ad-hoc-install pip packages

    Provides tools to ensure that a command's scope will have expected, compatible
    packages. Manages prioritization of the import scope, and ensures consistency
    regardless of how a virtualenv is used (whether via in-process activation, or when
    used standalone to invoke a script).

    A few notes:

    * The command environment always inherits Mach's import scope. This is
      because "unloading" packages in Python is error-prone, so in-process activations
      will always carry Mach's dependencies along with it. Accordingly, compatibility
      between each command environment and the Mach environment must be maintained

    * Unlike the Mach environment, command environments *always* have an associated
      physical virtualenv on-disk. This is because some commands invoke child Python
      processes, and that child process should have the same import scope.

    """

    def __init__(
        self,
        topsrcdir: str,
        mach_virtualenv_root: Optional[str],
        virtualenv_root: str,
        site_name: str,
        active_metadata: MozSiteMetadata,
        populate_virtualenv: bool,
        requirements: MachEnvRequirements,
        quiet: bool = False,
    ):
        """
        Args:
            topsrcdir: The path to the Firefox repo
            mach_virtualenv_root: The path to the Mach virtualenv, if any
            virtualenv_root: The path to the virtualenv associated with this site
            site_name: The name of this site, such as "build"
            active_metadata: The currently-active moz-managed site
            populate_virtualenv: True if packages should be installed to the on-disk
                virtualenv with "pip". False if the virtualenv should only include
                sys.path modifications, and all 3rd-party packages should be imported from
                Mach's site packages source.
            requirements: The requirements associated with this site, parsed from
                the file at python/sites/<site_name>.txt
        """
        self._topsrcdir = topsrcdir
        self._mach_virtualenv_root = mach_virtualenv_root
        self.virtualenv_root = virtualenv_root
        self._site_name = site_name
        self._quiet = quiet
        self._virtualenv = PythonVirtualenv(self.virtualenv_root, self._quiet)
        self.python_path = self._virtualenv.python_path
        self.bin_path = self._virtualenv.bin_path
        self._populate_virtualenv = populate_virtualenv
        self._mach_site_packages_source = active_metadata.mach_site_packages_source
        self._requirements = requirements
        self._metadata = MozSiteMetadata(
            sys.hexversion,
            site_name,
            active_metadata.mach_site_packages_source,
            active_metadata.original_python,
            virtualenv_root,
        )

    def _log(self, message: str):
        if not self._quiet:
            # Ideally we would write to stderr here, but mozharness
            # has tests that fail if there is any output to stderr.
            # So until that changes, this will have to do.
            print(message)

    @classmethod
    def from_environment(
        cls,
        topsrcdir: str,
        get_state_dir: Callable[[], Optional[str]],
        site_name: str,
        command_virtualenvs_dir: str,
        quiet: bool = False,
    ):
        """
        Args:
            topsrcdir: The path to the Firefox repo
            get_state_dir: A function that resolves the path to the checkout-scoped
                state_dir, generally ~/.mozbuild/srcdirs/<checkout-based-dir>/
            site_name: The name of this site, such as "build"
            command_virtualenvs_dir: The location under which this site's virtualenv
            should be created
        """
        active_metadata = MozSiteMetadata.from_runtime()
        assert active_metadata, (
            "A Mach-managed site must be active before doing work with command sites"
        )

        mach_site_packages_source = active_metadata.mach_site_packages_source
        pip_restricted_site = site_name in PIP_NETWORK_INSTALL_RESTRICTED_VIRTUALENVS
        if (
            not pip_restricted_site
            and mach_site_packages_source == SitePackagesSource.SYSTEM
        ):
            # Sites that aren't pip-network-install-restricted are likely going to be
            # incompatible with the system. Besides, this use case shouldn't exist, since
            # using the system packages is supposed to only be needed to lower risk of
            # important processes like building Firefox.
            raise Exception(
                'Cannot use MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE="system" for any '
                f"sites other than {PIP_NETWORK_INSTALL_RESTRICTED_VIRTUALENVS}. The "
                f'current attempted site is "{site_name}".'
            )

        mach_virtualenv_root = (
            _mach_virtualenv_root(get_state_dir())
            if mach_site_packages_source == SitePackagesSource.VENV
            else None
        )
        populate_virtualenv = (
            mach_site_packages_source == SitePackagesSource.VENV
            or not pip_restricted_site
        )
        return cls(
            topsrcdir,
            mach_virtualenv_root,
            os.path.join(command_virtualenvs_dir, site_name),
            site_name,
            active_metadata,
            populate_virtualenv,
            resolve_requirements(topsrcdir, site_name),
            quiet,
        )

    def ensure(self):
        """Ensure that this virtualenv is built, up-to-date, and ready for use
        If using a virtualenv Python binary directly, it's useful to call this function
        first to ensure that the virtualenv doesn't have obsolete references or packages.
        """
        lock_file = Path(self.virtualenv_root).with_suffix(".lock")
        timeout = 60

        # In the scenario where multiple processes use the same site that does not yet exist,
        # they will trample each other when attempting to create it. To resolve this, we use
        # a file lock. The first process to reach the lock will create it and ensure it is up
        # to date, while the other(s) wait(s). Once the first releases the lock, the others
        # will continue one-by-one and determine it's up-to-date.
        try:
            with FileLock(lock_file, timeout=timeout):
                result = self._up_to_date()
                if not result.is_up_to_date:
                    active_site = MozSiteMetadata.from_runtime()
                    if active_site.site_name == self._site_name:
                        print(result.reason, file=sys.stderr)
                        raise Exception(
                            f'The "{self._site_name}" site is out-of-date, even though it has '
                            f"already been activated. Was it modified while this Mach process "
                            f"was running?"
                        )
                    self._log(
                        f"Creating the '{self._site_name}' site at {self.virtualenv_root}"
                    )
                    with resource_marker("Mach", f"create {self._site_name} site"):
                        _create_venv_with_pthfile(
                            self._virtualenv,
                            self._pthfile_lines(),
                            self._populate_virtualenv,
                            self._requirements,
                            self._metadata,
                        )
        except Timeout:
            raise SiteLockTimeout(lock_file, self._site_name, timeout)

    def activate(self):
        """Activate this site in the current Python context.

        If you run a random Python script and wish to "activate" the
        site, you can simply instantiate an instance of this class
        and call .activate() to make the virtualenv active.
        """
        active_site = MozSiteMetadata.from_runtime()
        site_is_already_active = active_site.site_name == self._site_name
        if (
            active_site.site_name not in ("mach", "common")
            and not site_is_already_active
        ):
            raise Exception(
                f'Activating from one command site ("{active_site.site_name}") to '
                f'another ("{self._site_name}") is not allowed, because they may '
                "be incompatible."
            )

        self.ensure()

        if site_is_already_active:
            return

        with self._metadata.update_current_site(self._virtualenv.python_path):
            activate_virtualenv(self._virtualenv)

    def install_pip_package(self, package):
        """Install a package via pip.

        The supplied package is specified using a pip requirement specifier.
        e.g. 'foo' or 'foo==1.0'.

        If the package is already installed, this is a no-op.
        """
        if Path(sys.prefix) == Path(self.virtualenv_root):
            # If we're already running in this interpreter, we can optimize in
            # the case that the package requirement is already satisfied.
            from pip._internal.req.constructors import install_req_from_line

            req = install_req_from_line(package)
            req.check_if_exists(use_user_site=False)
            if req.satisfied_by is not None:
                return

        self._virtualenv.pip_install_with_constraints([package])

    def install_pip_requirements(self, path, require_hashes=True, quiet=False):
        """Install a pip requirements.txt file.

        The supplied path is a text file containing pip requirement
        specifiers.

        If require_hashes is True, each specifier must contain the
        expected hash of the downloaded package. See:
        https://pip.pypa.io/en/stable/reference/pip_install/#hash-checking-mode
        """
        self._log(f"Installing pip requirements to the '{self._site_name}' site.")

        if not os.path.isabs(path):
            path = os.path.join(self._topsrcdir, path)

        args = ["--requirement", path]

        if require_hashes:
            args.append("--require-hashes")

        try:
            install_result = self._virtualenv.pip_install(args)
        except subprocess.CalledProcessError:
            raise InstallPipRequirementsException(
                f'Failed to install "{path}" into the "{self._site_name}" site.'
            )

        check_errors: str = "\n"  # save output when check fails
        check_result = subprocess.run(
            pip_command(python_executable=self.python_path, subcommand="check"),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

        if not check_result.returncode:
            return
        else:
            check_errors += "\n" + check_result.stdout

        """
        Some commands may use the "setup.py" script of first-party modules. This causes
        a "*.egg-info" dir to be created for that module (which pip can then detect as
        a package). Since we add all first-party module directories to the .pthfile for
        the "mach" venv, these first-party modules are then detected by all venvs after
        they are created. The problem is that these .egg-info directories can become
        stale (since if the first-party module is updated it's not guaranteed that the
        command that runs the "setup.py" was ran afterwards). This can cause
        incompatibilities with the pip check (since the dependencies can change between
        different versions).

        These .egg-info dirs are in our VCS ignore lists (eg: ".hgignore") because they
        are necessary to run some commands, so we don't want to always purge them, and we
        also don't want to accidentally commit them. Given this, we can leverage our VCS
        to find all the current first-party .egg-info dirs.

        If we're in the case where 'pip check' fails, then we can try purging the
        first-party .egg-info dirs, then run the 'pip check' again afterwards. If it's
        still failing, then we know the .egg-info dirs weren't the problem. If that's
        the case we can just raise the error encountered, which is the same as before.
        """

        def _delete_ignored_egg_info_dirs():
            from pathlib import Path

            from mozversioncontrol import (
                MissingConfigureInfo,
                MissingVCSInfo,
                get_repository_from_env,
            )

            try:
                with get_repository_from_env() as repo:
                    ignored_file_finder = repo.get_ignored_files_finder().find(
                        "**/*.egg-info"
                    )

                    unique_egg_info_dirs = {
                        Path(found[0]).parent for found in ignored_file_finder
                    }

                    for egg_info_dir in unique_egg_info_dirs:
                        shutil.rmtree(egg_info_dir)

            except (MissingVCSInfo, MissingConfigureInfo):
                pass

        _delete_ignored_egg_info_dirs()

        check_result = subprocess.run(
            pip_command(python_executable=self.python_path, subcommand="check"),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

        if check_result.returncode:
            if check_result.stdout not in check_errors:
                check_errors += "\n" + check_result.stdout
            if quiet:
                # If "quiet" was specified, then the "pip install" output wasn't printed
                # earlier, and was buffered instead. Print that buffer so that debugging
                # the "pip check" failure is easier.
                print(install_result.stdout)

            result = subprocess.run(
                pip_command(
                    python_executable=self.python_path, subcommand="list", args=["-v"]
                ),
                stdout=subprocess.PIPE,
                text=True,
                check=True,
            )
            print(result.stdout, file=sys.stderr)
            raise InstallPipRequirementsException(
                f'As part of validation after installing "{path}" into the '
                f'"{self._site_name}" site, the site appears to contain installed '
                "packages that are incompatible with each other." + check_errors
            )

    def _pthfile_lines(self):
        """Generate the prioritized import scope to encode in the venv's pthfile

        The import priority looks like this:

        1. Mach's vendored/first-party modules
        2. Mach's site-package source (the Mach virtualenv, the system Python, or neither)
        3. The command's vendored/first-party modules
        4. The command's site-package source (either the virtualenv or the system Python,
           if it's not already added)

        Note that, when using the system Python, it may either be prioritized before or
        after the command's vendored/first-party modules. This is a symptom of us
        attempting to avoid conflicting with the system packages.

        For example, there's at least one job in CI that operates with an ancient
        environment with a bunch of old packages, many of whom conflict with our vendored
        packages. However, the specific command that we're running for the job doesn't
        need any of the system's packages, so we're safe to insulate ourselves.

        Mach doesn't know the command being run when it's preparing its import scope,
        so it has to be defensive. Therefore:

        1. If Mach needs a system package: system packages are higher priority.
        2. If Mach doesn't need a system package, but the current command does: system
           packages are still be in the list, albeit at a lower priority.
        """

        # Prioritize Mach's vendored and first-party modules first.
        lines = resolve_requirements(self._topsrcdir, "mach").pths_as_absolute(
            self._topsrcdir
        )
        mach_site_packages_source = self._mach_site_packages_source
        if mach_site_packages_source == SitePackagesSource.SYSTEM:
            # When Mach is using the system environment, add it next.
            _, system_site_paths = self._metadata.original_python.sys_path()
            lines.extend(system_site_paths)
        elif mach_site_packages_source == SitePackagesSource.VENV:
            # When Mach is using its on-disk virtualenv, add its site-packages directory.
            assert self._mach_virtualenv_root
            lines.extend(
                PythonVirtualenv(self._mach_virtualenv_root).site_packages_dirs()
            )

        # Add this command's vendored and first-party modules.
        lines.extend(self._requirements.pths_as_absolute(self._topsrcdir))
        # Finally, ensure that pip-installed packages are the lowest-priority
        # source to import from.
        lines.extend(
            _deprioritize_venv_packages(self._virtualenv, self._populate_virtualenv)
        )
        # Except for fallback that should come after the pip installed package.
        lines.extend(
            resolve_requirements(self._topsrcdir, "mach").pths_fallback_as_absolute(
                self._topsrcdir
            )
        )

        # Note that an on-disk virtualenv is always created for commands, even if they
        # are using the system as their site-packages source. This is to support use
        # cases where a fresh Python process must be created, but it also must have
        # access to <site>'s 1st- and 3rd-party packages.
        return lines

    def _up_to_date(self):
        pthfile_lines = self._pthfile_lines()
        if self._mach_site_packages_source == SitePackagesSource.SYSTEM:
            _assert_pip_check(
                pthfile_lines,
                self._site_name,
                self._requirements if not self._populate_virtualenv else None,
            )

        return _is_venv_up_to_date(
            self._virtualenv,
            pthfile_lines,
            self._requirements,
            self._metadata,
            self._topsrcdir,
        )


class PythonVirtualenv:
    """Calculates paths of interest for general python virtual environments"""

    def __init__(self, prefix, quiet=False):
        self.prefix = os.path.realpath(prefix)
        self.paths = self._get_sysconfig_paths(self.prefix)

        # Name of the Python executable to use in virtual environments.
        # An executable with the same name as sys.executable might not exist in
        # virtual environments. An executable with 'python' as the steam —
        # without version numbers or ABI flags — will always be present in
        # virtual environments, so we use that.
        python_exe_name = "python" + sysconfig.get_config_var("EXE")

        self.bin_path = self.paths["scripts"]
        self.python_path = os.path.join(self.bin_path, python_exe_name)
        self._quiet = quiet

    @staticmethod
    def _get_sysconfig_paths(prefix):
        """Calculate the sysconfig paths of a virtual environment in the given prefix.

        The virtual environment MUST be using the same Python distribution as us.
        """
        # Determine the sysconfig scheme used in virtual environments
        if "venv" in sysconfig.get_scheme_names():
            # A 'venv' scheme was added in Python 3.11 to allow users to
            # calculate the paths for a virtual environment, since the default
            # scheme may not always be the same as used on virtual environments.
            # Some common examples are the system Python distributed by macOS,
            # Debian, and Fedora.
            # For more information, see https://github.com/python/cpython/issues/89576
            venv_scheme = "venv"
        elif os.name == "nt":
            # We know that before the 'venv' scheme was added, on Windows,
            # the 'nt' scheme was used in virtual environments.
            venv_scheme = "nt"
        elif os.name == "posix":
            # We know that before the 'venv' scheme was added, on POSIX,
            # the 'posix_prefix' scheme was used in virtual environments.
            venv_scheme = "posix_prefix"
        else:
            # This should never happen with upstream Python, as the 'venv'
            # scheme should always be available on >=3.11, and no other
            # platforms are supported by the upstream on older Python versions.
            #
            # Since the 'venv' scheme isn't available, and we have no knowledge
            # of this platform/distribution, fallback to the default scheme.
            #
            # Hitting this will likely be the result of running a custom Python
            # distribution targetting a platform that is not supported by the
            # upstream.
            # In this case, unless the Python vendor patched the Python
            # distribution in such a way as the default scheme may not always be
            # the same scheme, using the default scheme should be correct.
            # If the vendor did patch Python as such, to work around this issue,
            # I would recommend them to define a 'venv' scheme that matches
            # the layout used on virtual environments in their Python distribution.
            # (rec. signed Filipe Laíns — upstream sysconfig maintainer)
            venv_scheme = sysconfig.get_default_scheme()
            warnings.warn(
                f"Unknown platform '{os.name}', using the default install scheme '{venv_scheme}'. "
                "If this is incorrect, please ask your Python vendor to add a 'venv' sysconfig scheme "
                "(see https://github.com/python/cpython/issues/89576, or check the code comment).",
                stacklevel=2,
            )
        # Build the sysconfig config_vars dictionary for the virtual environment.
        venv_vars = sysconfig.get_config_vars().copy()
        venv_vars["base"] = venv_vars["platbase"] = prefix
        # Get sysconfig paths for the virtual environment.
        return sysconfig.get_paths(venv_scheme, vars=venv_vars)

    def resolve_sysconfig_packages_path(self, sysconfig_path):
        return self.paths[sysconfig_path]

    def site_packages_dirs(self):
        dirs = []
        if sys.platform.startswith("win"):
            dirs.append(os.path.normpath(os.path.normcase(self.prefix)))
        purelib = self.resolve_sysconfig_packages_path("purelib")
        platlib = self.resolve_sysconfig_packages_path("platlib")

        dirs.append(purelib)
        if platlib != purelib:
            dirs.append(platlib)

        return dirs

    def pip_install_with_constraints(self, pip_args, excludes=()):
        """Create a pip constraints file or existing packages

        When pip installing an incompatible package, pip will follow through with
        the install but raise a warning afterwards.

        To defend our environment from breakage, we run "pip install" but add all
        existing packages to a "constraints file". This ensures that conflicts are
        raised as errors up-front, and the virtual environment doesn't have conflicting
        packages installed.

        Note: pip_args is expected to contain either the requested package or
              requirements file.
        """
        existing_packages = {
            name: version
            for name, version in self._resolve_installed_packages().items()
            if name not in excludes
        }

        if existing_packages:
            with tempfile.TemporaryDirectory() as tempdir:
                constraints_path = os.path.join(tempdir, "site-constraints.txt")
                with open(constraints_path, "w") as file:
                    file.write(
                        "\n".join([
                            f"{name}=={version}"
                            for name, version in existing_packages.items()
                        ])
                    )

                self.pip_install(["--constraint", constraints_path] + pip_args)
        else:
            self.pip_install(pip_args)

    def pip_install(self, pip_install_args, **kwargs):
        # setuptools will use the architecture of the running Python instance when
        # building packages. However, it's possible for the Xcode Python to be a universal
        # binary (x86_64 and arm64) without the associated macOS SDK supporting arm64,
        # thereby causing a build failure. To avoid this, we explicitly influence the
        # build to only target a single architecture - our current architecture.
        kwargs.setdefault("env", os.environ.copy()).setdefault(
            "ARCHFLAGS", f"-arch {platform.machine()}"
        )
        kwargs.setdefault("check", True)
        kwargs.setdefault("stdout", None if show_pip_output() else subprocess.PIPE)
        kwargs.setdefault("stderr", None if show_pip_output() else subprocess.PIPE)
        kwargs.setdefault("text", True)
        kwargs.setdefault("encoding", "utf-8")

        # It's tempting to call pip natively via pip.main(). However,
        # the current Python interpreter may not be the virtualenv python.
        # This will confuse pip and cause the package to attempt to install
        # against the executing interpreter. By creating a new process, we
        # force the virtualenv's interpreter to be used and all is well.
        # It /might/ be possible to cheat and set sys.executable to
        # self.python_path. However, this seems more risk than it's worth.

        try:
            with resource_marker(
                "uv pip install" if use_uv() else "pip install",
                " ".join(pip_install_args),
            ):
                install_result = subprocess.run(  # noqa PLW1510
                    pip_command(
                        python_executable=self.python_path,
                        subcommand="install",
                        args=pip_install_args,
                    ),
                    check=kwargs.pop("check", True),
                    **kwargs,
                )
        except subprocess.CalledProcessError as cpe:
            if not self._quiet:
                # We print the stdout/stderr on a failed install here so that we don't
                # need to do it for every code path. We still raise the CalledProcessError
                # afterward so that the different paths can do their own handling.
                if cpe.stdout:
                    print(cpe.stdout)
                if cpe.stderr:
                    print(cpe.stderr, file=sys.stderr)
            raise cpe

        # On one code path we do a 'pip check', and if that fails, having the stdout
        # of the 'pip install' is helpful for debugging, so we pass it along here so
        # that we can print later if we hit that scenario.
        return install_result

    def install_optional_packages(self, optional_requirements):
        for requirement in optional_requirements:
            try:
                self.pip_install_with_constraints([str(requirement.requirement)])
            except subprocess.CalledProcessError:
                if not self._quiet:
                    print(
                        f"Could not install {requirement.requirement.name}, so "
                        f"{requirement.repercussion}. Continuing."
                    )

    def _resolve_installed_packages(self):
        return _resolve_installed_packages(self.python_path)


class RequirementsValidationResult:
    def __init__(self):
        self._package_discrepancies = []
        self.has_all_packages = True
        self.provides_any_package = False

    def add_discrepancy(self, requirement, found):
        self._package_discrepancies.append((requirement, found))
        self.has_all_packages = False

    def report(self):
        lines = []
        for requirement, found in self._package_discrepancies:
            if found:
                error = f'Installed with unexpected version "{found}"'
            else:
                error = "Not installed"
            lines.append(f"{requirement}: {error}")
        return "\n".join(lines)

    @classmethod
    def from_packages(cls, packages, requirements):
        result = cls()
        for pkg in requirements.pypi_requirements:
            installed_version = packages.get(canonicalize_name(pkg.requirement.name))
            if not installed_version or not pkg.requirement.specifier.contains(
                installed_version
            ):
                result.add_discrepancy(pkg.requirement, installed_version)
            elif installed_version:
                result.provides_any_package = True

        for pkg in requirements.pypi_optional_requirements:
            installed_version = packages.get(canonicalize_name(pkg.requirement.name))
            if installed_version and not pkg.requirement.specifier.contains(
                installed_version
            ):
                result.add_discrepancy(pkg.requirement, installed_version)
            elif installed_version:
                result.provides_any_package = True

        return result


class ExternalPythonSite:
    """Represents the Python site that is executing Mach

    The external Python site could be a virtualenv (created by venv or virtualenv) or
    the system Python itself, so we can't make any significant assumptions on its
    structure.
    """

    def __init__(self, python_executable):
        self._prefix = os.path.dirname(os.path.dirname(python_executable))
        self.python_path = python_executable

    @functools.cache
    def sys_path(self):
        """Return lists of sys.path entries: one for standard library, one for the site

        These two lists are calculated at the same time so that we can interpret them
        in a single Python subprocess, as running a whole Python instance is
        very expensive in the context of Mach initialization.
        """
        env = {
            k: v
            for k, v in os.environ.items()
            # Don't include items injected by IDEs into the system path.
            if k not in ("PYTHONPATH", "PYDEVD_LOAD_VALUES_ASYNC")
        }
        stdlib = subprocess.Popen(
            [
                self.python_path,
                # Don't "import site" right away, so we can split the standard library
                # paths from the site paths.
                "-S",
                "-c",
                "import sys;"
                # Skip the first item in the sys.path, as it's the working directory
                # of the invoked script (so, in this case, "").
                # Use list(dict(...)) to de-dupe items, such as when using
                # pyenv on Linux.
                "print(list(dict.fromkeys(sys.path[1:])))",
            ],
            universal_newlines=True,
            env=env,
            stdout=subprocess.PIPE,
        )
        system = subprocess.Popen(
            [
                self.python_path,
                "-c",
                "import os; import sys; import site; "
                "packages = site.getsitepackages(); "
                # Only add the "user site packages" if not in a virtualenv (which is
                # identified by the prefix == base_prefix check
                "packages.insert(0, site.getusersitepackages()) if "
                "    sys.prefix == sys.base_prefix else None; "
                # When a Python instance launches, it only adds each
                # "site.getsitepackages()" entry if it exists on the file system.
                # Replicate that behaviour to get a more accurate list of system paths.
                "packages = [p for p in packages if os.path.exists(p)]; "
                "print(packages)",
            ],
            universal_newlines=True,
            env=env,
            stdout=subprocess.PIPE,
        )
        # Run python processes in parallel - they take roughly the same time, so this
        # cuts this functions run time in half.
        stdlib_out, _ = stdlib.communicate()
        system_out, _ = system.communicate()
        assert stdlib.returncode == 0
        assert system.returncode == 0
        stdlib = ast.literal_eval(stdlib_out)
        system = ast.literal_eval(system_out)
        # On Windows, some paths are both part of the default sys.path *and* are included
        # in the "site packages" list. Keep the "stdlib" one, and remove the dupe from
        # the "system packages" list.
        system = [path for path in system if path not in stdlib]
        return stdlib, system

    def sys_path_stdlib(self):
        """Return list of default sys.path entries for the standard library"""
        stdlib, _ = self.sys_path()
        return stdlib


@functools.cache
def resolve_requirements(topsrcdir, site_name):
    thunderbird_dir = Path(topsrcdir, "comm")
    is_thunderbird = thunderbird_dir.exists() and any(thunderbird_dir.iterdir())
    prefixes = [Path(topsrcdir)]
    if is_thunderbird:
        prefixes.insert(0, thunderbird_dir)

    manifest_suffix = Path("python", "sites", f"{site_name}.txt")
    manifest_path = next(
        (
            prefix / manifest_suffix
            for prefix in prefixes
            if (prefix / manifest_suffix).exists()
        ),
        None,
    )

    if manifest_path is None:
        raise Exception(
            f'The current command is using the "{site_name}" '
            "site. However, that site is missing its associated "
            f'requirements definition file at "{manifest_path}".'
        )

    try:
        requirements = MachEnvRequirements.from_requirements_definition(
            topsrcdir,
            is_thunderbird,
            site_name not in PIP_NETWORK_INSTALL_RESTRICTED_VIRTUALENVS,
            manifest_path,
        )
    except UnexpectedFlexibleRequirementException as e:
        raise Exception(
            f'The "{site_name}" site does not have all pypi packages pinned '
            f'in the format "package==version" (found "{e.raw_requirement}").\n'
            f"Only the {PIP_NETWORK_INSTALL_RESTRICTED_VIRTUALENVS} sites are "
            "allowed to have unpinned packages."
        )

    current_version = (
        f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}"
    )
    specifier = SpecifierSet(requirements.requires_python)
    if current_version not in specifier:
        site_name = manifest_path.stem
        print(
            f"The current Python version ({current_version}) does not meet the required "
            f"version specification ({requirements.requires_python}) for the '{site_name}' site "
            f"({manifest_path}).\n\n"
            f"You will need to switch to a compatible Python version to run this command.\n\n"
            f"To manage multiple versions of Python, we recommend a tool called 'uv': "
            f"https://docs.astral.sh/uv/guides/install-python/"
        )

        sys.exit(1)

    return requirements


def _resolve_installed_packages(python_executable):
    result = subprocess.run(
        [
            python_executable,
            "-c",
            textwrap.dedent(
                """
                import importlib.metadata, json
                print(json.dumps([
                    [d.metadata["Name"], d.version]
                    for d in importlib.metadata.distributions()
                    if d.metadata["Name"]
                ]))
                """
            ),
        ],
        text=True,
        capture_output=True,
        check=True,
    )

    packages = {}
    for name, version in json.loads(result.stdout):
        # Keep the first copy found on sys.path, since that is the one Python imports.
        packages.setdefault(canonicalize_name(name), version)
    return packages


def _ensure_python_exe(python_exe_root: Path):
    """On some machines in CI venv does not behave consistently. Sometimes
    only a "python3" executable is created, but we expect "python". Since
    they are functionally identical, we can just copy "python3" to "python"
    (and vice-versa) to solve the problem.
    """
    python3_exe_path = python_exe_root / "python3"
    python_exe_path = python_exe_root / "python"

    if _is_windows:
        python3_exe_path = python3_exe_path.with_suffix(".exe")
        python_exe_path = python_exe_path.with_suffix(".exe")

    if python3_exe_path.exists() and not python_exe_path.exists():
        shutil.copy(str(python3_exe_path), str(python_exe_path))

    if python_exe_path.exists() and not python3_exe_path.exists():
        shutil.copy(str(python_exe_path), str(python3_exe_path))

    if not python_exe_path.exists() and not python3_exe_path.exists():
        raise Exception(
            f'Neither a "{python_exe_path.name}" or "{python3_exe_path.name}" '
            f"were found. This means something unexpected happened during the "
            f"virtual environment creation and we cannot proceed."
        )


def _assert_pip_check(pthfile_lines, virtualenv_name, requirements):
    """Check if the provided pthfile lines have a package incompatibility

    If there's an incompatibility, raise an exception and allow it to bubble up since
    it will require user intervention to resolve.

    If requirements aren't provided (such as when Mach is using SYSTEM, but the command
    site is using VENV), then skip the "pthfile satisfies requirements" step.
    """
    if os.environ.get(
        f"MACH_SYSTEM_ASSERTED_COMPATIBLE_WITH_{virtualenv_name.upper()}_SITE", None
    ):
        # Don't re-assert compatibility against the system python within Mach subshells.
        return

    print(
        'Running "pip check" to verify compatibility between the system Python and the '
        f'"{virtualenv_name}" site.'
    )

    with tempfile.TemporaryDirectory() as check_env_path:
        # Pip detects packages on the "sys.path" that have a ".dist-info" or
        # a ".egg-info" directory. The majority of our Python dependencies are
        # vendored as extracted wheels or sdists, so they are automatically picked up.
        # This gives us sufficient confidence to do a `pip check` with both vendored
        # packages + system packages in scope, and trust the results.
        # Note: rather than just running the system pip with a modified "sys.path",
        # we create a new virtualenv that has our pinned pip version, so that
        # we get consistent results (there's been lots of pip resolver behaviour
        # changes recently).
        process = subprocess.run(
            [sys.executable, "-m", "venv", "--without-pip", check_env_path],
            check=False,
            capture_output=True,
            encoding="UTF-8",
        )

        if process.returncode != 0:
            if "No module named venv" in process.stderr:
                raise VenvModuleNotFoundException()
            else:
                raise subprocess.CalledProcessError(
                    process.returncode,
                    process.args,
                    output=process.stdout,
                    stderr=process.stderr,
                )

        if process.stdout:
            print(process.stdout)

        check_env = PythonVirtualenv(check_env_path)
        _ensure_python_exe(Path(check_env.python_path).parent)

        with open(
            os.path.join(
                os.path.join(check_env.resolve_sysconfig_packages_path("platlib")),
                PTH_FILENAME,
            ),
            "w",
        ) as f:
            f.write("\n".join(pthfile_lines))

        pip = pip_command(python_executable=check_env.python_path)
        if requirements:
            packages = _resolve_installed_packages(check_env.python_path)
            validation_result = RequirementsValidationResult.from_packages(
                packages, requirements
            )
            if not validation_result.has_all_packages:
                subprocess.check_call(pip + ["list", "-v"], stdout=sys.stderr)
                print(validation_result.report(), file=sys.stderr)
                raise Exception(
                    f'The "{virtualenv_name}" site is not compatible with the installed '
                    "system Python packages."
                )

        check_result = subprocess.run(
            pip + ["check"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if check_result.returncode:
            subprocess.check_call(pip + ["list", "-v"], stdout=sys.stderr)
            print(check_result.stdout, file=sys.stderr)
            raise Exception(
                'According to "pip check", the current Python '
                "environment has package-compatibility issues."
            )

        os.environ[
            f"MACH_SYSTEM_ASSERTED_COMPATIBLE_WITH_{virtualenv_name.upper()}_SITE"
        ] = "1"


def _deprioritize_venv_packages(virtualenv, populate_virtualenv):
    # Virtualenvs implicitly add some "site packages" to the sys.path upon being
    # activated. However, Mach generally wants to prioritize the existing sys.path
    # (such as vendored packages) over packages installed to virtualenvs.
    # So, this function moves the virtualenv's site-packages to the bottom of the sys.path
    # at activation-time.

    return [
        line
        for site_packages_dir in virtualenv.site_packages_dirs()
        # repr(...) is needed to ensure Windows path backslashes aren't mistaken for
        # escape sequences.
        # Additionally, when removing the existing "site-packages" folder's entry, we have
        # to do it in a case-insensitive way because, on Windows:
        # * Python adds it as <venv>/lib/site-packages
        # * While sysconfig tells us it's <venv>/Lib/site-packages
        # * (note: on-disk, it's capitalized, so sysconfig is slightly more accurate).
        for line in filter(
            None,
            (
                "import sys; sys.path = [p for p in sys.path if "
                f"p.lower() != {repr(site_packages_dir)}.lower()]",
                (
                    f"import sys; sys.path.append({repr(site_packages_dir)})"
                    if populate_virtualenv
                    else None
                ),
            ),
        )
    ]


def _remove_virtualenv(virtualenv_root):
    if os.path.exists(virtualenv_root):
        # Moving first keeps the venv intact on failure. On Windows, loaded DLLs
        # block deleting the moved copy, but not moving it.
        old_dir = tempfile.mkdtemp(
            prefix=f"{os.path.basename(virtualenv_root)}.old-",
            dir=os.path.dirname(virtualenv_root),
        )
        os.rename(virtualenv_root, os.path.join(old_dir, "venv"))

    for old_dir in glob.glob(f"{glob.escape(virtualenv_root)}.old-*"):
        shutil.rmtree(old_dir, ignore_errors=True)


def _create_venv_with_pthfile(
    target_venv,
    pthfile_lines,
    populate_with_pip,
    requirements,
    metadata,
):
    virtualenv_root = target_venv.prefix
    _remove_virtualenv(virtualenv_root)

    os.makedirs(virtualenv_root)
    metadata.write(is_finalized=False)

    process = subprocess.run(
        [sys.executable, "-m", "venv", "--without-pip", virtualenv_root],
        check=False,
        capture_output=True,
        encoding="UTF-8",
    )

    if process.returncode != 0:
        # Clean up what we've made on failure so that we're not in an incomplete state
        shutil.rmtree(virtualenv_root)
        if "No module named venv" in process.stderr:
            raise VenvModuleNotFoundException()
        else:
            raise subprocess.CalledProcessError(
                process.returncode,
                process.args,
                output=process.stdout,
                stderr=process.stderr,
            )

    if process.stdout:
        print(process.stdout)

    _ensure_python_exe(Path(target_venv.python_path).parent)

    platlib_site_packages_dir = target_venv.resolve_sysconfig_packages_path("platlib")
    pthfile_contents = "\n".join(pthfile_lines)
    with open(os.path.join(platlib_site_packages_dir, PTH_FILENAME), "w") as f:
        f.write(pthfile_contents)

    # Since we now support 'uv pip', "VIRTUAL_ENV" needs to be set to where we
    # want it to install the packages into. With 'pip' we just use the venv's python
    # executable, which is relative to the venv. That's not possible with 'uv`, and it's
    # also not in the venv directory.
    # Prior to this, we would only set this env var when activating the venv, but now we
    # also need it set here (it still needs to be set in 'activate_virtualenv' as well
    # since we won't always take this code path).
    os.environ["VIRTUAL_ENV"] = virtualenv_root

    if populate_with_pip:
        for requirements_txt_file in requirements.requirements_txt_files:
            target_venv.pip_install([
                "--requirement",
                requirements_txt_file.path,
                "--require-hashes",
            ])
        if requirements.pypi_requirements:
            requirements_list = [
                str(req.requirement) for req in requirements.pypi_requirements
            ]
            target_venv.pip_install_with_constraints(
                requirements_list,
                excludes={
                    canonicalize_name(req.requirement.name)
                    for req in requirements.pypi_requirements
                },
            )
        target_venv.install_optional_packages(requirements.pypi_optional_requirements)

    metadata.write(is_finalized=True)


def _is_venv_up_to_date(
    target_venv,
    expected_pthfile_lines,
    requirements,
    expected_metadata,
    topsrcdir,
):
    if not os.path.exists(target_venv.prefix):
        return SiteUpToDateResult(False, f'"{target_venv.prefix}" does not exist')

    metadata_file = os.path.join(target_venv.prefix, METADATA_FILENAME)

    if not os.path.exists(metadata_file):
        return SiteUpToDateResult(
            False, f'"{METADATA_FILENAME}" does not exist for "{target_venv.prefix}".'
        )

    # Modifications to any of the requirements manifest files mean the virtualenv should be rebuilt:
    metadata_mtime = os.path.getmtime(metadata_file)
    dep_files = [
        *requirements.requirements_paths,
        __file__,
        os.path.join(os.path.dirname(__file__), "requirements.py"),
    ]
    uv_lock = os.path.join(topsrcdir, "third_party", "python", "uv.lock")
    if os.path.exists(uv_lock):
        dep_files.append(uv_lock)
    for dep_file in dep_files:
        if os.path.getmtime(dep_file) > metadata_mtime:
            return SiteUpToDateResult(
                False, f'"{dep_file}" has changed since the virtualenv was created'
            )

    for requirements_txt_file in requirements.requirements_txt_files:
        req_txt_path = requirements_txt_file.path
        if (
            os.path.exists(req_txt_path)
            and os.path.getmtime(req_txt_path) > metadata_mtime
        ):
            return SiteUpToDateResult(
                False, f'"{req_txt_path}" has changed since the virtualenv was created'
            )

    try:
        existing_metadata = MozSiteMetadata.from_path(target_venv.prefix)
    except MozSiteMetadataOutOfDateError as e:
        # The metadata is missing required fields, so must be out-of-date.
        return SiteUpToDateResult(False, str(e))

    if existing_metadata != expected_metadata:
        # The metadata doesn't exist or some fields have different values.
        return SiteUpToDateResult(
            False,
            f"The existing metadata on-disk ({vars(existing_metadata)}) does not match "
            f"the expected metadata ({vars(expected_metadata)}",
        )

    platlib_site_packages_dir = target_venv.resolve_sysconfig_packages_path("platlib")
    pthfile_path = os.path.join(platlib_site_packages_dir, PTH_FILENAME)
    try:
        with open(pthfile_path) as file:
            current_pthfile_contents = file.read().strip()
    except FileNotFoundError:
        return SiteUpToDateResult(False, f'No pthfile found at "{pthfile_path}"')

    expected_pthfile_contents = "\n".join(expected_pthfile_lines)
    if current_pthfile_contents != expected_pthfile_contents:
        return SiteUpToDateResult(
            False,
            f'The pthfile at "{pthfile_path}" does not match the expected value.\n'
            f"# --- on-disk pthfile: ---\n"
            f"{current_pthfile_contents}\n"
            f"# --- expected pthfile contents ---\n"
            f"{expected_pthfile_contents}\n"
            f"# ---",
        )

    return SiteUpToDateResult(True)


def activate_virtualenv(virtualenv: PythonVirtualenv):
    os.environ["PATH"] = os.pathsep.join(
        [virtualenv.bin_path] + os.environ.get("PATH", "").split(os.pathsep)
    )
    os.environ["VIRTUAL_ENV"] = virtualenv.prefix

    for path in virtualenv.site_packages_dirs():
        site.addsitedir(os.path.realpath(path))

    sys.prefix = virtualenv.prefix


def _mach_virtualenv_root(checkout_scoped_state_dir):
    workspace = os.environ.get("WORKSPACE")
    if os.environ.get("MOZ_AUTOMATION") and workspace:
        # In CI, put Mach virtualenv in the $WORKSPACE dir, which should be cleaned
        # between jobs.
        return os.path.join(workspace, "mach_virtualenv")
    return os.path.join(checkout_scoped_state_dir, "_virtualenvs", "mach")
