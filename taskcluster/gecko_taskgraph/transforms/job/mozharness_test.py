# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import os
import re
from typing import Literal, Optional

from taskgraph.util import json
from taskgraph.util.schema import Schema
from taskgraph.util.taskcluster import get_artifact_path

from gecko_taskgraph.transforms.job import configure_taskdesc_for_run, run_job_using
from gecko_taskgraph.transforms.job.common import (
    docker_worker_add_artifacts,
    generic_worker_add_artifacts,
    get_expiration,
    support_vcs_checkout,
)
from gecko_taskgraph.transforms.test import TestDescriptionSchema, normpath
from gecko_taskgraph.util.chunking import get_test_tags
from gecko_taskgraph.util.perftest import is_external_browser

VARIANTS = [
    "shippable",
    "shippable-qr",
    "shippable-lite",
    "shippable-lite-qr",
    "devedition",
    "pgo",
    "asan",
    "stylo",
    "qr",
    "ccov",
]


def get_variant(test_platform):
    for v in VARIANTS:
        if f"-{v}/" in test_platform:
            return v
    return ""


class MozharnessTestSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    test_platform: str
    mozharness: TestDescriptionSchema.__annotations__["mozharness"]  # noqa: F821
    docker_image: TestDescriptionSchema.__annotations__[
        "docker_image"  # noqa: F821
    ]
    loopback_video: TestDescriptionSchema.__annotations__["loopback_video"]  # noqa: F821
    loopback_audio: TestDescriptionSchema.__annotations__["loopback_audio"]  # noqa: F821
    max_run_time: TestDescriptionSchema.__annotations__["max_run_time"]  # noqa: F821
    retry_exit_status: TestDescriptionSchema.__annotations__["retry_exit_status"] = None


class MozharnessTestRunSchema(Schema, kw_only=True):
    using: Literal["mozharness-test"]
    test: MozharnessTestSchema  # noqa: F821
    # Base work directory used to set up the task.
    workdir: Optional[str] = None
    # How to clone the upstream repo for the checkout, either "hg" or "git"
    # (default: "git")
    clone_with: Optional[Literal["hg", "git"]] = "git"


def test_packages_url(taskdesc):
    """Account for different platforms that name their test packages differently"""
    artifact_path = "target.test_packages.json"
    # for android shippable we need to add 'en-US' to the artifact url
    test = taskdesc["run"]["test"]
    if (
        "android" in test["test-platform"]
        and (
            get_variant(test["test-platform"])
            in ("shippable", "shippable-qr", "shippable-lite", "shippable-lite-qr")
        )
        and not is_external_browser(test.get("try-name", ""))
    ):
        artifact_path = os.path.join("en-US", artifact_path)
    return f"<build/{get_artifact_path(taskdesc, artifact_path)}>"


def installer_url(taskdesc):
    test = taskdesc["run"]["test"]
    mozharness = test["mozharness"]

    if "installer-url" in mozharness:
        return mozharness["installer-url"]
    upstream_task = "build-signing" if mozharness["requires-signed-builds"] else "build"
    return f"<{upstream_task}/{mozharness['build-artifact-name']}>"


