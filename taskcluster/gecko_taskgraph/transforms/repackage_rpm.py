# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

from collections import defaultdict

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.taskcluster import get_artifact_prefix

transforms = TransformSequence()


def _extract_locale_from_langpack_artifact_path(langpack_artifact_path):
    return langpack_artifact_path.split("/")[2]


@transforms.add
def repackage_rpm(config, tasks):
    kind_deps = config.kind_dependencies_tasks
    l10n_signing_kind = (
        "devedition-l10n-signing"
        if config.params["release_product"] == "devedition"
        else "shippable-l10n-signing"
    )
    # langpacks are platform independent, so we just pick one.
    l10n_signing_build_platform = (
        "linux64-devedition"
        if config.params["release_product"] == "devedition"
        else "linux64-shippable"
    )
    for task in tasks:
        # depend on the shippable l10n tasks so we can use the xpi artifacts
        # in the creation of a unified RPM package.
        l10n_signing_deps = {
            dep: dep
            for dep in kind_deps
            if all(
                (
                    kind_deps[dep].kind == l10n_signing_kind,
                    kind_deps[dep].attributes.get("build_platform")
                    == l10n_signing_build_platform,
                )
            )
        }
        fetches = defaultdict(
            list,
            **{
                "build-signing": [
                    {
                        "artifact": "target.tar.xz",
                        "extract": False,
                        "dest": "/builds/worker/fetches",
                    }
                ]
            },
        )
        for l10n_signing_dep in l10n_signing_deps:
            langpack_artifact_paths = [
                release_artifact
                for release_artifact in kind_deps[l10n_signing_dep].attributes[
                    "release_artifacts"
                ]
                if "langpack" in release_artifact
            ]
            for langpack_artifact_path in langpack_artifact_paths:
                locale = _extract_locale_from_langpack_artifact_path(
                    langpack_artifact_path
                )
                prefix = get_artifact_prefix(kind_deps[l10n_signing_dep])
                langpack_artifact_path_no_prefix = langpack_artifact_path[
                    len(prefix) :
                ].lstrip("/")
                fetches[l10n_signing_dep].append(
                    {
                        "artifact": langpack_artifact_path_no_prefix,
                        "extract": False,
                        "dest": f"/builds/worker/fetches/{locale}",
                    }
                )
        task["dependencies"] = {**task["dependencies"], **l10n_signing_deps}
        task["fetches"] = dict(fetches)
        yield task
