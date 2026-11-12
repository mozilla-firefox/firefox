# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from typing import Optional

import msgspec
from gecko_taskgraph.transforms.task import payload_builder
from taskgraph.util.schema import Schema, taskref_or_string_msgspec


class ArtifactMapPathSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    destinations: list[str]


class ArtifactMapEntrySchema(
    msgspec.Struct, kw_only=True, rename="camel", forbid_unknown_fields=False
):
    task_id: taskref_or_string_msgspec
    paths: dict[str, ArtifactMapPathSchema]


class BeetmoverUpstreamArtifactSchema(
    msgspec.Struct, kw_only=True, rename="camel", forbid_unknown_fields=False
):
    task_id: taskref_or_string_msgspec
    task_type: str
    paths: list[str]


class ScriptworkerBeetmoverSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    action: str
    version: str
    artifact_map: list[ArtifactMapEntrySchema]
    beetmover_application_name: str
    bucket: str
    upstream_artifacts: list[BeetmoverUpstreamArtifactSchema]


@payload_builder("scriptworker-beetmover", schema=ScriptworkerBeetmoverSchema)
def build_scriptworker_beetmover_payload(config, task, task_def):
    worker = task["worker"]

    task_def["tags"]["worker-implementation"] = "scriptworker"

    # Needed by beetmover-scriptworker
    for map_ in worker["artifact-map"]:
        map_["locale"] = "en-US"
        for path_config in map_["paths"].values():
            path_config["checksums_path"] = ""

    task_def["payload"] = {
        "artifactMap": worker["artifact-map"],
        "releaseProperties": {"appName": worker.pop("beetmover-application-name")},
        "upstreamArtifacts": worker["upstream-artifacts"],
        "version": worker["version"],
    }

    scope_prefix = config.graph_config["scriptworker"]["scope-prefix"]
    task_def["scopes"].extend([
        "{}:beetmover:action:{}".format(scope_prefix, worker["action"]),
        "{}:beetmover:bucket:{}".format(scope_prefix, worker["bucket"]),
    ])


class PushApkUpstreamArtifactSchema(
    msgspec.Struct, kw_only=True, rename="camel", forbid_unknown_fields=False
):
    task_id: taskref_or_string_msgspec
    task_type: str
    paths: list[str]


class ScriptworkerPushApkSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    upstream_artifacts: list[PushApkUpstreamArtifactSchema]
    certificate_alias: Optional[str] = None
    target_store: Optional[str] = None
    submit: Optional[bool] = None
    scheduled_release_date: Optional[str] = None
    channel: str
    commit: bool
    product: str
    dep: bool


@payload_builder("scriptworker-pushapk", schema=ScriptworkerPushApkSchema)
def build_push_apk_payload(config, task, task_def):
    worker = task["worker"]

    task_def["tags"]["worker-implementation"] = "scriptworker"

    task_def["payload"] = {
        "channel": worker["channel"],
        "commit": worker["commit"],
        "upstreamArtifacts": worker["upstream-artifacts"],
    }

    if "certificate-alias" in worker:
        task_def["payload"]["certificate_alias"] = worker["certificate-alias"]

    if "target-store" in worker:
        task_def["payload"]["target_store"] = worker["target-store"]

    if "submit" in worker:
        task_def["payload"]["submit"] = worker["submit"]

    if "scheduled-release-date" in worker:
        task_def["payload"]["scheduled_release_date"] = worker["scheduled-release-date"]

    scope_prefix = config.graph_config["scriptworker"]["scope-prefix"]
    task_def["scopes"].append(
        "{}:googleplay:product:{}{}".format(
            scope_prefix, worker["product"], ":dep" if worker["dep"] else ""
        )
    )
