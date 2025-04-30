# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# An easy way for distribution-specific bootstrappers to share the code
# needed to install Stylo and Node dependencies.  This class must come before
# BaseBootstrapper in the inheritance list.

import platform


def is_non_x86_64():
    return platform.machine() != "x86_64"


class MobileAndroidBootstrapper:
    def __init__(self, **kwargs):
        pass

    def install_mobile_android_packages(self, mozconfig_builder, artifact_mode=False):
        from mozboot import android

        os_arch = platform.machine()

        android.ensure_android(
            "linux",
            os_arch,
            artifact_mode=artifact_mode,
            no_interactive=self.no_interactive,
            avd_manifest_path=android.AVD_MANIFEST_X86_64,
        )

    def install_mobile_android_artifact_mode_packages(self, mozconfig_builder):
        self.install_mobile_android_packages(mozconfig_builder, artifact_mode=True)

    def ensure_mobile_android_packages(self):
        from mozboot import android

        android.ensure_java("linux", platform.machine())
        self.install_toolchain_artifact(android.X86_64_ANDROID_AVD)

    def generate_mobile_android_mozconfig(self, artifact_mode=False):
        from mozboot import android

        return android.generate_mozconfig("linux", artifact_mode=artifact_mode)

    def generate_mobile_android_artifact_mode_mozconfig(self):
        return self.generate_mobile_android_mozconfig(artifact_mode=True)


class LinuxBootstrapper(MobileAndroidBootstrapper):
    def __init__(self, **kwargs):
        pass

    def ensure_sccache_packages(self):
        pass

    def install_system_packages(self):
        self.install_packages(
            [
                "bash",
                "findutils",  # contains xargs
                "gzip",
                "libxml2",  # used by bootstrapped clang
                "m4",
                "make",
                "perl",
                "tar",
                "unzip",
            ]
        )
        # Optional packages
        try:
            self.install_packages(["watchman"])
        except Exception:
            pass

    def install_browser_packages(self, mozconfig_builder, artifact_mode=False):
        pass

    def install_browser_artifact_mode_packages(self, mozconfig_builder):
        pass
