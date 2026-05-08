# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the partner attribution task into an actual task description.
"""

import logging
from collections import defaultdict

from taskgraph.transforms.base import TransformSequence
from taskgraph.util import json
from taskgraph.util.taskcluster import get_artifact_prefix

from gecko_taskgraph.util.partners import (
    apply_partner_priority,
    build_macos_attribution_dmg_command,
    check_if_partners_enabled,
    generate_attribution_code,
    get_ftp_platform,
    get_partner_config_by_kind,
    locales_per_build_platform,
)

log = logging.getLogger(__name__)

transforms = TransformSequence()
transforms.add(check_if_partners_enabled)
transforms.add(apply_partner_priority)


@transforms.add
def add_command_arguments(config, tasks):
    enabled_partners = config.params.get("release_partners")
    attribution_config = get_partner_config_by_kind(config, config.kind)

    for task in tasks:
        dependencies = {}
        fetches = defaultdict(set)
        attributions = []
        release_artifacts = []

        task_platforms = task.pop("platforms", [])

        for partner_config in attribution_config.get("configs", []):
            # we might only be interested in a subset of all partners, eg for a respin
            if enabled_partners and partner_config["campaign"] not in enabled_partners:
                continue
            attribution_code = generate_attribution_code(
                attribution_config["defaults"], partner_config
            )
            for platform in partner_config["platforms"]:
                if platform not in task_platforms:
                    continue

                for locale in locales_per_build_platform(
                    platform, partner_config["locales"]
                ):
                    attributed_build_config = _get_attributed_build_configuration(
                        task, partner_config, platform, locale
                    )
                    upstream_label = attributed_build_config["upstream_label"]
                    if upstream_label not in config.kind_dependencies_tasks:
                        raise Exception(
                            f"Can't find upstream task for {platform} {locale}"
                        )
                    upstream = config.kind_dependencies_tasks[upstream_label]

                    # set the dependencies to just what we need rather than all of l10n
                    dependencies.update({upstream.label: upstream.label})

                    fetches[upstream_label].add(attributed_build_config["fetch_config"])

                    attributions.append({
                        "input": attributed_build_config["input_path"],
                        "output": attributed_build_config["output_path"],
                        "attribution": attribution_code,
                    })
                    release_artifacts.append(
                        attributed_build_config["release_artifact"]
                    )

        if attributions:
            worker = task.get("worker", {})
            worker["chain-of-trust"] = True

            task.setdefault("dependencies", {}).update(dependencies)
            task.setdefault("fetches", {})
            for upstream_label, upstream_artifacts in fetches.items():
                task["fetches"][upstream_label] = [
                    {
                        "artifact": upstream_artifact,
                        "dest": f"{platform}/{locale}",
                        "extract": False,
                        "verify-hash": True,
                    }
                    for upstream_artifact, platform, locale in upstream_artifacts
                ]
            task.setdefault("attributes", {})["release_artifacts"] = release_artifacts

            _build_attribution_config(task, task_platforms, attributions)

            yield task


def _get_attributed_build_configuration(task, partner_config, platform, locale):
    stage_platform = platform.replace("-shippable", "")
    artifact_file_name = _get_artifact_file_name(platform)

    output_artifact = _get_output_path(
        get_artifact_prefix(task), partner_config, platform, locale, artifact_file_name
    )
    return {
        "fetch_config": (
            _get_upstream_artifact_path(artifact_file_name, locale),
            stage_platform,
            locale,
        ),
        "input_path": _get_input_path(stage_platform, locale, artifact_file_name),
        "output_path": f"/builds/worker/artifacts/{output_artifact}",
        "release_artifact": output_artifact,
        "upstream_label": _get_upstream_task_label(platform, locale),
    }


def _get_input_path(stage_platform, locale, artifact_file_name):
    return f"/builds/worker/fetches/{stage_platform}/{locale}/{artifact_file_name}"


def _get_output_path(
    artifact_prefix, partner_config, platform, locale, artifact_file_name
):
    return "{artifact_prefix}/{partner}/{sub_partner}/{ftp_platform}/{locale}/{artifact_file_name}".format(
        artifact_prefix=artifact_prefix,
        partner=partner_config["campaign"],
        sub_partner=partner_config["content"],
        ftp_platform=get_ftp_platform(platform),
        locale=locale,
        artifact_file_name=artifact_file_name,
    )


def _get_artifact_file_name(platform):
    if platform.startswith("win32"):
        return "target.stub-installer.exe"
    elif platform.startswith("macos"):
        return "target.dmg"
    else:
        raise NotImplementedError(f'Case for platform "{platform}" is not implemented')


def _get_upstream_task_label(platform, locale):
    if platform.startswith("win"):
        if locale == "en-US":
            upstream_label = f"repackage-signing-{platform}/opt"
        else:
            upstream_label = f"repackage-signing-l10n-{locale}-{platform}/opt"
    elif platform.startswith("macos"):
        if locale == "en-US":
            upstream_label = f"repackage-{platform}/opt"
        else:
            upstream_label = f"repackage-l10n-{locale}-{platform}/opt"
    else:
        raise NotImplementedError(f'Case for platform "{platform}" is not implemented')

    return upstream_label


def _get_upstream_artifact_path(artifact_file_name, locale):
    return artifact_file_name if locale == "en-US" else f"{locale}/{artifact_file_name}"


def _build_attribution_config(task, task_platforms, attributions):
    if any(p.startswith("win") for p in task_platforms):
        worker = task.get("worker", {})
        worker.setdefault("env", {})["ATTRIBUTION_CONFIG"] = json.dumps(
            attributions, sort_keys=True
        )
    elif any(p.startswith("macos") for p in task_platforms):
        run = task.setdefault("run", {})
        run["command"] = build_macos_attribution_dmg_command(
            "/builds/worker/fetches/dmg/dmg", attributions
        )
    else:
        raise NotImplementedError(
            f"Case for platforms {task_platforms} is not implemented"
        )
