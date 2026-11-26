# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import re
from typing import Literal, Union

from mozilla_taskgraph.util.attributes import release_level
from taskgraph.util.attributes import _match_run_on

INTEGRATION_PROJECTS = {
    "autoland",
}

TRUNK_PROJECTS = INTEGRATION_PROJECTS | {"mozilla-central", "comm-central"}

# Mapping of project to list of branches that should be considered "release"
# level. A value of `True` means all branches are considered release
# (used by hg.mozilla.org based projects).
PROJECT_RELEASE_BRANCHES: dict[str, Union[list[str], Literal[True]]] = {
    # https://github.com/mozilla-firefox/firefox
    "firefox": [
        "main",
        "beta",
        "release",
        "esr140",
        "esr153",
    ],
    "mozilla-central": True,
    "mozilla-beta": True,
    "mozilla-release": True,
    "mozilla-esr115": True,
    "mozilla-esr140": True,
    "mozilla-esr153": True,
    "comm-central": True,
    "comm-beta": True,
    "comm-release": True,
    "comm-esr140": True,
    "comm-esr153": True,
    # bug 1845368: pine is a permanent project branch used for testing
    # nightly updates
    "pine": True,
    # bug 1877483: larch has similar needs for nightlies
    "larch": True,
    # maple is also an L3 branch: https://phabricator.services.mozilla.com/D184833
    "maple": True,
    # bug 1988213: cypress project branch
    "cypress": True,
}
RELEASE_PROJECTS = set(PROJECT_RELEASE_BRANCHES)
RELEASE_PROMOTION_PROJECTS = {
    "jamun",
    "maple",
    "try",
    "try-comm-central",
} | RELEASE_PROJECTS

TEMPORARY_PROJECTS = set({
    # When using a "Disposable Project Branch" you can specify your branch here. e.g.:
    "oak",
})

TRY_PROJECTS = {
    "staging-firefox",  # https://github.com/mozilla-releng/staging-firefox
    "try",
    "try-comm-central",
}

ALL_PROJECTS = RELEASE_PROMOTION_PROJECTS | TRUNK_PROJECTS | TEMPORARY_PROJECTS

RUN_ON_PROJECT_ALIASES = {
    # key is alias, value is lambda to test it against
    "all": lambda params: True,
    "integration": lambda params: (
        params["project"] in INTEGRATION_PROJECTS or params["project"] == "toolchains"
    ),
    "release": lambda params: (
        release_level(PROJECT_RELEASE_BRANCHES, params) == "production"
        or params["project"] == "toolchains"
    ),
    "trunk": lambda params: (
        params["project"] in TRUNK_PROJECTS or params["project"] == "toolchains"
    ),
    "trunk-only": lambda params: params["project"] in TRUNK_PROJECTS,
    "autoland": lambda params: params["project"] in ("autoland", "toolchains"),
    "autoland-only": lambda params: params["project"] == "autoland",
    "mozilla-central": lambda params: (
        params["project"] in ("mozilla-central", "toolchains")
    ),
    "mozilla-central-only": lambda params: params["project"] == "mozilla-central",
}


def match_run_on_projects(params, run_on_projects):
    """Determine whether the given project is included in the `run-on-projects`
    parameter, applying expansions for things like "integration" mentioned in
    the attribute documentation."""
    aliases = RUN_ON_PROJECT_ALIASES.keys()
    run_aliases = set(aliases) & set(run_on_projects)
    if run_aliases:
        if any(RUN_ON_PROJECT_ALIASES[alias](params) for alias in run_aliases):
            return True

    return params["project"] in run_on_projects


def match_run_on_hg_branches(hg_branch, run_on_hg_branches):
    """Determine whether the given project is included in the `run-on-hg-branches`
    parameter. Allows 'all'."""
    if "all" in run_on_hg_branches:
        return True

    for expected_hg_branch_pattern in run_on_hg_branches:
        if re.match(expected_hg_branch_pattern, hg_branch):
            return True

    return False


match_run_on_repo_type = _match_run_on


def task_name(task):
    if task.label.startswith(task.kind + "-"):
        return task.label[len(task.kind) + 1 :]
    raise AttributeError(f"Task {task.label} does not have a name.")
