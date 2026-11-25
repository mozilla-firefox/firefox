# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from fnmatch import fnmatch

import mozunit
import pytest

from mozbuild.repackaging import flatpak


@pytest.mark.parametrize(
    "flatpak_name", ["org.mozilla.firefox", "org.mozilla.firefox_nightly"]
)
def test_dbus_app_name_is_allowed_by_sandbox(flatpak_name):
    args = flatpak._build_finish_args(flatpak_name)
    env = dict(
        arg.removeprefix("--env=").split("=", 1)
        for arg in args
        if arg.startswith("--env=")
    )
    own_names = [
        arg.removeprefix("--own-name=") for arg in args if arg.startswith("--own-name=")
    ]

    # Matches the bus name built by nsDBusRemoteServer::Startup.
    bus_name = f"org.mozilla.{env['MOZ_DBUS_APP_NAME']}.cHJvZmlsZQ__"
    assert any(fnmatch(bus_name, pattern) for pattern in own_names)


if __name__ == "__main__":
    mozunit.main()
