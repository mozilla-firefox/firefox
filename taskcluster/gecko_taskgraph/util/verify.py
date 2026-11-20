# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import datetime
import logging
import os
import re
import sys

import attr
from mozsystemmonitor.resourcemonitor import SystemResourceMonitor
from taskcluster.utils import fromNow
from taskgraph.util.keyed_by import evaluate_keyed_by
from taskgraph.util.verify import (
    VerificationSequence,
    verify_always_optimized,
    verify_routes_notification_filters,
    verify_task_graph_symbol,
    verify_trust_domain_v2_routes,
)

from gecko_taskgraph import GECKO
from gecko_taskgraph.util.attributes import (
    ALL_PROJECTS,
    RELEASE_PROJECTS,
    RUN_ON_PROJECT_ALIASES,
)
from gecko_taskgraph.util.constants import TEST_KINDS
from gecko_taskgraph.util.sparse_profiles import (
    is_path_covered_by_taskgraph_sparse_profile,
)

logger = logging.getLogger(__name__)
doc_base_path = os.path.join(GECKO, "taskcluster", "docs")


verifications = VerificationSequence()

verifications.add("full_task_graph")(verify_task_graph_symbol)
verifications.add("full_task_graph")(verify_trust_domain_v2_routes)
verifications.add("full_task_graph")(verify_routes_notification_filters)


@verifications.add("full_task_graph")
def verify_dependency_tiers(task, taskgraph, scratch_pad, graph_config, parameters):
    """Like taskgraph's verify_dependency_tiers, but partner tasks, which don't
    report to Treeherder, are also exempt."""
    tiers = scratch_pad
    if task is not None:
        tiers[task.label] = (
            task.task.get("extra", {}).get("treeherder", {}).get("tier", sys.maxsize)
        )
        return

    def printable_tier(tier):
        return "unknown" if tier == sys.maxsize else tier

    for graph_task in taskgraph.tasks.values():
        tier = tiers[graph_task.label]
        for d in graph_task.dependencies.values():
            dep = taskgraph[d]
            if dep.task.get("workerType") == "always-optimized":
                continue
            if "dummy" in dep.kind or "partner" in dep.kind:
                continue
            if tier < tiers[d]:
                raise Exception(
                    f"{graph_task.label} (tier {printable_tier(tier)}) cannot depend on {d} (tier {printable_tier(tiers[d])})"
                )


verifications.add("optimized_task_graph")(verify_always_optimized)


def profiled_verifications(name, *args, **kwargs):
    """Run the verifications of a generation phase, recording a marker covering
    them in the resource usage profile."""
    SystemResourceMonitor.begin_marker("Verify", name)
    try:
        verifications(name, *args, **kwargs)
    finally:
        SystemResourceMonitor.end_marker("Verify", name)


@attr.s(frozen=True)
class DocPaths:
    _paths = attr.ib(factory=list)

    def get_files(self, filename):
        rv = []
        for p in self._paths:
            doc_path = os.path.join(p, filename)
            if os.path.exists(doc_path):
                rv.append(doc_path)
        return rv

    def add(self, path):
        """
        Projects that make use of Firefox's taskgraph can extend it with
        their own task kinds by registering additional paths for documentation.
        documentation_paths.add() needs to be called by the project's Taskgraph
        registration function. See taskgraph.config.
        """
        self._paths.append(path)


documentation_paths = DocPaths()
documentation_paths.add(doc_base_path)


def verify_docs(filename, identifiers, appearing_as):
    """
    Look for identifiers of the type appearing_as in the files
    returned by documentation_paths.get_files(). Firefox will have
    a single file in a list, but projects such as Thunderbird can have
    documentation in another location and may return multiple files.
    """
    # We ignore identifiers starting with '_' for the sake of tests.
    # Strings starting with "_" are ignored for doc verification
    # hence they can be used for faking test values
    doc_files = documentation_paths.get_files(filename)
    doctext = "".join([open(d).read() for d in doc_files])

    if appearing_as == "inline-literal":
        expression_list = [
            "`" + identifier + "`"
            for identifier in identifiers
            if not identifier.startswith("_")
        ]
    elif appearing_as == "heading":
        expression_list = [
            "\n#+ " + identifier + "\n"
            for identifier in identifiers
            if not identifier.startswith("_")
        ]
    else:
        raise Exception(f"appearing_as = `{appearing_as}` not defined")

    for expression, identifier in zip(expression_list, identifiers):
        match_group = re.search(expression, doctext)
        if not match_group:
            raise Exception(
                f"{appearing_as}: `{identifier}` missing from doc file: `{filename}`"
            )


