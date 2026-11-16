# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import logging
import os
from typing import Literal, Optional, Union

from taskgraph.parameters import extend_parameters_schema
from taskgraph.util.schema import Schema

from gecko_taskgraph import GECKO
from gecko_taskgraph.files_changed import get_locally_changed_files

logger = logging.getLogger(__name__)


class GitHubConfig(Schema, kw_only=True, rename=None):
    branch: str
    pull_head_sha: str
    pull_number: int
    repo_url: str


class TasksRegex(Schema, kw_only=True):
    include: Optional[list[str]] = None
    exclude: Optional[list[str]] = None


class TryTaskConfig(Schema, kw_only=True):
    tasks: Optional[list[str]] = None
    browsertime: Optional[bool] = None
    disable_pgo: Optional[bool] = None
    env: Optional[dict[str, str]] = None
    gecko_profile: Optional[bool] = None
    gecko_profile_interval: Optional[float] = None
    gecko_profile_entries: Optional[int] = None
    gecko_profile_features: Optional[str] = None
    gecko_profile_threads: Optional[str] = None
    # Github pull request triggering a code-review analysis
    github: Optional[GitHubConfig] = None
    # adjust parameters, chunks, etc. to speed up the process of greening up a new test config.
    new_test_config: Optional[bool] = None
    # Options passed from `mach perftest` to try.
    perftest_options: Optional[object] = None
    # Alternative optimization strategies to use instead of the default.
    # A module path pointing to a dict to be use as the `strategy_override`
    # argument in `taskgraph.optimize.base.optimize_task_graph`.
    optimize_strategies: Optional[str] = None
    # Record an rr trace on supported tasks using the Pernosco debugging service.
    pernosco: Optional[bool] = None
    priority: Optional[Literal["lowest", "very-low", "low"]] = None
    rebuild: Optional[Union[int, dict[str, int]]] = None
    repeat_tests: Optional[int] = None
    tasks_regex: Optional[TasksRegex] = None
    use_artifact_builds: Optional[bool] = None
    # Mapping of worker alias to worker pools to use for those aliases.
    worker_overrides: Optional[dict[str, str]] = None
    # List of worker types that we will use to run tasks on.
    worker_types: Optional[list[str]] = None
    routes: Optional[list[str]] = None


class GeckoParametersSchema(Schema, kw_only=True, rename=None):
    android_perftest_backstop: bool
    app_version: str
    backstop: bool
    dontbuild: bool
    build_number: int
    enable_always_target: Union[bool, list[str]]
    files_changed: list[str]
    hg_branch: Optional[str]
    next_version: Optional[str]
    optimize_strategies: Optional[str]
    phabricator_diff: Optional[str]
    release_enable_emefree: bool
    release_enable_partner_repack: bool
    release_enable_partner_attribution: bool
    release_eta: Optional[str]
    release_history: dict[str, dict]
    release_partners: Optional[list[str]]
    release_partner_config: Optional[dict]
    release_partner_build_number: int
    release_type: str
    release_product: Optional[str]
    shipping: bool
    test_manifest_loader: str
    try_mode: Optional[str]
    try_task_config: TryTaskConfig
    version: str
    head_git_ref: Optional[str] = None
    head_git_repository: Optional[str] = None
    head_git_rev: Optional[str] = None
    pull_request_number: Optional[int] = None


def get_contents(path):
    with open(path) as fh:
        contents = fh.readline().rstrip()
    return contents


def get_version(product_dir="browser"):
    version_path = os.path.join(GECKO, product_dir, "config", "version_display.txt")
    return get_contents(version_path)


def get_app_version(product_dir="browser"):
    app_version_path = os.path.join(GECKO, product_dir, "config", "version.txt")
    return get_contents(app_version_path)


def get_defaults(repo_root=None):
    return {
        "android_perftest_backstop": False,
        "app_version": get_app_version(),
        "backstop": False,
        "dontbuild": False,
        "base_repository": "https://hg.mozilla.org/mozilla-unified",
        "build_number": 1,
        "enable_always_target": ["docker-image"],
        "files_changed": lambda: sorted(get_locally_changed_files(repo_root)),
        "head_repository": "https://hg.mozilla.org/mozilla-central",
        "hg_branch": "default",
        "next_version": None,
        "optimize_strategies": None,
        "phabricator_diff": None,
        "project": "mozilla-central",
        "release_enable_emefree": False,
        "release_enable_partner_repack": False,
        "release_enable_partner_attribution": False,
        "release_eta": "",
        "release_history": {},
        "release_partners": [],
        "release_partner_config": None,
        "release_partner_build_number": 1,
        "release_product": None,
        "release_type": "nightly",
        # This refers to the upstream repo rather than the local checkout, so
        # should be hardcoded to 'hg' even with git-cinnabar.
        "repository_type": "hg",
        "shipping": False,
        "test_manifest_loader": "default",
        "try_mode": None,
        "try_task_config": {},
        "version": get_version(),
    }


def register_parameters():
    extend_parameters_schema(GeckoParametersSchema, defaults_fn=get_defaults)


def get_decision_parameters(graph_config, parameters):
    if pr_number := os.environ.get("GECKO_PULL_REQUEST_NUMBER", None):
        parameters["pull_request_number"] = int(pr_number)
