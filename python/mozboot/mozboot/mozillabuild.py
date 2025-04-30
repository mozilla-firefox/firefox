# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import ctypes
import os
import platform
import subprocess
import sys
from pathlib import Path

from mozbuild.buildversion import mozilla_build_version
from packaging.version import Version

from mozboot.base import BaseBootstrapper


def is_aarch64_host():
    from ctypes import wintypes

    kernel32 = ctypes.windll.kernel32
    IMAGE_FILE_MACHINE_UNKNOWN = 0
    IMAGE_FILE_MACHINE_ARM64 = 0xAA64

    try:
        iswow64process2 = kernel32.IsWow64Process2
    except Exception:
        # If we can't access the symbol, we know we're not on aarch64.
        return False

    currentProcess = kernel32.GetCurrentProcess()
    processMachine = wintypes.USHORT(IMAGE_FILE_MACHINE_UNKNOWN)
    nativeMachine = wintypes.USHORT(IMAGE_FILE_MACHINE_UNKNOWN)

    gotValue = iswow64process2(
        currentProcess, ctypes.byref(processMachine), ctypes.byref(nativeMachine)
    )
    # If this call fails, we have no idea.
    if not gotValue:
        return False

    return nativeMachine.value == IMAGE_FILE_MACHINE_ARM64


def get_is_windefender_disabled():
    import winreg

    try:
        with winreg.OpenKeyEx(
            winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Defender"
        ) as windefender_key:
            is_antivirus_disabled, _ = winreg.QueryValueEx(
                windefender_key, "DisableAntiSpyware"
            )
            # is_antivirus_disabled is either 0 (False) or 1 (True)
            return bool(is_antivirus_disabled)
    except FileNotFoundError:
        return True


def get_windefender_exclusion_paths():
    import winreg

    paths = []
    try:
        with winreg.OpenKeyEx(
            winreg.HKEY_LOCAL_MACHINE,
            r"SOFTWARE\Microsoft\Windows Defender\Exclusions\Paths",
        ) as exclusions_key:
            _, values_count, __ = winreg.QueryInfoKey(exclusions_key)
            for i in range(0, values_count):
                path, _, __ = winreg.EnumValue(exclusions_key, i)
                paths.append(Path(path))
    except FileNotFoundError:
        pass

    return paths


def is_windefender_affecting_srcdir(src_dir: Path):
    if get_is_windefender_disabled():
        return False

    # When there's a match, but path cases aren't the same between srcdir and exclusion_path,
    # commonpath will use the casing of the first path provided.
    # To avoid surprises here, we normcase(...) so we don't get unexpected breakage if we change
    # the path order.
    src_dir = src_dir.resolve()

    try:
        exclusion_paths = get_windefender_exclusion_paths()
    except OSError as e:
        if e.winerror == 5:
            # A version of Windows 10 released in 2021 raises an "Access is denied"
            # error (ERROR_ACCESS_DENIED == 5) to un-elevated processes when they
            # query Windows Defender's exclusions. Skip the exclusion path checking.
            return
        raise

    for exclusion_path in exclusion_paths:
        exclusion_path = exclusion_path.resolve()
        try:
            if Path(os.path.commonpath((exclusion_path, src_dir))) == exclusion_path:
                # exclusion_path is an ancestor of srcdir
                return False
        except ValueError:
            # ValueError: Paths don't have the same drive - can't be ours
            pass
    return True


class MozillaBuildBootstrapper(BaseBootstrapper):
    """Bootstrapper for MozillaBuild to install rustup."""

    def __init__(self, no_interactive=False, no_system_changes=False):
        BaseBootstrapper.__init__(
            self, no_interactive=no_interactive, no_system_changes=no_system_changes
        )

    def validate_environment(self):
        if is_windefender_affecting_srcdir(self.srcdir):
            print(
                "Warning: the Firefox checkout directory is currently not in the "
                "Windows Defender exclusion list. This can cause the build process "
                "to be dramatically slowed or broken. To resolve this, follow the "
                "directions here: "
                "https://firefox-source-docs.mozilla.org/setup/windows_build.html"
                "#antivirus-performance",
                file=sys.stderr,
            )

    def install_system_packages(self):
        pass

    def upgrade_mercurial(self, current):
        # Mercurial upstream sometimes doesn't upload wheels, and building
        # from source requires MS Visual C++ 9.0. So we force pip to install
        # the last version that comes with wheels.
        if mozilla_build_version() >= Version("4.0"):
            pip_dir = (
                Path(os.environ["MOZILLABUILD"]) / "python3" / "Scripts" / "pip.exe"
            )
        else:
            pip_dir = (
                Path(os.environ["MOZILLABUILD"]) / "python" / "Scripts" / "pip.exe"
            )

        command = [
            str(pip_dir),
            "install",
            "--upgrade",
            "mercurial",
            "--only-binary",
            "mercurial",
        ]
        self.run(command)

    def install_browser_packages(self, mozconfig_builder):
        pass

    def install_browser_artifact_mode_packages(self, mozconfig_builder):
        pass

    def _os_arch(self):
        os_arch = platform.machine()
        if os_arch == "AMD64":
            # On Windows, x86_64 is reported as AMD64 but we use x86_64
            # everywhere else, so let's normalized it here.
            return "x86_64"
        return os_arch

    def install_mobile_android_packages(self, mozconfig_builder, artifact_mode=False):
        from mozboot import android

        os_arch = self._os_arch()

        android.ensure_android(
            "windows",
            os_arch,
            artifact_mode=artifact_mode,
            no_interactive=self.no_interactive,
            avd_manifest_path=android.AVD_MANIFEST_X86_64,
        )

    def ensure_mobile_android_packages(self):
        from mozboot import android

        android.ensure_java("windows", self._os_arch())
        self.install_toolchain_artifact(android.X86_64_ANDROID_AVD)

    def install_mobile_android_artifact_mode_packages(self, mozconfig_builder):
        self.install_mobile_android_packages(mozconfig_builder, artifact_mode=True)

    def generate_mobile_android_mozconfig(self, artifact_mode=False):
        from mozboot import android

        return android.generate_mozconfig("windows", artifact_mode=artifact_mode)

    def generate_mobile_android_artifact_mode_mozconfig(self):
        return self.generate_mobile_android_mozconfig(artifact_mode=True)

    def ensure_sccache_packages(self):
        from mozboot import sccache

        self.install_toolchain_artifact(sccache.RUSTC_DIST_TOOLCHAIN)
        self.install_toolchain_artifact(sccache.CLANG_DIST_TOOLCHAIN)

    def _update_package_manager(self):
        pass

    def run(self, command):
        subprocess.check_call(command, stdin=sys.stdin)