@verifications.add("initial")
def verify_run_using():
    from gecko_taskgraph.transforms.job import registry

    verify_docs(
        filename="transforms/job.md",
        identifiers=registry.keys(),
        appearing_as="inline-literal",
    )


@verifications.add("parameters")
def verify_parameters_docs(parameters):
    if not parameters.strict:
        return

    parameters_dict = dict(**parameters)
    verify_docs(
        filename="parameters.md",
        identifiers=list(parameters_dict),
        appearing_as="inline-literal",
    )


@verifications.add("kinds")
def verify_kinds_docs(kinds):
    verify_docs(filename="kinds.md", identifiers=kinds.keys(), appearing_as="heading")


@verifications.add("full_task_set")
def verify_attributes(task, taskgraph, scratch_pad, graph_config, parameters):
    if task is None:
        verify_docs(
            filename="attributes.md",
            identifiers=list(scratch_pad["attribute_set"]),
            appearing_as="heading",
        )
        return
    scratch_pad.setdefault("attribute_set", set()).update(task.attributes.keys())


@verifications.add("full_task_graph")
def verify_toolchain_resources_in_sparse_profile(
    task, taskgraph, scratch_pad, graph_config, parameters
):
    """
    Verify that all toolchain resources are covered by the taskgraph sparse profile.
    If not, the decision task's sparse checkout won't have these files,
    causing incorrect hashes and breaking 'mach bootstrap' for developers.
    """
    if task is not None:
        if task.kind != "toolchain":
            return
        resources = task.attributes.get("toolchain-resources", [])
        uncovered = [
            f for f in resources if not is_path_covered_by_taskgraph_sparse_profile(f)
        ]
        if uncovered:
            uncovered_list = "\n".join(f"  path:{path}" for path in uncovered)
            scratch_pad.setdefault("errors", []).append(
                f"Toolchain '{task.label}' has resources not covered "
                f"by the taskgraph sparse profile.\n"
                f"Uncovered resources:\n{uncovered_list}"
            )
    else:
        errors = scratch_pad.get("errors", [])
        if errors:
            raise Exception(
                "Found toolchain resource(s) not covered by taskgraph sparse profile.\n"
                "This will cause incorrect hashes in the decision task.\n\n"
                + "\n\n".join(errors)
                + "\n\nTo fix, add the above path(s) to 'build/sparse-profiles/taskgraph'."
            )


@verifications.add("full_task_graph")
def verify_aliases(task, taskgraph, scratch_pad, graph_config, parameters):
    """
    This function verifies that aliases are not reused.
    """
    if task is None:
        return
    if task.kind not in ("toolchain", "fetch"):
        return
    for_kind = scratch_pad.setdefault(task.kind, {})
    aliases = for_kind.setdefault("aliases", {})
    alias_attribute = f"{task.kind}-alias"
    if task.label in aliases:
        raise Exception(
            f"Task `{aliases[task.label]}` has a {alias_attribute} of `{task.label[len(task.kind) + 1 :]}`, masking a task of that name."
        )
    labels = for_kind.setdefault("labels", set())
    labels.add(task.label)
    attributes = task.attributes
    if alias_attribute in attributes:
        keys = attributes[alias_attribute]
        if not keys:
            keys = []
        elif isinstance(keys, str):
            keys = [keys]
        for key in keys:
            full_key = f"{task.kind}-{key}"
            if full_key in labels:
                raise Exception(
                    f"Task `{task.label}` has a {alias_attribute} of `{key}`,"
                    " masking a task of that name."
                )
            if full_key in aliases:
                raise Exception(
                    f"Duplicate {alias_attribute} in tasks `{task.label}`and `{aliases[full_key]}`: {key}"
                )
            else:
                aliases[full_key] = task.label


@verifications.add("full_task_graph", run_on_projects=RELEASE_PROJECTS)
def verify_shippable_no_sccache(task, taskgraph, scratch_pad, graph_config, parameters):
    if task and task.attributes.get("shippable"):
        if task.task.get("payload", {}).get("env", {}).get("USE_SCCACHE"):
            raise Exception(f"Shippable job {task.label} cannot use sccache")


@verifications.add("full_task_graph", run_on_projects=RELEASE_PROJECTS)
def verify_shipping_no_gradle_cache(
    task, taskgraph, scratch_pad, graph_config, parameters
):
    if task and (
        task.attributes.get("shippable") or task.attributes.get("shipping_phase")
    ):
        caches = task.task.get("payload", {}).get("cache", {})
        if any("-gradle-user-home-" in name for name in caches):
            raise Exception(
                f"Shipping job {task.label} cannot use the Gradle user home cache"
            )


