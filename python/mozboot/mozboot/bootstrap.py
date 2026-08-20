# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import platform
import re
import shutil
import subprocess
import sys
import time
from collections import OrderedDict
from pathlib import Path
from typing import Optional

# Use distro package to retrieve linux platform information
import distro
from mach.site import MachSiteManager
from mach.telemetry import initialize_telemetry_setting
from mach.util import (
    UserError,
    get_state_dir,
    to_optional_path,
)
from mozbuild.base import MozbuildObject
from mozfile import which
from mozversioncontrol import get_repository_object
from packaging.version import Version

from mozboot.aerynos import AerynOsBootstrapper
from mozboot.archlinux import ArchlinuxBootstrapper
from mozboot.base import MODERN_RUST_VERSION
from mozboot.centosfedora import CentOSFedoraBootstrapper
from mozboot.debian import DebianBootstrapper
from mozboot.freebsd import FreeBSDBootstrapper
from mozboot.gentoo import GentooBootstrapper
from mozboot.mozconfig import MozconfigBuilder
from mozboot.mozillabuild import MozillaBuildBootstrapper
from mozboot.openbsd import OpenBSDBootstrapper
from mozboot.opensuse import OpenSUSEBootstrapper
from mozboot.osx import OSXBootstrapper, OSXBootstrapperLight
from mozboot.solus import SolusBootstrapper
from mozboot.void import VoidBootstrapper
from mozboot.windows import WindowsBootstrapper

APPLICATION_CHOICE = """
Note on Artifact Mode:

Artifact builds download prebuilt C++ components rather than building
them locally. Artifact builds are faster!

Artifact builds are recommended for people working on Firefox or
Firefox for Android frontends, or the GeckoView Java API. They are unsuitable
for those working on C++ code. For more information see:
https://firefox-source-docs.mozilla.org/contributing/build/artifact_builds.html.

Please choose the version of Firefox you want to build (see note above):
%s
Your choice: """

APPLICATIONS = OrderedDict([
    ("Firefox for Desktop Artifact Mode", "browser_artifact_mode"),
    ("Firefox for Desktop", "browser"),
    ("GeckoView/Firefox for Android Artifact Mode", "mobile_android_artifact_mode"),
    ("GeckoView/Firefox for Android", "mobile_android"),
    ("SpiderMonkey JavaScript engine", "js"),
])

FINISHED = """
Your system should be ready to build %s!
"""

MOZCONFIG_SUGGESTION_TEMPLATE = """
Paste the lines between the chevrons (>>> and <<<) into
%s:

>>>
%s
<<<
"""

MOZCONFIG_MISMATCH_WARNING_TEMPLATE = """
WARNING! Mismatch detected between the selected build target and the
mozconfig file %s:

Current config
>>>
%s
<<<

Expected config
>>>
%s
<<<
"""

CONFIGURE_MERCURIAL = """
Mozilla recommends a number of changes to Mercurial to enhance your
experience with it.

Would you like to run a configuration wizard to ensure Mercurial is
optimally configured? (This will also ensure 'version-control-tools' is up-to-date)"""

CONFIGURE_GIT = """
Would you like to run a few configuration steps to ensure Git is
optimally configured?"""

CONFIGURE_SCCACHE = """
sccache allows speeding up subsequent builds by caching compilation
results. Note that the initial build will be slower, and local changes
that invalidate the cache will not benefit from it.

Would you like to enable sccache?"""


DEBIAN_DISTROS = (
    "debian",
    "ubuntu",
    "linuxmint",
    "elementary",
    "neon",
    "pop",
    "kali",
    "devuan",
    "pureos",
    "deepin",
    "tuxedo",
    "zorin",
)

FEDORA_DISTROS = (
    "centos",
    "fedora",
    "rocky",
    "nobara",
    "oracle",
    "fedora-asahi-remix",
    "ultramarine",
)


