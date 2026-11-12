# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Apply some defaults and minor modifications to the jobs defined in the build
kind.
"""

import logging

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by

logger = logging.getLogger(__name__)

transforms = TransformSequence()


@transforms.add
def resolve_keys(config, tasks):
    for task in tasks:
        for key in (
            "worker.channel",
            "worker.commit",
            "worker.submit",
            "worker.dep",
            "worker.certificate-alias",
            "worker.product",
            "routes",
        ):
            resolve_keyed_by(
                task,
                key,
                item_name=task["name"],
                **{
                    "build-type": task["attributes"]["build-type"],
                    "level": config.params["level"],
                },
            )
        yield task


@transforms.add
def schedule_release(config, tasks):
    for task in tasks:
        worker = task["worker"]
        # pushapkscript rejects scheduled_release_date for Google Play.
        if worker.get("submit") and worker["target-store"] != "google":
            release_eta = config.params.get("release_eta")
            if release_eta:
                worker["scheduled-release-date"] = release_eta
            else:
                worker["submit"] = False
                logger.warning(
                    "%s has no release_eta; upload only, without submitting",
                    task["label"],
                )
        yield task
