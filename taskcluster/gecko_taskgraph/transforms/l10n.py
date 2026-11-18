# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Do transforms specific to l10n kind
"""

from typing import Literal, Optional, Union

from mozbuild.chunkify import chunkify
from mozilla_taskgraph.util.attributes import copy_attributes_from_dependent_job
from taskgraph.transforms.base import TransformSequence
from taskgraph.util import json
from taskgraph.util.copy import deepcopy
from taskgraph.util.dependencies import get_dependencies, get_primary_dependency
from taskgraph.util.schema import (
    Schema,
    optionally_keyed_by,
    resolve_keyed_by,
    taskref_or_string_msgspec,
)
from taskgraph.util.taskcluster import get_artifact_prefix
from taskgraph.util.treeherder import add_suffix

from gecko_taskgraph.transforms.job import JobDescriptionSchema
from gecko_taskgraph.transforms.task import TaskDescriptionSchema
from gecko_taskgraph.util.attributes import task_name


def _by_platform(arg):
    return optionally_keyed_by("build-platform", arg, use_msgspec=True)


def _by_platform_or_project(arg):
    return optionally_keyed_by("build-platform", "project", arg, use_msgspec=True)


class MozharnessSchema(Schema, kw_only=True):
    # Script to invoke for mozharness
    script: _by_platform(str)  # type: ignore  # noqa: F821
    # Config files passed to the mozharness script
    config: _by_platform(list[str])  # type: ignore  # noqa: F821
    # Additional paths to look for mozharness configs in. These should be
    # relative to the base of the source checkout
    config_paths: Optional[list[str]] = None
    # Options to pass to the mozharness script
    options: Optional[_by_platform(list[str])] = None  # type: ignore
    # Action commands to provide to mozharness script
    actions: _by_platform(list[str])  # type: ignore  # noqa: F821
    # if true, perform a checkout of a comm-central based branch inside the
    # gecko checkout
    comm_checkout: Optional[bool] = None


class L10nTreeherderSchema(Schema, kw_only=True):
    # Platform to display the task on in treeherder
    platform: _by_platform(str)  # type: ignore  # noqa: F821
    # Symbol to use
    symbol: str
    # Tier this task is
    tier: _by_platform(int)  # type: ignore  # noqa: F821


class L10nIndexSchema(Schema, kw_only=True):
    # Product to identify as in the taskcluster index
    product: _by_platform(str)  # type: ignore  # noqa: F821
    # Job name to identify as in the taskcluster index
    job_name: _by_platform(str)  # type: ignore  # noqa: F821
    # Type of index
    type: Optional[_by_platform(str)] = None  # type: ignore


class InTreeDockerImageSchema(Schema):
    in_tree: str


class WhenSchema(Schema, kw_only=True):
    files_changed: Optional[list[str]] = None


class L10nDescriptionSchema(Schema, kw_only=True):
    # Name for this job, inferred from the dependent job before validation
    name: str
    # build-platform, inferred from dependent job before validation
    build_platform: str
    # max run time of the task
    run_time: _by_platform(int)  # type: ignore  # noqa: F821
    # Locales not to repack for
    ignore_locales: _by_platform(list[str])  # type: ignore  # noqa: F821
    # All l10n jobs use mozharness
    mozharness: MozharnessSchema  # noqa: F821
    # Items for the taskcluster index
    index: Optional[L10nIndexSchema] = None
    # Description of the localized task
    description: _by_platform(str)  # type: ignore  # noqa: F821
    run_on_projects: JobDescriptionSchema.__annotations__["run_on_projects"] = None
    run_on_repo_type: JobDescriptionSchema.__annotations__["run_on_repo_type"] = None
    # worker-type to utilize
    worker_type: _by_platform(str)  # type: ignore  # noqa: F821
    # File which contains the used locales
    locales_file: _by_platform_or_project(str)  # type: ignore  # noqa: F821
    # Tooltool visibility required for task.
    tooltool: _by_platform(Literal["internal", "public"])  # type: ignore  # noqa: F821
    # Docker image required for task.  We accept only in-tree images
    # -- generally desktop-build or android-build -- for now.
    docker_image: Optional[_by_platform(InTreeDockerImageSchema)] = None  # type: ignore
    fetches: Optional[dict[str, object]] = None
    # The set of secret names to which the task has access
    secrets: Optional[_by_platform(Union[bool, list[str]])] = None  # type: ignore
    # Information for treeherder
    treeherder: L10nTreeherderSchema  # noqa: F821
    # Extra environment values to pass to the worker
    env: Optional[_by_platform(dict[str, taskref_or_string_msgspec])] = None  # type: ignore
    # Max number locales per chunk
    locales_per_chunk: Optional[_by_platform(int)] = None  # type: ignore
    # Task deps to chain this task with, added in transforms from primary dependency
    # if this is a shippable-style build
    dependencies: Optional[dict[str, str]] = None
    # Run the task when the listed files change (if present).
    when: Optional[WhenSchema] = None
    # passed through directly to the job description
    attributes: JobDescriptionSchema.__annotations__["attributes"] = None
    extra: JobDescriptionSchema.__annotations__["extra"] = None
    # Shipping product and phase
    shipping_product: TaskDescriptionSchema.__annotations__["shipping_product"] = None
    shipping_phase: TaskDescriptionSchema.__annotations__["shipping_phase"] = None
    task_from: TaskDescriptionSchema.__annotations__["task_from"] = None


transforms = TransformSequence()


def parse_locales_file(locales_file, platform=None):
    """Parse the passed locales file for a list of locales."""
    locales = []

    with open(locales_file) as f:
        if locales_file.endswith("json"):
            all_locales = json.load(f)
            # XXX Only single locales are fetched
            locales = {
                locale: data["revision"]
                for locale, data in all_locales.items()
                if platform is None or platform in data["platforms"]
            }
        else:
            all_locales = f.read().split()
            # 'default' is the hg revision at the top of hg repo, in this context
            locales = {locale: "default" for locale in all_locales}
    return locales


def _remove_locales(locales, to_remove=None):
    # ja-JP-mac is a mac-only locale, but there are no mac builds being repacked,
    # so just omit it unconditionally
    return {
        locale: revision
        for locale, revision in locales.items()
        if locale not in to_remove
    }


@transforms.add
def setup_name(config, jobs):
    for job in jobs:
        dep = get_primary_dependency(config, job)
        assert dep
        # Set the name to the same as the dep task, without kind name.
        # Label will get set automatically with this kinds name.
        job["name"] = job.get("name", task_name(dep))
        yield job


@transforms.add
def copy_in_useful_magic(config, jobs):
    for job in jobs:
        dep = get_primary_dependency(config, job)
        assert dep
        attributes = copy_attributes_from_dependent_job(dep)
        attributes.update(job.get("attributes", {}))
        # build-platform is needed on `job` for by-build-platform
        job["build-platform"] = attributes.get("build_platform")
        job["attributes"] = attributes
        yield job


transforms.add_validate(L10nDescriptionSchema)


@transforms.add
def remove_repackage_dependency(config, jobs):
    for job in jobs:
        build_platform = job["attributes"]["build_platform"]
        if not build_platform.startswith("macosx"):
            del job["dependencies"]["repackage"]

        yield job


@transforms.add
def handle_keyed_by(config, jobs):
    """Resolve fields that can be keyed by platform, etc."""
    fields = [
        "locales-file",
        "locales-per-chunk",
        "worker-type",
        "description",
        "run-time",
        "docker-image",
        "secrets",
        "fetches.toolchain",
        "fetches.fetch",
        "tooltool",
        "env",
        "ignore-locales",
        "mozharness.config",
        "mozharness.options",
        "mozharness.actions",
        "mozharness.script",
        "treeherder.tier",
        "treeherder.platform",
        "index.type",
        "index.product",
        "index.job-name",
        "when.files-changed",
    ]
    for job in jobs:
        job = deepcopy(job)  # don't overwrite dict values here
        for field in fields:
            resolve_keyed_by(
                item=job,
                field=field,
                item_name=job["name"],
                project=config.params["project"],
            )
        yield job


@transforms.add
def handle_artifact_prefix(config, jobs):
    """Resolve ``artifact_prefix`` in env vars"""
    for job in jobs:
        artifact_prefix = get_artifact_prefix(job)
        for k1, v1 in job.get("env", {}).items():
            if isinstance(v1, str):
                job["env"][k1] = v1.format(artifact_prefix=artifact_prefix)
            elif isinstance(v1, dict):
                for k2, v2 in v1.items():
                    job["env"][k1][k2] = v2.format(artifact_prefix=artifact_prefix)
        yield job


@transforms.add
def all_locales_attribute(config, jobs):
    for job in jobs:
        locales_platform = job["attributes"]["build_platform"].replace("-shippable", "")
        locales_platform = locales_platform.replace("-pgo", "")
        locales_with_changesets = parse_locales_file(
            job["locales-file"], platform=locales_platform
        )
        locales_with_changesets = _remove_locales(
            locales_with_changesets, to_remove=job["ignore-locales"]
        )

        locales = sorted(locales_with_changesets.keys())
        attributes = job.setdefault("attributes", {})
        attributes["all_locales"] = locales
        attributes["all_locales_with_changesets"] = locales_with_changesets
        if job.get("shipping-product"):
            attributes["shipping_product"] = job["shipping-product"]
        yield job


@transforms.add
def chunk_locales(config, jobs):
    """Utilizes chunking for l10n stuff"""
    for job in jobs:
        locales_per_chunk = job.get("locales-per-chunk")
        locales_with_changesets = job["attributes"]["all_locales_with_changesets"]
        if locales_per_chunk:
            chunks, remainder = divmod(len(locales_with_changesets), locales_per_chunk)
            if remainder:
                chunks = int(chunks + 1)
            for this_chunk in range(1, chunks + 1):
                chunked = deepcopy(job)
                chunked["name"] = chunked["name"].replace("/", f"-{this_chunk}/", 1)
                chunked["mozharness"]["options"] = chunked["mozharness"].get(
                    "options", []
                )
                # chunkify doesn't work with dicts
                locales_with_changesets_as_list = sorted(
                    locales_with_changesets.items()
                )
                chunked_locales = chunkify(
                    locales_with_changesets_as_list, this_chunk, chunks
                )
                chunked["mozharness"]["options"].extend([
                    f"locale={locale}:{changeset}"
                    for locale, changeset in chunked_locales
                ])
                chunked["attributes"]["l10n_chunk"] = str(this_chunk)
                # strip revision
                chunked["attributes"]["chunk_locales"] = [
                    locale for locale, _ in chunked_locales
                ]

                # add the chunk number to the TH symbol
                chunked["treeherder"]["symbol"] = add_suffix(
                    chunked["treeherder"]["symbol"], this_chunk
                )
                yield chunked
        else:
            job["mozharness"]["options"] = job["mozharness"].get("options", [])
            job["mozharness"]["options"].extend([
                f"locale={locale}:{changeset}"
                for locale, changeset in sorted(locales_with_changesets.items())
            ])
            yield job


transforms.add_validate(L10nDescriptionSchema)


@transforms.add
def stub_installer(config, jobs):
    for job in jobs:
        job.setdefault("attributes", {})
        job.setdefault("env", {})
        if job["attributes"].get("stub-installer"):
            job["env"].update({"USE_STUB_INSTALLER": "1"})
        yield job


@transforms.add
def set_extra_config(config, jobs):
    for job in jobs:
        job["mozharness"].setdefault("extra-config", {})["branch"] = config.params[
            "project"
        ]
        if "update-channel" in job["attributes"]:
            job["mozharness"]["extra-config"]["update_channel"] = job["attributes"][
                "update-channel"
            ]
        yield job


@transforms.add
def make_job_description(config, jobs):
    for job in jobs:
        job["mozharness"].update({
            "using": "mozharness",
            "clone-with": "hg",
            "job-script": "taskcluster/scripts/builder/build-l10n.sh",
            "secrets": job.get("secrets", False),
        })
        job_description = {
            "name": job["name"],
            "worker-type": job["worker-type"],
            "description": job["description"],
            "run": job["mozharness"],
            "attributes": job["attributes"],
            "treeherder": {
                "kind": "build",
                "tier": job["treeherder"]["tier"],
                "symbol": job["treeherder"]["symbol"],
                "platform": job["treeherder"]["platform"],
            },
            "run-on-projects": (
                job.get("run-on-projects") if job.get("run-on-projects") else []
            ),
            "run-on-repo-type": job.get("run-on-repo-type", ["git", "hg"]),
        }
        if job.get("extra"):
            job_description["extra"] = job["extra"]

        job_description["run"]["tooltool-downloads"] = job["tooltool"]

        job_description["worker"] = {
            "max-run-time": job["run-time"],
            "chain-of-trust": True,
        }
        if job["worker-type"] in ["b-win2012", "b-win2022", "b-win2025"]:
            job_description["worker"]["os"] = "windows"
            job_description["run"]["use-simple-package"] = False
            job_description["run"]["use-magic-mh-args"] = False

        if job.get("docker-image"):
            job_description["worker"]["docker-image"] = job["docker-image"]

        if job.get("fetches"):
            job_description["fetches"] = job["fetches"]

        if job.get("index"):
            job_description["index"] = {
                "product": job["index"]["product"],
                "job-name": job["index"]["job-name"],
                "type": job["index"].get("type", "generic"),
            }

        if job.get("dependencies"):
            job_description["dependencies"] = job["dependencies"]
        if job.get("env"):
            job_description["worker"]["env"] = job["env"]
        if job.get("when", {}).get("files-changed"):
            job_description.setdefault("when", {})
            job_description["when"]["files-changed"] = [job["locales-file"]] + job[
                "when"
            ]["files-changed"]

        if "shipping-phase" in job:
            job_description["shipping-phase"] = job["shipping-phase"]

        if "shipping-product" in job:
            job_description["shipping-product"] = job["shipping-product"]

        yield job_description


@transforms.add
def add_macos_signing_artifacts(config, jobs):
    for job in jobs:
        if "macosx" not in job["name"]:
            yield job
            continue
        build_dep = None
        for dep_job in get_dependencies(config, job):
            if dep_job.kind == "build":
                build_dep = dep_job
                break
        assert build_dep, f"l10n job {job['name']} has no build dependency"
        for path, artifact in build_dep.task["payload"]["artifacts"].items():
            if path.startswith("public/build/security/"):
                job["worker"].setdefault("artifacts", []).append({
                    "name": path,
                    "path": artifact["path"],
                    "type": "file",
                })
        yield job