OLD_REVISION_WARNING = """
WARNING! You appear to be running `mach bootstrap` from an old revision.
bootstrap is meant primarily for getting developer environments up-to-date to
build the latest version of tree. Running bootstrap on old revisions may fail
and is not guaranteed to bring your machine to any working state in particular.
Proceed at your own peril.
"""


# Dev Drives were added in 22621.2338 and should be available in all subsequent versions
DEV_DRIVE_MINIMUM_VERSION = Version("10.0.22621.2338")
DEV_DRIVE_SUGGESTION = """
Mach has detected that the Firefox source repository ({}) is located on an {} drive.
Your current version of Windows ({}) supports ReFS drives (Dev Drive).

It has been shown that Firefox builds are 5-10% faster on
ReFS, it is recommended that you create an ReFS drive and move the Firefox
source repository to it before proceeding.

The instructions for how to do that can be found here: https://learn.microsoft.com/en-us/windows/dev-drive/

If you wish disregard this recommendation, you can hide this message by setting
'MACH_HIDE_DEV_DRIVE_SUGGESTION=1' in your environment variables (and restarting your shell)."""
DEV_DRIVE_DETECTION_ERROR = """
Error encountered while checking for Dev Drive.
 Reason: {} (skipping)
"""


def check_for_hgrc_state_dir_mismatch(state_dir):
    ignore_hgrc_state_dir_mismatch = os.environ.get(
        "MACH_IGNORE_HGRC_STATE_DIR_MISMATCH", ""
    )
    if ignore_hgrc_state_dir_mismatch:
        return

    import subprocess

    result = subprocess.run(
        ["hg", "config", "--source", "-T", "json"],
        check=False,
        capture_output=True,
        text=True,
    )

    if result.returncode:
        print("Failed to run 'hg config'. hg configuration checks will be skipped.")
        return

    from mozfile import json

    try:
        json_data = json.loads(result.stdout)
    except json.JSONDecodeError as e:
        print(
            f"Error parsing 'hg config' JSON: {e}\n\n"
            f"hg configuration checks will be skipped."
        )
        return

    mismatched_paths = []
    pattern = re.compile(r"(.*\.mozbuild)[\\/](.*)")
    for entry in json_data:
        if not entry["name"].startswith("extensions."):
            continue

        extension_path = entry["value"]
        match = pattern.search(extension_path)
        if match:
            extension = entry["name"]
            source_path = entry["source"]
            state_dir_from_hgrc = Path(match.group(1))
            extension_suffix = match.group(2)

            if state_dir != state_dir_from_hgrc.expanduser():
                expected_extension_path = state_dir / extension_suffix

                mismatched_paths.append(
                    f"Extension: '{extension}' found in config file '{source_path}'\n"
                    f" Current: {extension_path}\n"
                    f" Expected: {expected_extension_path}\n"
                )

    if mismatched_paths:
        hgrc_state_dir_mismatch_error_message = (
            f"Paths for extensions in your hgrc file appear to be referencing paths that are not in "
            f"the current '.mozbuild' state directory.\nYou may have set the `MOZBUILD_STATE_PATH` "
            f"environment variable and/or moved the `.mozbuild` directory. You should update the "
            f"paths for the following extensions manually to be inside '{state_dir}'\n"
            f"(If you instead wish to hide this error, set 'MACH_IGNORE_HGRC_STATE_DIR_MISMATCH=1' "
            f"in your environment variables and restart your shell before rerunning mach).\n\n"
            f"You can either use the command 'hg config --edit' to make changes to your hg "
            f"configuration or manually edit the 'config file' specified for each extension "
            f"below:\n\n"
        )
        hgrc_state_dir_mismatch_error_message += "".join(mismatched_paths)

        raise Exception(hgrc_state_dir_mismatch_error_message)


