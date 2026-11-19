# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import contextlib
import unittest
from unittest import mock

import buildconfig
import mozunit

with mock.patch.object(buildconfig, "substs", {"OS_TARGET": "Linux"}):
    from mozbuild.action import check_binary

_CHECKS = (
    "check_android_megazord_bionic_allocations",
    "check_android_megazord_mozglue",
    "check_binary_compat",
    "check_mozglue_order",
    "check_pt_load",
    "check_textrel",
)


class TestCheckBinary(unittest.TestCase):
    def _run_checks(self, prebuilt):
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                mock.patch.object(
                    check_binary.buildconfig, "substs", {"MOZ_STDCXX_COMPAT": "1"}
                )
            )
            stack.enter_context(mock.patch.object(check_binary, "PLATFORM", "Linux"))
            mocks = {}
            for name in _CHECKS:
                mocks[name] = mock.Mock()
                mocks[name].__name__ = name
                stack.enter_context(mock.patch.object(check_binary, name, mocks[name]))
            self.assertEqual(check_binary.checks("libfoo.so", prebuilt), 0)
        return [name for name, m in mocks.items() if m.called]

    def test_checks(self):
        self.assertEqual(self._run_checks(prebuilt=False), list(_CHECKS))

    def test_checks_prebuilt(self):
        self.assertEqual(
            self._run_checks(prebuilt=True),
            ["check_binary_compat", "check_textrel"],
        )

    def test_main_prebuilt(self):
        with mock.patch.object(check_binary, "checks", return_value=0) as checks:
            check_binary.main(["libfoo.so"])
            check_binary.main(["--prebuilt", "libfoo.so"])
        self.assertEqual(
            checks.call_args_list,
            [mock.call("libfoo.so", False), mock.call("libfoo.so", True)],
        )


if __name__ == "__main__":
    mozunit.main()
