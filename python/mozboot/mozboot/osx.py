# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import platform
import subprocess
import sys
import tempfile
from urllib.request import urlopen

import certifi
from mach.util import to_optional_path, to_optional_str
from mozfile import which
from packaging.version import Version

from mozboot.base import BaseBootstrapper

HOMEBREW_BOOTSTRAP = (
    "https://raw.githubusercontent.com/Homebrew/install/master/install.sh"
)

BREW_INSTALL = """
We will install the Homebrew package manager to install required packages.

You will be prompted to install Homebrew with its default settings. If you
would prefer to do this manually, hit CTRL+c, install Homebrew yourself, ensure
"brew" is in your $PATH, and relaunch bootstrap.
"""

BREW_PACKAGES = """
We are now installing all required packages via Homebrew. You will see a lot of
output as packages are built.
"""

NO_BREW_INSTALLED = "It seems you don't have Homebrew installed."


class OSXAndroidBootstrapper:
    def install_mobile_android_packages(self, mozconfig_builder, artifact_mode=False):
        os_arch = platform.machine()
        if os_arch != "x86_64" and os_arch != "arm64":
            raise Exception(
                "You need a 64-bit version of Mac OS X to build "
                "GeckoView/Firefox for Android."
            )

        from mozboot import android

        if os_arch == "x86_64" or os_arch == "x86":
            avd_manifest_path = android.AVD_MANIFEST_X86_64
        else:
            avd_manifest_path = android.AVD_MANIFEST_ARM64

        android.ensure_android(
            "macosx",
            os_arch,
            artifact_mode=artifact_mode,
            no_interactive=self.no_interactive,
            avd_manifest_path=avd_manifest_path,
        )

    def ensure_mobile_android_packages(self):
        from mozboot import android

        arch = platform.machine()
        android.ensure_java("macosx", arch)

        if arch == "x86_64" or arch == "x86":
            self.install_toolchain_artifact(android.X86_64_ANDROID_AVD)
        elif arch == "arm64":
            self.install_toolchain_artifact(android.ARM64_ANDROID_AVD)

    def install_mobile_android_artifact_mode_packages(self, mozconfig_builder):
        self.install_mobile_android_packages(mozconfig_builder, artifact_mode=True)

    def generate_mobile_android_mozconfig(self):
        return self._generate_mobile_android_mozconfig()

    def generate_mobile_android_artifact_mode_mozconfig(self):
        return self._generate_mobile_android_mozconfig(artifact_mode=True)

    def _generate_mobile_android_mozconfig(self, artifact_mode=False):
        from mozboot import android

        return android.generate_mozconfig("macosx", artifact_mode=artifact_mode)


