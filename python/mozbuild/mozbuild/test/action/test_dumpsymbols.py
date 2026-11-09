# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import contextlib
import os
import subprocess
import sys
import unittest
from unittest import mock

import mozunit

from mozbuild.action import dumpsymbols

_WINDOWS = {"OS_ARCH": "WINNT", "WINCHECKSEC": "/tools/winchecksec.exe"}


@contextlib.contextmanager
def _patched(substs, check_call=None):
    with contextlib.ExitStack() as es:
        es.enter_context(mock.patch.object(dumpsymbols.buildconfig, "substs", substs))
        es.enter_context(
            mock.patch.object(dumpsymbols.buildconfig, "topsrcdir", "/src")
        )
        es.enter_context(mock.patch.object(dumpsymbols, "dump_symbols"))
        yield es.enter_context(
            mock.patch.object(
                dumpsymbols.subprocess, "check_call", check_call or mock.DEFAULT
            )
        )


class TestDumpSymbols(unittest.TestCase):
    def test_winchecksec(self):
        with _patched(_WINDOWS) as check_call:
            self.assertEqual(dumpsymbols.main(["xul.dll", "xul.dll_syms.track"]), 0)

        check_call.assert_called_once_with([
            sys.executable,
            os.path.join("/src", "build", "win32", "autowinchecksec.py"),
            "xul.dll",
        ])

    def test_winchecksec_failure(self):
        error = subprocess.CalledProcessError(1, "autowinchecksec.py")
        with _patched(_WINDOWS, mock.Mock(side_effect=error)):
            self.assertEqual(dumpsymbols.main(["xul.dll", "xul.dll_syms.track"]), 1)

    def test_winchecksec_unset(self):
        with _patched({"OS_ARCH": "WINNT"}) as check_call:
            self.assertEqual(dumpsymbols.main(["xul.dll", "xul.dll_syms.track"]), 0)

        check_call.assert_not_called()

    def test_winchecksec_other_os(self):
        substs = {"OS_ARCH": "Linux", "WINCHECKSEC": "/tools/winchecksec"}
        with _patched(substs) as check_call:
            self.assertEqual(dumpsymbols.main(["libxul.so", "libxul.so_syms.track"]), 0)

        check_call.assert_not_called()


if __name__ == "__main__":
    mozunit.main()
