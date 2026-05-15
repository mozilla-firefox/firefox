# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from taskgraph.transforms.base import TransformSequence

transforms = TransformSequence()

_SENTRY_SECRET_TMPL = "project/releng/gecko/build/level-{level}/sentry-upload-{app}"


# Attributes that `copy-attributes` brings over from the upstream build-bundle task but that the
# task transform then overwrites with its own defaults, paired with the task key each has to be
# promoted back into. `shipping_phase` and `shipping_product` are deliberately absent: the task
# transform sets those with setdefault, so the copied values already survive.
_SCHEDULING_ATTRIBUTES = (
    ("run_on_projects", "run-on-projects"),
    ("run_on_repo_type", "run-on-repo-type"),
)


def _get_app(build_type):
    if build_type.startswith("fenix-"):
        return "fenix"
    return "focus-android"


@transforms.add
def sentry_upload(config, tasks):
    level = config.params["level"]
    for task in tasks:
        attributes = task["attributes"]
        build_type = attributes.get("build-type", "")
        secret_path = _SENTRY_SECRET_TMPL.format(
            app=_get_app(build_type),
            level=level,
        )

        task["worker"].setdefault("env", {})["SENTRY_SECRET"] = secret_path
        task.setdefault("scopes", []).append(f"secrets:get:{secret_path}")

        # Mirror the upstream build's scheduling rather than restating it per build type, so an
        # upload never pulls a shippable build into a graph the build itself would not run in.
        for attribute, key in _SCHEDULING_ATTRIBUTES:
            value = attributes.get(attribute)
            if value is not None:
                task.setdefault(key, value)

        yield task
