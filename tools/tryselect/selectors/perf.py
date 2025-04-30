# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import copy
import itertools
import json
import os
import pathlib
import shutil
import subprocess
import time
from contextlib import redirect_stdout
from datetime import datetime, timedelta

import requests
from mach.util import get_state_dir
from mozbuild.base import MozbuildObject
from mozversioncontrol import get_repository_object

from ..push import generate_try_task_config, push_to_try
from ..util.fzf import (
    FZF_NOT_FOUND,
    build_base_cmd,
    fzf_bootstrap,
    run_fzf,
    setup_tasks_for_fzf,
)
from .compare import CompareParser
from .perfselector.classification import (
    Apps,
    ClassificationProvider,
    Platforms,
    Suites,
    Variants,
)
from .perfselector.perfcomparators import get_comparator
from .perfselector.perfpushinfo import PerfPushInfo
from .perfselector.utils import LogProcessor

here = os.path.abspath(os.path.dirname(__file__))
build = MozbuildObject.from_environment(cwd=here)
cache_file = pathlib.Path(get_state_dir(), "try_perf_revision_cache.json")
PREVIEW_SCRIPT = pathlib.Path(
    build.topsrcdir, "tools/tryselect/selectors/perf_preview.py"
)

PERFHERDER_BASE_URL = (
    "https://treeherder.mozilla.org/perfherder/"
    "compare?originalProject=try&originalRevision=%s&newProject=try&newRevision=%s"
    "&framework=%s"
)
PERFHERDER_BASE_URL_GIT = (
    "https://treeherder.mozilla.org/perfherder/"
    "compare?originalProject=try&originalHash=%s&newProject=try&newHash=%s"
    "&framework=%s"
)
PERFCOMPARE_BASE_URL = (
    "https://perf.compare/compare-results?"
    "baseRev=%s&newRev=%s&baseRepo=try&newRepo=try&framework=%s"
)
PERFCOMPARE_BASE_URL_GIT = (
    "https://perf.compare/compare-results?"
    "baseHash=%s&newHash=%s&baseRepo=try&newRepo=try&framework=%s"
)
TREEHERDER_TRY_BASE_URL = "https://treeherder.mozilla.org/jobs?repo=try&revision=%s"
TREEHERDER_ALERT_TASKS_URL = (
    "https://treeherder.mozilla.org/api/performance/alertsummary-tasks/?id=%s"
)

# Our ./mach try perf changes should only take effect after we have migrated to github
HG_TO_GIT_MIGRATION_COMPLETE = False
ON_GIT = get_repository_object(build.topsrcdir).name == "git"

# Prevent users from running more than 300 tests at once. It's possible, but
# it's more likely that a query is broken and is selecting far too much.
MAX_PERF_TASKS = 600

# Name of the base category with no variants applied to it
BASE_CATEGORY_NAME = "base"

# Add environment variable for firefox-android integration.
# This will let us find the APK to upload automatically. However,
# the following option will need to be supplied:
#       --browsertime-upload-apk firefox-android
# OR    --mozperftest-upload-apk firefox-android
MOZ_FIREFOX_ANDROID_APK_OUTPUT = os.getenv("MOZ_FIREFOX_ANDROID_APK_OUTPUT", None)


class InvalidCategoryException(Exception):
    """Thrown when a category is found to be invalid.

    See the `PerfParser.run_category_checks()` method for more info.
    """

    pass


class APKNotFound(Exception):
    """Raised when a user-supplied path to an APK is invalid."""

    pass


class InvalidRegressionDetectorQuery(Exception):
    """Thrown when the detector query produces anything other than 1 task."""

    pass


