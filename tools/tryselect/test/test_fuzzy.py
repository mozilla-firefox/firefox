# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os

import mozunit
import pytest


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize("show_chunk_numbers", [True, False])
def test_query_paths(run_mach, capfd, show_chunk_numbers):
    cmd = [
        "try",
        "fuzzy",
        "--no-push",
        "-q",
        "^test-linux '64-qr/debug-mochitest-chrome-1proc-",
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
    assert tasks == ["test-linux1804-64-qr/debug-mochitest-chrome-1proc-%s" % chunk]


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize("show_chunk_numbers", [True, False])
def test_query_paths_no_chunks(run_mach, capfd, show_chunk_numbers):
    cmd = [
        "try",
        "fuzzy",
        "--no-push",
        "-q",
        "^test-linux '64-qr/debug-cppunittest",
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
    assert tasks == ["test-linux1804-64-qr/debug-cppunittest-1proc"]


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize("variant", ["", "spi-nw"])
def test_query_paths_variants(run_mach, capfd, variant):
    if variant:
        variant = "-%s" % variant

    cmd = [
        "try",
        "fuzzy",
        "--no-push",
        "-q",
        "^test-linux '64-qr/debug-mochitest-browser-chrome%s-" % variant,
    ]
    assert run_mach(cmd) == 0

    output = capfd.readouterr().out
    print(output)

    if variant:
        expected = ["test-linux1804-64-qr/debug-mochitest-browser-chrome%s-*" % variant]
    else:
        expected = [
            "test-linux1804-64-qr/debug-mochitest-browser-chrome-ioi",
            "test-linux1804-64-qr/debug-mochitest-browser-chrome-spi-nw-*",
            "test-linux1804-64-qr/debug-mochitest-browser-chrome-swr-*",
            "test-linux1804-64-qr/debug-mochitest-browser-chrome-vt",
        ]

    delim = "Calculated try_task_config.json:"
    index = output.find(delim)
    result = json.loads(output[index + len(delim) :])
    tasks = result["parameters"]["try_task_config"]["tasks"]
    assert sorted(tasks) == sorted(expected)


@pytest.mark.skipif(os.name == "nt", reason="fzf not installed on host")
@pytest.mark.parametrize("full", [True, False])
def test_query(run_mach, capfd, full):
    cmd = ["try", "fuzzy", "--no-push", "-q", "'source-test-python-taskgraph-tests-py3"]
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
    assert tasks == ["source-test-python-taskgraph-tests-py3"]


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
        "^test-linux '64-qr/debug- !http !spi !swr !nofis !headless !xorig !ioi",
    ]
    if tag == "not_a_valid_tag":
        assert run_mach(cmd) == 1
    else:
        assert run_mach(cmd) == 0

        output = capfd.readouterr().out
        print(output)

        expected = [
            "test-linux1804-64-qr/debug-mochitest-browser-chrome-vt",
            "test-linux1804-64-qr/debug-mochitest-devtools-chrome-*",
            "test-linux1804-64-qr/debug-mochitest-chrome-1proc-*",
            "test-linux1804-64-qr/debug-mochitest-chrome-gpu-1proc",
            "test-linux1804-64-qr/debug-mochitest-plain-*",
            "test-linux1804-64-qr/debug-mochitest-plain-gpu",
            "test-linux1804-64-qr/debug-xpcshell-*",
            "test-linux1804-64-qr/debug-test-verify",
            "test-linux1804-64-qr/debug-test-verify-gpu",
            "test-linux1804-64-qr/debug-test-verify-wpt",
        ]

        delim = "Calculated try_task_config.json:"
        index = output.find(delim)
        result = json.loads(output[index + len(delim) :])
        tasks = result["parameters"]["try_task_config"]["tasks"]

        # If enough test files change, the test-verify task may get chunked.
        def canonical(tasks):
            return sorted(t.rstrip("-*") for t in tasks)

        assert canonical(tasks) == canonical(expected)


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
        "^test-linux '64-qr/debug- !http !spi !swr !nofis !headless !xorig",
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


if __name__ == "__main__":
    mozunit.main()
