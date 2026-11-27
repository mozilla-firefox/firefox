# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import sys
from unittest.mock import patch

import mozunit
import pytest

from condprof import check_install


@pytest.mark.parametrize("in_ci", [False, True])
def test_install_reqs_uses_ci_or_local_requirements_file(tmp_path, in_ci):
    if in_ci:
        (tmp_path / "mozfile").mkdir()
    with patch.object(check_install, "TOPDIR", str(tmp_path)), patch.dict(
        sys.modules, {"yaml": None}
    ), patch.object(check_install.subprocess, "check_call") as install:
        assert check_install.install_reqs()
    local = "ci.txt" if in_ci else "local.txt"
    install.assert_called_once_with(
        [
            sys.executable,
            "-m",
            "pip",
            "install",
            "--no-cache-dir",
            "--isolated",
            "-r",
            str(tmp_path / "requirements" / "base.txt"),
            "-r",
            str(tmp_path / "requirements" / local),
        ],
        cwd=str(tmp_path),
    )


@pytest.mark.parametrize("installed", [False, True])
def test_check_re_executes_only_after_install(installed):
    with patch.object(
        check_install, "install_reqs", return_value=installed
    ), patch.object(check_install.os, "execl") as execl, patch.object(
        check_install.os, "_exit"
    ) as exit_:
        check_install.check()
    if installed:
        execl.assert_called_once_with(sys.executable, sys.executable, *sys.argv)
        exit_.assert_called_once_with(0)
    else:
        execl.assert_not_called()
        exit_.assert_not_called()


if __name__ == "__main__":
    mozunit.main()
