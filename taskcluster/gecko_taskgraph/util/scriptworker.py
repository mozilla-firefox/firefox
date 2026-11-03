# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Make scriptworker.cot.verify more user friendly by making scopes dynamic.

Scriptworker uses certain scopes to determine which sets of credentials to use.
Certain scopes are restricted by branch in chain of trust verification, and are
checked again at the script level.  This file provides functions to adjust
these scopes automatically by project; this makes pushing to try, forking a
project branch, and merge day uplifts more user friendly.

In the future, we may adjust scopes by other settings as well, e.g. different
scopes for `push-to-candidates` rather than `push-to-releases`, even if both
happen on mozilla-beta and mozilla-release.

Additional configuration is found in the :ref:`graph config <taskgraph-graph-config>`.
"""

import functools
import itertools
import os
from datetime import datetime

import jsone
from taskgraph.util.copy import deepcopy
from taskgraph.util.schema import resolve_keyed_by
from taskgraph.util.taskcluster import get_artifact_prefix
from taskgraph.util.yaml import load_yaml

# constants {{{1
"""Map signing scope aliases to sets of projects.

Currently m-c and DevEdition on m-b use nightly signing; Beta on m-b and m-r
use release signing. These data structures aren't set-up to handle different
scopes on the same repo, so we use a different set of them for DevEdition, and
callers are responsible for using the correct one (by calling the appropriate
helper below). More context on this in https://bugzilla.mozilla.org/show_bug.cgi?id=1358601.

We will need to add esr support at some point. Eventually we want to add
nuance so certain m-b and m-r tasks use dep or nightly signing, and we only
release sign when we have a signed-off set of candidate builds.  This current
approach works for now, though.

This is a list of list-pairs, for ordering.
"""
SIGNING_SCOPE_ALIAS_TO_PROJECT = [
    [
        "all-nightly-branches",
        {
            "mozilla-central",
            "comm-central",
            # bug 1845368: pine is a permanent project branch used for testing
            # nightly updates
            "pine",
            # bug 1877483: larch has similar needs for nightlies
            "larch",
            # maple is also an L3 branch: https://phabricator.services.mozilla.com/D184833
            "maple",
            # bug 1988213: cypress project branch
            "cypress",
        },
    ],
    [
        "all-release-branches",
        {
            "mozilla-beta",
            "mozilla-release",
            "mozilla-esr115",
            "mozilla-esr140",
            "mozilla-esr153",
            "comm-beta",
            "comm-release",
            "comm-esr140",
            "comm-esr153",
        },
    ],
]

"""Map the signing scope aliases to the actual scopes.
"""
SIGNING_TYPES = {
    "all-release-branches": "release-signing",
    "all-nightly-branches": "nightly-signing",
    "default": "dep-signing",
}

DEVEDITION_SIGNING_SCOPE_ALIAS_TO_PROJECT = [
    [
        "beta",
        {
            "mozilla-beta",
        },
    ]
]

DEVEDITION_SIGNING_TYPES = {
    "beta": "nightly-signing",
    "default": "dep-signing",
}

"""Map beetmover scope aliases to sets of projects.
"""
BEETMOVER_SCOPE_ALIAS_TO_PROJECT = [
    [
        "all-nightly-branches",
        {
            "mozilla-central",
            "comm-central",
            # bug 1845368: pine is a permanent project branch used for testing
            # nightly updates
            "pine",
            # bug 1877483: larch has similar needs for nightlies
            "larch",
            # bug 1988213: cypress project branch
            "cypress",
        },
    ],
    [
        "all-release-branches",
        {
            "mozilla-beta",
            "mozilla-release",
            "mozilla-esr115",
            "mozilla-esr140",
            "mozilla-esr153",
            "comm-beta",
            "comm-release",
            "comm-esr140",
            "comm-esr153",
        },
    ],
]

"""Map the beetmover scope aliases to the actual scopes.
"""
BEETMOVER_BUCKET_SCOPES = {
    "all-release-branches": "beetmover:bucket:release",
    "all-nightly-branches": "beetmover:bucket:nightly",
    "default": "beetmover:bucket:dep",
}

"""Map the beetmover scope aliases to the actual scopes.
These are the scopes needed to import artifacts into the product delivery APT repos.
"""
BEETMOVER_APT_REPO_SCOPES = {
    "all-release-branches": "beetmover:apt-repo:release",
    "all-nightly-branches": "beetmover:apt-repo:nightly",
    "default": "beetmover:apt-repo:dep",
}

"""Map the beetmover scope aliases to the actual scopes.
These are the scopes needed to import artifacts into the product delivery YUM repos.
"""
BEETMOVER_YUM_REPO_SCOPES = {
    "all-release-branches": "beetmover:yum-repo:release",
    "all-nightly-branches": "beetmover:yum-repo:nightly",
    "default": "beetmover:yum-repo:dep",
}

"""Map the beetmover tasks aliases to the actual action scopes.
"""
BEETMOVER_ACTION_SCOPES = {
    "nightly": "beetmover:action:push-to-nightly",
    # bug 1845368: pine is a permanent project branch used for testing
    # nightly updates
    "nightly-pine": "beetmover:action:push-to-nightly",
    # bug 1877483: larch has similar needs for nightlies
    "nightly-larch": "beetmover:action:push-to-nightly",
    # bug 1988213: cypress project branch
    "nightly-cypress": "beetmover:action:push-to-nightly",
    "default": "beetmover:action:push-to-candidates",
}

"""Map the beetmover tasks aliases to the actual action scopes.
The action scopes are generic across different repo types.
"""
BEETMOVER_REPO_ACTION_SCOPES = {
    "default": "beetmover:action:import-from-gcs-to-artifact-registry",
}

"""Known balrog actions."""
BALROG_ACTIONS = (
    "submit-locale",
    "submit-toplevel",
    "schedule",
    "v2-submit-locale",
    "v2-submit-toplevel",
)

"""Map balrog scope aliases to sets of projects.

