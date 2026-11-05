# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import os
import pathlib
import shutil
import tempfile
from unittest.mock import MagicMock, patch

import jsonschema
import pytest
from mozunit import MockedOpen, main
from taskgraph.util import json
from taskgraph.util.yaml import load_yaml

from gecko_taskgraph import GECKO, decision
from gecko_taskgraph.parameters import register_parameters

FAKE_GRAPH_CONFIG = {"product-dir": "browser", "taskgraph": {}}
TTC_FILE = os.path.join(os.getcwd(), "try_task_config.json")
PERFHERDER_SCHEMA_PATH = pathlib.Path(
    GECKO, "testing", "performance", "common", "performance-artifact-schema.json"
)


@pytest.fixture(scope="module", autouse=True)
def register():
    register_parameters()


@pytest.fixture(scope="module")
def options():
    return {
        "base_repository": "https://hg.mozilla.org/mozilla-unified",
        "base_ref": "mybranch",
        "base_rev": "1234",
        "head_repository": "https://hg.mozilla.org/mozilla-central",
        "head_rev": "abcd",
        "head_ref": "ef01",
        "head_tag": "",
        "message": "",
        "project": "mozilla-central",
        "pushlog_id": "143",
        "pushdate": 1503691511,
        "owner": "nobody@mozilla.com",
        "repository_type": "hg",
        "tasks_for": "hg-push",
        "level": "3",
    }


def test_write_artifact_json():
    data = [{"some": "data"}]
    tmpdir = tempfile.mkdtemp()
    try:
        decision.ARTIFACTS_DIR = os.path.join(tmpdir, "artifacts")
        decision.write_artifact("artifact.json", data)
        with open(os.path.join(decision.ARTIFACTS_DIR, "artifact.json")) as f:
            assert json.load(f) == data
    finally:
        if os.path.exists(tmpdir):
            shutil.rmtree(tmpdir)
        decision.ARTIFACTS_DIR = "artifacts"


def test_write_artifact_yml():
    data = [{"some": "data"}]
    tmpdir = tempfile.mkdtemp()
    try:
        decision.ARTIFACTS_DIR = os.path.join(tmpdir, "artifacts")
        decision.write_artifact("artifact.yml", data)
        assert load_yaml(decision.ARTIFACTS_DIR, "artifact.yml") == data
    finally:
        if os.path.exists(tmpdir):
            shutil.rmtree(tmpdir)
        decision.ARTIFACTS_DIR = "artifacts"


def _hg_metadata(desc):
    return {
        "desc": desc,
        "branch": "default",
        "extras": {"branch": "default", "git_commit": "bcde"},
    }


@patch("gecko_taskgraph.decision.get_hg_revision_metadata")
@patch("gecko_taskgraph.decision.get_repository")
@patch("gecko_taskgraph.decision.get_changed_files")
@pytest.mark.parametrize(
    "extra_options,commit_msg,ttc,expected",
    (
        pytest.param(
            {},
            None,
            None,
            {
                "pushlog_id": "143",
                "build_date": 1503691511,
                "files_changed": ["bar/baz.md", "foo.txt"],
                "hg_branch": "default",
                "moz_build_date": "20170825200511",
                "try_mode": None,
                "try_task_config": {},
                "head_git_rev": "bcde",
            },
            id="simple_options",
        ),
        pytest.param(
            {"owner": "ffxbld"},
            None,
            None,
            {
                "owner": "ffxbld@noreply.mozilla.org",
            },
            id="no_email_owner",
        ),
        pytest.param(
            {"project": "try"},
            "try: -b do -t all --artifact",
            None,
            {
                "try_mode": None,
                "try_task_config": {},
                "head_git_rev": "bcde",
            },
            id="try_options",
        ),
        pytest.param(
            {
                "project": "try",
                "allow_parameter_override": True,
            },
            "Fuzzy query=foo",
            {"tasks": ["a", "b"]},
            {
                "try_mode": "try_task_config",
                "try_task_config": {"tasks": ["a", "b"]},
                "head_git_rev": "bcde",
            },
            id="try_task_config",
        ),
    ),
)
def test_get_decision_parameters(
    mock_get_changed_files,
    mock_get_repository,
    mock_get_hg_revision_metadata,
    options,
    extra_options,
    commit_msg,
    ttc,
    expected,
):
    mock_get_hg_revision_metadata.return_value = _hg_metadata(
        commit_msg or "commit message"
    )

    mock_repo = MagicMock()
    mock_repo.default_branch = "baseref"
    mock_get_repository.return_value = mock_repo
    mock_get_changed_files.return_value = ["foo.txt", "bar/baz.md"]

    options.update(extra_options)
    contents = None
    if ttc:
        contents = json.dumps(ttc)
    with MockedOpen({TTC_FILE: contents}):
        params = decision.get_decision_parameters(FAKE_GRAPH_CONFIG, options)

    for key in expected:
        assert params[key] == expected[key], f"key {key} does not match!"