@verifications.add("full_task_graph")
def verify_test_packaging(task, taskgraph, scratch_pad, graph_config, parameters):
    if task is None:
        # In certain cases there are valid reasons for tests to be missing,
        # don't error out when that happens.
        missing_tests_allowed = any((
            # user specified `--target-kind`
            bool(parameters.get("target-kinds")),
            # manifest scheduling is enabled
            parameters["test_manifest_loader"] != "default",
            # tests that don't support artifact builds have been dropped
            parameters["try_task_config"].get("use-artifact-builds", False),
        ))

        test_env = parameters["try_task_config"].get("env", {})
        if test_env.get("MOZHARNESS_TEST_PATHS", "") or test_env.get(
            "MOZHARNESS_TEST_TAG", ""
        ):
            # This is sort of a hack, as we are filtering, we might filter out all test jobs
            missing_tests_allowed = True

        exceptions = []
        for current_task in taskgraph.tasks.values():
            if current_task.kind == "build" and not current_task.attributes.get(
                "skip-verify-test-packaging"
            ):
                build_env = current_task.task.get("payload", {}).get("env", {})
                package_tests = build_env.get("MOZ_AUTOMATION_PACKAGE_TESTS")
                shippable = current_task.attributes.get("shippable", False)
                build_has_tests = scratch_pad.get(current_task.label)

                if package_tests != "1":
                    # Shippable builds should always package tests.
                    if shippable:
                        exceptions.append(
                            f"Build job {current_task.label} is shippable and does not specify "
                            "MOZ_AUTOMATION_PACKAGE_TESTS=1 in the "
                            "environment."
                        )

                    # Build tasks in the scratch pad have tests dependent on
                    # them, so we need to package tests during build.
                    if build_has_tests:
                        exceptions.append(
                            f"Build job {current_task.label} has tests dependent on it and does not specify "
                            "MOZ_AUTOMATION_PACKAGE_TESTS=1 in the environment"
                        )
                # Build tasks that aren't in the scratch pad have no
                # dependent tests, so we shouldn't package tests.
                # With the caveat that we expect shippable jobs to always
                # produce tests.
                elif not build_has_tests and not shippable:
                    # If we have not generated all task kinds, we can't verify that
                    # there are no dependent tests.
                    if not missing_tests_allowed:
                        exceptions.append(
                            f"Build job {current_task.label} has no tests, but specifies "
                            f"MOZ_AUTOMATION_PACKAGE_TESTS={package_tests} in the environment. "
                            "Unset MOZ_AUTOMATION_PACKAGE_TESTS in the task definition "
                            "to fix."
                        )
        if exceptions:
            raise Exception("\n".join(exceptions))
        return
    if task.kind in TEST_KINDS:
        build_task = taskgraph[task.dependencies["build"]]
        scratch_pad[build_task.label] = 1


@verifications.add("full_task_graph")
def verify_run_known_projects(task, taskgraph, scratch_pad, graph_config, parameters):
    """Validates the inputs in run-on-projects.

    We should never let 'try' (or 'try-comm-central') be in run-on-projects even though it
    is valid because it is not considered for try pushes.  While here we also validate for
    other unknown projects or typos.
    """
    if task and task.attributes.get("run_on_projects"):
        projects = set(task.attributes["run_on_projects"])
        if {"try", "try-comm-central"} & set(projects):
            raise Exception(
                f"In task {task.label}: using try in run-on-projects is invalid; use try "
                "selectors to select this task on try"
            )
        # try isn't valid, but by the time we get here its not an available project anyway.
        valid_projects = ALL_PROJECTS | set(RUN_ON_PROJECT_ALIASES.keys())
        invalid_projects = projects - valid_projects
        if invalid_projects:
            raise Exception(
                f"Task '{task.label}' has an invalid run-on-projects value: "
                f"{invalid_projects}"
            )


@verifications.add("graph_config")
def verify_try_expiration_policies(graph_config):
    """We don't want any configuration leading to anything with an expiry longer
    than 28 days on try."""
    now = datetime.datetime.utcnow()
    cap = "28 days"
    cap_from_now = fromNow(cap, now)
    expiration_policy = evaluate_keyed_by(
        graph_config["expiration-policy"],
        "task expiration",
        {"project": "try", "level": "1"},
    )
    for policy, expires in expiration_policy.items():
        if fromNow(expires, now) > cap_from_now:
            raise Exception(
                f'expiration-policy "{policy}" ({expires}) is larger than {cap} for try'
            )
