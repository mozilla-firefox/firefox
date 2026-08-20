# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Source-test jobs can run on multiple platforms.  These transforms allow jobs
with either `platform` or a list of `platforms`, and set the appropriate
treeherder configuration and attributes for that platform.
"""

import copy
import os
from typing import Optional, Union

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.attributes import keymatch
from taskgraph.util.schema import Schema, optionally_keyed_by, resolve_keyed_by
from taskgraph.util.treeherder import join_symbol, split_symbol
from taskgraph.util.yaml import load_yaml

from gecko_taskgraph import GECKO
from gecko_taskgraph.transforms.job import JobDescriptionSchema


class SourceTestDescriptionSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    # most fields are passed directly through as job fields, and are not repeated here
    # The platform on which this task runs.  This will be used to set up attributes
    # (for try selection) and treeherder metadata (for display).  If given as a list,
    # the job will be "split" into multiple tasks, one with each platform.
    platform: Union[str, list[str]]
    # Build labels required for the task. If this key is provided it must
    # contain a build label for the task platform.
    # The task will then depend on a build task, and the installer url will be
    # saved to the GECKO_INSTALLER_URL environment variable.
    require_build: Optional[  # type: ignore
        optionally_keyed_by("project", dict[str, str], use_msgspec=True)
    ] = None
    # These fields can be keyed by "platform", and are otherwise identical to
    # job descriptions.
    worker_type: optionally_keyed_by(
        "platform",
        JobDescriptionSchema.__annotations__["worker_type"],
        use_msgspec=True,
    )
    worker: optionally_keyed_by(
        "platform", JobDescriptionSchema.__annotations__["worker"], use_msgspec=True
    )
    dependencies: Optional[  # type: ignore
        dict[
            str,
            optionally_keyed_by(
                "platform",
                JobDescriptionSchema.__annotations__["dependencies"],
                use_msgspec=True,
            ),
        ]
    ] = None
    # For vendoring-verification tasks: the moz.yaml files this task checks. The
    # run command and the files-changed patterns are built from this list, in
    # vendor_verify_members below.
    members: Optional[list[str]] = None
    # A list of artifacts to install from 'fetch' tasks.
    fetches: Optional[  # type: ignore
        dict[
            str,
            optionally_keyed_by(
                "platform",
                JobDescriptionSchema.__annotations__["fetches"],
                use_msgspec=True,
            ),
        ]
    ] = None


transforms = TransformSequence()

transforms.add_validate(SourceTestDescriptionSchema)


@transforms.add
def set_job_name(config, jobs):
    for job in jobs:
        if "task-from" in job and job["task-from"] != "kind.yml":
            from_name = os.path.splitext(job["task-from"])[0]
            job["name"] = "{}-{}".format(from_name, job["name"])
        yield job


VENDOR_VERIFY_TASKS_FILE = "vendor-verify.yml"
VENDOR_VERIFY_WRAPPER = "taskcluster/scripts/misc/verify-vendored-library.py"
VENDOR_VERIFY_EXPECTED_FAIL = "taskcluster/scripts/misc/vendor-verify-expected-fail.yml"
# A change to any of these can affect every library's re-vendoring, so they
# trigger every group: the vendoring machinery, the version-control layer it uses
# to detect changes (add_remove_files / working_directory_clean / diff), the
# wrapper, and the annotations it reads.
VENDOR_VERIFY_SHARED_WHEN = [
    "python/mozbuild/mozbuild/vendor/**",
    "python/mozversioncontrol/**",
    VENDOR_VERIFY_WRAPPER,
    VENDOR_VERIFY_EXPECTED_FAIL,
]
# Some libraries' update-actions run a tool by name rather than by path, so the
# toolchains the tasks fetch also have to be on PATH. gn is found by path and so
# needs no entry here.
VENDOR_VERIFY_TOOL_PATH = "$MOZ_FETCHES_DIR/rustc/bin:$MOZ_FETCHES_DIR/node/bin"


def vendor_verify_triggers(member):
    """files-changed patterns for one vendored library.

    Its moz.yaml directory, which covers the moz.yaml, its patches and any in-tree
    generated files, plus its vendor-directory -- for some libraries (e.g. gfx/angle
    vendors into third_party/angle) that lives elsewhere in the tree, so the moz.yaml
    directory alone would miss changes to the sources.
    """
    dirs = {os.path.dirname(member)}
    try:
        manifest = load_yaml(os.path.join(GECKO, member)) or {}
        vendor_dir = (manifest.get("vendoring") or {}).get("vendor-directory")
        if vendor_dir:
            dirs.add(vendor_dir.rstrip("/"))
    except Exception:
        pass
    return [f"{d}/**" for d in dirs]


@transforms.add
def vendor_verify_members(config, jobs):
    """Build the command and triggers for vendoring-verification tasks.

    Tasks in vendor-verify.yml name the libraries they check in `members`. Neither
    field can be expressed in the yaml: the command has to join the list, and the
    triggers need each library's moz.yaml read to find its vendor-directory.
    """
    for job in jobs:
        if job.get("task-from") != VENDOR_VERIFY_TASKS_FILE:
            yield job
            continue
        members = sorted(job.pop("members", []))
        if members:
            job["run"]["command"] = (
                f'PATH="{VENDOR_VERIFY_TOOL_PATH}:$PATH" '
                f"./mach python {VENDOR_VERIFY_WRAPPER} "
                f"--expected-fail {VENDOR_VERIFY_EXPECTED_FAIL} " + " ".join(members)
            )
            triggers = [p for m in members for p in vendor_verify_triggers(m)]
            job["when"] = {
                "files-changed": sorted(set(triggers) | set(VENDOR_VERIFY_SHARED_WHEN))
            }
        else:
            # The coverage task verifies no library, so it needs none of the
            # toolchains, and it is the most frequently triggered of these tasks.
            # Fetches inherited from task-defaults merge additively, so they cannot
            # be cleared in the yaml.
            job.pop("fetches", None)
        yield job


@transforms.add
def expand_platforms(config, jobs):
    for job in jobs:
        if isinstance(job["platform"], str):
            yield job
            continue

        for platform in job["platform"]:
            pjob = copy.deepcopy(job)
            pjob["platform"] = platform

            if "name" in pjob:
                pjob["name"] = "{}-{}".format(pjob["name"], platform)
            else:
                pjob["label"] = "{}-{}".format(pjob["label"], platform)
            yield pjob


@transforms.add
def nightly_only_codereview(config, jobs):
    for job in jobs:
        # No easy way to determine if the try run is from nightly, or another
        # branch like beta/release/esr
        if "perfdocs-verify" in job["name"] and (
            config.params.get("release_type", "").lower() != "nightly"
            or "a" not in config.params.get("version", "150.0a0")
        ):
            job.setdefault("attributes", {})["code-review"] = False
            job["always-target"] = False
        yield job


@transforms.add
def split_jsshell(config, jobs):
    all_shells = {"sm": "Spidermonkey", "v8": "Google V8"}

    for job in jobs:
        if not job["name"].startswith("jsshell"):
            yield job
            continue

        test = job.pop("test")
        for shell in job.get("shell", all_shells.keys()):
            assert shell in all_shells

            new_job = copy.deepcopy(job)
            new_job["name"] = "{}-{}".format(new_job["name"], shell)
            new_job["description"] = "{} on {}".format(
                new_job["description"], all_shells[shell]
            )
            new_job["shell"] = shell

            group = f"js-bench-{shell}"
            symbol = split_symbol(new_job["treeherder"]["symbol"])[1]
            new_job["treeherder"]["symbol"] = join_symbol(group, symbol)

            run = new_job["run"]
            run["mach"] = run["mach"].format(
                shell=shell, SHELL=shell.upper(), test=test
            )
            yield new_job


def add_build_dependency(config, job):
    """
    Add build dependency to the job and installer_url to env.
    """
    key = job["platform"]
    build_labels = job.pop("require-build", {})
    matches = keymatch(build_labels, key)
    if not matches:
        raise Exception(
            "No build platform found. "
            f"Define 'require-build' for {key} in the task config."
        )

    if len(matches) > 1:
        raise Exception(f"More than one build platform found for '{key}'.")

    label = matches[0]
    deps = job.setdefault("dependencies", {})
    deps.update({"build": label})


@transforms.add
def handle_platform(config, jobs):
    """
    Handle the 'platform' property, setting up treeherder context as well as
    try-related attributes.
    """
    fields = [
        "always-target",
        "fetches.toolchain",
        "require-build",
        "worker-type",
        "worker",
    ]

    for job in jobs:
        platform = job["platform"]

        for field in fields:
            resolve_keyed_by(
                job, field, item_name=job["name"], project=config.params["project"]
            )
        for field in job.get("dependencies", {}):
            resolve_keyed_by(
                job,
                f"dependencies.{field}",
                item_name=job["name"],
                project=config.params["project"],
            )

        if "treeherder" in job:
            job["treeherder"].setdefault("platform", platform)

        if "require-build" in job:
            add_build_dependency(config, job)

        del job["platform"]
        yield job


@transforms.add
def handle_shell(config, jobs):
    """
    Handle the 'shell' property.
    """
    fields = [
        "run-on-projects",
        "worker.env",
    ]

    for job in jobs:
        if not job.get("shell"):
            yield job
            continue

        for field in fields:
            resolve_keyed_by(job, field, item_name=job["name"])

        del job["shell"]
        yield job


@transforms.add
def set_code_review_env(config, jobs):
    """
    Add a CODE_REVIEW environment variable when running in code-review bot mode
    """
    is_code_review = config.params["target_tasks_method"] == "codereview"

    for job in jobs:
        attrs = job.get("attributes", {})
        if is_code_review and attrs.get("code-review") is True:
            env = job["worker"].setdefault("env", {})
            env["CODE_REVIEW"] = "1"

        yield job


@transforms.add
def remove_optimization_on_central(config, jobs):
    """
    For pushes to mozilla-central run all source-test tasks that are enabled for
    code-review in order to have the code-review bot populate the DB according
    with the push hash.
    """
    if (
        config.params["project"] != "mozilla-central"
        or config.params["tasks_for"] != "hg-push"
    ):
        yield from jobs
        return

    for job in jobs:
        if not job.get("attributes", {}).get("code-review", False):
            yield job
            continue
        if "when" in job:
            del job["when"]
        if "optimization" in job and "skip-unless-mozlint" in job["optimization"]:
            del job["optimization"]
        yield job
