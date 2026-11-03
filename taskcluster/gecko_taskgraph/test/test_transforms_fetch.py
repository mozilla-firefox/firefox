# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main
from taskgraph.util.schema import SchemaValidationError

from gecko_taskgraph.transforms import job  # noqa: F401
from gecko_taskgraph.transforms.fetch import configure_fetch

REVISION = "b0fd1b23dcb06bb68b5d60d18c7b1524c65a94af"


def configure_git_fetch(fetch_mode=None):
    fetch = {
        "type": "git",
        "repo": "https://huggingface.co/Mozilla/smart-tab-topic",
        "revision": REVISION,
        "path-prefix": "onnx-models/Mozilla/smart-tab-topic/main",
    }
    if fetch_mode is not None:
        fetch["fetch-mode"] = fetch_mode
    return configure_fetch(None, "git", "smart-tab-topic", fetch)


def test_git_fetch_without_fetch_mode_uses_clone_command():
    result = configure_git_fetch()

    assert "--fetch-mode" not in result["command"]
    assert not any(data.startswith("fetch-mode=") for data in result["digest_data"])


def test_git_fetch_clone_mode_is_passed_to_fetch_content():
    result = configure_git_fetch("clone")

    assert result["command"][-2:] == ["--fetch-mode", "clone"]


def test_git_fetch_fetch_mode_is_passed_to_fetch_content():
    result = configure_git_fetch("fetch")

    assert result["command"][-2:] == ["--fetch-mode", "fetch"]


@pytest.mark.parametrize("fetch_mode", ["clone", "fetch"])
def test_git_fetch_mode_keeps_default_digest(fetch_mode):
    mode_digest = configure_git_fetch(fetch_mode)["digest_data"]

    assert mode_digest == configure_git_fetch()["digest_data"]


def test_git_fetch_rejects_unknown_fetch_mode():
    with pytest.raises(SchemaValidationError):
        configure_git_fetch("bogus")


if __name__ == "__main__":
    main()