@run_job_using(
    "docker-worker",
    "mozharness-test",
    schema=MozharnessTestRunSchema,
    defaults={"clone-with": "git"},
)
def mozharness_test_on_docker(config, job, taskdesc):
    run = job["run"]
    test = taskdesc["run"]["test"]
    mozharness = test["mozharness"]
    worker = taskdesc["worker"] = job["worker"]

    # apply some defaults
    worker["docker-image"] = test["docker-image"]
    worker["allow-ptrace"] = True  # required for all tests, for crashreporter
    worker["loopback-video"] = test["loopback-video"]
    worker["loopback-audio"] = test["loopback-audio"]
    worker["max-run-time"] = test["max-run-time"]
    worker["retry-exit-status"] = test["retry-exit-status"]
    if "android-em-" in test["test-platform"]:
        worker["kvm"] = True

    artifacts = [
        # (artifact name prefix, in-image path)
        ("public/logs", "{workdir}/workspace/logs/".format(**run)),
        ("public/test", "{workdir}/artifacts/".format(**run)),
        (
            "public/test_info",
            "{workdir}/workspace/build/blobber_upload_dir/".format(**run),
        ),
    ]

    installer = installer_url(taskdesc)

    worker.setdefault("artifacts", [])
    worker["artifacts"].extend([
        {
            "name": prefix,
            "path": os.path.join("{workdir}/workspace".format(**run), path),
            "type": "directory",
            "expires-after": get_expiration(config, "default"),
        }
        for (prefix, path) in artifacts
    ])
    worker["artifacts"].append({
        "name": "public/xsession-errors.log",
        "path": "{workdir}/.xsession-errors".format(**run),
        "type": "file",
        "expires-after": get_expiration(config, "default"),
    })
    docker_worker_add_artifacts(config, job, taskdesc)

    env = worker.setdefault("env", {})
    env.update({
        "MOZHARNESS_CONFIG": " ".join(mozharness["config"]),
        "MOZHARNESS_SCRIPT": mozharness["script"],
        "NEED_WINDOW_MANAGER": "true",
        "ENABLE_E10S": str(bool(test.get("e10s"))).lower(),
        "WORKING_DIR": "/builds/worker",
    })

    env["PYTHON"] = "python3"

    if test.get("docker-image", {}).get("in-tree") == "ubuntu2404-test":
        env["NEED_PIPEWIRE"] = "true"

    # Set MOZ_ENABLE_WAYLAND env variables to enable Wayland backend.
    if "wayland" in job["label"]:
        env["MOZ_ENABLE_WAYLAND"] = "1"
    else:
        assert "MOZ_ENABLE_WAYLAND" not in env

    if mozharness.get("mochitest-flavor"):
        env["MOCHITEST_FLAVOR"] = mozharness["mochitest-flavor"]

    if mozharness["set-moz-node-path"]:
        env["MOZ_NODE_PATH"] = "/usr/local/bin/node"

    if "actions" in mozharness:
        env["MOZHARNESS_ACTIONS"] = " ".join(mozharness["actions"])

    # handle some of the mozharness-specific options
    if test["reboot"]:
        raise Exception(
            "reboot: {} not supported on generic-worker".format(test["reboot"])
        )

    if not test["checkout"]:
        # Support vcs checkouts regardless of whether the task runs from
        # source or not in case it is needed on an interactive loaner.
        support_vcs_checkout(config, job, taskdesc, config.repo_configs)

    # If we have a source checkout, run mozharness from it instead of
    # downloading a zip file with the same content.
    if test["checkout"]:
        env["MOZHARNESS_PATH"] = "{workdir}/checkouts/gecko/testing/mozharness".format(
            **run
        )
    else:
        mozharness_url = f"<build/{get_artifact_path(taskdesc, 'mozharness.zip')}>"
        env["MOZHARNESS_URL"] = {"artifact-reference": mozharness_url}

    extra_config = {
        "installer_url": installer,
        "test_packages_url": test_packages_url(taskdesc),
    }
    env["EXTRA_MOZHARNESS_CONFIG"] = {
        "artifact-reference": json.dumps(extra_config, sort_keys=True)
    }

    # Bug 1634554 - pass in decision task artifact URL to mozharness for WPT.
    # Bug 1645974 - test-verify-wpt and test-coverage-wpt need artifact URL.
    if "web-platform-tests" in test["suite"] or re.match(
        "test-(coverage|verify)-wpt", test["suite"]
    ):
        env["TESTS_BY_MANIFEST_URL"] = {
            "artifact-reference": "<decision/public/tests-by-manifest.json.gz>"
        }

    command = [
        "{workdir}/bin/test-linux.sh".format(**run),
    ]
    command.extend(mozharness.get("extra-options", []))

    if test.get("test-manifests"):
        env["MOZHARNESS_TEST_PATHS"] = json.dumps(
            {test["suite"]: test["test-manifests"]}, sort_keys=True
        )

    test_tags = get_test_tags(config, env)
    if test_tags:
        env["MOZHARNESS_TEST_TAG"] = json.dumps(test_tags)
        command.extend([f"--tag={x}" for x in test_tags])

    # TODO: remove the need for run['chunked']
    elif mozharness.get("chunked") or test["chunks"] > 1:
        command.append("--total-chunk={}".format(test["chunks"]))
        command.append("--this-chunk={}".format(test["this-chunk"]))

    if test.get("timeoutfactor"):
        command.append("--timeout-factor={}".format(test["timeoutfactor"]))

    if "download-symbols" in mozharness:
        download_symbols = mozharness["download-symbols"]
        download_symbols = {True: "true", False: "false"}.get(
            download_symbols, download_symbols
        )
        command.append("--download-symbols=" + download_symbols)

    use_caches = test.get("use-caches", ["checkout", "pip", "uv"])
    job["run"] = {
        "workdir": run["workdir"],
        "clone-with": run["clone-with"],
        "tooltool-downloads": mozharness["tooltool-downloads"],
        "checkout": test["checkout"],
        "command": command,
        "use-caches": use_caches,
        "using": "run-task",
    }
    configure_taskdesc_for_run(config, job, taskdesc, worker["implementation"])


