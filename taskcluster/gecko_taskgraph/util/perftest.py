# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re

RAPTOR_SCRIPT = re.compile(r"(?<=[/\\])raptor_script\.py\b")


def is_external_browser(label):
    if any(
        external_browser in label
        for external_browser in (
            "safari",
            "safari-tp",
            "chrome",
            "custom-car",
            "chrome-m",
            "cstm-car-m",
        )
    ):
        return True
    return False


def use_raptor_ab_script(command):
    """Return a raptor task's command with raptor_ab_script.py in place of
    raptor_script.py, which makes the task run the test once per AB side.

    Depending on the worker, the command is a string or nested lists of strings.
    """
    replacements = 0

    def replace(value):
        nonlocal replacements
        if isinstance(value, list):
            return [replace(item) for item in value]
        if isinstance(value, str):
            value, count = RAPTOR_SCRIPT.subn("raptor_ab_script.py", value)
            replacements += count
        return value

    new_command = replace(command)
    if replacements != 1:
        raise Exception(
            f"Expected raptor_script.py once in the command, found it "
            f"{replacements} times: {command!r}"
        )
    return new_command
