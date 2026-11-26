# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Apply some defaults and minor modifications to the jobs defined in the build
kind.
"""

import copy
import logging
import re

from mozbuild.artifact_builds import JOB_CHOICES as ARTIFACT_JOBS
from mozilla_taskgraph.util.attributes import release_level
from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by
from taskgraph.util.treeherder import add_suffix

from gecko_taskgraph.util.attributes import RELEASE_PROJECTS
from gecko_taskgraph.util.workertypes import worker_type_implementation

logger = logging.getLogger(__name__)

transforms = TransformSequence()


@transforms.add
def set_ccov_attribute(config, jobs):
    for job in jobs:
        label = job.get("label", job["name"])
        if "ccov" in label:
            job.setdefault("attributes", {})["ccov"] = True
        yield job


@transforms.add
def set_defaults(config, jobs):
    """Set defaults, including those that differ per worker implementation"""
    for job in jobs:
        job["treeherder"].setdefault("kind", "build")
        job["treeherder"].setdefault("tier", 1)
        _, worker_os = worker_type_implementation(
            config.graph_config, config.params, job["worker-type"]
        )
        worker = job.setdefault("worker", {})
        worker.setdefault("env", {})
        worker["chain-of-trust"] = True
        yield job


@transforms.add
def stub_installer(config, jobs):
    for job in jobs:
        resolve_keyed_by(
            job,
            "stub-installer",
            item_name=job["name"],
            project=config.params["project"],
            **{
                "release-type": config.params["release_type"],
            },
        )
        job.setdefault("attributes", {})
        if job.get("stub-installer"):
            job["attributes"]["stub-installer"] = job["stub-installer"]
            job["worker"]["env"].update({"USE_STUB_INSTALLER": "1"})
        if "stub-installer" in job:
            del job["stub-installer"]
        yield job


@transforms.add
def resolve_shipping_product(config, jobs):
    for job in jobs:
        resolve_keyed_by(
            job,
            "shipping-product",
            item_name=job["name"],
            **{
                "release-type": config.params["release_type"],
            },
        )
        yield job


@transforms.add
def update_channel(config, jobs):
    keys = [
        "run.update-channel",
        "run.mar-channel-id",
        "run.accepted-mar-channel-ids",
    ]
    for job in jobs:
        job["worker"].setdefault("env", {})
        for key in keys:
            resolve_keyed_by(
                job,
                key,
                item_name=job["name"],
                **{
                    "project": config.params["project"],
                    "release-type": config.params["release_type"],
                },
            )
        update_channel = job["run"].pop("update-channel", None)
        if update_channel:
            job["run"].setdefault("extra-config", {})["update_channel"] = update_channel
            job["attributes"]["update-channel"] = update_channel
        mar_channel_id = job["run"].pop("mar-channel-id", None)
        if mar_channel_id:
            job["attributes"]["mar-channel-id"] = mar_channel_id
            job["worker"]["env"]["MAR_CHANNEL_ID"] = mar_channel_id
        accepted_mar_channel_ids = job["run"].pop("accepted-mar-channel-ids", None)
        if accepted_mar_channel_ids:
            job["attributes"]["accepted-mar-channel-ids"] = accepted_mar_channel_ids
            job["worker"]["env"]["ACCEPTED_MAR_CHANNEL_IDS"] = accepted_mar_channel_ids

        yield job


UNIFY_JOB_SCRIPT = "taskcluster/scripts/misc/unify.sh"

# Toolchains that are only needed to compile or link, which artifact builds
# don't do. Binaries such as the ONNX runtime come from the build the
# artifacts are taken from.
COMPILE_TOOLCHAINS = re.compile(
    r"^(?:linux64|win64)-(?:clang(?:-.*)?|cctools-port|rust(?:-.*)?|cbindgen"
    r"|dump_syms|nasm|pkgconf|sccache|winchecksec)$"
    r"|^(?:sysroot|onnxruntime|dxc|winappsdk)-.*$"
    r"|^(?:macosx64-sdk|vs)$"
)
# Likewise for fetches.
COMPILE_FETCHES = {"windows-rs"}


def _use_artifact(config):
    if "try_task_config" not in config.params:
        return False
    return config.params["try_task_config"].get("use-artifact-builds", False)


def _label(config, job):
    return job.get("label", f"{config.kind}-{job['name']}")


def _supports_artifact_build(job, env):
    return (
        job.get("index", {}).get("job-name") in ARTIFACT_JOBS
        # If tests aren't packaged, then we are not able to rebuild all the packages
        and env.get("MOZ_AUTOMATION_PACKAGE_TESTS") == "1"
    )


@transforms.add
def collapse_unified_builds(config, jobs):
    """Turn the macOS universal builds into plain artifact builds.

    A universal build lipos an x64 and an aarch64 build together, and hardcodes
    fetches for artifacts that artifact builds don't produce, such as the gtest
    tarball. There is nothing to unify in artifact build mode anyway: the
    universal build is already indexed, and downloading it is exactly what a
    local macOS artifact build does. So take over the configuration of the x64
    build being unified, which `use_artifact` below then turns into an artifact
    build, and let the two per-architecture builds fall out of the graph.
    """
    # The whole kind has to be rewritten before anything is yielded: downstream
    # transforms consume a job as soon as it is yielded, and consuming the x64
    # build empties the `run` this needs to copy.
    jobs = list(jobs)
    if config.kind == "build" and _use_artifact(config):
        by_label = {_label(config, job): job for job in jobs}
        for job in jobs:
            if job["run"].get("job-script") != UNIFY_JOB_SCRIPT:
                continue

            # unify.sh unpacks the x64 halves of what it lipos under `x64`.
            x64 = by_label[
                next(
                    job["dependencies"][name]
                    for name, fetches in job["fetches"].items()
                    if any(
                        isinstance(fetch, dict)
                        and fetch.get("dest", "").split("/")[0] == "x64"
                        for fetch in fetches
                    )
                )
            ]
            if not _supports_artifact_build(job, x64["worker"]["env"]):
                continue

            job.pop("dependencies")
            job["run"] = copy.deepcopy(x64["run"])
            job["fetches"] = copy.deepcopy(x64["fetches"])
            job["mozconfig"] = copy.deepcopy(x64["mozconfig"])
            job["worker"]["env"].update(copy.deepcopy(x64["worker"]["env"]))
            job["worker"]["max-run-time"] = x64["worker"]["max-run-time"]

    yield from jobs


@transforms.add
def use_artifact(config, jobs):
    use_artifact = _use_artifact(config)
    for job in jobs:
        if (
            config.kind == "build"
            and use_artifact
            and _supports_artifact_build(job, job["worker"]["env"])
        ):
            job["treeherder"]["symbol"] = add_suffix(job["treeherder"]["symbol"], "a")
            job["worker"]["env"]["USE_ARTIFACT"] = "1"
            job["attributes"]["artifact-build"] = True
            # Artifact builds don't compile anything.
            job["use-sccache"] = False
            toolchains = job.get("fetches", {}).get("toolchain")
            if toolchains:
                job["fetches"]["toolchain"] = [
                    t for t in toolchains if not COMPILE_TOOLCHAINS.match(t)
                ]
            fetches = job.get("fetches", {}).get("fetch")
            if fetches:
                job["fetches"]["fetch"] = [
                    f for f in fetches if f not in COMPILE_FETCHES
                ]
        yield job


@transforms.add
def use_profile_data(config, jobs):
    for job in jobs:
        use_pgo = job.pop("use-pgo", False)
        disable_pgo = config.params["try_task_config"].get("disable-pgo", False)
        artifact_build = job["attributes"].get("artifact-build")
        if not use_pgo or disable_pgo or artifact_build:
            yield job
            continue

        # If use_pgo is True, the task uses the generate-profile task of the
        # same name. Otherwise a task can specify a specific generate-profile
        # task to use in the use_pgo field.
        if use_pgo is True:
            name = job["name"]
        else:
            name = use_pgo
        dependencies = f"generate-profile-{name}"
        job.setdefault("dependencies", {})["generate-profile"] = dependencies
        job.setdefault("fetches", {})["generate-profile"] = ["profdata.tar.xz"]
        job["worker"]["env"].update({"TASKCLUSTER_PGO_PROFILE_USE": "1"})

        _, worker_os = worker_type_implementation(
            config.graph_config, config.params, job["worker-type"]
        )
        if worker_os == "linux":
            # LTO linkage needs more open files than the default from run-task.
            job["worker"]["env"].update({"MOZ_LIMIT_NOFILE": "8192"})

        if job.get("use-sccache"):
            raise Exception(
                "use-sccache is incompatible with use-pgo in {}".format(job["name"])
            )

        yield job


@transforms.add
def resolve_keys(config, jobs):
    for job in jobs:
        resolve_keyed_by(
            job,
            "use-sccache",
            item_name=job["name"],
            **{
                "release-level": release_level(
                    config.graph_config["release-branches"], config.params
                )
            },
        )
        yield job


@transforms.add
def enable_full_crashsymbols(config, jobs):
    """Enable full crashsymbols on jobs with
    'enable-full-crashsymbols' set to True and on release branches, or
    on try"""
    branches = RELEASE_PROJECTS | {"toolchains", "try", "try-comm-central"}
    for job in jobs:
        enable_full_crashsymbols = job["attributes"].get("enable-full-crashsymbols")
        if enable_full_crashsymbols and config.params["project"] in branches:
            logger.debug("Enabling full symbol generation for %s", job["name"])
            job["worker"]["env"]["MOZ_ENABLE_FULL_SYMBOLS"] = "1"
        else:
            logger.debug("Disabling full symbol generation for %s", job["name"])
            job["attributes"].pop("enable-full-crashsymbols", None)
        yield job


@transforms.add
def disable_clang_plugins(config, jobs):
    all_jobs = list(jobs)
    opt_jobs_platform = {}
    dbg_build_platforms = set()
    for job in all_jobs:
        try:
            build_platform, build_type = job["name"].split("/")
        except ValueError:
            continue

        if build_type == "debug":
            dbg_build_platforms.add(build_platform)

        if build_type == "opt":
            opt_jobs_platform[id(job)] = build_platform

    for job in all_jobs:
        build_platform = opt_jobs_platform.get(id(job), None)
        # Disable clang plugins for opt configuration when we have a fallback
        # debug configuration that supposedly have similar coverage.
        # This saves build time while not reducing coverage too much.
        if build_platform in dbg_build_platforms:
            worker_env = job["worker"]["env"]
            worker_env["DISABLE_CLANG_PLUGIN"] = "1"
        yield job


@transforms.add
def set_expiry(config, jobs):
    for job in jobs:
        attributes = job["attributes"]
        if (
            "shippable" in attributes
            and attributes["shippable"]
            and config.kind
            in {
                "build",
            }
        ):
            expiration_policy = "long"
        else:
            expiration_policy = "medium"

        job["expiration-policy"] = expiration_policy
        yield job


@transforms.add
def add_signing_artifacts(config, jobs):
    """
    Add signing artifacts to macOS build jobs.
    """
    is_prod_project = (
        release_level(config.graph_config["release-branches"], config.params)
        == "production"
    )
    for job in jobs:
        if "macosx" not in job["name"] or "searchfox" in job["name"]:
            # Not macosx build or no artifacts defined, so skip
            yield job
            continue
        assert "artifacts" in job["worker"], (
            "macosx build jobs must have worker.artifacts defined."
        )
        is_shippable = (
            ("shippable" in job["attributes"] and job["attributes"]["shippable"])
            # Instrumented builds don't have attributes.shippable set
            or "shippable" in job["name"]
        )

        entitlement_directory = "developer"
        if is_shippable and is_prod_project:
            entitlement_directory = "production"

        # Decide which browser entitlement to use
        if entitlement_directory == "developer":
            # Try/debug builds
            browser_entitlement = "browser"
        elif "devedition" in job.get("shipping-product", ""):
            # Devedition
            browser_entitlement = "firefoxdeveloperedition.browser"
        elif "mozilla-central" == config.params["project"]:
            # Nightly
            browser_entitlement = "nightly.browser"
        else:
            # Release and Beta
            browser_entitlement = "firefox.browser"

        for entry in job.get("worker", {}).get("artifacts", []):
            for key in ("name", "path"):
                if key in entry:
                    entry[key] = entry[key].format(
                        entitlement_directory=entitlement_directory,
                        browser_entitlement=browser_entitlement,
                    )
        # Add utility.xml if not prod/shippable
        if not is_prod_project or not is_shippable:
            job["worker"]["artifacts"].append({
                "name": "public/build/security/utility.xml",
                "path": "checkouts/gecko/security/mac/hardenedruntime/developer/utility.xml",
                "type": "file",
            })
        impl, _ = worker_type_implementation(
            config.graph_config, config.params, job["worker-type"]
        )
        if impl == "docker-worker":
            # For builds using docker-worker we can't use relative paths
            # Once we switch builds to generic-worker, this can be removed
            for entry in job.get("worker", {}).get("artifacts", []):
                if entry.get("path", "").startswith("checkouts/gecko/security"):
                    entry["path"] = "/builds/worker/" + entry["path"]
        yield job
