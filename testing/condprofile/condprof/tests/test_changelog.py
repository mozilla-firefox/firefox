# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json

import mozunit

from condprof.changelog import Changelog

TC_KEYS = ("TC_SCHEDULER_ID", "TASK_ID", "TC_OWNER", "TC_SOURCE", "TC_PROJECT")


def test_new_changelog_saves_entry_with_taskcluster_env(tmp_path, monkeypatch):
    for key in TC_KEYS:
        monkeypatch.delenv(key, raising=False)
    monkeypatch.setenv("TASK_ID", "abc")
    changelog = Changelog(str(tmp_path))
    changelog.append("update", platform="linux64", name="settled")
    changelog.save()
    data = json.loads((tmp_path / "changelog.json").read_text())
    (entry,) = data["changes"]
    assert entry["action"] == "update"
    assert entry["platform"] == "linux64"
    assert entry["name"] == "settled"
    assert entry["TASK_ID"] == "abc"
    assert "TC_OWNER" not in entry
    assert isinstance(entry["when"], float)


def test_existing_changelog_history_is_newest_first(tmp_path):
    (tmp_path / "changelog.json").write_text(
        json.dumps({"changes": [{"when": 1}, {"when": 3}, {"when": 2}]})
    )
    changelog = Changelog(str(tmp_path))
    assert [entry["when"] for entry in changelog.history()] == [3, 2, 1]


def test_existing_changelog_without_changes(tmp_path):
    (tmp_path / "changelog.json").write_text(json.dumps({"other": 1}))
    changelog = Changelog(str(tmp_path))
    assert changelog.history() == []


def test_save_to_other_directory_writes_changelog_there(tmp_path):
    first = tmp_path / "first"
    second = tmp_path / "second"
    first.mkdir()
    second.mkdir()
    changelog = Changelog(str(first))
    changelog.append("update")
    changelog.save(str(first))
    changelog.save(str(second))
    assert (first / "changelog.json").exists()
    assert json.loads((second / "changelog.json").read_text())["changes"]


if __name__ == "__main__":
    mozunit.main()
