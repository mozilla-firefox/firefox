# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Support for running jobs that are invoked via the `run-task` script.
"""

import dataclasses
import functools
import json
import os
from typing import Literal, Union
from typing import Optional as TOptional

from mozpack import path
from taskgraph.transforms.run.common import support_caches
from taskgraph.util.schema import (
    Schema,
    taskref_or_string_msgspec,
)
from taskgraph.util.yaml import load_yaml

from gecko_taskgraph.transforms.job import get_platform, run_job_using
from gecko_taskgraph.transforms.job.common import (
    add_tooltool,
    clone_type,
    support_vcs_checkout,
)
from gecko_taskgraph.util.sparse_profiles import (
    git_checkout_is_full,
    list_directory_files,
    load_sparse_profile,
    to_git_sparse_patterns,
)


@functools.cache
def git_sparse_patterns(sparse_profile_path):
    if git_checkout_is_full(sparse_profile_path):
        return None
    includes, excludes = load_sparse_profile(sparse_profile_path)
    return json.dumps(to_git_sparse_patterns(includes, excludes, list_directory_files))


class RunTaskSchema(Schema, kw_only=True):
    using: Literal["run-task"]
    # Use the specified caches.
    use_caches: TOptional[Union[bool, list[str]]] = None
    # if true (the default), perform a checkout of gecko on the worker
    checkout: bool = True
    # Path to run command in. If a checkout is present, the path
    # to the checkout will be interpolated with the key `checkout`
    cwd: TOptional[str] = None
    # The sparse checkout profile to use. Value is the filename relative to
    # "sparse-profile-prefix" which defaults to "build/sparse-profiles/".
    sparse_profile: TOptional[Union[str, None]] = None
    # The relative path to the sparse profile.
    sparse_profile_prefix: TOptional[str] = None
    # Whether to use a shallow clone or not, default True (git only).
    shallow_clone: TOptional[bool] = None
    # How to clone the upstream repo for the checkout, either "hg" or "git"
    # (default: "git")
    clone_with: TOptional[Literal["hg", "git"]] = "git"
    # if true, perform a checkout of a comm-central based branch inside the
    # gecko checkout
    comm_checkout: bool = False
    # The command arguments to pass to the `run-task` script, after the
    # checkout arguments.  If a list, it will be passed directly; otherwise
    # it will be included in a single argument to `bash -cx`.
    command: Union[list[taskref_or_string_msgspec], taskref_or_string_msgspec]
    # Base work directory used to set up the task.
    workdir: TOptional[str] = None
    # If not false, tooltool downloads will be enabled via relengAPIProxy
    # for either just public files, or all files. Only supported on
    # docker-worker.
    tooltool_downloads: Union[bool, Literal["public", "internal"]] = False
    # Whether to run as root. (defaults to False)
    run_as_root: TOptional[bool] = None

    def __post_init__(self):
        super().__post_init__()
        if self.tooltool_downloads is True:
            raise ValueError(
                "tooltool-downloads must be False, 'public', or 'internal'"
            )


def common_setup(config, job, taskdesc, command):
    run = job["run"]
    run_cwd = run.get("cwd")
    if run["checkout"]:
        repo_configs = config.repo_configs
        if len(repo_configs) > 1 and run["checkout"] is True:
            raise Exception("Must explicitly specify checkouts with multiple repos.")
        elif run["checkout"] is not True:
            repo_configs = {
                repo: dataclasses.replace(repo_configs[repo], **config)
                for (repo, config) in run["checkout"].items()
            }

        gecko_path = support_vcs_checkout(config, job, taskdesc, repo_configs)
        command.append(f"--gecko-checkout={gecko_path}")
        if clone_type(config, job) == "git" and run.get("shallow-clone", True):
            command.append("--gecko-shallow-clone")

        # For Mercurial checkouts, point run-task at the decision task's source
        # bundle artifact so robustcheckout can obtain the head revision without
        # an expensive pull from hg.mozilla.org. clone_type() resolves to "hg"
        # exactly when run-task-hg / robustcheckout will run (e.g. on try, where
        # there is no git push info). A missing or unusable bundle is non-fatal.
        if clone_type(config, job) == "hg":
            taskdesc["worker"].setdefault("env", {})["GECKO_HEAD_BUNDLE"] = {
                "artifact-reference": "<decision/public/checkout.bundle>"
            }

        if run_cwd:
            run_cwd = path.normpath(run_cwd.format(checkout=gecko_path))

    elif run_cwd and "{checkout}" in run_cwd:
        raise Exception(
            "Found `{{checkout}}` interpolation in `cwd` for task {name} "
            "but the task doesn't have a checkout: {cwd}".format(
                cwd=run_cwd, name=job.get("name", job.get("label"))
            )
        )

    if run["sparse-profile"] and run["checkout"]:
        sparse_profile_prefix = run.pop(
            "sparse-profile-prefix", "build/sparse-profiles"
        )
        sparse_profile_path = path.join(sparse_profile_prefix, run["sparse-profile"])
        if clone_type(config, job) == "hg":
            command.append(f"--gecko-sparse-profile={sparse_profile_path}")
        elif patterns := git_sparse_patterns(sparse_profile_path):
            command.append(f"--gecko-sparse-profile={sparse_profile_path}")
            taskdesc["worker"].setdefault("env", {})["GECKO_SPARSE_PATTERNS"] = patterns

    if run_cwd:
        command.append(f"--task-cwd={run_cwd}")

    support_caches(config, job, taskdesc)
    taskdesc["worker"].setdefault("env", {})["MOZ_SCM_LEVEL"] = config.params["level"]


worker_defaults = {
    "checkout": True,
    "comm-checkout": False,
    "sparse-profile": None,
    "tooltool-downloads": False,
    "run-as-root": False,
    "clone-with": "git",
}


load_yaml = functools.cache(load_yaml)

RESOURCE_USAGE_ARTIFACT = "public/profile_run-task-resource-usage.json"

# Fetch tasks providing abi3 wheels, which work with any python 3 version
# run-task may run with, keyed by the platform of that python, as determined
# in run-task. They are mounted rather than fetched with fetch-content because
# resource usage is monitored from before fetch-content runs.
PSUTIL_FETCHES = {
    platform: f"psutil-{platform}"
    for platform in (
        "linux-x86_64",
        "linux-aarch64",
        "macosx-x86_64",
        "macosx-arm64",
        "win-amd64",
        "win-arm64",
    )
}

# The wheels to provide on each worker platform. The system python on arm64
# workers may run natively, or as x86_64 under emulation.
PSUTIL_PLATFORMS = {
    "linux64": ["linux-x86_64"],
    "linux64-aarch64": ["linux-aarch64"],
    "macosx64": ["macosx-x86_64"],
    "macosx64-aarch64": ["macosx-arm64", "macosx-x86_64"],
    "win64": ["win-amd64"],
    "win64-aarch64": ["win-arm64", "win-amd64"],
}


def psutil_platforms(job):
    platform = get_platform(job)
    worker_type = job["worker-type"]
    if platform == "win64" and ("aarch64" in worker_type or "-a64" in worker_type):
        platform = "win64-aarch64"
    return PSUTIL_PLATFORMS[platform]


def script_url(config, script):
    if "MOZ_AUTOMATION" in os.environ and "TASK_ID" not in os.environ:
        raise Exception("TASK_ID must be defined to use run-task on generic-worker")
    task_id = os.environ.get("TASK_ID", "<TASK_ID>")
    tc_url = "http://firefox-ci-tc.services.mozilla.com"
    return f"{tc_url}/api/queue/v1/task/{task_id}/artifacts/public/{script}"


@run_job_using(
    "docker-worker", "run-task", schema=RunTaskSchema, defaults=worker_defaults
)
def docker_worker_run_task(config, job, taskdesc):
    run = job["run"]
    worker = taskdesc["worker"] = job["worker"]
    run_task_bin = f"run-task-{clone_type(config, job)}"
    command = [f"/builds/worker/bin/{run_task_bin}"]
    common_setup(config, job, taskdesc, command)

    worker.setdefault("env", {}).setdefault(
        "RUN_TASK_PRE_COMMAND_HOOK", "/builds/worker/bin/run-task-setup.py"
    )

    if run["tooltool-downloads"]:
        internal = run["tooltool-downloads"] == "internal"
        add_tooltool(config, job, taskdesc, internal=internal)

    run_command = run["command"]

    # dict is for the case of `{'task-reference': text_type}`.
    if isinstance(run_command, (str, dict)):
        run_command = ["bash", "-cx", run_command]
    if run["comm-checkout"]:
        command.append(
            "--comm-checkout={}/comm".format(taskdesc["worker"]["env"]["GECKO_PATH"])
        )
    if run["run-as-root"]:
        command.extend(("--user", "root", "--group", "root"))
    command.append("--")
    command.extend(run_command)
    worker["command"] = command

    if run_task_bin == "run-task-git":
        worker["env"]["RUN_TASK_STARTUP_HOOK"] = (
            "/builds/worker/bin/run_task_resource_monitor.py"
        )
    profile_path = "/builds/worker/run-task-resource-usage.json"
    worker["env"]["RUN_TASK_RESOURCE_USAGE"] = profile_path
    worker["env"]["RUN_TASK_RESOURCE_USAGE_NAME"] = taskdesc["label"]
    worker.setdefault("artifacts", []).append({
        "name": RESOURCE_USAGE_ARTIFACT,
        "path": profile_path,
        "type": "file",
    })


@run_job_using(
    "generic-worker", "run-task", schema=RunTaskSchema, defaults=worker_defaults
)
def generic_worker_run_task(config, job, taskdesc):
    run = job["run"]
    worker = taskdesc["worker"] = job["worker"]
    is_win = worker["os"] == "windows"
    is_mac = worker["os"] == "macosx"
    is_bitbar = worker["os"] == "linux-bitbar"
    is_lambda = worker["os"] == "linux-lambda"

    if run["tooltool-downloads"]:
        internal = run["tooltool-downloads"] == "internal"
        add_tooltool(config, job, taskdesc, internal=internal)

    if is_win:
        command = ["C:/mozilla-build/python3/python3.exe", "run-task"]
    elif is_mac:
        command = ["/usr/local/bin/python3", "run-task"]
    else:
        command = ["./run-task"]

    common_setup(config, job, taskdesc, command)

    worker.setdefault("mounts", [])
    run_task_bin = f"run-task-{clone_type(config, job)}"
    worker["mounts"].append({
        "content": {
            "url": script_url(config, run_task_bin),
        },
        "file": "./run-task",
    })

    worker["mounts"].append({
        "content": {
            "url": script_url(config, "run_task_resource_monitor.py"),
        },
        "file": "./run_task_resource_monitor.py",
    })
    if run_task_bin == "run-task-git":
        worker["env"]["RUN_TASK_STARTUP_HOOK"] = "run_task_resource_monitor.py"
    worker["mounts"].append({
        "content": {
            "url": script_url(config, "resourcemonitor.py"),
        },
        "file": "./mozsystemmonitor/resourcemonitor.py",
    })
    for psutil_platform in psutil_platforms(job):
        fetch_label = f"fetch-{PSUTIL_FETCHES[psutil_platform]}"
        taskdesc.setdefault("dependencies", {})[fetch_label] = fetch_label
        worker["mounts"].append({
            "content": {
                "artifact": f"public/psutil-{psutil_platform}.whl",
                "task-id": {"task-reference": f"<{fetch_label}>"},
            },
            "directory": f"./resource-monitor-deps/{psutil_platform}",
            "format": "zip",
        })
    profile_path = "run-task-resource-usage.json"
    worker["env"]["RUN_TASK_RESOURCE_USAGE"] = profile_path
    worker["env"]["RUN_TASK_RESOURCE_USAGE_NAME"] = taskdesc["label"]
    worker.setdefault("artifacts", []).append({
        "name": RESOURCE_USAGE_ARTIFACT,
        "path": profile_path,
        "type": "file",
    })

    if (
        job.get("fetches")
        or job.get("use-uv")
        or job.get("use-python", "system") != "system"
    ):
        worker["mounts"].append({
            "content": {
                "url": script_url(config, "fetch-content"),
            },
            "file": "./fetch-content",
        })
    if run.get("checkout"):
        worker["mounts"].append({
            "content": {
                "url": script_url(config, "robustcheckout.py"),
            },
            "file": "./robustcheckout.py",
        })

    worker["mounts"].append({
        "content": {
            "url": script_url(config, "run-task-setup.py"),
        },
        "file": "./run-task-setup.py",
    })
    worker.setdefault("env", {}).setdefault(
        "RUN_TASK_PRE_COMMAND_HOOK", "./run-task-setup.py"
    )

    run_command = run["command"]

    # dict is for the case of `{'task-reference': text_type}`.
    if isinstance(run_command, (str, dict)):
        if is_win:
            if isinstance(run_command, dict):
                for k in run_command.keys():
                    run_command[k] = f'"{run_command[k]}"'
            else:
                run_command = f'"{run_command}"'
        run_command = ["bash", "-cx", run_command]

    if run["comm-checkout"]:
        command.append(
            "--comm-checkout={}/comm".format(taskdesc["worker"]["env"]["GECKO_PATH"])
        )

    if run["run-as-root"]:
        command.extend(("--user", "root", "--group", "root"))
    command.append("--")
    if is_bitbar:
        # Use the bitbar wrapper script which sets up the device and adb
        # environment variables
        command.append("/builds/taskcluster/script.py")
    elif is_lambda:
        command.append("/home/ltuser/taskcluster/script.py")
    command.extend(run_command)

    if is_win:
        taskref = False
        for c in command:
            if isinstance(c, dict):
                taskref = True

        if taskref:
            cmd = []
            for c in command:
                if isinstance(c, dict):
                    for v in c.values():
                        cmd.append(v)
                else:
                    cmd.append(c)
            worker["command"] = [{"artifact-reference": " ".join(cmd)}]
        else:
            worker["command"] = [" ".join(command)]
    else:
        worker["command"] = [
            ["chmod", "+x", "run-task"],
            command,
        ]
