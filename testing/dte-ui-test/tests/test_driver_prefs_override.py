# Any copyright is dedicated to the Public Domain.
# https://creativecommons.org/publicdomain/zero/1.0/

import pytest
from selenium.webdriver import Firefox


@pytest.fixture()
def add_to_prefs_list():
    return [("dom.disable_beforeunload", False)]


def test_add_to_prefs_list_overrides_driver_default(driver: Firefox):
    """A test's own prefs win over the driver's dom.disable_beforeunload default."""
    with driver.context(driver.CONTEXT_CHROME):
        # Read the pref Firefox is actually running with, from the chrome context.
        value = driver.execute_script(
            'return Services.prefs.getBoolPref("dom.disable_beforeunload");'
        )
    assert value is False
