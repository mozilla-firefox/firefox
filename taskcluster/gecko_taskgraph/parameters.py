# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import logging
import os

from taskgraph.parameters import extend_parameters_schema
from voluptuous import Any, Optional, Required

from gecko_taskgraph import GECKO
from gecko_taskgraph.files_changed import get_locally_changed_files

logger = logging.getLogger(__name__)


gecko_parameters_schema = {
    Required("android_perftest_backstop"): bool,
    Required("app_version"): str,
    Required("backstop"): bool,
    Required("build_number"): int,
    Required("enable_always_target"): Any(bool, [str]),
    Required("files_changed"): [str],
    Required("hg_branch"): str,
    Required("message"): str,
    Required("next_version"): Any(None, str),
    Required("optimize_strategies"): Any(None, str),
    Required("phabricator_diff"): Any(None, str),
    Required("release_enable_emefree"): bool,
    Required("release_enable_partner_repack"): bool,
    Required("release_enable_partner_attribution"): bool,
    Required("release_eta"): Any(None, str),
    Required("release_history"): {str: dict},
    Required("release_partners"): Any(None, [str]),
    Required("release_partner_config"): Any(None, dict),
    Required("release_partner_build_number"): int,
    Required("release_type"): str,
    Required("release_product"): Any(None, str),
    Required("required_signoffs"): [str],
    Required("signoff_urls"): dict,
    Required("test_manifest_loader"): str,
    Required("try_mode"): Any(None, str),
    Required("try_options"): Any(None, dict),
    Required("try_task_config"): {
        Optional("tasks"): [str],
        Optional("browsertime"): bool,
        Optional("chemspill-prio"): bool,
        Optional("disable-pgo"): bool,
        Optional("env"): {str: str},
        Optional("gecko-profile"): bool,
        Optional("gecko-profile-interval"): float,
        Optional("gecko-profile-entries"): int,
        Optional("gecko-profile-features"): str,
        Optional("gecko-profile-threads"): str,
        Optional(
            "new-test-config",
            description="adjust parameters, chunks, etc. to speed up the process "
            "of greening up a new test config.",
        ): bool,
        Optional(
            "perftest-options",
            description="Options passed from `mach perftest` to try.",
        ): object,
        Optional(
            "optimize-strategies",
            description="Alternative optimization strategies to use instead of the default. "
            "A module path pointing to a dict to be use as the `strategy_override` "
            "argument in `taskgraph.optimize.base.optimize_task_graph`.",
        ): str,
        Optional(
            "pernosco",
            description="Record an rr trace on supported tasks using the Pernosco debugging "
            "service.",
        ): bool,
        Optional("rebuild"): int,
        Optional("tasks-regex"): {
            "include": Any(None, [str]),
            "exclude": Any(None, [str]),
        },
        Optional("use-artifact-builds"): bool,
        Optional(
            "worker-overrides",
            description="Mapping of worker alias to worker pools to use for those aliases.",
        ): {str: str},
        Optional(
            "worker-types",
            description="List of worker types that we will use to run tasks on.",
        ): [str],
        Optional("routes"): [str],
    },
    Required("version"): str,
    Optional("head_git_rev"): str,
}


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
        "base_repository": "https://hg.mozilla.org/mozilla-unified",
        "build_number": 1,
        "enable_always_target": ["docker-image"],
        "files_changed": lambda: sorted(get_locally_changed_files(repo_root)),
        "head_repository": "https://hg.mozilla.org/mozilla-central",
        "hg_branch": "default",
        "message": "",
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
        "required_signoffs": [],
        "signoff_urls": {},
        "test_manifest_loader": "default",
        "try_mode": None,
        "try_options": None,
        "try_task_config": {},
        "version": get_version(),
    }


def register_parameters():
    extend_parameters_schema(gecko_parameters_schema, defaults_fn=get_defaults)
