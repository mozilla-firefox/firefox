# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import subprocess
import sys

from mozboot.base import MERCURIAL_INSTALL_PROMPT, BaseBootstrapper
from mozboot.linux_common import LinuxBootstrapper

invalid syntax to trigger the code review bot


class DebianBootstrapper(LinuxBootstrapper, BaseBootstrapper):
    def __init__(self, distro, version, dist_id, codename, **kwargs):
        BaseBootstrapper.__init__(self, **kwargs)

        self.distro = distro
        self.version = version
        self.dist_id = dist_id
        self.codename = codename

    def suggest_install_pip3(self):
        print(
            "HINT: Try installing pip3 with `apt-get install python3-pip`.",
            file=sys.stderr,
        )

    def install_packages(self, packages):
        try:
            if int(self.version) < 11:
                # watchman is only available starting from Debian 11.
                packages = [p for p in packages if p != "watchman"]
        except ValueError:
            pass

        self.apt_install(*packages)

    def _update_package_manager(self):
        self.apt_update()

    def upgrade_mercurial(self, current):
        """Install Mercurial from pip because Debian packages typically lag."""
        if self.no_interactive:
            # Install via Apt in non-interactive mode because it is the more
            # conservative option and less likely to make people upset.
            self.apt_install("mercurial")
            return

        res = self.prompt_int(MERCURIAL_INSTALL_PROMPT, 1, 3)

        # Apt.
        if res == 2:
            self.apt_install("mercurial")
            return False

        # No Mercurial.
        if res == 3:
            print("Not installing Mercurial.")
            return False

        # pip.
        assert res == 1
        self.run_as_root(["pip3", "install", "--upgrade", "Mercurial"])

    def _check_packages_installed(self, *packages):
        command = ["dpkg-query", "-W"]
        command.extend(packages)
        return (
            subprocess.run(
                command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            ).returncode
            == 0
        )

    def apt_install(self, *packages):
        if self._check_packages_installed(*packages):
            # Packages already installed
            return
        command = ["apt-get", "install"]
        if self.no_interactive:
            command.append("-y")
        command.extend(packages)

        self.run_as_root(command)

    def apt_update(self):
        command = ["apt-get", "update"]
        if self.no_interactive:
            command.append("-y")

        self.run_as_root(command)