class PerfParser(CompareParser):
    name = "perf"
    common_groups = ["push"]
    task_configs = [
        "artifact",
        "browsertime",
        "disable-pgo",
        "env",
        "gecko-profile",
        "path",
        "rebuild",
    ]

    provider = ClassificationProvider()
    platforms = provider.platforms
    apps = provider.apps
    variants = provider.variants
    suites = provider.suites
    categories = provider.categories

    push_info = PerfPushInfo()

    arguments = [
        [
            ["--show-all", "--full", "--all-tasks"],
            {
                "action": "store_true",
                "default": False,
                "help": "Show all available tasks. Alternatively, --full may be used.",
            },
        ],
        [
            ["--chrome"],
            {
                "action": "store_true",
                "default": False,
                "help": "Show tests available for Chrome-based browsers "
                "(disabled by default).",
            },
        ],
        [
            ["--custom-car"],
            {
                "action": "store_true",
                "default": False,
                "help": "Show tests available for Custom Chromium-as-Release (disabled by default)",
            },
        ],
        [
            ["--safari"],
            {
                "action": "store_true",
                "default": False,
                "help": "Show tests available for Safari (disabled by default).",
            },
        ],
        [
            ["--safari-tp"],
            {
                "action": "store_true",
                "default": False,
                "help": "Show tests available for Safari Technology Preview(disabled by default).",
            },
        ],
        [
            ["--live-sites"],
            {
                "action": "store_true",
                "default": False,
                "help": "Run tasks with live sites (if possible). "
                "You can also use the `live-sites` variant.",
            },
        ],
        [
            ["--profile"],
            {
                "action": "store_true",
                "default": False,
                "help": "Run tasks with profiling (if possible). "
                "You can also use the `profiling` variant.",
            },
        ],
        [
            ["--single-run"],
            {
                "action": "store_true",
                "default": False,
                "help": "Run tasks without a comparison",
            },
        ],
        [
            ["-q", "--query"],
            {
                "type": str,
                "default": None,
                "help": "Query to run in either the perf-category selector, "
                "or the fuzzy selector if --show-all/--full is provided.",
            },
        ],
        [
            # Bug 1866047 - Remove once monorepo changes are complete
            ["--browsertime-upload-apk"],
            {
                "type": str,
                "default": None,
                "help": "Path to an APK to upload. Note that this "
                "will replace the APK installed in all Android Performance "
                "tests. If the Activity, Binary Path, or Intents required "
                "change at all relative to the existing GeckoView, and Fenix "
                "tasks, then you will need to make fixes in the associated "
                "taskcluster files (e.g. taskcluster/kinds/test/browsertime-mobile.yml). "
                "Alternatively, set MOZ_FIREFOX_ANDROID_APK_OUTPUT to a path to "
                "an APK, and then run the command with --browsertime-upload-apk "
                "firefox-android. This option will only copy the APK for browsertime, see "
                "--mozperftest-upload-apk to upload APKs for startup tests.",
            },
        ],
        [
            # Bug 1866047 - Remove once monorepo changes are complete
            ["--mozperftest-upload-apk"],
            {
                "type": str,
                "default": None,
                "help": "See --browsertime-upload-apk. This option does the same "
                "thing except it's for mozperftest tests such as the startup ones. "
                "Note that those tests only exist through --show-all/--full as they "
                "aren't contained in any existing categories.",
            },
        ],
        [
            ["--detect-changes"],
            {
                "action": "store_true",
                "default": False,
                "help": "Adds a task that detects performance changes using MWU.",
            },
        ],
        [
            ["--comparator"],
            {
                "type": str,
                "default": "BasePerfComparator",
                "help": "Either a path to a file to setup a custom comparison, "
                "or a builtin name. See the Firefox source docs for mach try perf for "
                "examples of how to build your own, along with the interface.",
            },
        ],
        [
            ["--comparator-args"],
            {
                "nargs": "*",
                "type": str,
                "default": [],
                "dest": "comparator_args",
                "help": "Arguments provided to the base, and new revision setup stages "
                "of the comparator.",
                "metavar": "ARG=VALUE",
            },
        ],
        [
            ["--variants"],
            {
                "nargs": "*",
                "type": str,
                "default": [BASE_CATEGORY_NAME],
                "dest": "requested_variants",
                "choices": list(variants.keys()),
                "help": "Select variants to display in the selector from: "
                + ", ".join(list(variants.keys())),
                "metavar": "",
            },
        ],
        [
            ["--platforms"],
            {
                "nargs": "*",
                "type": str,
                "default": [],
                "dest": "requested_platforms",
                "choices": list(platforms.keys()),
                "help": "Select specific platforms to target. Available platforms: "
                + ", ".join(list(platforms.keys())),
                "metavar": "",
            },
        ],
        [
            ["--apps"],
            {
                "nargs": "*",
                "type": str,
                "default": [],
                "dest": "requested_apps",
                "choices": list(apps.keys()),
                "help": "Select specific applications to target from: "
                + ", ".join(list(apps.keys())),
                "metavar": "",
            },
        ],
        [
            ["--clear-cache"],
            {
                "action": "store_true",
                "default": False,
                "help": "Deletes the try_perf_revision_cache file",
            },
        ],
        [
            ["--alert"],
            {
                "type": str,
                "default": None,
                "help": "Run all tests that produced this alert summary ID "
                "based on the alert summary table in either the alerts view or "
                "the regression bug. The comparison that is produced will be based on "
                "the base revision in your local repository (i.e. the base revision "
                "your patches, if any, are based on). If only specific tests "
                "need to run, use --tests to specify them (e.g. --tests webaudio).",
            },
        ],
        [
            ["--extra-args"],
            {
                "nargs": "*",
                "type": str,
                "default": [],
                "dest": "extra_args",
                "help": "Set the extra args "
                "(e.x, --extra-args verbose post-startup-delay=1)",
                "metavar": "",
            },
        ],
        [
            ["--non-pgo"],
            {
                "action": "store_true",
                "default": False,
                "help": "Use opt/non-pgo builds instead of shippable/pgo builds. "
                "Setting this flag will result in faster try runs.",
            },
        ],
        [
            ["--tests", "-t"],
            {
                "nargs": "*",
                "type": str,
                "default": [],
                "dest": "tests",
                "help": "Select from all tasks that run these specific tests "
                "(e.g. amazon, or speedometer3).",
            },
        ],
    ]

    def get_tasks(base_cmd, queries, query_arg=None, candidate_tasks=None):
        cmd = base_cmd[:]
        if query_arg:
            cmd.extend(["-f", query_arg])

        query_str, tasks = run_fzf(cmd, sorted(candidate_tasks))
        queries.append(query_str)
        return set(tasks)

    def get_perf_tasks(base_cmd, all_tg_tasks, perf_categories, query=None):
        # Convert the categories to tasks
        selected_tasks = set()
        queries = []

        selected_categories = PerfParser.get_tasks(
            base_cmd, queries, query, perf_categories
        )

        for category, category_info in perf_categories.items():
            if category not in selected_categories:
                continue
            print("Gathering tasks for %s category" % category)

            category_tasks = set()
            for suite in PerfParser.suites:
                # Either perform a query to get the tasks (recommended), or
                # use a hardcoded task list
                suite_queries = category_info["queries"].get(suite)

                category_suite_tasks = set()
                if suite_queries:
                    print(
                        "Executing %s queries: %s" % (suite, ", ".join(suite_queries))
                    )

                    for perf_query in suite_queries:
                        if not category_suite_tasks:
                            # Get all tasks selected with the first query
                            category_suite_tasks |= PerfParser.get_tasks(
                                base_cmd, queries, perf_query, all_tg_tasks
                            )
                        else:
                            # Keep only those tasks that matched in all previous queries
                            category_suite_tasks &= PerfParser.get_tasks(
                                base_cmd, queries, perf_query, category_suite_tasks
                            )

                        if len(category_suite_tasks) == 0:
                            print("Failed to find any tasks for query: %s" % perf_query)
                            break

                    if category_suite_tasks:
                        category_tasks |= category_suite_tasks

            if category_info["tasks"]:
                category_tasks = set(category_info["tasks"]) & all_tg_tasks
                if category_tasks != set(category_info["tasks"]):
                    print(
                        "Some expected tasks could not be found: %s"
                        % ", ".join(category_info["tasks"] - category_tasks)
                    )

            if not category_tasks:
                print("Could not find any tasks for category %s" % category)
            else:
                # Add the new tasks to the currently selected ones
                selected_tasks |= category_tasks

        return selected_tasks, selected_categories, queries

    def _check_app(app, target):
        """Checks if the app exists in the target."""
        if app.value in target:
            return True
        return False

    def _check_platform(platform, target):
        """Checks if the platform, or it's type exists in the target."""
        if (
            platform.value in target
            or PerfParser.platforms[platform.value]["platform"] in target
        ):
            return True
        return False

    def _build_initial_decision_matrix():
        # Build first stage of matrix APPS X PLATFORMS
        initial_decision_matrix = []
        for platform in Platforms:
            platform_row = []
            for app in Apps:
                if PerfParser._check_platform(
                    platform, PerfParser.apps[app.value]["platforms"]
                ):
                    # This app can run on this platform
                    platform_row.append(True)
                else:
                    platform_row.append(False)
            initial_decision_matrix.append(platform_row)
        return initial_decision_matrix

    def _build_intermediate_decision_matrix():
        # Second stage of matrix building applies the 2D matrix found above
        # to each suite
        initial_decision_matrix = PerfParser._build_initial_decision_matrix()

        intermediate_decision_matrix = []
        for suite in Suites:
            suite_matrix = copy.deepcopy(initial_decision_matrix)
            suite_info = PerfParser.suites[suite.value]

            # Restric the platforms for this suite now
            for platform in Platforms:
                for app in Apps:
                    runnable = False
                    if PerfParser._check_app(
                        app, suite_info["apps"]
                    ) and PerfParser._check_platform(platform, suite_info["platforms"]):
                        runnable = True
                    suite_matrix[platform][app] = (
                        runnable and suite_matrix[platform][app]
                    )

            intermediate_decision_matrix.append(suite_matrix)
        return intermediate_decision_matrix

    def _build_variants_matrix():
        # Third stage is expanding the intermediate matrix
        # across all the variants (non-expanded). Start with the
        # intermediate matrix in the list since it provides our
        # base case with no variants
        intermediate_decision_matrix = PerfParser._build_intermediate_decision_matrix()

        variants_matrix = []
        for variant in Variants:
            variant_matrix = copy.deepcopy(intermediate_decision_matrix)

            for suite in Suites:
                if variant.value in PerfParser.suites[suite.value]["variants"]:
                    # Allow the variant through and set it's platforms and apps
                    # based on how it sets it -> only restrict, don't make allowances
                    # here
                    for platform in Platforms:
                        for app in Apps:
                            if not (
                                PerfParser._check_platform(
                                    platform,
                                    PerfParser.variants[variant.value]["platforms"],
                                )
                                and PerfParser._check_app(
                                    app, PerfParser.variants[variant.value]["apps"]
                                )
                            ):
                                variant_matrix[suite][platform][app] = False
                else:
                    # This variant matrix needs to be completely False
                    variant_matrix[suite] = [
                        [False] * len(platform_row)
                        for platform_row in variant_matrix[suite]
                    ]

            variants_matrix.append(variant_matrix)

        return variants_matrix, intermediate_decision_matrix

    def _build_decision_matrix():
        """Build the decision matrix.

        This method builds the decision matrix that is used
        to determine what categories will be shown to the user.
        This matrix has the following form (as lists):
            - Variants
                - Suites
                    - Platforms
                        - Apps

        Each element in the 4D Matrix is either True or False and tells us
        whether the particular combination is "runnable" according to
        the given specifications. This does not mean that the combination
        exists, just that it's fully configured in this selector.

        The ("base",) variant combination found in the matrix has
        no variants applied to it. At this stage, it's a catch-all for those
        categories. The query it uses is reduced further in later stages.
        """
        # Get the variants matrix (see methods above) and the intermediate decision
        # matrix to act as the base category
        (
            variants_matrix,
            intermediate_decision_matrix,
        ) = PerfParser._build_variants_matrix()

        # Get all possible combinations of the variants
        expanded_variants = [
            variant_combination
            for set_size in range(len(Variants) + 1)
            for variant_combination in itertools.combinations(list(Variants), set_size)
        ]

        # Final stage combines the intermediate matrix with the
        # expanded variants and leaves a "base" category which
        # doesn't have any variant specifications (it catches them all)
        decision_matrix = {(BASE_CATEGORY_NAME,): intermediate_decision_matrix}
        for variant_combination in expanded_variants:
            expanded_variant_matrix = []

            # Perform an AND operation on the combination of variants
            # to determine where this particular combination can run
            for suite in Suites:
                suite_matrix = []
                suite_variants = PerfParser.suites[suite.value]["variants"]

                # Disable the variant combination if none of them
                # are found in the suite
                disable_variant = not any(
                    [variant.value in suite_variants for variant in variant_combination]
                )

                for platform in Platforms:
                    if disable_variant:
                        platform_row = [False for _ in Apps]
                    else:
                        platform_row = [
                            all(
                                variants_matrix[variant][suite][platform][app]
                                for variant in variant_combination
                                if variant.value in suite_variants
                            )
                            for app in Apps
                        ]
                    suite_matrix.append(platform_row)

                expanded_variant_matrix.append(suite_matrix)
            decision_matrix[variant_combination] = expanded_variant_matrix

        return decision_matrix

    def _skip_with_restrictions(value, restrictions, requested=[]):
        """Determines if we should skip an app, platform, or variant.

        We add base here since it's the base category variant that
        would always be displayed and it won't affect the app, or
        platform selections.
        """
        if restrictions is not None and value not in restrictions + [
            BASE_CATEGORY_NAME
        ]:
            return True
        if requested and value not in requested + [BASE_CATEGORY_NAME]:
            return True
        return False

    def build_category_matrix(**kwargs):
        """Build a decision matrix for all the categories.

        It will have the form:
            - Category
                - Variants
                    - ...
        """
        requested_variants = kwargs.get("requested_variants", [BASE_CATEGORY_NAME])
        requested_platforms = kwargs.get("requested_platforms", [])
        requested_apps = kwargs.get("requested_apps", [])

        # Build the base decision matrix
        decision_matrix = PerfParser._build_decision_matrix()

        # Here, the variants are further restricted by the category settings
        # using the `_skip_with_restrictions` method. This part also handles
        # explicitly requested platforms, apps, and variants.
        category_decision_matrix = {}
        for category, category_info in PerfParser.categories.items():
            category_matrix = copy.deepcopy(decision_matrix)

            for variant_combination, variant_matrix in decision_matrix.items():
                variant_runnable = True
                if BASE_CATEGORY_NAME not in variant_combination:
                    # Make sure that all portions of the variant combination
                    # target at least one of the suites in the category
                    tmp_variant_combination = set(
                        [v.value for v in variant_combination]
                    )
                    for suite in Suites:
                        if suite.value not in category_info["suites"]:
                            continue
                        tmp_variant_combination = tmp_variant_combination - set(
                            [
                                variant.value
                                for variant in variant_combination
                                if variant.value
                                in PerfParser.suites[suite.value]["variants"]
                            ]
                        )
                    if tmp_variant_combination:
                        # If it's not empty, then some variants
                        # are non-existent
                        variant_runnable = False

                for suite, platform, app in itertools.product(Suites, Platforms, Apps):
                    runnable = variant_runnable

                    # Disable this combination if there are any variant
                    # restrictions for this suite, or if the user didn't request it
                    # (and did request some variants). The same is done below with
                    # the apps, and platforms.
                    if any(
                        PerfParser._skip_with_restrictions(
                            variant.value if not isinstance(variant, str) else variant,
                            category_info.get("variant-restrictions", {}).get(
                                suite.value, None
                            ),
                            requested_variants,
                        )
                        for variant in variant_combination
                    ):
                        runnable = False

                    if PerfParser._skip_with_restrictions(
                        platform.value,
                        category_info.get("platform-restrictions", None),
                        requested_platforms,
                    ):
                        runnable = False

                    # If the platform is restricted, check if the appropriate
                    # flags were provided (or appropriate conditions hit). We do
                    # the same thing for apps below.
                    if (
                        PerfParser.platforms[platform.value].get("restriction", None)
                        is not None
                    ):
                        runnable = runnable and PerfParser.platforms[platform.value][
                            "restriction"
                        ](**kwargs)

                    if PerfParser._skip_with_restrictions(
                        app.value,
                        category_info.get("app-restrictions", {}).get(
                            suite.value, None
                        ),
                        requested_apps,
                    ):
                        runnable = False
                    if PerfParser.apps[app.value].get("restriction", None) is not None:
                        runnable = runnable and PerfParser.apps[app.value][
                            "restriction"
                        ](**kwargs)

                    category_matrix[variant_combination][suite][platform][app] = (
                        runnable and variant_matrix[suite][platform][app]
                    )

            category_decision_matrix[category] = category_matrix

        return category_decision_matrix

    def _enable_restriction(restriction, **kwargs):
        """Used to simplify checking a restriction."""
        return restriction is not None and restriction(**kwargs)

    def _category_suites(category_info):
        """Returns all the suite enum entries in this category."""
        return [suite for suite in Suites if suite.value in category_info["suites"]]

    def _add_variant_queries(
        category_info, variant_matrix, variant_combination, platform, queries, app=None
    ):
        """Used to add the variant queries to various categories."""
        for variant in variant_combination:
            for suite in PerfParser._category_suites(category_info):
                if (app is not None and variant_matrix[suite][platform][app]) or (
                    app is None and any(variant_matrix[suite][platform])
                ):
                    queries[suite.value].append(
                        PerfParser.variants[variant.value]["query"]
                    )

    def _build_categories(category, category_info, category_matrix, **kwargs):
        """Builds the categories to display."""
        categories = {}

        for variant_combination, variant_matrix in category_matrix.items():
            base_category = BASE_CATEGORY_NAME in variant_combination

            for platform in Platforms:
                if not any(
                    any(variant_matrix[suite][platform])
                    for suite in PerfParser._category_suites(category_info)
                ):
                    # There are no apps available on this platform in either
                    # of the requested suites
                    continue

                # This code has the effect of restricting all suites to
                # a platform. This means categories with mixed suites will
                # be available even if some suites will no longer run
                # given this platform constraint. The reasoning for this is that
                # it's unexpected to receive desktop tests when you explicitly
                # request android.
                platform_queries = {
                    suite: (
                        category_info["query"][suite]
                        + [
                            PerfParser.platforms[platform.value]["query"].get(
                                suite,
                                PerfParser.platforms[platform.value]["query"][
                                    "default"
                                ],
                            )
                        ]
                    )
                    for suite in category_info["suites"]
                }

                if kwargs.get("non_pgo"):
                    for key, query_list in platform_queries.items():
                        updated_query_list = []
                        for query in query_list:
                            updated_query = query.replace(
                                "'shippable", "!shippable !nightlyasrelease"
                            )
                            updated_query_list.append(updated_query)
                        platform_queries[key] = updated_query_list

                platform_category_name = f"{category} {platform.value}"
                platform_category_info = {
                    "queries": platform_queries,
                    "tasks": category_info["tasks"],
                    "platform": platform,
                    "app": None,
                    "suites": category_info["suites"],
                    "base-category": base_category,
                    "base-category-name": category,
                    "description": category_info["description"],
                }
                for app in Apps:
                    if not any(
                        variant_matrix[suite][platform][app]
                        for suite in PerfParser._category_suites(category_info)
                    ):
                        # This app is not available on the given platform
                        # for any of the suites
                        continue

                    # Add the queries for the app for any suites that need it and
                    # the variant queries if needed
                    app_queries = copy.deepcopy(platform_queries)
                    for suite in Suites:
                        if suite.value not in app_queries:
                            continue
                        app_queries[suite.value].append(
                            PerfParser.apps[app.value]["query"]
                        )
                    if not base_category:
                        PerfParser._add_variant_queries(
                            category_info,
                            variant_matrix,
                            variant_combination,
                            platform,
                            app_queries,
                            app=app,
                        )

                    app_category_name = f"{platform_category_name} {app.value}"
                    if not base_category:
                        app_category_name = (
                            f"{app_category_name} "
                            f"{'+'.join([v.value for v in variant_combination])}"
                        )
                    categories[app_category_name] = {
                        "queries": app_queries,
                        "tasks": category_info["tasks"],
                        "platform": platform,
                        "app": app,
                        "suites": category_info["suites"],
                        "base-category": base_category,
                        "description": category_info["description"],
                    }

                if not base_category:
                    platform_category_name = (
                        f"{platform_category_name} "
                        f"{'+'.join([v.value for v in variant_combination])}"
                    )
                    PerfParser._add_variant_queries(
                        category_info,
                        variant_matrix,
                        variant_combination,
                        platform,
                        platform_queries,
                    )
                categories[platform_category_name] = platform_category_info

        return categories

    def _handle_variant_negations(category, category_info, **kwargs):
        """Handle variant negations.

        The reason why we're negating variants here instead of where we add
        them to the queries is because we need to iterate over all of the variants
        but when we add them, we only look at the variants in the combination. It's
        possible to combine these, but that increases the complexity of the code
        by quite a bit so it's best to do it separately.
        """
        for variant in Variants:
            if category_info["base-category"] and variant.value in kwargs.get(
                "requested_variants", [BASE_CATEGORY_NAME]
            ):
                # When some particular variant(s) are requested, and we are at a
                # base category, don't negate it. Otherwise, if the variant
                # wasn't requested negate it
                continue
            if variant.value in category:
                # If this variant is in the category name, skip negations
                continue
            if not PerfParser._check_platform(
                category_info["platform"],
                PerfParser.variants[variant.value]["platforms"],
            ):
                # Make sure the variant applies to the platform
                continue

            for suite in category_info["suites"]:
                if variant.value not in PerfParser.suites[suite]["variants"]:
                    continue
                category_info["queries"][suite].append(
                    PerfParser.variants[variant.value]["negation"]
                )

    def _handle_app_negations(category, category_info, **kwargs):
        """Handle app negations.

        This is where the global chrome/safari negations get added. We use kwargs
        along with the app restriction method to make this decision.
        """
        for app in Apps:
            if PerfParser.apps[app.value].get("negation", None) is None:
                continue
            elif any(
                PerfParser.apps[app.value]["negation"]
                in category_info["queries"][suite]
                for suite in category_info["suites"]
            ):
                # Already added the negations
                continue
            if category_info.get("app", None) is not None:
                # We only need to handle this for categories that
                # don't specify an app
                continue

            if PerfParser.apps[app.value].get("restriction", None) is None:
                # If this app has no restriction flag, it means we should select it
                # as much as possible and not negate it. However, if specific apps were requested,
                # we should allow the negation to proceed since a `negation` field
                # was provided (checked above), assuming this app was requested.
                requested_apps = kwargs.get("requested_apps", [])
                if requested_apps and app.value in requested_apps:
                    # Apps were requested, and this was is included
                    continue
                elif not requested_apps:
                    # Apps were not requested, so we should keep this one
                    continue

            if PerfParser._enable_restriction(
                PerfParser.apps[app.value].get("restriction", None), **kwargs
            ):
                continue

            for suite in category_info["suites"]:
                if app.value not in PerfParser.suites[suite]["apps"]:
                    continue
                category_info["queries"][suite].append(
                    PerfParser.apps[app.value]["negation"]
                )

    def _handle_negations(category, category_info, **kwargs):
        """This method handles negations.

        This method should only include things that should be globally applied
        to all the queries. The apps are included as chrome is negated if
        --chrome isn't provided, and the variants are negated here too.
        """
        PerfParser._handle_variant_negations(category, category_info, **kwargs)
        PerfParser._handle_app_negations(category, category_info, **kwargs)

    def get_categories(**kwargs):
        """Get the categories to be displayed.

        The categories are built using the decision matrices from `build_category_matrix`.
        The methods above provide more detail on how this is done. Here, we use
        this matrix to determine if we should show a category to a user.

        We also apply the negations for restricted apps/platforms and variants
        at the end before displaying the categories.
        """
        categories = {}

        # Setup the restrictions, and ease-of-use variants requested (if any)
        for variant in Variants:
            if PerfParser._enable_restriction(
                PerfParser.variants[variant.value].get("restriction", None), **kwargs
            ):
                kwargs.setdefault("requested_variants", []).append(variant.value)

        category_decision_matrix = PerfParser.build_category_matrix(**kwargs)

        # Now produce the categories by finding all the entries that are True
        for category, category_matrix in category_decision_matrix.items():
            categories.update(
                PerfParser._build_categories(
                    category, PerfParser.categories[category], category_matrix, **kwargs
                )
            )

        # Handle the restricted app queries, and variant negations
        for category, category_info in categories.items():
            PerfParser._handle_negations(category, category_info, **kwargs)

        return categories

    def _get_common_test_task_substring(tasks):
        """Returns the longest common substring among a set of task labels"""

        def __substrings(task_label):
            return {
                task_label[i:j]
                for j in range(len(task_label) + 1)
                for i in range(j + 1)
            }

        return max(set.intersection(*map(__substrings, tasks)), key=len)

    def set_categories_for_test(full_task_graph_path, tests):
        """Parses the full task-graph to find all tasks that run this test.

        Returns a new category for the test to replace our existing ones.
        """
        print("Searching for requested tests in the generated tasks...")
        with full_task_graph_path.open() as f:
            full_task_graph = json.load(f)

        all_tasks = set()
        categories = {}
        for test in tests:
            tasks = set()
            found_suite = ""

            for task_label, task_info in full_task_graph.items():
                cmds = task_info.get("task", {}).get("payload", {}).get("command", [])
                for suite, suite_info in PerfParser.suites.items():
                    if suite_info["task-specifier"] not in task_label:
                        continue
                    modified_task_label = PerfParser.suites[suite]["task-test-finder"](
                        cmds, task_label, test
                    )
                    if modified_task_label:
                        found_suite = suite
                        all_tasks.add(task_label)
                        tasks.add(modified_task_label)

            if not tasks:
                print(f"Could not find any tasks for test {test}")
                continue

            query = PerfParser._get_common_test_task_substring(tasks)
            categories[test] = {
                "query": {found_suite: [query]},
                "suites": [found_suite],
                "variant-restrictions": {},
                "app-restrictions": {},
                "tasks": [],
                "description": f"Tasks that run the test {test}.",
            }

        PerfParser.categories = categories
        return all_tasks

    def inject_change_detector(base_cmd, all_tasks, selected_tasks):
        query = "'perftest 'mwu 'detect"
        mwu_task = PerfParser.get_tasks(base_cmd, [], query, all_tasks)

        if len(mwu_task) > 1 or len(mwu_task) == 0:
            raise InvalidRegressionDetectorQuery(
                f"Expected 1 task from change detector "
                f"query, but found {len(mwu_task)}"
            )

        selected_tasks |= set(mwu_task)

    def check_cached_revision(selected_tasks, base_commit=None):
        """
        If the base_commit parameter does not exist, remove expired cache data.
        Cache data format:
        {
                base_commit[str]: [
                    {
                        "base_revision_treeherder": "2b04563b5",
                        "date": "2023-03-12",
                        "tasks": ["a-task"],
                    },
                    {
                        "base_revision_treeherder": "999998888",
                        "date": "2023-03-12",
                        "tasks": ["b-task"],
                    },
                ]
        }

        The list represents different pushes with different task selections.

        TODO: See if we can request additional tests on a given base revision.

        :param selected_tasks list: The list of tasks selected by the user
        :param base_commit str: The base commit to search
        :return: The base_revision_treeherder if found, else None
        """
        today = datetime.now()
        expired_date = (today - timedelta(weeks=2)).strftime("%Y-%m-%d")
        today = today.strftime("%Y-%m-%d")

        if not cache_file.is_file():
            return

        with cache_file.open("r") as f:
            cache_data = json.load(f)

        # Remove expired cache data
        if base_commit is None:
            for cached_base_commit in list(cache_data):
                if not isinstance(cache_data[cached_base_commit], list):
                    # TODO: Remove in the future, this is for backwards-compatibility
                    # with the previous cache structure
                    cache_data.pop(cached_base_commit)
                else:
                    # Go through the pushes, and expire any that are too old
                    new_pushes = []
                    for push in cache_data[cached_base_commit]:
                        if push["date"] > expired_date:
                            new_pushes.append(push)
                    # If no pushes are left after expiration, expire the base commit
                    if new_pushes:
                        cache_data[cached_base_commit] = new_pushes
                    else:
                        cache_data.pop(cached_base_commit)
            with cache_file.open("w") as f:
                json.dump(cache_data, f, indent=4)

        cached_base_commit = cache_data.get(base_commit, None)
        if cached_base_commit:
            for push in cached_base_commit:
                if set(selected_tasks) <= set(push["tasks"]):
                    return push["base_revision_treeherder"]

    def save_revision_treeherder(selected_tasks, base_commit):
        """
        Save the base revision of treeherder to the cache.
        See "check_cached_revision" for more information about the data structure.

        :param selected_tasks list: The list of tasks selected by the user
        :param base_commit str: The base commit to save
        :param base_revision_treeherder str: The base revision of treeherder to save
        :return: None
        """
        today = datetime.now().strftime("%Y-%m-%d")
        new_revision = {
            "date": today,
            "tasks": list(selected_tasks),
            "base_revision_treeherder": (
                PerfParser.push_info.base_hash
                if ON_GIT and HG_TO_GIT_MIGRATION_COMPLETE
                else PerfParser.push_info.base_revision
            ),
        }

        cache_data = {}

        if cache_file.is_file():
            with cache_file.open("r") as f:
                cache_data = json.load(f)
                cache_data.setdefault(base_commit, []).append(new_revision)
        else:
            cache_data[base_commit] = [new_revision]

        with cache_file.open(mode="w") as f:
            json.dump(cache_data, f, indent=4)

    def found_android_tasks(selected_tasks):
        """
        Check if any of the selected tasks are android.

        :param selected_tasks list: List of tasks selected.
        :return bool: True if android tasks were found, False otherwise.
        """
        return any("android" in task for task in selected_tasks)

    def setup_try_config(try_config_params, extra_args, selected_tasks):
        """
        Setup the try config for a push.

        :param try_config_params dict: The current try config to be modified.
        :param extra_args list: A list of extra options to add to the tasks being run.
        :param selected_tasks list: List of tasks selected. Used for determining if android
            tasks are selected to disable artifact mode.
        :param base_revision_treeherder str: The base revision of treeherder to save
        :return: None
        """
        if try_config_params is None:
            try_config_params = {}

        try_config = try_config_params.setdefault("try_task_config", {})
        env = try_config.setdefault("env", {})
        if extra_args:
            args = " ".join(extra_args)
            env["PERF_FLAGS"] = args
        if PerfParser.push_info.base_revision:
            # Reset updated since we no longer need to worry
            # about failing while we're on a base commit
            env["PERF_BASE_REVISION"] = PerfParser.push_info.base_revision
        if PerfParser.found_android_tasks(selected_tasks) and try_config.get(
            "use-artifact-builds", False
        ):
            # XXX: Fix artifact mode on android (no bug)
            try_config["use-artifact-builds"] = False
            print("Disabling artifact mode due to android task selection")

            if try_config.get("disable-pgo", False):
                print(
                    "WARNING: PGO builds are disabled as artifact mode is "
                    "enabled by default from your mozconfig."
                )

    def get_majority_framework(selected_tasks):
        suite_counts = {suite: 0 for suite in PerfParser.suites.keys()}

        for task in selected_tasks:
            for suite, suite_info in PerfParser.suites.items():
                if suite_info["task-specifier"] in task:
                    suite_counts[suite] += 1
                    break

        if all(value == 0 for value in suite_counts.values()):
            PerfParser.push_info.framework = 1
        else:
            PerfParser.push_info.framework = PerfParser.suites[
                max(suite_counts, key=suite_counts.get)
            ]["framework"]

    def perf_push_to_try(
        selected_tasks,
        selected_categories,
        queries,
        try_config_params,
        dry_run,
        single_run,
        extra_args,
        comparator,
        comparator_args,
        alert_summary_id,
    ):
        """Perf-specific push to try method.

        This makes use of logic from the CompareParser to do something
        very similar except with log redirection. We get the comparison
        revisions, then use the repository object to update between revisions
        and the LogProcessor for parsing out the revisions that are used
        to build the Perfherder links.
        """
        vcs = get_repository_object(build.topsrcdir)
        compare_commit, current_revision_ref = PerfParser.get_revisions_to_run(
            vcs, None
        )

        # Build commit message, and limit first line to 200 characters
        selected_categories_msg = ", ".join(selected_categories)
        if len(selected_categories_msg) > 200:
            selected_categories_msg = f"{selected_categories_msg[:200]}...\n...{selected_categories_msg[200:]}"
        msg = f"Perf selections={selected_categories_msg} \nQueries={json.dumps(queries, indent=4)}"
        if alert_summary_id:
            msg = f"Perf alert summary id={alert_summary_id}"

        new_commit_message = msg
        base_commit_message = msg
        new_commit_hash = ""
        base_commit_hash = ""
        if ON_GIT and HG_TO_GIT_MIGRATION_COMPLETE:
            # commit_author_and_time is used for the new_revision_hash
            new_commit_hash = str(
                hash(
                    subprocess.getoutput("git show -s --format='%ae'")
                    + str(time.time())
                )
            )
            new_commit_message += "\ncommit_id:" + new_commit_hash

            # current_user_and_current_commit is used for the base_revision_hash so it can be reused
            base_commit_hash = str(
                hash(
                    subprocess.getoutput("git show -s --format='%H'")
                    + subprocess.getoutput("git config user.email")
                )
            )
            base_commit_message += "\ncommit_id:" + base_commit_hash

        # Get the comparator to run
        comparator_klass = get_comparator(comparator)
        comparator_obj = comparator_klass(
            vcs, compare_commit, current_revision_ref, comparator_args
        )
        base_comparator = True
        if comparator_klass.__name__ != "BasePerfComparator":
            base_comparator = False

        try:
            # redirect_stdout allows us to feed each line into
            # a processor that we can use to catch the revision
            # while providing real-time output
            log_processor = LogProcessor()

            # Push the base revision first. This lets the new revision appear
            # first in the Treeherder view, and it also lets us enhance the new
            # revision with information about the base run.
            if base_comparator:
                # Don't cache the base revision when a custom comparison is being performed
                # since the base revision is now unique and not general to all pushes
                if ON_GIT and HG_TO_GIT_MIGRATION_COMPLETE:
                    PerfParser.push_info.base_hash = PerfParser.check_cached_revision(
                        selected_tasks, base_commit_hash
                    )
                    base_run_flag = PerfParser.push_info.base_hash
                else:
                    PerfParser.push_info.base_revision = (
                        PerfParser.check_cached_revision(selected_tasks, compare_commit)
                    )
                    base_run_flag = PerfParser.push_info.base_revision

            if not (dry_run or single_run or base_run_flag):
                # Setup the base revision, and try config. This lets us change the options
                # we run the tests with through the PERF_FLAGS environment variable.
                base_extra_args = list(extra_args)
                base_try_config_params = copy.deepcopy(try_config_params)
                comparator_obj.setup_base_revision(base_extra_args)
                PerfParser.setup_try_config(
                    base_try_config_params, base_extra_args, selected_tasks
                )

                with redirect_stdout(log_processor):
                    # XXX Figure out if we can use the `again` selector in some way
                    # Right now we would need to modify it to be able to do this.
                    # XXX Fix up the again selector for the perf selector (if it makes sense to)
                    push_to_try(
                        "perf-again",
                        f"{base_commit_message}",
                        try_task_config=generate_try_task_config(
                            "fuzzy", selected_tasks, params=base_try_config_params
                        ),
                        stage_changes=False,
                        dry_run=dry_run,
                        closed_tree=False,
                        allow_log_capture=True,
                        push_to_vcs=True,
                    )

                PerfParser.push_info.base_revision = log_processor.revision
                PerfParser.push_info.base_hash = base_commit_hash
                if base_comparator:
                    if ON_GIT and HG_TO_GIT_MIGRATION_COMPLETE:
                        PerfParser.save_revision_treeherder(
                            selected_tasks, base_commit_hash
                        )
                    else:
                        PerfParser.save_revision_treeherder(
                            selected_tasks, compare_commit
                        )

                comparator_obj.teardown_base_revision()

            new_extra_args = list(extra_args)
            comparator_obj.setup_new_revision(new_extra_args)
            PerfParser.setup_try_config(
                try_config_params,
                new_extra_args,
                selected_tasks,
            )

            with redirect_stdout(log_processor):
                push_to_try(
                    "perf",
                    f"{new_commit_message}",
                    # XXX Figure out if changing `fuzzy` to `perf` will break something
                    try_task_config=generate_try_task_config(
                        "fuzzy", selected_tasks, params=try_config_params
                    ),
                    stage_changes=False,
                    dry_run=dry_run,
                    closed_tree=False,
                    allow_log_capture=True,
                    push_to_vcs=True,
                )

            PerfParser.push_info.new_revision = log_processor.revision
            PerfParser.push_info.new_hash = new_commit_hash
            comparator_obj.teardown_new_revision()

        finally:
            comparator_obj.teardown()

    def run(
        update=False,
        show_all=False,
        parameters=None,
        try_config_params=None,
        dry_run=False,
        single_run=False,
        query=None,
        detect_changes=False,
        rebuild=1,
        clear_cache=False,
        **kwargs,
    ):
        # Setup fzf
        fzf = fzf_bootstrap(update)

        if not fzf:
            print(FZF_NOT_FOUND)
            return

        if clear_cache:
            print(f"Removing cached {cache_file} file")
            cache_file.unlink(missing_ok=True)

        all_tasks, dep_cache, cache_dir = setup_tasks_for_fzf(
            not dry_run,
            parameters,
            full=True,
            disable_target_task_filter=False,
        )
        base_cmd = build_base_cmd(
            fzf,
            dep_cache,
            cache_dir,
            show_estimates=False,
            preview_script=PREVIEW_SCRIPT,
        )
        full_task_graph = pathlib.Path(cache_dir, "full_task_graph")

        if kwargs.get("tests"):
            all_tasks = PerfParser.set_categories_for_test(
                full_task_graph, kwargs.get("tests")
            )
            if not all_tasks:
                print("Could not find any tasks for the requested tests")
                return

        # Perform the selection, then push to try and return the revisions
        queries = []
        selected_categories = []
        alert_summary_id = kwargs.get("alert")
        if alert_summary_id:
            alert_tasks = requests.get(
                TREEHERDER_ALERT_TASKS_URL % alert_summary_id,
                headers={"User-Agent": "mozilla-central"},
            )
            if alert_tasks.status_code != 200:
                print(
                    "\nFailed to obtain tasks from alert due to:\n"
                    f"Alert ID: {alert_summary_id}\n"
                    f"Status Code: {alert_tasks.status_code}\n"
                    f"Response Message: {alert_tasks.json()}\n"
                )
                alert_tasks.raise_for_status()
            alert_tasks = set([task for task in alert_tasks.json()["tasks"] if task])
            selected_tasks = alert_tasks & set(all_tasks)
            if not selected_tasks:
                raise Exception("Alert ID has no task to run.")
            elif len(selected_tasks) != len(alert_tasks):
                print(
                    "\nAll the tasks of the Alert Summary couldn't be found in the taskgraph.\n"
                    f"Not exist tasks: {alert_tasks - set(all_tasks)}\n"
                )
        elif not show_all:
            # Expand the categories first
            categories = PerfParser.get_categories(**kwargs)
            PerfParser.build_category_description(base_cmd, categories)

            selected_tasks, selected_categories, queries = PerfParser.get_perf_tasks(
                base_cmd, all_tasks, categories, query=query
            )
        else:
            selected_tasks = PerfParser.get_tasks(base_cmd, queries, query, all_tasks)

        if len(selected_tasks) == 0:
            print("No tasks selected")
            return

        total_task_count = len(selected_tasks) * rebuild
        if total_task_count > MAX_PERF_TASKS:
            print(
                "\n\n----------------------------------------------------------------------------------------------\n"
                f"You have selected {total_task_count} total test runs! (selected tasks({len(selected_tasks)}) * rebuild"
                f" count({rebuild}) \nThese tests won't be triggered as the current maximum for a single ./mach try "
                f"perf run is {MAX_PERF_TASKS}. \nIf this was unexpected, please file a bug in Testing :: Performance."
                "\n----------------------------------------------------------------------------------------------\n\n"
            )
            return

        if detect_changes:
            PerfParser.inject_change_detector(base_cmd, all_tasks, selected_tasks)

        PerfParser.get_majority_framework(selected_tasks)
        PerfParser.perf_push_to_try(
            selected_tasks,
            selected_categories,
            queries,
            try_config_params,
            dry_run,
            single_run,
            kwargs.get("extra_args", []),
            kwargs.get("comparator", "BasePerfComparator"),
            kwargs.get("comparator_args", []),
            alert_summary_id,
        )

    def run_category_checks():
        # XXX: Add a jsonschema check for the category definition
        # Make sure the queries don't specify variants in them
        variant_queries = {
            suite: [
                PerfParser.variants[variant]["query"]
                for variant in suite_info.get(
                    "variants", list(PerfParser.variants.keys())
                )
            ]
            + [
                PerfParser.variants[variant]["negation"]
                for variant in suite_info.get(
                    "variants", list(PerfParser.variants.keys())
                )
            ]
            for suite, suite_info in PerfParser.suites.items()
        }

        for category, category_info in PerfParser.categories.items():
            for suite, query in category_info["query"].items():
                if len(variant_queries[suite]) == 0:
                    # This suite has no variants
                    continue
                if any(any(v in q for q in query) for v in variant_queries[suite]):
                    raise InvalidCategoryException(
                        f"The '{category}' category suite query for '{suite}' "
                        f"uses a variant in it's query '{query}'."
                        "If you don't want a particular variant use the "
                        "`variant-restrictions` field in the category."
                    )

        return True

    def setup_apk_upload(framework, apk_upload_path):
        """Setup the APK for uploading to test on try.

        There are two ways of performing the upload:
            (1) Passing a path to an APK with:
                --browsertime-upload-apk <PATH/FILE.APK>
                --mozperftest-upload-apk <PATH/FILE.APK>
            (2) Setting MOZ_FIREFOX_ANDROID_APK_OUTPUT to a path that will
                always point to an APK (<PATH/FILE.APK>) that we can upload.

        The file is always copied to testing/raptor/raptor/user_upload.apk to
        integrate with minimal changes for simpler cases when using raptor-browsertime.

        For mozperftest, the APK is always uploaded here for the same reasons:
        python/mozperftest/mozperftest/user_upload.apk
        """
        frameworks_to_locations = {
            "browsertime": pathlib.Path(
                build.topsrcdir, "testing", "raptor", "raptor", "user_upload.apk"
            ),
            "mozperftest": pathlib.Path(
                build.topsrcdir,
                "python",
                "mozperftest",
                "mozperftest",
                "user_upload.apk",
            ),
        }

        print("Setting up custom APK upload")
        if apk_upload_path in ("firefox-android"):
            apk_upload_path = MOZ_FIREFOX_ANDROID_APK_OUTPUT
            if apk_upload_path is None:
                raise APKNotFound(
                    "MOZ_FIREFOX_ANDROID_APK_OUTPUT is not defined. It should "
                    "point to an APK to upload."
                )
            apk_upload_path = pathlib.Path(apk_upload_path)
            if not apk_upload_path.exists() or apk_upload_path.is_dir():
                raise APKNotFound(
                    "MOZ_FIREFOX_ANDROID_APK_OUTPUT needs to point to an APK."
                )
        else:
            apk_upload_path = pathlib.Path(apk_upload_path)
            if not apk_upload_path.exists():
                raise APKNotFound(f"Path does not exist: {str(apk_upload_path)}")

        print("\nCopying file in-tree for upload...")
        shutil.copyfile(
            str(apk_upload_path),
            frameworks_to_locations[framework],
        )

        hg_cmd = ["hg", "add", str(frameworks_to_locations[framework])]
        print(
            f"\nRunning the following hg command (RAM warnings are expected):\n"
            f" {hg_cmd}"
        )
        subprocess.check_output(hg_cmd)
        print(
            "\nAPK is setup for uploading. Please commit the changes, "
            "and re-run this command. \nEnsure you "
            "select the correct tasks (fenix, geckoview) or use "
            "--show-all/--full for mozperftest task selection. \nFor Fenix, ensure "
            "you also provide the --fenix flag."
        )

    def build_category_description(base_cmd, categories):
        descriptions = {}

        for category in categories:
            if categories[category].get("description"):
                descriptions[category] = categories[category].get("description")

        description_file = pathlib.Path(
            get_state_dir(), "try_perf_categories_info.json"
        )
        with description_file.open("w") as f:
            json.dump(descriptions, f, indent=4)

        preview_option = base_cmd.index("--preview") + 1
        base_cmd[preview_option] = (
            base_cmd[preview_option] + f' -d "{description_file}" -l "{{}}"'
        )

        for idx, cmd in enumerate(base_cmd):
            if "--preview-window" in cmd:
                base_cmd[idx] += ":wrap"


