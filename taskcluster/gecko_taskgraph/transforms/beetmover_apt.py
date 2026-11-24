# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from taskgraph.transforms.base import TransformSequence

from gecko_taskgraph.util.platforms import architecture
from gecko_taskgraph.util.scriptworker import (
    generate_artifact_registry_gcs_sources,
    get_beetmover_apt_repo_scope,
    get_beetmover_repo_action_scope,
)

transforms = TransformSequence()


@transforms.add
def skip_project_branch_nightlies(config, tasks):
    """Project branches such as pine have their own nightly release types and
    don't publish to the APT repos."""
    if config.params["release_type"].startswith("nightly-"):
        return
    yield from tasks


@transforms.add
def filter_out_extra_langpacks(config, tasks):
    """Langpacks are architecture-independent so we only upload them from the x86_64 tasks"""
    for task in tasks:
        deps = list(task["dependencies"])
        for dep in deps:
            dep_task = config.kind_dependencies_tasks[task["dependencies"][dep]]
            if not filter_beetmover_apt_dep(dep_task):
                del task["dependencies"][dep]
        yield task


@transforms.add
def beetmover_apt(config, tasks):
    apt_repo_scope = get_beetmover_apt_repo_scope(config)
    repo_action_scope = get_beetmover_repo_action_scope(config)

    for task in tasks:
        product = task["attributes"]["shipping_product"]
        gcs_sources = []

        for dep_label in task["dependencies"].values():
            dep = config.kind_dependencies_tasks[dep_label]
            gcs_sources.extend(generate_artifact_registry_gcs_sources(dep))

        description = f"Beetmover APT submissions for the {product} {config.params['release_type']} .deb packages"
        task.update({
            "label": f"{config.kind}-{product}",
            "description": description,
            "scopes": [apt_repo_scope, repo_action_scope],
        })
        task.setdefault("worker", {}).update({
            "product": task["attributes"]["shipping_product"],
            "gcs-sources": gcs_sources,
        })
        task.setdefault("treeherder", {})["platform"] = f"{product}-release/opt"
        yield task


def filter_beetmover_apt_dep(task):
    # We only create beetmover-apt tasks for l10n beetmover-repackage tasks that
    # beetmove langpack .deb packages. The langpack .deb packages support all
    # architectures, so we generate them only on x86_64 tasks.
    return is_x86_64_l10n_task(task) or is_not_l10n_task(task)


def is_x86_64_l10n_task(task):
    locale = task.attributes.get("locale")
    return locale and architecture(task.attributes["build_platform"]) == "x86_64"


def is_not_l10n_task(task):
    return not task.attributes.get("locale")