This is a list of list-pairs, for ordering.
"""
BALROG_SCOPE_ALIAS_TO_PROJECT = [
    [
        "nightly",
        {
            "mozilla-central",
            "comm-central",
            # bug 1845368: pine is a permanent project branch used for testing
            # nightly updates
            "pine",
            # bug 1877483: larch has similar needs for nightlies
            "larch",
            # bug 1988213: cypress project branch
            "cypress",
        },
    ],
    [
        "beta",
        {
            "mozilla-beta",
            "comm-beta",
        },
    ],
    [
        "release",
        {
            "mozilla-release",
            "comm-release",
        },
    ],
    [
        "esr115",
        {
            "mozilla-esr115",
            "comm-esr115",
        },
    ],
    [
        "esr140",
        {
            "mozilla-esr140",
            "comm-esr140",
        },
    ],
    [
        "esr153",
        {
            "mozilla-esr153",
            "comm-esr153",
        },
    ],
]

"""Map the balrog scope aliases to the actual scopes.
"""
BALROG_SERVER_SCOPES = {
    "nightly": "balrog:server:nightly",
    "aurora": "balrog:server:aurora",
    "beta": "balrog:server:beta",
    "release": "balrog:server:release",
    "esr115": "balrog:server:esr",
    "esr140": "balrog:server:esr",
    "esr153": "balrog:server:esr",
    "default": "balrog:server:dep",
}


""" The list of the release promotion phases which we send notifications for
"""
RELEASE_NOTIFICATION_PHASES = ("promote", "push", "ship")


def add_scope_prefix(config, scope):
    """
    Prepends the scriptworker scope prefix from the :ref:`graph config
    <taskgraph-graph-config>`.

    Args:
        config (TransformConfig): The configuration for the kind being transformed.
        scope (string): The suffix of the scope

    Returns:
        string: the scope to use.
    """
    return "{prefix}:{scope}".format(
        prefix=config.graph_config["scriptworker"]["scope-prefix"],
        scope=scope,
    )


def with_scope_prefix(f):
    """
    Wraps a function, calling :py:func:`add_scope_prefix` on the result of
    calling the wrapped function.

    Args:
        f (callable): A function that takes a ``config`` and some keyword
            arguments, and returns a scope suffix.

    Returns:
        callable: the wrapped function
    """

    @functools.wraps(f)
    def wrapper(config, **kwargs):
        scope_or_scopes = f(config, **kwargs)
        if isinstance(scope_or_scopes, list):
            return map(functools.partial(add_scope_prefix, config), scope_or_scopes)
        return add_scope_prefix(config, scope_or_scopes)

    return wrapper


def get_signing_type_from_project(
    config, alias_to_project_map, alias_to_signing_type_map
):
    """Determine the restricted scope from `config.params['project']`.

    Args:
        config (TransformConfig): The configuration for the kind being transformed.
        alias_to_project_map (list of lists): each list pair contains the
            alias and the set of projects that match.  This is ordered.
        alias_to_signing_type_map (dict): the alias to signing type

    Returns:
        string: the scope to use.
    """
    for alias, projects in alias_to_project_map:
        if config.params["project"] in projects and alias in alias_to_signing_type_map:
            return alias_to_signing_type_map[alias]
    return alias_to_signing_type_map["default"]


# scope functions {{{1
@with_scope_prefix
def get_scope_from_project(config, alias_to_project_map, alias_to_scope_map):
    """Determine the restricted scope from `config.params['project']`.

    Args:
        config (TransformConfig): The configuration for the kind being transformed.
        alias_to_project_map (list of lists): each list pair contains the
            alias and the set of projects that match.  This is ordered.
        alias_to_scope_map (dict): the alias alias to scope

    Returns:
        string: the scope to use.
    """
    for alias, projects in alias_to_project_map:
        if config.params["project"] in projects and alias in alias_to_scope_map:
            return alias_to_scope_map[alias]
    return alias_to_scope_map["default"]


@with_scope_prefix
def get_scope_from_release_type(config, release_type_to_scope_map):
    """Determine the restricted scope from `config.params['target_tasks_method']`.

    Args:
        config (TransformConfig): The configuration for the kind being transformed.
        release_type_to_scope_map (dict): the maps release types to scopes

    Returns:
        string: the scope to use.
    """
    return release_type_to_scope_map.get(
        config.params["release_type"], release_type_to_scope_map["default"]
    )


def get_phase_from_target_method(config, alias_to_tasks_map, alias_to_phase_map):
    """Determine the phase from `config.params['target_tasks_method']`.

    Args:
        config (TransformConfig): The configuration for the kind being transformed.
        alias_to_tasks_map (list of lists): each list pair contains the
            alias and the set of target methods that match. This is ordered.
        alias_to_phase_map (dict): the alias to phase map

    Returns:
        string: the phase to use.
    """
    for alias, tasks in alias_to_tasks_map:
        if (
            config.params["target_tasks_method"] in tasks
            and alias in alias_to_phase_map
        ):
            return alias_to_phase_map[alias]
    return alias_to_phase_map["default"]


get_signing_type = functools.partial(
    get_signing_type_from_project,
    alias_to_project_map=SIGNING_SCOPE_ALIAS_TO_PROJECT,
    alias_to_signing_type_map=SIGNING_TYPES,
)

get_devedition_signing_type = functools.partial(
    get_signing_type_from_project,
    alias_to_project_map=DEVEDITION_SIGNING_SCOPE_ALIAS_TO_PROJECT,
    alias_to_signing_type_map=DEVEDITION_SIGNING_TYPES,
)

get_beetmover_bucket_scope = functools.partial(
    get_scope_from_project,
    alias_to_project_map=BEETMOVER_SCOPE_ALIAS_TO_PROJECT,
    alias_to_scope_map=BEETMOVER_BUCKET_SCOPES,
)

get_beetmover_apt_repo_scope = functools.partial(
    get_scope_from_project,
    alias_to_project_map=BEETMOVER_SCOPE_ALIAS_TO_PROJECT,
    alias_to_scope_map=BEETMOVER_APT_REPO_SCOPES,
)

get_beetmover_yum_repo_scope = functools.partial(
    get_scope_from_project,
    alias_to_project_map=BEETMOVER_SCOPE_ALIAS_TO_PROJECT,
    alias_to_scope_map=BEETMOVER_YUM_REPO_SCOPES,
)

get_beetmover_repo_action_scope = functools.partial(
    get_scope_from_release_type,
    release_type_to_scope_map=BEETMOVER_REPO_ACTION_SCOPES,
)

get_beetmover_action_scope = functools.partial(
    get_scope_from_release_type,
    release_type_to_scope_map=BEETMOVER_ACTION_SCOPES,
)

get_balrog_server_scope = functools.partial(
    get_scope_from_project,
    alias_to_project_map=BALROG_SCOPE_ALIAS_TO_PROJECT,
    alias_to_scope_map=BALROG_SERVER_SCOPES,
)

cached_load_yaml = functools.cache(load_yaml)


def get_signing_type_per_platform(build_platform, is_shippable, config):
    if "devedition" in build_platform:
        return get_devedition_signing_type(config)
    if is_shippable:
        return get_signing_type(config)
    return "dep-signing"


# generate_beetmover_upstream_artifacts {{{1
def generate_beetmover_upstream_artifacts(
    config, job, platform, locale=None, dependencies=None, **kwargs
):
    """Generate the upstream artifacts for beetmover, using the artifact map.

    Currently only applies to beetmover tasks.

    Args:
        job (dict): The current job being generated
        dependencies (list): A list of the job's dependency labels.
        platform (str): The current build platform
        locale (str): The current locale being beetmoved.

    Returns:
        list: A list of dictionaries conforming to the upstream_artifacts spec.
    """
    base_artifact_prefix = get_artifact_prefix(job)
    resolve_keyed_by(
        job,
        "attributes.artifact_map",
        "artifact map",
        **{
            "release-type": config.params["release_type"],
            "platform": platform,
        },
    )
    map_config = deepcopy(cached_load_yaml(job["attributes"]["artifact_map"]))
    upstream_artifacts = list()

    if not locale:
        locales = map_config["default_locales"]
    elif isinstance(locale, list):
        locales = locale
    else:
        locales = [locale]

    if not dependencies:
        if job.get("dependencies"):
            dependencies = job["dependencies"].keys()
        else:
            raise Exception(f"Unsupported type of dependency. Got job: {job}")

    for current_locale, dep in itertools.product(locales, dependencies):
        paths = list()

        for filename in map_config["mapping"]:
            resolve_keyed_by(
                map_config["mapping"][filename],
                "from",
                f"beetmover filename {filename}",
                platform=platform,
                level=str(config.params.get("level", "1")),
            )
            if dep not in map_config["mapping"][filename]["from"]:
                continue
            if (
                current_locale not in ("en-US", "multi")
                and not map_config["mapping"][filename]["all_locales"]
            ):
                continue
            if (
                "only_for_platforms" in map_config["mapping"][filename]
                and platform
                not in map_config["mapping"][filename]["only_for_platforms"]
            ):
                continue
            if (
                "not_for_platforms" in map_config["mapping"][filename]
                and platform in map_config["mapping"][filename]["not_for_platforms"]
            ):
                continue
            if (
                "not_for_locales" in map_config["mapping"][filename]
                and current_locale in map_config["mapping"][filename]["not_for_locales"]
            ):
                continue
            if "partials_only" in map_config["mapping"][filename]:
                continue
            # The next time we look at this file it might be a different locale.
            file_config = deepcopy(map_config["mapping"][filename])
            resolve_keyed_by(
                file_config,
                "source_path_modifier",
                "source path modifier",
                locale=current_locale,
            )

            kwargs["locale"] = current_locale

            paths.append(
                os.path.join(
                    base_artifact_prefix,
                    jsone.render(file_config["source_path_modifier"], kwargs),
                    jsone.render(filename, kwargs),
                )
            )

        if (
            job.get("dependencies")
            and getattr(job["dependencies"][dep], "attributes", None)
            and job["dependencies"][dep].attributes.get("release_artifacts")
        ):
            paths = [
                path
                for path in paths
                if path in job["dependencies"][dep].attributes["release_artifacts"]
            ]

        if not paths:
            continue

        upstream_artifacts.append({
            "taskId": {"task-reference": f"<{dep}>"},
            "taskType": map_config["tasktype_map"].get(dep),
            "paths": sorted(paths),
            "locale": current_locale,
        })

    upstream_artifacts.sort(key=lambda u: u["paths"])
    return upstream_artifacts


def generate_artifact_registry_gcs_sources(dep):
    gcs_sources = []
    locale = dep.attributes.get("locale")
    if not locale:
        repackage_deb_reference = "<repackage-deb>"
        repackage_deb_artifact = "public/build/target.deb"
    else:
        repackage_deb_reference = "<repackage-deb-l10n>"
        repackage_deb_artifact = f"public/build/{locale}/target.langpack.deb"
    for config in dep.task["payload"]["artifactMap"]:
        if (
            config["taskId"]["task-reference"] == repackage_deb_reference
            and repackage_deb_artifact in config["paths"]
        ):
            gcs_sources.append(
                config["paths"][repackage_deb_artifact]["destinations"][0]
            )
    return gcs_sources


def generate_artifact_registry_gcs_sources_rpm(dep):
    """Generate GCS sources for RPM packages from beetmover-repackage-rpm task.

    The beetmover-repackage-rpm task contains all RPM packages (firefox + langpacks)
    for a given platform in its artifactMap, alongside non-package artifacts such as
    RPM-KEY. This function extracts the destinations of the packages themselves, which
    are the only thing to upload to the YUM repository.
    """
    gcs_sources = []
    for config in dep.task["payload"]["artifactMap"]:
        if config["taskId"]["task-reference"] == "<repackage-rpm-signing>":
            for path_info in config["paths"].values():
                destinations = path_info.get("destinations")
                if destinations and destinations[0].endswith(".rpm"):
                    gcs_sources.append(destinations[0])
    return gcs_sources


def _join_destination(bucket_path, dest_path, filename):
    # Empty parts are skipped, which allows publishing at the root of a bucket.
    return "/".join(part for part in (bucket_path, dest_path, filename) if part)


# generate_beetmover_artifact_map {{{1
def generate_beetmover_artifact_map(config, job, **kwargs):
    """Generate the beetmover artifact map.

    Currently only applies to beetmover tasks.

    Args:
        config (): Current taskgraph configuration.
        job (dict): The current job being generated
    Common kwargs:
        platform (str): The current build platform
        locale (str): The current locale being beetmoved.

    Returns:
        list: A list of dictionaries containing source->destination
            maps for beetmover.
    """
    platform = kwargs.get("platform", "")
    resolve_keyed_by(
        job,
        "attributes.artifact_map",
        job["label"],
        **{
            "release-type": config.params["release_type"],
            "platform": platform,
        },
    )
    map_config = deepcopy(cached_load_yaml(job["attributes"]["artifact_map"]))
    base_artifact_prefix = map_config.get(
        "base_artifact_prefix", get_artifact_prefix(job)
    )

    artifacts = list()

    dependencies = job["dependencies"].keys()

    if kwargs.get("locale"):
        if isinstance(kwargs["locale"], list):
            locales = kwargs["locale"]
        else:
            locales = [kwargs["locale"]]
    else:
        locales = map_config["default_locales"]

    keyed_by_kwargs = {
        "platform": platform,
        "build-type": job["attributes"].get("build-type", ""),
    }
    resolve_keyed_by(map_config, "bucket_paths", job["label"], **keyed_by_kwargs)
    resolve_keyed_by(map_config, "folder_prefix", job["label"], **keyed_by_kwargs)

    for locale, dep in sorted(itertools.product(locales, dependencies)):
        paths = dict()
        for filename in map_config["mapping"]:
            # Relevancy checks
            resolve_keyed_by(
                map_config["mapping"][filename],
                "from",
                "blah",
                platform=platform,
                level=str(config.params.get("level", "1")),
            )
            if dep not in map_config["mapping"][filename]["from"]:
                # We don't get this file from this dependency.
                continue
            if (
                locale not in ("en-US", "multi")
                and not map_config["mapping"][filename]["all_locales"]
            ):
                # This locale either doesn't produce or shouldn't upload this file.
                continue
            if (
                "only_for_platforms" in map_config["mapping"][filename]
                and platform
                not in map_config["mapping"][filename]["only_for_platforms"]
            ):
                # This platform either doesn't produce or shouldn't upload this file.
                continue
            if (
                "not_for_platforms" in map_config["mapping"][filename]
                and platform in map_config["mapping"][filename]["not_for_platforms"]
            ):
                # This platform either doesn't produce or shouldn't upload this file.
                continue
            if (
                "not_for_locales" in map_config["mapping"][filename]
                and locale in map_config["mapping"][filename]["not_for_locales"]
            ):
                # This locale either doesn't produce or shouldn't upload this file
                continue
            if "partials_only" in map_config["mapping"][filename]:
                continue

            # deepcopy because the next time we look at this file the locale will differ.
            file_config = deepcopy(map_config["mapping"][filename])

            for field in [
                "destinations",
                "locale_prefix",
                "source_path_modifier",
                "update_balrog_manifest",
                "pretty_name",
                "checksums_path",
            ]:
                resolve_keyed_by(
                    file_config, field, job["label"], locale=locale, platform=platform
                )

            destinations = [
                _join_destination(
                    bucket_path,
                    dest_path,
                    file_config["locale_prefix"]
                    + file_config.get("pretty_name", filename),
                )
                for dest_path, bucket_path in itertools.product(
                    file_config["destinations"], map_config["bucket_paths"]
                )
            ]
            # Creating map entries
            # Key must be artifact path, to avoid trampling duplicates, such
            # as public/build/target.apk and public/build/en-US/target.apk
            key = os.path.join(
                base_artifact_prefix,
                file_config["source_path_modifier"],
                filename,
            )

            paths[key] = {
                "destinations": destinations,
            }
            if file_config.get("checksums_path"):
                paths[key]["checksums_path"] = file_config["checksums_path"]

            # optional flag: balrog manifest
            if file_config.get("update_balrog_manifest"):
                paths[key]["update_balrog_manifest"] = True
                if file_config.get("balrog_format"):
                    paths[key]["balrog_format"] = file_config["balrog_format"]

            # optional flag: expiry
            if file_config.get("expiry"):
                paths[key]["expiry"] = {"relative-datestamp": file_config["expiry"]}

        if not paths:
            # No files for this dependency/locale combination.
            continue

        # Render all variables for the artifact map
        platforms = deepcopy(map_config.get("platform_names", {}))
        if platform:
            for key in platforms.keys():
                resolve_keyed_by(platforms, key, job["label"], platform=platform)

        upload_date = datetime.fromtimestamp(config.params["build_date"])

        kwargs.update({
            "locale": locale,
            "version": config.params["version"],
            "branch": config.params["project"],
            "build_number": config.params["build_number"],
            "year": upload_date.year,
            "month": upload_date.strftime("%m"),  # zero-pad the month
            "day": upload_date.strftime("%d"),
            "upload_date": upload_date.strftime("%Y-%m-%d-%H-%M-%S"),
            "head_rev": config.params["head_rev"],
        })

        if "folder_prefix" in map_config:
            kwargs["folder_prefix"] = jsone.render(map_config["folder_prefix"], kwargs)
        kwargs.update(**platforms)
        paths = jsone.render(paths, kwargs)
        artifacts.append({
            "taskId": {"task-reference": f"<{dep}>"},
            "locale": locale,
            "paths": paths,
        })

    return artifacts


# generate_beetmover_partials_artifact_map {{{1
def generate_beetmover_partials_artifact_map(config, job, partials_info, **kwargs):
    """Generate the beetmover partials artifact map.

    Currently only applies to beetmover tasks.

    Args:
        config (): Current taskgraph configuration.
        job (dict): The current job being generated
        partials_info (dict): Current partials and information about them in a dict
    Common kwargs:
        platform (str): The current build platform
        locale (str): The current locale being beetmoved.

    Returns:
        list: A list of dictionaries containing source->destination
            maps for beetmover.
    """
    platform = kwargs.get("platform", "")
    resolve_keyed_by(
        job,
        "attributes.artifact_map",
        "artifact map",
        **{
            "release-type": config.params["release_type"],
            "platform": platform,
        },
    )
    map_config = deepcopy(cached_load_yaml(job["attributes"]["artifact_map"]))
    base_artifact_prefix = map_config.get(
        "base_artifact_prefix", get_artifact_prefix(job)
    )

    artifacts = list()
    dependencies = job["dependencies"].keys()

    if kwargs.get("locale"):
        locales = [kwargs["locale"]]
    else:
        locales = map_config["default_locales"]

    resolve_keyed_by(map_config, "bucket_paths", "bucket_paths", platform=platform)

    platforms = deepcopy(map_config.get("platform_names", {}))
    if platform:
        for key in platforms.keys():
            resolve_keyed_by(platforms, key, key, platform=platform)
    upload_date = datetime.fromtimestamp(config.params["build_date"])

    for locale, dep in itertools.product(locales, dependencies):
        paths = dict()
        for filename in map_config["mapping"]:
            # Relevancy checks
            if dep not in map_config["mapping"][filename]["from"]:
                # We don't get this file from this dependency.
                continue
            if locale != "en-US" and not map_config["mapping"][filename]["all_locales"]:
                # This locale either doesn't produce or shouldn't upload this file.
                continue
            if "partials_only" not in map_config["mapping"][filename]:
                continue
            # deepcopy because the next time we look at this file the locale will differ.
            file_config = deepcopy(map_config["mapping"][filename])

            for field in [
                "destinations",
                "locale_prefix",
                "source_path_modifier",
                "update_balrog_manifest",
                "from_buildid",
                "pretty_name",
                "checksums_path",
            ]:
                resolve_keyed_by(
                    file_config, field, field, locale=locale, platform=platform
                )

            destinations = [
                _join_destination(
                    bucket_path,
                    dest_path,
                    file_config["locale_prefix"]
                    + file_config.get("pretty_name", filename),
                )
                for dest_path, bucket_path in itertools.product(
                    file_config["destinations"], map_config["bucket_paths"]
                )
            ]
            # Creating map entries
            # Key must be artifact path, to avoid trampling duplicates, such
            # as public/build/target.apk and public/build/en-US/target.apk
            key = os.path.join(
                base_artifact_prefix,
                file_config["source_path_modifier"],
                filename,
            )
            partials_paths = {}
            for pname, info in partials_info.items():
                partials_paths[key] = {
                    "destinations": destinations,
                }
                if file_config.get("checksums_path"):
                    partials_paths[key]["checksums_path"] = file_config[
                        "checksums_path"
                    ]

                # optional flag: balrog manifest
                if file_config.get("update_balrog_manifest"):
                    partials_paths[key]["update_balrog_manifest"] = True
                    if file_config.get("balrog_format"):
                        partials_paths[key]["balrog_format"] = file_config[
                            "balrog_format"
                        ]
                # optional flag: from_buildid
                if file_config.get("from_buildid"):
                    partials_paths[key]["from_buildid"] = file_config["from_buildid"]

                # optional flag: expiry
                if file_config.get("expiry"):
                    partials_paths[key]["expiry"] = {
                        "relative-datestamp": file_config["expiry"]
                    }

                # render buildid
                kwargs.update({
                    "partial": pname,
                    "from_buildid": info["buildid"],
                    "previous_version": info.get("previousVersion"),
                    "buildid": str(config.params["moz_build_date"]),
                    "locale": locale,
                    "version": config.params["version"],
                    "branch": config.params["project"],
                    "build_number": config.params["build_number"],
                    "year": upload_date.year,
                    "month": upload_date.strftime("%m"),  # zero-pad the month
                    "upload_date": upload_date.strftime("%Y-%m-%d-%H-%M-%S"),
                })
                kwargs.update(**platforms)
                paths.update(jsone.render(partials_paths, kwargs))

        if not paths:
            continue

        artifacts.append({
            "taskId": {"task-reference": f"<{dep}>"},
            "locale": locale,
            "paths": paths,
        })

    artifacts.sort(key=lambda a: sorted(a["paths"].items()))
    return artifacts