def run(**kwargs):
    if (
        kwargs.get("browsertime_upload_apk") is not None
        or kwargs.get("mozperftest_upload_apk") is not None
    ):
        framework = "browsertime"
        upload_apk = kwargs.get("browsertime_upload_apk")
        if upload_apk is None:
            framework = "mozperftest"
            upload_apk = kwargs.get("mozperftest_upload_apk")

        PerfParser.setup_apk_upload(framework, upload_apk)
        return

    # Make sure the categories are following
    # the rules we've setup
    PerfParser.run_category_checks()
    PerfParser.check_cached_revision([])
    PerfParser.run(
        profile=kwargs.get("try_config_params", {})
        .get("try_task_config", {})
        .get("gecko-profile", False),
        rebuild=kwargs.get("try_config_params", {})
        .get("try_task_config", {})
        .get("rebuild", 1),
        **kwargs,
    )

    if not PerfParser.push_info.finished_run:
        return

    # Provide link to perfherder for comparisons now
    if not kwargs.get("single_run", False) and not kwargs.get("dry_run", False):
        perfcompare_url = (
            PERFCOMPARE_BASE_URL % PerfParser.push_info.get_perfcompare_settings()
        )
        compareview_url = (
            PERFHERDER_BASE_URL % PerfParser.push_info.get_perfcompare_settings()
        )
        if HG_TO_GIT_MIGRATION_COMPLETE:
            perfcompare_url = (
                PERFCOMPARE_BASE_URL_GIT
                % PerfParser.push_info.get_perfcompare_settings_git()
            )
            compareview_url = (
                PERFHERDER_BASE_URL_GIT
                % PerfParser.push_info.get_perfcompare_settings_git()
            )
        original_try_url = TREEHERDER_TRY_BASE_URL % PerfParser.push_info.base_revision
        # Bug 1958944 - Make gathering of base/new revisions optional
        local_change_try_url = (
            TREEHERDER_TRY_BASE_URL % PerfParser.push_info.new_revision
        )

        print(
            "\n!!!NOTE!!!\n You'll be able to find a performance comparison here "
            "once the tests are complete (ensure you select the right "
            f"framework):\n {perfcompare_url}\n\n"
            f" The old comparison tool is still available at this URL:\n {compareview_url}\n"
        )
        print("\n*******************************************************")
        print("*          2 commits/try-runs are created...          *")
        print("*******************************************************")
        print(f"Base revision's try run: {original_try_url}")
        print(f"Local revision's try run: {local_change_try_url}\n")
    print(
        "If you need any help, you can find us in the #perf-help Matrix channel:\n"
        "https://matrix.to/#/#perf-help:mozilla.org\n"
    )
    print(
        "For more information on the performance tests, see our PerfDocs here:\n"
        "https://firefox-source-docs.mozilla.org/testing/perfdocs/"
    )
