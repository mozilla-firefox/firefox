# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import json
import os
from unittest.mock import MagicMock

import mozunit
import pytest
from tryselect.selectors.fuzzy import FuzzyParser


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize("show_chunk_numbers", [True, False])
def test_query_paths(run_mach, capfd, show_chunk_numbers):
    cmd = [
        "try",
        "fuzzy",
        "--no-push",
        "-q",
        "^test-linux '64/debug-mochitest-chrome-1proc-",
        "caps/tests/mochitest/test_addonMayLoad.html",
    ]
    chunk = "*"
    if show_chunk_numbers:
        cmd.append("--show-chunk-numbers")
        chunk = "1"

    assert run_mach(cmd) == 0

    output = capfd.readouterr().out
    print(output)

    delim = "Calculated try_task_config.json:"
    index = output.find(delim)
    result = json.loads(output[index + len(delim) :])

    # If there are more than one tasks here, it means that something went wrong
    # with the path filtering.
    tasks = result["parameters"]["try_task_config"]["tasks"]
    assert tasks == [f"test-linux2404-64/debug-mochitest-chrome-1proc-{chunk}"]


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize("show_chunk_numbers", [True, False])
def test_query_paths_no_chunks(run_mach, capfd, show_chunk_numbers):
    cmd = [
        "try",
        "fuzzy",
        "--no-push",
        "-q",
        "^test-linux '64/debug-cppunittest",
    ]
    if show_chunk_numbers:
        cmd.append("--show-chunk-numbers")

    assert run_mach(cmd) == 0

    output = capfd.readouterr().out
    print(output)

    delim = "Calculated try_task_config.json:"
    index = output.find(delim)
    result = json.loads(output[index + len(delim) :])

    # If there are more than one tasks here, it means that something went wrong
    # with the path filtering.
    tasks = result["parameters"]["try_task_config"]["tasks"]
    assert tasks == ["test-linux2404-64/debug-cppunittest-1proc"]


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize("variant", [""])
def test_query_paths_variants(monkeypatch, run_mach, capfd, variant):
    # Freeze time to avoid test failures due to an expired variant
    datetime_mock = MagicMock(wraps=datetime.datetime)
    datetime_mock.today.return_value = datetime.datetime.strptime(
        "2025-08-01", "%Y-%m-%d"
    )
    monkeypatch.setattr(datetime, "datetime", datetime_mock)
    # also patch the cache key since faking the date means we don't want to reuse another graph
    monkeypatch.setattr(
        "tryselect.tasks.cache_key",
        lambda attr, *args: f"{attr}-test_query_paths_variants",
    )

    if variant:
        variant = f"-{variant}"

    cmd = [
        "try",
        "fuzzy",
        "--no-push",
        "-q",
        f"^test-linux !ioi !vt '64/debug-mochitest-browser-chrome{variant}-",
    ]
    assert run_mach(cmd) == 0

    output = capfd.readouterr().out
    print(output)

    delim = "Calculated try_task_config.json:"
    index = output.find(delim)
    result = json.loads(output[index + len(delim) :])
    tasks = result["parameters"]["try_task_config"]["tasks"]

    if variant:
        assert tasks == [f"test-linux2404-64/debug-mochitest-browser-chrome{variant}-*"]
    else:
        # Specific variants differ across branches; check all tasks match the pattern.
        assert len(tasks) >= 1
        assert all("debug-mochitest-browser-chrome-" in t for t in tasks)


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize("full", [True, False])
def test_query(run_mach, capfd, full):
    cmd = ["try", "fuzzy", "--no-push", "-q", "'source-test-python-taskgraph-tests"]
    if full:
        cmd.append("--full")
    assert run_mach(cmd) == 0

    output = capfd.readouterr().out
    print(output)

    delim = "Calculated try_task_config.json:"
    index = output.find(delim)
    result = json.loads(output[index + len(delim) :])

    # Should only ever mach one task exactly.
    tasks = result["parameters"]["try_task_config"]["tasks"]
    assert tasks == ["source-test-python-taskgraph-tests"]


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize("include_duplicates", [True, False])
def test_query_duplicates(run_mach, capfd, include_duplicates):
    cmd = [
        "try",
        "fuzzy",
        "--no-push",
        "--full",
        "-q",
        "^build-components-browser-state !-nightly !-beta !-release",
    ]
    if include_duplicates:
        cmd.append("--include-duplicates")
    assert run_mach(cmd) == 0

    output = capfd.readouterr().out
    print(output)

    delim = "Calculated try_task_config.json:"
    index = output.find(delim)
    result = json.loads(output[index + len(delim) :])

    tasks = result["parameters"]["try_task_config"]["tasks"]
    expected = ["build-components-browser-state"]
    if include_duplicates:
        expected.append("build-components-browser-state-appservices-in-tree")
    assert sorted(tasks) == expected


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize("tag", ["webextensions", "not_a_valid_tag"])
def test_query_tags(run_mach, capfd, tag):
    cmd = [
        "try",
        "fuzzy",
        "--no-push",
        "--tag",
        tag,
        "-q",
        "^test-linux '64/debug- !http !spi !swr !nofis !headless !xorig !async !ioi !vt",
    ]
    if tag == "not_a_valid_tag":
        assert run_mach(cmd) == 1
    else:
        assert run_mach(cmd) == 0

        output = capfd.readouterr().out
        print(output)

        delim = "Calculated try_task_config.json:"
        index = output.find(delim)
        result = json.loads(output[index + len(delim) :])
        tasks = result["parameters"]["try_task_config"]["tasks"]

        assert len(tasks) > 0
        if tag == "webextensions":
            # devtools-chrome is not tagged 'webextensions' and must not appear
            assert not any("devtools-chrome" in t for t in tasks)


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize(
    "tag",
    [
        {"tags": ["webextensions"], "results": 0},
        {"tags": ["webextensions", "devtools"], "results": 0},
    ],
)
def test_query_multiple_tags(run_mach, capfd, tag):
    cmd = [
        "try",
        "fuzzy",
        "--no-push",
        "-q",
        "^test-linux '64/debug- !http !spi !swr !nofis !headless !xorig",
    ]
    for t in tag["tags"]:
        cmd.extend(["--tag", t])

    if tag["results"] == 0:
        assert run_mach(cmd) == tag["results"]
    else:
        with pytest.raises(SystemExit) as result:
            run_mach(cmd)
        assert result.value.code == tag["results"]

    output = capfd.readouterr().out
    print(output)


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
def test_target_tasks_method_pre_filter(run_mach, capfd):
    cmd = [
        "try",
        "fuzzy",
        "--no-push",
        "--target-tasks-method=os-integration",
        "-xq",
        "^test 'talos",
    ]
    assert run_mach(cmd) == 0

    output = capfd.readouterr().out
    print(output)

    delim = "Calculated try_task_config.json:"
    index = output.find(delim)
    result = json.loads(output[index + len(delim) :])
    assert "target_tasks_method" not in result["parameters"]

    tasks = result["parameters"]["try_task_config"]["tasks"]

    # Assert we didn't select any unexpected talos tests, which implies the
    # os-integration pre-filtering worked. Talos was chosen because the tasks
    # we add to os-integration are unlikely to change much, but another type
    # of task could be used instead if needed.
    expected_talos_tests = {"other", "xperf", "webgl"}
    for label in tasks:
        assert any(e in label for e in expected_talos_tests)


def test_rebuild_and_repeat_tests_are_distinct():
    args = FuzzyParser().parse_args(["--rebuild", "2", "--repeat-tests", "3"])
    assert args.rebuild == 2
    assert args.repeat_tests == 3


if __name__ == "__main__":
    mozunit.main()
