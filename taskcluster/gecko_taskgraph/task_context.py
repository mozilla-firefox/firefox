# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from mozilla_taskgraph.util.attributes import release_level
from taskgraph.util.task_context import custom_context


@custom_context("release-level")
def release_level_context(config, task):
    return {
        "release-level": release_level(
            config.graph_config["release-branches"], config.params
        )
    }


def source_url(params):
    """URL of the commit being tested, on the repository the push was made to."""
    path = "commit" if params["repository_type"] == "git" else "rev"
    return f"{params['head_repository'].rstrip('/')}/{path}/{params['head_rev']}"


@custom_context("source-url")
def source_url_context(config, task):
    return {"source_url": source_url(config.params)}