def _note_mock_repo(mock_get_repository, mock_get_hg_revision_metadata, note):
    mock_get_hg_revision_metadata.return_value = _hg_metadata("commit message")
    mock_repo = MagicMock()
    mock_repo.default_branch = "baseref"
    mock_repo.get_note.return_value = note
    mock_get_repository.return_value = mock_repo
    return mock_repo


@patch("gecko_taskgraph.decision.get_hg_revision_metadata")
@patch("gecko_taskgraph.decision.get_repository")
@patch("gecko_taskgraph.decision.get_changed_files")
def test_decision_parameters_note(
    mock_get_changed_files,
    mock_get_repository,
    mock_get_hg_revision_metadata,
    options,
):
    mock_get_changed_files.return_value = []
    mock_repo = _note_mock_repo(
        mock_get_repository,
        mock_get_hg_revision_metadata,
        note=json.dumps({"build_number": 99}),
    )
    opts = {**options, "allow_parameter_override": True}
    with MockedOpen({TTC_FILE: None}):
        params = decision.get_decision_parameters(FAKE_GRAPH_CONFIG, opts)
    mock_repo.get_note.assert_called_once_with(
        "refs/notes/decision-parameters", options["head_repository"]
    )
    assert params["build_number"] == 99


@patch("gecko_taskgraph.decision.get_hg_revision_metadata")
@patch("gecko_taskgraph.decision.get_repository")
@patch("gecko_taskgraph.decision.get_changed_files")
def test_decision_parameters_note_disallow_override(
    mock_get_changed_files,
    mock_get_repository,
    mock_get_hg_revision_metadata,
    options,
):
    mock_get_changed_files.return_value = []
    mock_repo = _note_mock_repo(
        mock_get_repository,
        mock_get_hg_revision_metadata,
        note=json.dumps({"build_number": 99}),
    )
    opts = {**options, "allow_parameter_override": False}
    with MockedOpen({TTC_FILE: None}):
        params = decision.get_decision_parameters(FAKE_GRAPH_CONFIG, opts)
    mock_repo.get_note.assert_not_called()
    assert params["build_number"] == 1


@patch("gecko_taskgraph.decision.get_hg_revision_metadata")
@patch("gecko_taskgraph.decision.get_repository")
@patch("gecko_taskgraph.decision.get_changed_files")
def test_decision_parameters_note_invalid_json(
    mock_get_changed_files,
    mock_get_repository,
    mock_get_hg_revision_metadata,
    options,
):
    mock_get_changed_files.return_value = []
    _note_mock_repo(
        mock_get_repository,
        mock_get_hg_revision_metadata,
        note="not valid json {",
    )
    opts = {**options, "allow_parameter_override": True}
    with MockedOpen({TTC_FILE: None}):
        with pytest.raises(
            Exception, match="Failed to parse refs/notes/decision-parameters as JSON"
        ):
            decision.get_decision_parameters(FAKE_GRAPH_CONFIG, opts)