def ensure_command_line_tools():
    # We need either the command line tools or Xcode (one is sufficient).
    # Python 3, required to run this code, is not installed by default on macos
    # as of writing (macos <= 11.x).
    # There are at least 5 different ways to obtain it:
    # - macports
    # - homebrew
    # - command line tools
    # - Xcode
    # - python.org
    # The first two require to install the command line tools.
    # So only in the last case we may not have command line tools or xcode
    # available.
    # When the command line tools are installed, `xcode-select --print-path`
    # prints their path.
    # When Xcode is installed, `xcode-select --print-path` prints its path.
    # When neither is installed, `xcode-select --print-path` prints an error
    # to stderr and nothing to stdout.
    # So in the rare case where we detect neither the command line tools or
    # Xcode is installed, we trigger an intall of the command line tools
    # (via `xcode-select --install`).
    proc = subprocess.run(
        ["xcode-select", "--print-path"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    if not proc.stdout:
        subprocess.run(["xcode-select", "--install"], check=True)
        # xcode-select --install triggers a separate process to be started by
        # launchd, and tracking its successful outcome would require something
        # like figuring its pid and using kqueue to get a notification when it
        # finishes. Considering how unlikely it is that someone would end up
        # here in the first place, we just bail out.
        print("Please follow the command line tools installer instructions")
        print("and rerun `./mach bootstrap` when it's finished.")
        sys.exit(1)


class OSXBootstrapperLight(OSXAndroidBootstrapper, BaseBootstrapper):
    def __init__(self, version, **kwargs):
        BaseBootstrapper.__init__(self, **kwargs)

    def install_system_packages(self):
        ensure_command_line_tools()

    # All the installs below are assumed to be handled by mach configure/build by
    # default, which is true for arm64.
    def install_browser_packages(self, mozconfig_builder):
        pass

    def install_browser_artifact_mode_packages(self, mozconfig_builder):
        pass


class OSXBootstrapper(OSXAndroidBootstrapper, BaseBootstrapper):
    def __init__(self, version, **kwargs):
        BaseBootstrapper.__init__(self, **kwargs)

        self.os_version = Version(version)

        if self.os_version < Version("10.6"):
            raise Exception("OS X 10.6 or above is required.")

        self.minor_version = version.split(".")[1]

    def install_system_packages(self):
        ensure_command_line_tools()

        self.ensure_homebrew_installed()
        _, hg_modern, _ = self.is_mercurial_modern()
        if not hg_modern:
            print(
                "Mercurial wasn't found or is not sufficiently modern. "
                "It will be installed with brew"
            )

        packages = ["git", "gnu-tar", "terminal-notifier", "watchman"]
        if not hg_modern:
            packages.append("mercurial")
        self._ensure_homebrew_packages(packages)

    def install_browser_packages(self, mozconfig_builder):
        pass

    def install_browser_artifact_mode_packages(self, mozconfig_builder):
        pass

    def _ensure_homebrew_found(self):
        self.brew = to_optional_path(which("brew"))

        return self.brew is not None

    def _ensure_homebrew_packages(self, packages, is_for_cask=False):
        package_type_flag = "--cask" if is_for_cask else "--formula"
        self.ensure_homebrew_installed()

        def create_homebrew_cmd(*parameters):
            base_cmd = [to_optional_str(self.brew)]
            base_cmd.extend(parameters)
            return base_cmd + [package_type_flag]

        installed = set(
            subprocess.check_output(
                create_homebrew_cmd("list"), universal_newlines=True
            ).split()
        )
        outdated = set(
            subprocess.check_output(
                create_homebrew_cmd("outdated", "--quiet"), universal_newlines=True
            ).split()
        )

        to_install = set(package for package in packages if package not in installed)
        to_upgrade = set(package for package in packages if package in outdated)

        if to_install or to_upgrade:
            print(BREW_PACKAGES)
        if to_install:
            subprocess.check_call(create_homebrew_cmd("install") + list(to_install))
        if to_upgrade:
            subprocess.check_call(create_homebrew_cmd("upgrade") + list(to_upgrade))

    def _ensure_homebrew_casks(self, casks):
        self._ensure_homebrew_found()

        known_taps = subprocess.check_output([to_optional_str(self.brew), "tap"])

        # Ensure that we can access old versions of packages.
        if b"homebrew/cask-versions" not in known_taps:
            subprocess.check_output(
                [to_optional_str(self.brew), "tap", "homebrew/cask-versions"]
            )

        # "caskroom/versions" has been renamed to "homebrew/cask-versions", so
        # it is safe to remove the old tap. Removing the old tap is necessary
        # to avoid the error "Cask [name of cask] exists in multiple taps".
        # See https://bugzilla.mozilla.org/show_bug.cgi?id=1544981
        if b"caskroom/versions" in known_taps:
            subprocess.check_output(
                [to_optional_str(self.brew), "untap", "caskroom/versions"]
            )

        self._ensure_homebrew_packages(casks, is_for_cask=True)

    def ensure_homebrew_browser_packages(self):
        # TODO: Figure out what not to install for artifact mode
        packages = ["yasm"]
        self._ensure_homebrew_packages(packages)

    def ensure_homebrew_installed(self):
        """
        Search for Homebrew in sys.path, if not found, prompt the user to install it.
        Then assert our PATH ordering is correct.
        """
        homebrew_found = self._ensure_homebrew_found()
        if not homebrew_found:
            self.install_homebrew()

    def ensure_sccache_packages(self):
        from mozboot import sccache

        self.install_toolchain_artifact(sccache.RUSTC_DIST_TOOLCHAIN)
        self.install_toolchain_artifact(sccache.CLANG_DIST_TOOLCHAIN)

    def install_homebrew(self):
        print(BREW_INSTALL)
        bootstrap = urlopen(
            url=HOMEBREW_BOOTSTRAP, cafile=certifi.where(), timeout=20
        ).read()
        with tempfile.NamedTemporaryFile() as tf:
            tf.write(bootstrap)
            tf.flush()

            subprocess.check_call(["bash", tf.name])

        homebrew_found = self._ensure_homebrew_found()
        if not homebrew_found:
            print(
                "Homebrew was just installed but can't be found on PATH. "
                "Please file a bug."
            )
            sys.exit(1)

    def _update_package_manager(self):
        subprocess.check_call([to_optional_str(self.brew), "-v", "update"])

    def _upgrade_package(self, package):
        self._ensure_homebrew_installed()

        try:
            subprocess.check_output(
                [to_optional_str(self.brew), "-v", "upgrade", package],
                stderr=subprocess.STDOUT,
            )
        except subprocess.CalledProcessError as e:
            if b"already installed" not in e.output:
                raise

    def upgrade_mercurial(self, current):
        self._upgrade_package("mercurial")
