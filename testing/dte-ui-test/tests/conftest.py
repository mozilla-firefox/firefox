import logging
import os
import platform
import re
from pathlib import Path
from shutil import rmtree
from subprocess import run

import mozinfo
import mozinstall
import pytest
from auto_fxui.browser_object import FindToolbar
from auto_fxui.util import BrowserActions
from selenium.common.exceptions import TimeoutException, WebDriverException
from selenium.webdriver import Firefox
from selenium.webdriver.common.by import By
from selenium.webdriver.firefox.options import Options
from selenium.webdriver.firefox.service import Service
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.support.ui import WebDriverWait

# Markers used by migrated STARfox tests. Registering them does not change how
# the tests run.
MARKERS = [
    "audio: test is reliant on audio",
    "headed: test must run in headed mode (uses desktop input automation)",
    "locale_de: tests run in DE locale versions",
    "locale_fr: tests run in FR locale versions",
    "locale_gb: tests run in GB locale versions",
    "noxvfb: tests that should not run in xvfb sessions",
    "slow: test is clocked at more than 30s on modern machines",
]


def pytest_configure(config):
    for marker in MARKERS:
        config.addinivalue_line("markers", marker)


def pytest_addoption(parser):
    parser.addoption(
        "--mock-mode",
        default="replay",
        choices=["replay", "record", "live"],
        help="How the Smart Window mock server answers: replay recordings "
        "(default), record new ones, or pass through live",
    )


@pytest.fixture(scope="session")
def opt_ci():
    """Return True when running in Mozilla CI."""
    return os.environ.get("MOZ_AUTOMATION") == "1"


@pytest.fixture(scope="session")
def opt_mock_mode(request):
    """Return the --mock-mode option: "replay", "record" or "live"."""
    return request.config.getoption("--mock-mode")


@pytest.fixture()
def find_toolbar(driver: Firefox):
    find_toolbar = FindToolbar(driver)
    yield find_toolbar


@pytest.fixture()
def browser_actions(driver: Firefox):
    ba = BrowserActions(driver)
    yield ba


@pytest.fixture()
def timeout():
    return 30


@pytest.fixture()
def prefs_list(add_to_prefs_list: list):
    """List of prefs to send to main conftest.py driver fixture"""
    prefs = []
    prefs.extend(add_to_prefs_list)
    return prefs


@pytest.fixture()
def add_to_prefs_list():
    return []


@pytest.fixture()
def fx_location(tmpdir):
    if local_fx := os.environ.get("LOCAL_FX"):
        return local_fx
    fetches_dir = Path(os.environ.get("MOZ_FETCHES_DIR", "fetches"))

    if mozinfo.os == "mac":
        # Install dmg if in MacOS
        fx_loc = Path(fetches_dir) / "target.dmg"
        install_loc = mozinstall.install(
            fx_loc,
            tmpdir / "firefox",
        )
        binary = mozinstall.get_binary(install_loc, "firefox")
    elif mozinfo.os == "linux":
        binary = Path(fetches_dir) / "firefox" / "firefox"
    elif mozinfo.os == "win":
        binary = Path(fetches_dir) / "firefox" / "firefox.exe"
    return str(binary)


@pytest.fixture()
def firefox_options():
    """
    Browser options for the driver; override to add capabilities before launch.

    The driver then applies its defaults and add_to_prefs_list on top, so set
    prefs through add_to_prefs_list: one set here can be overwritten.
    """
    return Options()


@pytest.fixture(autouse=True)
def driver(prefs_list, timeout, fx_location, firefox_options):
    """
    Return the webdriver object.

    """

    options = firefox_options
    service_args = ["--allow-system-access"]
    options.add_argument("--headless")
    options.binary_location = fx_location
    # A beforeunload prompt (e.g. an edited PDF) stops driver.quit() from returning.
    options.set_preference("dom.disable_beforeunload", True)

    for opt, value in prefs_list:
        options.set_preference(opt, value)
    try:
        service = Service(service_args=service_args)
        driver = Firefox(service=service, options=options)

        driver.implicitly_wait(timeout)
        WebDriverWait(driver, timeout=40).until(
            EC.presence_of_element_located((By.TAG_NAME, "body"))
        )

        yield driver

    except (WebDriverException, TimeoutException) as e:
        logging.warning(f"DRIVER exception: {e}")
        raise

    finally:
        if "driver" in locals() and driver:
            driver.quit()


@pytest.fixture()
def browser_name(driver: Firefox) -> str:
    """Return "Nightly" for a Nightly build, otherwise "Firefox"."""
    return (
        "Nightly" if "a" in driver.capabilities.get("browserVersion", "") else "Firefox"
    )


@pytest.fixture()
def screenshot(driver: Firefox, tmp_path):
    """Return a function that saves a screenshot and returns its path."""
    # MOZ_UPLOAD_DIR is where a CI task's uploadable artifacts go.
    out_dir = Path(os.environ.get("MOZ_UPLOAD_DIR", tmp_path))

    def _screenshot(filename: str) -> str:
        path = out_dir / (filename if filename.endswith(".png") else f"{filename}.png")
        driver.save_screenshot(str(path))
        return str(path)

    return _screenshot


@pytest.fixture(scope="session")
def sys_platform():
    """Return platform.system(): "Darwin", "Linux" or "Windows"."""
    return platform.system()


@pytest.fixture()
def home_folder():
    """Return the current user's home folder."""
    return str(Path.home())


@pytest.fixture()
def downloads_folder(home_folder):
    """Return the Downloads folder Firefox saves to by default."""
    return os.path.join(home_folder, "Downloads")


@pytest.fixture()
def delete_files_regex_string():
    """Override to tell delete_files which file names to remove."""
    # Matches nothing, so delete_files is safe unless a test overrides this.
    return r"zzxqxzqx"


@pytest.fixture()
def delete_files(downloads_folder, delete_files_regex_string):
    """Remove matching files from the Downloads folder before and after the test."""

    def _delete_files():
        if not os.path.isdir(downloads_folder):
            return
        pattern = re.compile(delete_files_regex_string)
        for name in os.listdir(downloads_folder):
            if not pattern.match(name):
                continue
            target = os.path.join(downloads_folder, name)
            if os.path.isdir(target):
                # Refuse to remove a folder that looks like a system location.
                if target == downloads_folder or len(Path(target).parts) < 4:
                    raise PermissionError(f"Refusing to delete {target}")
                rmtree(target)
            else:
                os.remove(target)

    _delete_files()
    yield True
    _delete_files()


@pytest.fixture()
def close_file_manager(sys_platform):
    """Close file manager windows a test opened, after the test."""
    yield
    if sys_platform == "Windows":
        # Closes Explorer windows without killing explorer.exe, the desktop shell.
        script = "(New-Object -ComObject Shell.Application).Windows() | % { $_.Quit() }"
        run(["powershell", "-NoProfile", "-Command", script], check=True)
    elif sys_platform == "Darwin":
        script = 'tell application "Finder" to close every Finder window'
        run(["osascript", "-e", script], check=True)
