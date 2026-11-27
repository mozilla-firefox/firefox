# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os

import mozunit
import pytest

from condprof import customization


def test_get_customizations_lists_bundled_files():
    names = sorted(
        os.path.basename(path) for path in customization.get_customizations()
    )
    assert names == ["default.json", "webext.json", "youtube.json"]


def test_find_customization_by_name():
    assert customization.find_customization("default") == os.path.join(
        customization.HERE, "default.json"
    )
    assert customization.find_customization("missing") is None


def test_find_customization_by_path(tmp_path):
    custom = tmp_path / "mine.json"
    custom.write_text("{}")
    assert customization.find_customization(str(custom)) == str(custom)
    assert customization.find_customization(str(tmp_path / "mine")) == str(custom)


def test_get_customization_loads_by_name_or_path_and_raises_if_missing(tmp_path):
    assert customization.get_customization("default")["name"] == "default"
    custom = tmp_path / "mine.json"
    custom.write_text(json.dumps({"name": "mine"}))
    assert customization.get_customization(str(custom)) == {"name": "mine"}
    with pytest.raises(OSError, match="missing"):
        customization.get_customization("missing")


if __name__ == "__main__":
    mozunit.main()