class Bootstrapper:
    """Main class that performs system bootstrap."""

    def __init__(
        self,
        choice=None,
        no_interactive=False,
        hg_configure=False,
        sccache_configure=False,
        no_system_changes=False,
        exclude=[],
        mach_context=None,
    ):
        self.instance = None
        self.choice = choice
        self.hg_configure = hg_configure
        self.sccache_configure = sccache_configure
        self.no_system_changes = no_system_changes
        self.exclude = exclude
        self.mach_context = mach_context
        cls = None
        args = {
            "no_interactive": no_interactive,
            "no_system_changes": no_system_changes,
        }

        if sys.platform.startswith("linux"):
            # distro package provides reliable ids for popular distributions so
            # we use those instead of the full distribution name
            dist_id = distro.id()
            version = distro.version()

            if dist_id in FEDORA_DISTROS:
                cls = CentOSFedoraBootstrapper
                args["distro"] = dist_id
            elif dist_id in DEBIAN_DISTROS:
                cls = DebianBootstrapper
                args["distro"] = dist_id
            elif dist_id in ("gentoo", "funtoo"):
                cls = GentooBootstrapper
            elif dist_id in ("solus"):
                cls = SolusBootstrapper
            elif dist_id in ("arch", "kaos") or Path("/etc/arch-release").exists():
                cls = ArchlinuxBootstrapper
            elif dist_id in ("aerynos"):
                cls = AerynOsBootstrapper
            elif dist_id in ("void"):
                cls = VoidBootstrapper
            elif dist_id in (
                "opensuse",
                "opensuse-leap",
                "opensuse-tumbleweed",
                "suse",
            ):
                cls = OpenSUSEBootstrapper
            else:
                raise NotImplementedError(
                    "Bootstrap support for this Linux "
                    "distro not yet available: " + dist_id
                )

            args["version"] = version
            args["dist_id"] = dist_id

        elif sys.platform.startswith("darwin"):
            # TODO Support Darwin platforms that aren't OS X.
            osx_version = platform.mac_ver()[0]
            if platform.machine() == "arm64" or _macos_is_running_under_rosetta():
                cls = OSXBootstrapperLight
            else:
                cls = OSXBootstrapper
            args["version"] = osx_version

        elif sys.platform.startswith("openbsd"):
            cls = OpenBSDBootstrapper
            args["version"] = platform.uname()[2]

        elif sys.platform.startswith(("dragonfly", "freebsd", "netbsd")):
            cls = FreeBSDBootstrapper
            args["version"] = platform.release()
            args["flavor"] = platform.system()

        elif sys.platform.startswith("win32") or sys.platform.startswith("msys"):
            if "MOZILLABUILD" in os.environ:
                cls = MozillaBuildBootstrapper
            else:
                cls = WindowsBootstrapper
        if cls is None:
            raise NotImplementedError(
                "Bootstrap support is not yet available for your OS."
            )

        self.instance = cls(**args)

    def maybe_install_private_packages_or_exit(
        self, application, checkout_type, mozconfig_builder
    ):
        # Install the clang packages needed for building the style system, as
        # well as the version of NodeJS that we currently support.
        # Also install the clang static-analysis package by default
        # The best place to install our packages is in the state directory
        # we have.  We should have created one above in non-interactive mode.
        self.instance.auto_bootstrap(application, self.exclude)
        self.instance.install_toolchain_artifact("fix-stacks")
        self.instance.install_toolchain_artifact("minidump-stackwalk")
        self.instance.install_toolchain_artifact("samply")
        self.instance.install_toolchain_artifact("profiler-node-tools")
        if not self.instance.artifact_mode:
            self._maybe_configure_sccache(mozconfig_builder)
            self.instance.install_toolchain_artifact("clang-tools/clang-tidy")
            self.instance.ensure_sccache_packages()
        # Like 'ensure_browser_packages' or 'ensure_mobile_android_packages'
        getattr(self.instance, "ensure_%s_packages" % application)()

    def _maybe_configure_sccache(self, mozconfig_builder):
        """Offer to enable sccache for non-artifact builds."""
        if self.instance.artifact_mode:
            return

        if not self.instance.no_interactive:
            should_configure = self.instance.prompt_yesno(prompt=CONFIGURE_SCCACHE)
        else:
            should_configure = self.sccache_configure

        if should_configure:
            mozconfig_builder.append("ac_add_options --with-ccache=sccache")

    def check_agentic_tools(self):
        if self.instance.no_interactive:
            return

        if not self.instance.cargo_tools_installed():
            if not self.instance.prompt_yesno(
                "Will you be using agentic coding tools to work on Firefox?"
            ):
                return

        self.instance.ensure_cargo_tools()

    def check_code_submission(self, checkout_root: Path):
        if self.instance.no_interactive or which("moz-phab"):
            return

        if not self.instance.prompt_yesno("Will you be submitting commits to Mozilla?"):
            return

        mach_binary = checkout_root / "mach"
        try:
            subprocess.check_call((
                sys.executable,
                str(mach_binary),
                "install-moz-phab",
            ))
        except subprocess.CalledProcessError as e:
            print(
                f"WARNING: './mach install-moz-phab' failed with exit code "
                f"{e.returncode}. You can retry with './mach install-moz-phab "
                f"--force'.",
                file=sys.stderr,
            )

    def bootstrap(self, settings):
        state_dir = Path(get_state_dir())

        hg = to_optional_path(which("hg"))
        hg_installed = bool(hg)

        if hg_installed:
            check_for_hgrc_state_dir_mismatch(state_dir)

        if self.choice is None:
            applications = APPLICATIONS
            # Like ['1. Firefox for Desktop', '2. Firefox for Android Artifact Mode', ...].
            labels = [
                "%s. %s" % (i, name) for i, name in enumerate(applications.keys(), 1)
            ]
            choices = [f"  {labels[0]} [default]"]
            choices += [f"  {label}" for label in labels[1:]]
            prompt = APPLICATION_CHOICE % "\n".join(choices)
            prompt_choice = self.instance.prompt_int(
                prompt=prompt, low=1, high=len(applications)
            )
            name, application = list(applications.items())[prompt_choice - 1]
        elif self.choice in APPLICATIONS.keys():
            name, application = self.choice, APPLICATIONS[self.choice]
        elif self.choice in APPLICATIONS.values():
            name, application = next(
                (k, v) for k, v in APPLICATIONS.items() if v == self.choice
            )
        else:
            raise Exception(
                "Please pick a valid application choice: (%s)"
                % "/".join(APPLICATIONS.keys())
            )

        mozconfig_builder = MozconfigBuilder()
        self.instance.application = application
        self.instance.artifact_mode = "artifact_mode" in application

        self.instance.warn_if_pythonpath_is_set()

        if sys.platform.startswith("darwin") and not os.environ.get(
            "MACH_I_DO_WANT_TO_USE_ROSETTA"
        ):
            # If running on arm64 mac, check whether we're running under
            # Rosetta and advise against it.
            if _macos_is_running_under_rosetta():
                print(
                    "Python is being emulated under Rosetta. Please use a native "
                    "Python instead. If you still really want to go ahead, set "
                    "the MACH_I_DO_WANT_TO_USE_ROSETTA environment variable.",
                    file=sys.stderr,
                )
                return 1

        self.instance.state_dir = state_dir

        # We need to enable the loading of hgrc in case extensions are
        # required to open the repo.
        (checkout_type, checkout_root) = current_firefox_checkout(
            env=self.instance._hg_cleanenv(load_hgrc=True),
            hg=hg,
        )
        repo = get_repository_object(checkout_root)
        self.instance.srcdir = checkout_root
        self.instance.validate_environment()
        self._populate_optional_packages(checkout_root)

        if sys.platform.startswith("win"):
            self._check_for_dev_drive(checkout_root)
            self._add_microsoft_defender_antivirus_exclusions(checkout_root, state_dir)

        if self.instance.no_system_changes:
            self.maybe_install_private_packages_or_exit(
                application, checkout_type, mozconfig_builder
            )
            self._output_mozconfig(application, mozconfig_builder)
            sys.exit(0)

        self.instance.install_system_packages()

        # Like 'install_browser_packages' or 'install_mobile_android_packages'.
        getattr(self.instance, "install_%s_packages" % application)(mozconfig_builder)

        if not self.instance.artifact_mode:
            self.instance.ensure_rust_modern()

        self.check_agentic_tools()

        git = to_optional_path(which("git"))

        # Possibly configure Mercurial, but not if the current checkout or repo
        # type is Git.
        if checkout_type == "hg":
            hg_installed, hg_modern = self.instance.ensure_mercurial_modern()

        if hg_installed and checkout_type == "hg":
            if not self.instance.no_interactive:
                configure_hg = self.instance.prompt_yesno(prompt=CONFIGURE_MERCURIAL)
            else:
                configure_hg = self.hg_configure

            if configure_hg:
                repo.configure(state_dir)

        # Offer to configure Git, if the current checkout or repo type is Git.
        elif git and checkout_type == "git":
            should_configure_git = False
            if not self.instance.no_interactive:
                should_configure_git = self.instance.prompt_yesno(prompt=CONFIGURE_GIT)
            else:
                # Assuming default configuration setting applies to all VCS.
                should_configure_git = self.hg_configure

            if should_configure_git:
                repo.configure(state_dir)

        self.maybe_install_private_packages_or_exit(
            application, checkout_type, mozconfig_builder
        )
        self.check_code_submission(checkout_root)
        # Wait until after moz-phab setup to check telemetry so that employees
        # will be automatically opted-in.
        if not self.instance.no_interactive and not settings.mach_telemetry.is_set_up:
            initialize_telemetry_setting(settings, str(checkout_root))

        self._output_mozconfig(application, mozconfig_builder)

        print(FINISHED % name)
        if not (
            which("rustc")
            and self.instance._parse_version(Path("rustc")) >= MODERN_RUST_VERSION
        ):
            print(
                "To build %s, please restart the shell (Start a new terminal window)"
                % name
            )

    def _check_for_dev_drive(self, topsrcdir):
        def extract_windows_version_number(raw_ver_output):
            pattern = re.compile(r"\bVersion (\d+(\.\d+)*)\b")
            match = pattern.search(raw_ver_output)

            if match:
                windows_version_number = match.group(1)
                return Version(windows_version_number)

            return Version("0")

        if os.environ.get("MACH_HIDE_DEV_DRIVE_SUGGESTION"):
            return

        print("Checking for Dev Drive...")

        if not shutil.which("powershell"):
            print(
                "PowerShell is not available on the system path. Unable to check for Dev Drive."
            )
            return

        try:
            ver_output = subprocess.check_output(["cmd.exe", "/c", "ver"], text=True)
            current_windows_version = extract_windows_version_number(ver_output)

            if current_windows_version < DEV_DRIVE_MINIMUM_VERSION:
                return

            file_system_info = subprocess.check_output(
                [
                    "powershell",
                    "Get-Item",
                    "-Path",
                    topsrcdir,
                    "|",
                    "Get-Volume",
                    "|",
                    "Select-Object",
                    "FileSystem",
                ],
                text=True,
            )

            file_system_type = file_system_info.strip().split("\n")[2]

            if file_system_type == "ReFS":
                print(" The Firefox source repository is on a Dev Drive.")
            else:
                print(
                    DEV_DRIVE_SUGGESTION.format(
                        topsrcdir, file_system_type, current_windows_version
                    )
                )
                if self.instance.no_interactive:
                    pass
                else:
                    input("\nPress enter to continue.")

        except subprocess.CalledProcessError as error:
            print(
                DEV_DRIVE_DETECTION_ERROR.format(f"CalledProcessError: {error.stderr}")
            )
            pass

    def _add_microsoft_defender_antivirus_exclusions(
        self, topsrcdir: Path, state_dir: Path
    ):
        if self.no_system_changes:
            return

        if os.environ.get("MOZ_AUTOMATION"):
            return

        # This will trigger a UAC prompt, and since it really only needs to be done
        # once, we can put a flag_file in the state_dir once we've done it and check
        # for its existence to prevent us from doing it again.
        flag_file = state_dir / ".ANTIVIRUS_EXCLUSIONS_DONE"
        if flag_file.exists():
            return

        powershell_exe = which("powershell")

        if not powershell_exe:
            return

        import ctypes

        powershell_exe = str(powershell_exe)
        paths = []

        # checkout root
        paths.append(topsrcdir)

        # MOZILLABUILD
        mozillabuild_dir = os.getenv("MOZILLABUILD")
        if mozillabuild_dir:
            paths.append(mozillabuild_dir)

        # .mozbuild
        paths.append(state_dir)

        joined_paths = "\n".join(f" '{p}'" for p in paths)
        print(
            "Attempting to add exclusion paths to Microsoft Defender Antivirus for:\n"
            f"{joined_paths}"
        )
        print(
            "Note: This will trigger a UAC prompt. If you decline, no exclusions will be added."
        )
        print(
            f"This step will not run again unless you delete the following file: '{flag_file}'\n"
        )

        args = ";".join(f"Add-MpPreference -ExclusionPath '{path}'" for path in paths)
        command = f'-Command "{args}"'

        # This will attempt to run as administrator by triggering a UAC prompt
        # for admin credentials. If "No" is selected, no exclusions are added.
        ctypes.windll.shell32.ShellExecuteW(
            None, "runas", powershell_exe, command, None, 0
        )

        try:
            flag_file.touch(exist_ok=True)
        except OSError as e:
            print(f"Could not write flag_file '{flag_file}': {e}")

    def _default_mozconfig_path(self):
        return Path(self.mach_context.topdir) / "mozconfig"

    def _read_default_mozconfig(self):
        path = self._default_mozconfig_path()
        with open(path) as mozconfig_file:
            return mozconfig_file.read()

    def _write_default_mozconfig(self, raw_mozconfig):
        path = self._default_mozconfig_path()
        with open(path, "w") as mozconfig_file:
            mozconfig_file.write(raw_mozconfig)
            print(f'Your requested configuration has been written to "{path}".')

    def _show_mozconfig_suggestion(self, raw_mozconfig):
        if raw_mozconfig:
            suggestion = MOZCONFIG_SUGGESTION_TEMPLATE % (
                self._default_mozconfig_path(),
                raw_mozconfig,
            )
            print(suggestion, end="")

    def _check_default_mozconfig_mismatch(
        self, current_mozconfig_info, expected_application, expected_raw_mozconfig
    ):
        current_raw_mozconfig = self._read_default_mozconfig()
        current_application = current_mozconfig_info["project"][0].replace("/", "_")
        if current_mozconfig_info["artifact-builds"]:
            current_application += "_artifact_mode"

        if expected_application == current_application:
            if expected_raw_mozconfig == current_raw_mozconfig:
                return

            # There's minor difference, show the suggestion.
            self._show_mozconfig_suggestion(expected_raw_mozconfig)
            return

        warning = MOZCONFIG_MISMATCH_WARNING_TEMPLATE % (
            self._default_mozconfig_path(),
            current_raw_mozconfig,
            expected_raw_mozconfig,
        )
        print(warning)

        if not self.instance.prompt_yesno("Do you want to overwrite the config?"):
            return

        self._write_default_mozconfig(expected_raw_mozconfig)

    def _output_mozconfig(self, application, mozconfig_builder):
        # Like 'generate_browser_mozconfig' or 'generate_mobile_android_mozconfig'.
        additional_mozconfig = getattr(
            self.instance, "generate_%s_mozconfig" % application
        )()
        if additional_mozconfig:
            mozconfig_builder.append(additional_mozconfig)
        raw_mozconfig = mozconfig_builder.generate()

        current_mozconfig_info = MozbuildObject.get_base_mozconfig_info(
            self.mach_context.topdir, None, ""
        )
        current_mozconfig_path = current_mozconfig_info["mozconfig"]["path"]

        if current_mozconfig_path:
            # mozconfig file exists
            if self._default_mozconfig_path().exists() and Path.samefile(
                Path(current_mozconfig_path), self._default_mozconfig_path()
            ):
                # This mozconfig file may be created by bootstrap.
                self._check_default_mozconfig_mismatch(
                    current_mozconfig_info, application, raw_mozconfig
                )
            elif raw_mozconfig:
                # The mozconfig file is created by user.
                self._show_mozconfig_suggestion(raw_mozconfig)
        elif raw_mozconfig:
            # No mozconfig file exists yet
            self._write_default_mozconfig(raw_mozconfig)

    def _populate_optional_packages(self, topsrcdir):
        mach_site = MachSiteManager.from_environment(
            topsrcdir,
            lambda: os.path.normpath(get_state_dir(True, topsrcdir=topsrcdir)),
        )
        mach_site.attempt_populate_optional_packages()