@run_job_using(
    "generic-worker",
    "mozharness-test",
    schema=MozharnessTestRunSchema,
    defaults={"clone-with": "git"},
)
def mozharness_test_on_generic_worker(config, job, taskdesc):
    run = job["run"]
    test = taskdesc["run"]["test"]
    mozharness = test["mozharness"]
    worker = taskdesc["worker"] = job["worker"]

    bitbar_script = "test-linux.sh"

    is_macosx = worker["os"] == "macosx"
    is_windows = worker["os"] == "windows"
    is_linux = worker["os"] == "linux" or worker["os"] in [
        "linux-bitbar",
        "linux-lambda",
    ]
    is_bitbar = worker["os"] == "linux-bitbar"
    is_lambda = worker["os"] == "linux-lambda"
    assert is_macosx or is_windows or is_linux

    artifacts = [
        {
            "name": "public/logs",
            "path": "logs",
            "type": "directory",
            "expires-after": get_expiration(config, "default"),
        }
    ]

    generic_worker_add_artifacts(config, job, taskdesc)

    # jittest doesn't have blob_upload_dir
    if test["test-name"] != "jittest":
        artifacts.append({
            "name": "public/test_info",
            "path": "build/blobber_upload_dir",
            "type": "directory",
            "expires-after": get_expiration(config, "default"),
        })

    if is_bitbar or is_lambda:
        artifacts = [
            {
                "name": "public/test/",
                "path": "artifacts/public",
                "type": "directory",
                "expires-after": get_expiration(config, "default"),
            },
            {
                "name": "public/logs/",
                "path": "workspace/logs",
                "type": "directory",
                "expires-after": get_expiration(config, "default"),
            },
            {
                "name": "public/test_info/",
                "path": "workspace/build/blobber_upload_dir",
                "type": "directory",
                "expires-after": get_expiration(config, "default"),
            },
        ]

    installer = installer_url(taskdesc)

    worker["os-groups"] = test["os-groups"]

    # run-as-administrator is a feature for workers with UAC enabled and as such should not be
    # included in tasks on workers that have UAC disabled. Currently UAC is only enabled on
    # gecko Windows 10 workers, however this may be subject to change. Worker type
    # environment definitions can be found in https://github.com/mozilla-releng/OpenCloudConfig
    # See https://docs.microsoft.com/en-us/windows/desktop/secauthz/user-account-control
    # for more information about UAC.
    if test.get("run-as-administrator", False):
        if job["worker-type"].startswith("win10-64") or job["worker-type"].startswith(
            "win11-64"
        ):
            worker["run-as-administrator"] = True
        else:
            raise Exception(
                "run-as-administrator not supported on {}".format(job["worker-type"])
            )

    if test["reboot"]:
        raise Exception(
            "reboot: {} not supported on generic-worker".format(test["reboot"])
        )

    worker["max-run-time"] = test["max-run-time"]
    worker["retry-exit-status"] = test["retry-exit-status"]
    worker.setdefault("artifacts", [])
    worker["artifacts"].extend(artifacts)

    env = worker.setdefault("env", {})
    env["GECKO_HEAD_REPOSITORY"] = config.params["head_repository"]
    env["GECKO_HEAD_REV"] = config.params["head_rev"]

    # this list will get cleaned up / reduced / removed in bug 1354088
    if is_macosx:
        env.update({
            "LC_ALL": "en_US.UTF-8",
            "LANG": "en_US.UTF-8",
            "MOZ_NODE_PATH": "/usr/local/bin/node",
            "PATH": "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
            "SHELL": "/bin/bash",
        })
    elif is_bitbar or is_lambda:
        env.update({
            "LANG": "en_US.UTF-8",
            "MOZHARNESS_CONFIG": " ".join(mozharness["config"]),
            "MOZHARNESS_SCRIPT": mozharness["script"],
            "MOZHARNESS_URL": {
                "artifact-reference": "<build/public/build/mozharness.zip>"
            },
            "NEED_XVFB": "false",
            "XPCOM_DEBUG_BREAK": "warn",
            "NO_FAIL_ON_TEST_ERRORS": "1",
            "MOZ_HIDE_RESULTS_TABLE": "1",
            "MOZ_NODE_PATH": "/usr/local/bin/node",
            "PYTHONUNBUFFERED": "1",
            "TASKCLUSTER_WORKER_TYPE": job["worker-type"],
        })

    extra_config = {
        "installer_url": installer,
        "test_packages_url": test_packages_url(taskdesc),
    }
    env["EXTRA_MOZHARNESS_CONFIG"] = {
        "artifact-reference": json.dumps(extra_config, sort_keys=True)
    }

    # Bug 1634554 - pass in decision task artifact URL to mozharness for WPT.
    # Bug 1645974 - test-verify-wpt and test-coverage-wpt need artifact URL.
    if "web-platform-tests" in test["suite"] or re.match(
        "test-(coverage|verify)-wpt", test["suite"]
    ):
        env["TESTS_BY_MANIFEST_URL"] = {
            "artifact-reference": "<decision/public/tests-by-manifest.json.gz>"
        }

    if is_windows:
        script = "mozharness\\scripts\\" + normpath(mozharness["script"])
        if job.get("use-python", "system") == "system":
            py_binary = "c:\\mozilla-build\\python3\\python3.exe"
            mh_command = [py_binary, "-u", script]
        else:
            # A bare `python3` is resolved by CreateProcess against the calling
            # executable's directory (mozilla-build) before PATH, so it would
            # ignore the fetched CI Python that run-task puts first on PATH.
            # `env` resolves python3 through PATH itself and execs it by full
            # path, so the fetched Python is used -- matching the other
            # platforms and the (uv-created) venv.
            py_binary = "python3"
            mh_command = ["env", "python3", "-u", script]
    elif is_bitbar or is_lambda:
        py_binary = "python3"
        mh_command = ["bash", f"./{bitbar_script}"]
    elif is_macosx:
        if job.get("use-python", "system") == "system":
            py_binary = "/usr/local/bin/{}".format("python3")
        else:
            # run-task prepends $MOZ_PYTHON_HOME/bin to PATH, so resolving
            # python3 via PATH picks the fetched CI Python rather than the
            # absolute system framework Python. This keeps mozharness on the
            # same interpreter as the (uv-created) venv; otherwise spawned
            # subprocesses mix the two stdlibs and fail on macOS.
            py_binary = "python3"
        mh_command = [
            py_binary,
            "-u",
            "mozharness/scripts/" + mozharness["script"],
        ]
    else:
        # is_linux
        if job.get("use-python", "system") == "system":
            py_binary = "/usr/bin/python3"
        else:
            # run-task prepends $MOZ_PYTHON_HOME/bin to PATH, so a bare python3
            # resolves to the fetched CI Python.
            py_binary = "python3"
        mh_command = [
            py_binary,
            "-u",
            "mozharness/scripts/" + mozharness["script"],
        ]

    env["PYTHON"] = py_binary

    for mh_config in mozharness["config"]:
        cfg_path = "mozharness/configs/" + mh_config
        if is_windows:
            cfg_path = normpath(cfg_path)
        mh_command.extend(["--cfg", cfg_path])
    mh_command.extend(mozharness.get("extra-options", []))
    if mozharness.get("download-symbols"):
        if isinstance(mozharness["download-symbols"], str):
            mh_command.extend(["--download-symbols", mozharness["download-symbols"]])
        else:
            mh_command.extend(["--download-symbols", "true"])
    if mozharness.get("include-blob-upload-branch"):
        mh_command.append("--blob-upload-branch=" + config.params["project"])

    if test.get("test-manifests"):
        env["MOZHARNESS_TEST_PATHS"] = json.dumps(
            {test["suite"]: test["test-manifests"]}, sort_keys=True
        )

    test_tags = get_test_tags(config, env)
    if test_tags:
        # do not add --tag for perf tests
        if test["suite"] not in ["talos", "raptor"]:
            env["MOZHARNESS_TEST_TAG"] = json.dumps(test_tags)
            mh_command.extend([f"--tag={x}" for x in test_tags])

    # TODO: remove the need for run['chunked']
    elif mozharness.get("chunked") or test["chunks"] > 1:
        mh_command.append("--total-chunk={}".format(test["chunks"]))
        mh_command.append("--this-chunk={}".format(test["this-chunk"]))

    if test.get("timeoutfactor"):
        mh_command.append("--timeout-factor={}".format(test["timeoutfactor"]))

    worker["mounts"] = [
        {
            "directory": "mozharness",
            "content": {
                "artifact": get_artifact_path(taskdesc, "mozharness.zip"),
                "task-id": {"task-reference": "<build>"},
            },
            "format": "zip",
        }
    ]
    if is_bitbar or is_lambda:
        a_url = config.params.file_url(
            f"taskcluster/scripts/tester/{bitbar_script}",
        )
        worker["mounts"] = [
            {
                "file": bitbar_script,
                "content": {
                    "url": a_url,
                },
            }
        ]

    use_caches = test.get("use-caches", ["checkout", "pip", "uv"])
    job["run"] = {
        "tooltool-downloads": mozharness["tooltool-downloads"],
        "clone-with": run["clone-with"],
        "checkout": test["checkout"],
        "command": mh_command,
        "use-caches": use_caches,
        "using": "run-task",
    }
    if is_bitbar or is_lambda:
        job["run"]["run-as-root"] = True
    configure_taskdesc_for_run(config, job, taskdesc, worker["implementation"])