@patch("gecko_taskgraph.decision.get_repository")
def test_decision_parameters_git_files_changed_base(mock_get_repository, options):
    mock_repo = MagicMock()
    mock_repo.NULL_REVISION = "0" * 40
    mock_repo.get_commit_message.return_value = "commit message"
    mock_repo.get_changed_files.return_value = ["python/mozboot/mozboot/debian.py"]
    mock_get_repository.return_value = mock_repo

    opts = {
        **options,
        "base_repository": "https://github.com/mozilla-firefox/firefox",
        "head_repository": "https://github.com/mozilla-firefox/firefox",
        "base_rev": "0" * 40,
        "project": "firefox",
        "repository_type": "git",
        "tasks_for": "github-push",
        "allow_parameter_override": True,
    }
    ttc = {"version": 2, "parameters": {"base_rev": "cafe"}}
    with MockedOpen({TTC_FILE: json.dumps(ttc)}):
        params = decision.get_decision_parameters(FAKE_GRAPH_CONFIG, opts)

    assert params["base_rev"] == "cafe"
    mock_repo.get_changed_files.assert_called_once_with(rev="abcd", base="cafe")
    assert params["files_changed"] == ["python/mozboot/mozboot/debian.py"]


@pytest.fixture
def bundle_env(monkeypatch, tmp_path):
    artifacts = tmp_path / "artifacts"
    monkeypatch.setattr(decision, "ARTIFACTS_DIR", str(artifacts))
    bundle_path = artifacts / "checkout.bundle"
    proc = MagicMock()

    def fake_popen(args, **kwargs):
        bundle_path.write_bytes(b"partial")
        kwargs["stdout"].write(b"680 changesets found\n")
        return proc

    popen = MagicMock(side_effect=fake_popen)
    monkeypatch.setattr(decision.subprocess, "Popen", popen)
    return popen, proc, bundle_path


def test_source_bundle_success(bundle_env):
    popen, proc, bundle_path = bundle_env
    proc.wait.return_value = 0

    with decision.source_bundle("abcd", "hg"):
        pass

    args = popen.call_args[0][0]
    assert args[:6] == ["hg", "--cwd", decision.GECKO, "bundle", "--type", "gzip-v2"]
    assert args[args.index("--rev") + 1] == "abcd"
    assert args[-1] == str(bundle_path)
    assert bundle_path.exists()


def test_source_bundle_hg_failure(bundle_env):
    popen, proc, bundle_path = bundle_env
    proc.wait.return_value = 1

    with decision.source_bundle("abcd", "hg"):
        pass

    assert not bundle_path.exists()


def test_source_bundle_body_raises(bundle_env):
    popen, proc, bundle_path = bundle_env
    proc.wait.return_value = -9

    with pytest.raises(RuntimeError):
        with decision.source_bundle("abcd", "hg"):
            raise RuntimeError("graph generation failed")

    assert not bundle_path.exists()


def test_source_bundle_popen_error(bundle_env):
    popen, proc, bundle_path = bundle_env
    popen.side_effect = OSError("hg not found")
    body_ran = False

    with decision.source_bundle("abcd", "hg"):
        body_ran = True

    assert body_ran


def test_source_bundle_not_hg(bundle_env):
    popen, proc, bundle_path = bundle_env

    with decision.source_bundle("abcd", "git"):
        pass

    assert not bundle_path.exists()


@pytest.mark.parametrize(
    "trust_domain,expected_suite_extra,forbidden_keys",
    (
        pytest.param(
            "gecko",
            {
                "monitor": True,
                "alertNotifyEmails": ["release+gecko-decision-alerts@mozilla.com"],
            },
            ["shouldAlert"],
            id="gecko",
        ),
        pytest.param(
            "comm",
            {"shouldAlert": False},
            ["monitor", "alertNotifyEmails"],
            id="comm",
        ),
    ),
)
def test_build_decision_perfherder_data(
    trust_domain, expected_suite_extra, forbidden_keys
):
    params_time = 1.5
    taskgraph_time = 2.5

    data = decision.build_decision_perfherder_data(
        trust_domain, params_time, taskgraph_time
    )

    assert data["framework"] == {"name": "build_metrics"}
    suite = data["suites"][0]
    assert suite["name"] == "decision"
    assert suite["value"] == params_time + taskgraph_time
    assert suite["subtests"] == [
        {"name": "parameters", "value": params_time, "lowerIsBetter": True},
        {"name": "taskgraph", "value": taskgraph_time, "lowerIsBetter": True},
    ]
    for key, value in expected_suite_extra.items():
        assert suite[key] == value
    for key in forbidden_keys:
        assert key not in suite

    with open(PERFHERDER_SCHEMA_PATH) as f:
        schema = json.load(f)
    jsonschema.validate(data, schema)


if __name__ == "__main__":
    main()
