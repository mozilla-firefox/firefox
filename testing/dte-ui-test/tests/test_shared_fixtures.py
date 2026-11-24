# Any copyright is dedicated to the Public Domain.
# https://creativecommons.org/publicdomain/zero/1.0/

import os
import uuid

import pytest

STALE_PREFIX = "dte-ui-test-delete-files-"


@pytest.fixture()
def delete_files_regex_string():
    return rf"^{STALE_PREFIX}.*\.txt$"


@pytest.fixture()
def stale_download(downloads_folder):
    """Create a file delete_files should remove, before delete_files runs."""
    os.makedirs(downloads_folder, exist_ok=True)
    path = os.path.join(downloads_folder, f"{STALE_PREFIX}{uuid.uuid4().hex}.txt")
    with open(path, "w") as f:
        f.write("stale")
    yield path
    if os.path.exists(path):
        os.remove(path)


def test_delete_files_removes_matching_downloads(stale_download, delete_files):
    """delete_files clears matching files from Downloads before the test runs."""
    assert not os.path.exists(stale_download)


def test_screenshot_saves_a_png(screenshot):
    """screenshot writes a PNG and returns its path."""
    path = screenshot("dte-ui-test-shared-fixtures")
    assert path.endswith(".png")
    assert os.path.getsize(path) > 0


def test_ci_and_mock_mode_options(opt_ci, opt_mock_mode):
    """Both option fixtures resolve; mach passes no options, so mock mode is replay."""
    assert isinstance(opt_ci, bool)
    assert opt_mock_mode == "replay"
