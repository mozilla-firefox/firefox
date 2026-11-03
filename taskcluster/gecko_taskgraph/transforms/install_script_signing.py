# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the install-script-signing task into an actual task description.
"""

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import Schema

from gecko_taskgraph.util.scriptworker import get_signing_type


class InstallScriptSigningSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    worker: dict[str, object]


transforms = TransformSequence()
transforms.add_validate(InstallScriptSigningSchema)


@transforms.add
def set_signing_type(config, tasks):
    for task in tasks:
        task["worker"]["signing-type"] = get_signing_type(config)
        yield task