def current_firefox_checkout(env, hg: Optional[Path] = None):
    """Determine whether we're in a Firefox checkout.

    Returns one of None, ``git``, or ``hg``.
    """
    HG_ROOT_REVISIONS = set([
        # From mozilla-unified.
        "8ba995b74e18334ab3707f27e9eb8f4e37ba3d29"
    ])

    path = Path.cwd()
    while path:
        hg_dir = path / ".hg"
        git_dir = path / ".git"
        known_file = path / "config" / "milestone.txt"
        if hg and hg_dir.exists():
            # Verify the hg repo is a Firefox repo by looking at rev 0.
            try:
                node = subprocess.check_output(
                    [str(hg), "log", "-r", "0", "--template", "{node}"],
                    cwd=str(path),
                    env=env,
                    universal_newlines=True,
                )
                if node in HG_ROOT_REVISIONS:
                    _warn_if_risky_revision(path)
                    return "hg", path
                # Else the root revision is different. There could be nested
                # repos. So keep traversing the parents.
            except subprocess.CalledProcessError:
                pass

        # Just check for known-good files in the checkout, to prevent attempted
        # foot-shootings.  Determining a canonical git checkout of mozilla-unified
        # is...complicated
        elif git_dir.exists() or hg_dir.exists():
            if known_file.exists():
                _warn_if_risky_revision(path)
                return ("git" if git_dir.exists() else "hg"), path
        elif known_file.exists():
            return "SOURCE", path

        if not len(path.parents):
            break
        path = path.parent

    raise UserError(
        "Could not identify the root directory of your checkout! "
        "Are you running `mach bootstrap` in an hg or git clone?"
    )


def _warn_if_risky_revision(path: Path):
    # Warn the user if they're trying to bootstrap from an obviously old
    # version of tree as reported by the version control system (a month in
    # this case). This is an approximate calculation but is probably good
    # enough for our purposes.
    NUM_SECONDS_IN_MONTH = 60 * 60 * 24 * 30

    repo = get_repository_object(path)
    if (time.time() - repo.get_commit_time()) >= NUM_SECONDS_IN_MONTH:
        print(OLD_REVISION_WARNING)


def _macos_is_running_under_rosetta():
    proc = subprocess.run(
        ["sysctl", "-n", "sysctl.proc_translated"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    return (
        proc.returncode == 0 and proc.stdout.decode("ascii", "replace").strip() == "1"
    )
