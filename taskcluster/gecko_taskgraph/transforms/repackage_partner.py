# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the repackage task into an actual task description.
"""

import copy
from typing import Optional

from mozilla_taskgraph.util.attributes import copy_attributes_from_dependent_job
from taskgraph.transforms.base import TransformSequence
from taskgraph.util.dependencies import get_primary_dependency
from taskgraph.util.schema import Schema, optionally_keyed_by, resolve_keyed_by
from taskgraph.util.taskcluster import get_artifact_prefix

from gecko_taskgraph.transforms.repackage import (
    PACKAGE_FORMATS as PACKAGE_FORMATS_VANILLA,
)
from gecko_taskgraph.transforms.task import TaskDescriptionSchema
from gecko_taskgraph.util.partners import get_partner_config_by_kind
from gecko_taskgraph.util.platforms import archive_format, executable_extension
from gecko_taskgraph.util.workertypes import worker_type_implementation

# When repacking the stub installer we need to pass a zip file and package name to the
# repackage task. This is not needed for vanilla stub but analogous to the full installer.
PACKAGE_FORMATS = copy.deepcopy(PACKAGE_FORMATS_VANILLA)
PACKAGE_FORMATS["installer-stub"]["inputs"]["package"] = "target-stub{archive_format}"
PACKAGE_FORMATS["installer-stub"]["args"].extend(["--package-name", "{package-name}"])


class MozharnessSchema(Schema, kw_only=True):
    # Config files passed to the mozharness script
    config: optionally_keyed_by("build-platform", list[str], use_msgspec=True)  # type: ignore
    # Additional paths to look for mozharness configs in. These should be
    # relative to the base of the source checkout
    config_paths: Optional[list[str]] = None
    # if true, perform a checkout of a comm-central based branch inside the
    # gecko checkout
    comm_checkout: Optional[bool] = None


class PackagingDescriptionSchema(Schema, kw_only=True):
    # unique label to describe this repackaging task
    label: Optional[str] = None
    # Routes specific to this task, if defined
    routes: Optional[list[str]] = None
    # passed through directly to the job description
    extra: TaskDescriptionSchema.__annotations__["extra"] = None
    # Shipping product and phase
    shipping_product: TaskDescriptionSchema.__annotations__["shipping_product"] = None
    shipping_phase: TaskDescriptionSchema.__annotations__["shipping_phase"] = None
    package_formats: optionally_keyed_by(
        "build-platform", "build-type", list[str], use_msgspec=True
    )  # type: ignore  # noqa: F821
    # All l10n jobs use mozharness
    mozharness: MozharnessSchema  # noqa: F821
    # Override the default priority for the project
    priority: TaskDescriptionSchema.__annotations__["priority"] = None
    task_from: TaskDescriptionSchema.__annotations__["task_from"] = None
    attributes: TaskDescriptionSchema.__annotations__["attributes"] = None
    dependencies: TaskDescriptionSchema.__annotations__["dependencies"] = None
    run_on_repo_type: TaskDescriptionSchema.__annotations__["run_on_repo_type"] = None


transforms = TransformSequence()


@transforms.add
def remove_name(config, jobs):
    for job in jobs:
        if "name" in job:
            del job["name"]
        yield job


transforms.add_validate(PackagingDescriptionSchema)


@transforms.add
def copy_in_useful_magic(config, jobs):
    """Copy attributes from upstream task to be used for keyed configuration."""
    for job in jobs:
        dep = get_primary_dependency(config, job)
        assert dep

        job["build-platform"] = dep.attributes.get("build_platform")
        yield job


@transforms.add
def handle_keyed_by(config, jobs):
    """Resolve fields that can be keyed by platform, etc."""
    fields = [
        "mozharness.config",
        "package-formats",
    ]
    for job in jobs:
        job = copy.deepcopy(job)  # don't overwrite dict values here
        for field in fields:
            resolve_keyed_by(item=job, field=field, item_name="?")
        yield job


@transforms.add
def make_repackage_description(config, jobs):
    for job in jobs:
        dep_job = get_primary_dependency(config, job)
        assert dep_job

        label = job.get("label", dep_job.label.replace("signing-", "repackage-"))
        job["label"] = label

        yield job


@transforms.add
def make_job_description(config, jobs):
    for job in jobs:
        dep_job = get_primary_dependency(config, job)
        assert dep_job

        attributes = copy_attributes_from_dependent_job(dep_job)
        build_platform = attributes["build_platform"]

        if job["build-platform"].startswith("win"):
            if dep_job.kind.endswith("signing"):
                continue
        if job["build-platform"].startswith("macosx"):
            if dep_job.kind.endswith("repack"):
                continue
        dependencies = {dep_job.attributes.get("kind"): dep_job.label}
        dependencies.update(dep_job.dependencies)

        package_dependency = None
        if build_platform.startswith("macosx"):
            package_dependency = dep_job.kind
        elif build_platform.startswith("win"):
            for dependency in dependencies:
                if dependency.endswith("repack"):
                    package_dependency = dependency

        attributes["repackage_type"] = "repackage"

        repack_id = job["extra"]["repack_id"]

        partner_config = get_partner_config_by_kind(config, config.kind)
        partner, subpartner, _ = repack_id.split("/")
        repack_stub_installer = partner_config[partner][subpartner].get(
            "repack_stub_installer"
        )
        if build_platform.startswith("win32") and repack_stub_installer:
            job["package-formats"].append("installer-stub")

        repackage_config = []
        for format in job.get("package-formats"):
            command = copy.deepcopy(PACKAGE_FORMATS[format])
            substs = {
                "archive_format": archive_format(build_platform),
                "executable_extension": executable_extension(build_platform),
            }
            command["inputs"] = {
                name: filename.format(**substs)
                for name, filename in command["inputs"].items()
            }
            repackage_config.append(command)

        run = job.get("mozharness", {})
        run.update({
            "using": "mozharness",
            "clone-with": "hg",
            "script": "mozharness/scripts/repackage.py",
            "job-script": "taskcluster/scripts/builder/repackage.sh",
            "actions": ["setup", "repackage"],
            "extra-config": {
                "repackage_config": repackage_config,
            },
        })

        worker = {
            "chain-of-trust": True,
            "max-run-time": 3600,
            "taskcluster-proxy": True if get_artifact_prefix(dep_job) else False,
            "env": {
                "REPACK_ID": repack_id,
            },
            # Don't add generic artifact directory.
            "skip-artifacts": True,
        }

        worker_type = "b-linux"

        worker["artifacts"] = _generate_task_output_files(
            dep_job,
            worker_type_implementation(config.graph_config, config.params, worker_type),
            repackage_config,
            partner=repack_id,
        )

        description = (
            "Repackaging for repack_id '{repack_id}' for build '"
            "{build_platform}/{build_type}'".format(
                repack_id=job["extra"]["repack_id"],
                build_platform=attributes.get("build_platform"),
                build_type=attributes.get("build_type"),
            )
        )

        task = {
            "label": job["label"],
            "description": description,
            "worker-type": worker_type,
            "dependencies": dependencies,
            "attributes": attributes,
            "scopes": ["queue:get-artifact:releng/partner/*"],
            "run-on-projects": dep_job.attributes.get("run_on_projects"),
            "routes": job.get("routes", []),
            "extra": job.get("extra", {}),
            "worker": worker,
            "run": run,
            "fetches": _generate_download_config(
                dep_job,
                build_platform,
                package_dependency,
                partner=repack_id,
                project=config.params["project"],
                repack_stub_installer=repack_stub_installer,
            ),
        }

        # we may have reduced the priority for partner jobs, otherwise task.py will set it
        if job.get("priority"):
            task["priority"] = job["priority"]
        if build_platform.startswith("macosx"):
            task.setdefault("fetches", {}).setdefault("toolchain", []).extend([
                "linux64-libdmg",
                "linux64-hfsplus",
                "linux64-node",
            ])
        elif build_platform.startswith("win"):
            task.setdefault("fetches", {}).setdefault("toolchain", []).append(
                "linux64-7zz"
            )
        yield task


def _generate_download_config(
    task,
    build_platform,
    package_dependency,
    partner=None,
    project=None,
    repack_stub_installer=False,
):
    locale_path = f"{partner}/" if partner else ""

    if build_platform.startswith("macosx"):
        return {
            package_dependency: [
                {
                    "artifact": f"{locale_path}target.tar.gz",
                    "extract": False,
                },
            ],
        }
    if build_platform.startswith("win"):
        download_config = [
            {
                "artifact": f"{locale_path}target.zip",
                "extract": False,
            },
            f"{locale_path}setup.exe",
        ]
        if build_platform.startswith("win32") and repack_stub_installer:
            download_config.extend([
                {
                    "artifact": f"{locale_path}target-stub.zip",
                    "extract": False,
                },
                f"{locale_path}setup-stub.exe",
            ])
        return {package_dependency: download_config}

    raise NotImplementedError(f'Unsupported build_platform: "{build_platform}"')


def _generate_task_output_files(task, worker_implementation, repackage_config, partner):
    """We carefully generate an explicit list here, but there's an artifacts directory
    too, courtesy of generic_worker_add_artifacts() (windows) or docker_worker_add_artifacts().
    Any errors here are likely masked by that.
    """
    partner_output_path = f"{partner}/"
    artifact_prefix = get_artifact_prefix(task)

    if worker_implementation == ("docker-worker", "linux"):
        local_prefix = "/builds/worker/workspace/"
    elif worker_implementation == ("generic-worker", "windows"):
        local_prefix = "workspace/"
    else:
        raise NotImplementedError(
            f'Unsupported worker implementation: "{worker_implementation}"'
        )

    output_files = []
    for config in repackage_config:
        output_files.append({
            "type": "file",
            "path": "{}outputs/{}{}".format(
                local_prefix, partner_output_path, config["output"]
            ),
            "name": "{}/{}{}".format(
                artifact_prefix, partner_output_path, config["output"]
            ),
        })
    return output_files
