# Any copyright is dedicated to the Public Domain.
# https://creativecommons.org/publicdomain/zero/1.0/

import pytest
from selenium.webdriver import Firefox


@pytest.fixture()
def firefox_options(firefox_options):
    firefox_options.set_capability("webSocketUrl", True)
    return firefox_options


def test_firefox_options_override_reaches_the_browser(driver: Firefox):
    """The driver starts Firefox with the options a test adds."""
    assert driver.capabilities["webSocketUrl"].startswith("ws://")
