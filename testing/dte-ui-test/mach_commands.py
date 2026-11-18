# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
# ---
from os import environ

from mach.decorators import Command, CommandArgument


@Command(
    "dte-ui-test",
    category="testing",
    virtualenv_name="dte-ui-test",
    description="Run automations to validate Fx Desktop builds",
)
@CommandArgument(
    "--binary-path", default=None, help="Firefox executable path for local run"
)
def build(command_context, binary_path, **kwargs):
    return run_tests(binary_path, **kwargs)


def run_tests(binary_path, **kwargs):
    import pytest

    if binary_path is not None:
        environ["LOCAL_FX"] = binary_path

    return pytest.main(["testing/dte-ui-test/", "-s"])
