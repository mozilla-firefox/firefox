# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
These transformations take a task description and turn it into a TaskCluster
task definition (along with attributes, label, etc.).  The input to these
transformations is generic to any kind of task, but abstracts away some of the
complexities of worker implementations, scopes, and treeherder annotations.
"""


import datetime
import hashlib
import os
import re
import time

import attr
from mozbuild.util import memoize
from taskcluster.utils import fromNow
from taskgraph import MAX_DEPENDENCIES
from taskgraph.transforms.base import TransformSequence
from taskgraph.util.copy import deepcopy
from taskgraph.util.keyed_by import evaluate_keyed_by
from taskgraph.util.schema import (
    Schema,
    optionally_keyed_by,
    resolve_keyed_by,
    taskref_or_string,
    validate_schema,
)
from taskgraph.util.treeherder import split_symbol
from voluptuous import All, Any, Extra, Match, NotIn, Optional, Required

from gecko_taskgraph import GECKO
from gecko_taskgraph.optimize.schema import OptimizationSchema
from gecko_taskgraph.transforms.job.common import get_expiration
from gecko_taskgraph.util import docker as dockerutil
from gecko_taskgraph.util.attributes import TRUNK_PROJECTS, is_try, release_level
from gecko_taskgraph.util.chunking import TEST_VARIANTS
from gecko_taskgraph.util.hash import hash_path
from gecko_taskgraph.util.partners import get_partners_to_be_published
from gecko_taskgraph.util.scriptworker import BALROG_ACTIONS, get_release_config
from gecko_taskgraph.util.signed_artifacts import get_signed_artifacts
from gecko_taskgraph.util.workertypes import get_worker_type, worker_type_implementation

RUN_TASK = os.path.join(GECKO, "taskcluster", "scripts", "run-task")

SCCACHE_GCS_PROJECT = "sccache-3"


@memoize
def _run_task_suffix():
    """String to append to cache names under control of run-task."""
    return hash_path(RUN_TASK)[0:20]


def _compute_geckoview_version(app_version, moz_build_date):
    """Geckoview version string that matches geckoview gradle configuration"""
    # Must be synchronized with /mobile/android/geckoview/build.gradle computeVersionCode(...)
    version_without_milestone = re.sub(r"a[0-9]", "", app_version, 1)
    parts = version_without_milestone.split(".")
    return f"{parts[0]}.{parts[1]}.{moz_build_date}"


# A task description is a general description of a TaskCluster task
task_description_schema = Schema(
    {
        # the label for this task
        Required("label"): str,
        # description of the task (for metadata)
        Required("description"): str,
        # attributes for this task
        Optional("attributes"): {str: object},
        # relative path (from config.path) to the file task was defined in
        Optional("task-from"): str,
        # dependencies of this task, keyed by name; these are passed through
        # verbatim and subject to the interpretation of the Task's get_dependencies
        # method.
        Optional("dependencies"): {
            All(
                str,
                NotIn(
                    ["self", "decision"],
                    "Can't use 'self` or 'decision' as depdency names.",
                ),
            ): object,
        },
        # Soft dependencies of this task, as a list of tasks labels
        Optional("soft-dependencies"): [str],
        # Dependencies that must be scheduled in order for this task to run.
        Optional("if-dependencies"): [str],
        Optional("requires"): Any("all-completed", "all-resolved"),
        # expiration and deadline times, relative to task creation, with units
        # (e.g., "14 days").  Defaults are set based on the project.
        Optional("expires-after"): str,
        Optional("deadline-after"): str,
        Optional("expiration-policy"): str,
        # custom routes for this task; the default treeherder routes will be added
        # automatically
        Optional("routes"): [str],
        # custom scopes for this task; any scopes required for the worker will be
        # added automatically. The following parameters will be substituted in each
        # scope:
        #  {level} -- the scm level of this push
        #  {project} -- the project of this push
        Optional("scopes"): [str],
        # Tags
        Optional("tags"): {str: str},
        # custom "task.extra" content
        Optional("extra"): {str: object},
        # treeherder-related information; see
        # https://firefox-ci-tc.services.mozilla.com/schemas/taskcluster-treeherder/v1/task-treeherder-config.json
        # If not specified, no treeherder extra information or routes will be
        # added to the task
        Optional("treeherder"): {
            # either a bare symbol, or "grp(sym)".
            "symbol": str,
            # the job kind
            "kind": Any("build", "test", "other"),
            # tier for this task
            "tier": int,
            # task platform, in the form platform/collection, used to set
            # treeherder.machine.platform and treeherder.collection or
            # treeherder.labels
            "platform": Match("^[A-Za-z0-9_-]{1,50}/[A-Za-z0-9_-]{1,50}$"),
        },
        # information for indexing this build so its artifacts can be discovered;
        # if omitted, the build will not be indexed.
        Optional("index"): {
            # the name of the product this build produces
            "product": str,
            # the names to use for this job in the TaskCluster index
            "job-name": str,
            # Type of gecko v2 index to use
            "type": Any(
                "generic",
                "l10n",
                "shippable",
                "shippable-l10n",
                "android-shippable",
                "android-shippable-with-multi-l10n",
                "shippable-with-multi-l10n",
            ),
            # The rank that the task will receive in the TaskCluster
            # index.  A newly completed task supercedes the currently
            # indexed task iff it has a higher rank.  If unspecified,
            # 'by-tier' behavior will be used.
            "rank": Any(
                # Rank is equal the timestamp of the build_date for tier-1
                # tasks, and one for non-tier-1.  This sorts tier-{2,3}
                # builds below tier-1 in the index, but above eager-index.
                "by-tier",
                # Rank is given as an integer constant (e.g. zero to make
                # sure a task is last in the index).
                int,
                # Rank is equal to the timestamp of the build_date.  This
                # option can be used to override the 'by-tier' behavior
                # for non-tier-1 tasks.
                "build_date",
            ),
        },
        # The `run_on_projects` attribute, defaulting to "all".  This dictates the
        # projects on which this task should be included in the target task set.
        # See the attributes documentation for details.
        Optional("run-on-projects"): optionally_keyed_by("build-platform", [str]),
        # Like `run_on_projects`, `run-on-hg-branches` defaults to "all".
        Optional("run-on-hg-branches"): optionally_keyed_by("project", [str]),
        # The `shipping_phase` attribute, defaulting to None. This specifies the
        # release promotion phase that this task belongs to.
        Required("shipping-phase"): Any(
            None,
            "build",
            "promote",
            "push",
            "ship",
        ),
        # The `shipping_product` attribute, defaulting to None. This specifies the
        # release promotion product that this task belongs to.
        Required("shipping-product"): Any(None, str),
        # The `always-target` attribute will cause the task to be included in the
        # target_task_graph regardless of filtering. Tasks included in this manner
        # will be candidates for optimization even when `optimize_target_tasks` is
        # False, unless the task was also explicitly chosen by the target_tasks
        # method.
        Required("always-target"): bool,
        # Optimization to perform on this task during the optimization phase.
        # Optimizations are defined in taskcluster/gecko_taskgraph/optimize.py.
        Required("optimization"): OptimizationSchema,
        # the provisioner-id/worker-type for the task.  The following parameters will
        # be substituted in this string:
        #  {level} -- the scm level of this push
        "worker-type": str,
        # Whether the job should use sccache compiler caching.
        Required("use-sccache"): bool,
        # information specific to the worker implementation that will run this task
        Optional("worker"): {
            Required("implementation"): str,
            Extra: object,
        },
        # Override the default priority for the project
        Optional("priority"): str,
    }
)

TC_TREEHERDER_SCHEMA_URL = (
    "https://github.com/taskcluster/taskcluster-treeherder/"
    "blob/master/schemas/task-treeherder-config.yml"
)


UNKNOWN_GROUP_NAME = (
    "Treeherder group {} (from {}) has no name; " "add it to taskcluster/config.yml"
)

V2_ROUTE_TEMPLATES = [
    "index.{trust-domain}.v2.{project}.latest.{product}.{job-name}",
    "index.{trust-domain}.v2.{project}.pushdate.{build_date_long}.{product}.{job-name}",
    "index.{trust-domain}.v2.{project}.pushdate.{build_date}.latest.{product}.{job-name}",
    "index.{trust-domain}.v2.{project}.pushlog-id.{pushlog_id}.{product}.{job-name}",
    "index.{trust-domain}.v2.{project}.revision.{branch_rev}.{product}.{job-name}",
    "index.{trust-domain}.v2.{project}.revision.{branch_git_rev}.{product}.{job-name}",
]

# {central, inbound, autoland} write to a "trunk" index prefix. This facilitates
# walking of tasks with similar configurations.
V2_TRUNK_ROUTE_TEMPLATES = [
    "index.{trust-domain}.v2.trunk.revision.{branch_rev}.{product}.{job-name}",
]

V2_SHIPPABLE_TEMPLATES = [
    "index.{trust-domain}.v2.{project}.shippable.latest.{product}.{job-name}",
    "index.{trust-domain}.v2.{project}.shippable.{build_date}.revision.{branch_rev}.{product}.{job-name}",  # noqa - too long
    "index.{trust-domain}.v2.{project}.shippable.{build_date}.latest.{product}.{job-name}",
    "index.{trust-domain}.v2.{project}.shippable.revision.{branch_rev}.{product}.{job-name}",
    "index.{trust-domain}.v2.{project}.shippable.revision.{branch_git_rev}.{product}.{job-name}",
]

V2_SHIPPABLE_L10N_TEMPLATES = [
    "index.{trust-domain}.v2.{project}.shippable.latest.{product}-l10n.{job-name}.{locale}",
    "index.{trust-domain}.v2.{project}.shippable.{build_date}.revision.{branch_rev}.{product}-l10n.{job-name}.{locale}",  # noqa - too long
    "index.{trust-domain}.v2.{project}.shippable.{build_date}.latest.{product}-l10n.{job-name}.{locale}",  # noqa - too long
    "index.{trust-domain}.v2.{project}.shippable.revision.{branch_rev}.{product}-l10n.{job-name}.{locale}",  # noqa - too long
]

V2_L10N_TEMPLATES = [
    "index.{trust-domain}.v2.{project}.revision.{branch_rev}.{product}-l10n.{job-name}.{locale}",
    "index.{trust-domain}.v2.{project}.pushdate.{build_date_long}.{product}-l10n.{job-name}.{locale}",  # noqa - too long
    "index.{trust-domain}.v2.{project}.pushlog-id.{pushlog_id}.{product}-l10n.{job-name}.{locale}",
    "index.{trust-domain}.v2.{project}.latest.{product}-l10n.{job-name}.{locale}",
]

# This index is specifically for builds that include geckoview releases,
# so we can hard-code the project to "geckoview"
V2_GECKOVIEW_RELEASE = "index.{trust-domain}.v2.{project}.geckoview-version.{geckoview-version}.{product}.{job-name}"  # noqa - too long

# the roots of the treeherder routes
TREEHERDER_ROUTE_ROOT = "tc-treeherder"


def get_branch_rev(config):
    return config.params[
        "{}head_rev".format(config.graph_config["project-repo-param-prefix"])
    ]


def get_branch_git_rev(config):
    return config.params[
        "{}head_git_rev".format(config.graph_config["project-repo-param-prefix"])
    ]


def get_branch_repo(config):
    return config.params[
        "{}head_repository".format(
            config.graph_config["project-repo-param-prefix"],
        )
    ]


@memoize
def get_default_priority(graph_config, project):
    return evaluate_keyed_by(
        graph_config["task-priority"], "Graph Config", {"project": project}
    )


# define a collection of payload builders, depending on the worker implementation
payload_builders = {}


@attr.s(frozen=True)
class PayloadBuilder:
    schema = attr.ib(type=Schema)
    builder = attr.ib()


def payload_builder(name, schema):
    schema = Schema({Required("implementation"): name, Optional("os"): str}).extend(
        schema
    )

    def wrap(func):
        assert name not in payload_builders, f"duplicate payload builder name {name}"
        payload_builders[name] = PayloadBuilder(schema, func)
        return func

    return wrap


# define a collection of index builders, depending on the type implementation
index_builders = {}


def index_builder(name):
    def wrap(func):
        assert name not in index_builders, f"duplicate index builder name {name}"
        index_builders[name] = func
        return func

    return wrap


UNSUPPORTED_INDEX_PRODUCT_ERROR = """\
The gecko-v2 product {product} is not in the list of configured products in
`taskcluster/config.yml'.
"""


def verify_index(config, index):
    product = index["product"]
    if product not in config.graph_config["index"]["products"]:
        raise Exception(UNSUPPORTED_INDEX_PRODUCT_ERROR.format(product=product))


@payload_builder(
    "docker-worker",
    schema={
        Required("os"): "linux",
        # For tasks that will run in docker-worker, this is the
        # name of the docker image or in-tree docker image to run the task in.  If
        # in-tree, then a dependency will be created automatically.  This is
        # generally `desktop-test`, or an image that acts an awful lot like it.
        Required("docker-image"): Any(
            # a raw Docker image path (repo/image:tag)
            str,
            # an in-tree generated docker image (from `taskcluster/docker/<name>`)
            {"in-tree": str},
            # an indexed docker image
            {"indexed": str},
        ),
        # worker features that should be enabled
        Required("chain-of-trust"): bool,
        Required("taskcluster-proxy"): bool,
        Required("allow-ptrace"): bool,
        Required("loopback-video"): bool,
        Required("loopback-audio"): bool,
        Required("docker-in-docker"): bool,  # (aka 'dind')
        Required("privileged"): bool,
        Optional("kvm"): bool,
        # Paths to Docker volumes.
        #
        # For in-tree Docker images, volumes can be parsed from Dockerfile.
        # This only works for the Dockerfile itself: if a volume is defined in
        # a base image, it will need to be declared here. Out-of-tree Docker
        # images will also require explicit volume annotation.
        #
        # Caches are often mounted to the same path as Docker volumes. In this
        # case, they take precedence over a Docker volume. But a volume still
        # needs to be declared for the path.
        Optional("volumes"): [str],
        Optional(
            "required-volumes",
            description=(
                "Paths that are required to be volumes for performance reasons. "
                "For in-tree images, these paths will be checked to verify that they "
                "are defined as volumes."
            ),
        ): [str],
        # caches to set up for the task
        Optional("caches"): [
            {
                # only one type is supported by any of the workers right now
                "type": "persistent",
                # name of the cache, allowing re-use by subsequent tasks naming the
                # same cache
                "name": str,
                # location in the task image where the cache will be mounted
                "mount-point": str,
                # Whether the cache is not used in untrusted environments
                # (like the Try repo).
                Optional("skip-untrusted"): bool,
            }
        ],
        # artifacts to extract from the task image after completion
        Optional("artifacts"): [
            {
                # type of artifact -- simple file, or recursive directory
                "type": Any("file", "directory"),
                # task image path from which to read artifact
                "path": str,
                # name of the produced artifact (root of the names for
                # type=directory)
                "name": str,
                "expires-after": str,
            }
        ],
        # environment variables
        Required("env"): {str: taskref_or_string},
        # the command to run; if not given, docker-worker will default to the
        # command in the docker image
        Optional("command"): [taskref_or_string],
        # the maximum time to run, in seconds
        Required("max-run-time"): int,
        # the exit status code(s) that indicates the task should be retried
        Optional("retry-exit-status"): [int],
        # the exit status code(s) that indicates the caches used by the task
        # should be purged
        Optional("purge-caches-exit-status"): [int],
        # Whether any artifacts are assigned to this worker
        Optional("skip-artifacts"): bool,
    },
)
def build_docker_worker_payload(config, task, task_def):
    worker = task["worker"]
    level = int(config.params["level"])

    image = worker["docker-image"]
    if isinstance(image, dict):
        if "in-tree" in image:
            name = image["in-tree"]
            docker_image_task = "docker-image-" + image["in-tree"]
            task.setdefault("dependencies", {})["docker-image"] = docker_image_task

            image = {
                "path": "public/image.tar.zst",
                "taskId": {"task-reference": "<docker-image>"},
                "type": "task-image",
            }

            # Find VOLUME in Dockerfile.
            volumes = dockerutil.parse_volumes(name)
            for v in sorted(volumes):
                if v in worker["volumes"]:
                    raise Exception(
                        "volume %s already defined; "
                        "if it is defined in a Dockerfile, "
                        "it does not need to be specified in the "
                        "worker definition" % v
                    )

                worker["volumes"].append(v)

        elif "indexed" in image:
            image = {
                "path": "public/image.tar.zst",
                "namespace": image["indexed"],
                "type": "indexed-image",
            }
        else:
            raise Exception("unknown docker image type")

    features = {}

    if worker.get("taskcluster-proxy"):
        features["taskclusterProxy"] = True

    if worker.get("allow-ptrace"):
        features["allowPtrace"] = True
        task_def["scopes"].append("docker-worker:feature:allowPtrace")

    if worker.get("chain-of-trust"):
        features["chainOfTrust"] = True

    if worker.get("docker-in-docker"):
        features["dind"] = True

    # Never enable sccache on the toolchains repo, as there is no benefit from it
    # because each push uses a different compiler.
    if task.get("use-sccache") and config.params["project"] != "toolchains":
        features["taskclusterProxy"] = True
        task_def["scopes"].append(
            "assume:project:taskcluster:{trust_domain}:level-{level}-sccache-buckets".format(
                trust_domain=config.graph_config["trust-domain"],
                level=config.params["level"],
            )
        )
        worker["env"]["USE_SCCACHE"] = "1"
        worker["env"]["SCCACHE_GCS_PROJECT"] = SCCACHE_GCS_PROJECT
        # Disable sccache idle shutdown.
        worker["env"]["SCCACHE_IDLE_TIMEOUT"] = "0"
    else:
        worker["env"]["SCCACHE_DISABLE"] = "1"

    capabilities = {}

    for lo in "audio", "video":
        if worker.get("loopback-" + lo):
            capitalized = "loopback" + lo.capitalize()
            devices = capabilities.setdefault("devices", {})
            devices[capitalized] = True
            task_def["scopes"].append("docker-worker:capability:device:" + capitalized)

    if worker.get("kvm"):
        devices = capabilities.setdefault("devices", {})
        devices["kvm"] = True
        task_def["scopes"].append("docker-worker:capability:device:kvm")

    if worker.get("privileged"):
        capabilities["privileged"] = True
        task_def["scopes"].append("docker-worker:capability:privileged")

    task_def["payload"] = payload = {
        "image": image,
        "env": worker["env"],
    }
    if "command" in worker:
        payload["command"] = worker["command"]

    if "max-run-time" in worker:
        payload["maxRunTime"] = worker["max-run-time"]

    run_task = payload.get("command", [""])[0].endswith("run-task")

    # run-task exits EXIT_PURGE_CACHES if there is a problem with caches.
    # Automatically retry the tasks and purge caches if we see this exit
    # code.
    # TODO move this closer to code adding run-task once bug 1469697 is
    # addressed.
    if run_task:
        worker.setdefault("retry-exit-status", []).append(72)
        worker.setdefault("purge-caches-exit-status", []).append(72)

    payload["onExitStatus"] = {}
    if "retry-exit-status" in worker:
        payload["onExitStatus"]["retry"] = worker["retry-exit-status"]
    if "purge-caches-exit-status" in worker:
        payload["onExitStatus"]["purgeCaches"] = worker["purge-caches-exit-status"]

    if "artifacts" in worker:
        artifacts = {}
        for artifact in worker["artifacts"]:
            artifacts[artifact["name"]] = {
                "path": artifact["path"],
                "type": artifact["type"],
                "expires": {"relative-datestamp": artifact["expires-after"]},
            }
        payload["artifacts"] = artifacts

    if isinstance(worker.get("docker-image"), str):
        out_of_tree_image = worker["docker-image"]
    else:
        out_of_tree_image = None
        image = worker.get("docker-image", {}).get("in-tree")

    if "caches" in worker:
        caches = {}

        # run-task knows how to validate caches.
        #
        # To help ensure new run-task features and bug fixes don't interfere
        # with existing caches, we seed the hash of run-task into cache names.
        # So, any time run-task changes, we should get a fresh set of caches.
        # This means run-task can make changes to cache interaction at any time
        # without regards for backwards or future compatibility.
        #
        # But this mechanism only works for in-tree Docker images that are built
        # with the current run-task! For out-of-tree Docker images, we have no
        # way of knowing their content of run-task. So, in addition to varying
        # cache names by the contents of run-task, we also take the Docker image
        # name into consideration. This means that different Docker images will
        # never share the same cache. This is a bit unfortunate. But it is the
        # safest thing to do. Fortunately, most images are defined in-tree.
        #
        # For out-of-tree Docker images, we don't strictly need to incorporate
        # the run-task content into the cache name. However, doing so preserves
        # the mechanism whereby changing run-task results in new caches
        # everywhere.

        # As an additional mechanism to force the use of different caches, the
        # string literal in the variable below can be changed. This is
        # preferred to changing run-task because it doesn't require images
        # to be rebuilt.
        cache_version = "v3"

        if run_task:
            suffix = f"{cache_version}-{_run_task_suffix()}"

            if out_of_tree_image:
                name_hash = hashlib.sha256(
                    out_of_tree_image.encode("utf-8")
                ).hexdigest()
                suffix += name_hash[0:12]

        else:
            suffix = cache_version

        skip_untrusted = is_try(config.params) or level == 1

        for cache in worker["caches"]:
            # Some caches aren't enabled in environments where we can't
            # guarantee certain behavior. Filter those out.
            if cache.get("skip-untrusted") and skip_untrusted:
                continue

            name = "{trust_domain}-level-{level}-{name}-{suffix}".format(
                trust_domain=config.graph_config["trust-domain"],
                level=config.params["level"],
                name=cache["name"],
                suffix=suffix,
            )

            caches[name] = cache["mount-point"]
            task_def["scopes"].append("docker-worker:cache:%s" % name)

        # Assertion: only run-task is interested in this.
        if run_task:
            payload["env"]["TASKCLUSTER_CACHES"] = ";".join(sorted(caches.values()))

        payload["cache"] = caches

    # And send down volumes information to run-task as well.
    if run_task and worker.get("volumes"):
        payload["env"]["TASKCLUSTER_VOLUMES"] = ";".join(sorted(worker["volumes"]))

    if payload.get("cache") and skip_untrusted:
        payload["env"]["TASKCLUSTER_UNTRUSTED_CACHES"] = "1"

    if features:
        payload["features"] = features
    if capabilities:
        payload["capabilities"] = capabilities

    check_caches_are_volumes(task)
    check_required_volumes(task)


@payload_builder(
    "generic-worker",
    schema={
        Required("os"): Any(
            "windows", "macosx", "linux", "linux-bitbar", "linux-lambda"
        ),
        # see http://schemas.taskcluster.net/generic-worker/v1/payload.json
        # and https://docs.taskcluster.net/reference/workers/generic-worker/payload
        # command is a list of commands to run, sequentially
        # on Windows, each command is a string, on OS X and Linux, each command is
        # a string array
        Required("command"): Any(
            [taskref_or_string], [[taskref_or_string]]  # Windows  # Linux / OS X
        ),
        # artifacts to extract from the task image after completion; note that artifacts
        # for the generic worker cannot have names
        Optional("artifacts"): [
            {
                # type of artifact -- simple file, or recursive directory
                "type": Any("file", "directory"),
                # filesystem path from which to read artifact
                "path": str,
                # if not specified, path is used for artifact name
                Optional("name"): str,
                "expires-after": str,
            }
        ],
        # Directories and/or files to be mounted.
        # The actual allowed combinations are stricter than the model below,
        # but this provides a simple starting point.
        # See https://docs.taskcluster.net/reference/workers/generic-worker/payload
        Optional("mounts"): [
            {
                # A unique name for the cache volume, implies writable cache directory
                # (otherwise mount is a read-only file or directory).
                Optional("cache-name"): str,
                # Optional content for pre-loading cache, or mandatory content for
                # read-only file or directory. Pre-loaded content can come from either
                # a task artifact or from a URL.
                Optional("content"): {
                    # *** Either (artifact and task-id) or url must be specified. ***
                    # Artifact name that contains the content.
                    Optional("artifact"): str,
                    # Task ID that has the artifact that contains the content.
                    Optional("task-id"): taskref_or_string,
                    # URL that supplies the content in response to an unauthenticated
                    # GET request.
                    Optional("url"): str,
                },
                # *** Either file or directory must be specified. ***
                # If mounting a cache or read-only directory, the filesystem location of
                # the directory should be specified as a relative path to the task
                # directory here.
                Optional("directory"): str,
                # If mounting a file, specify the relative path within the task
                # directory to mount the file (the file will be read only).
                Optional("file"): str,
                # Required if and only if `content` is specified and mounting a
                # directory (not a file). This should be the archive format of the
                # content (either pre-loaded cache or read-only directory).
                Optional("format"): Any("rar", "tar.bz2", "tar.gz", "zip", "tar.xz"),
            }
        ],
        # environment variables
        Required("env"): {str: taskref_or_string},
        # the maximum time to run, in seconds
        Required("max-run-time"): int,
        # os user groups for test task workers
        Optional("os-groups"): [str],
        # feature for test task to run as administarotr
        Optional("run-as-administrator"): bool,
        # optional features
        Required("chain-of-trust"): bool,
        Optional("taskcluster-proxy"): bool,
        # the exit status code(s) that indicates the task should be retried
        Optional("retry-exit-status"): [int],
        # Wether any artifacts are assigned to this worker
        Optional("skip-artifacts"): bool,
    },
)
def build_generic_worker_payload(config, task, task_def):
    worker = task["worker"]
    features = {}

    task_def["payload"] = {
        "command": worker["command"],
        "maxRunTime": worker["max-run-time"],
    }

    if worker["os"] == "windows":
        task_def["payload"]["onExitStatus"] = {
            "retry": [
                # These codes (on windows) indicate a process interruption,
                # rather than a task run failure. See bug 1544403.
                1073807364,  # process force-killed due to system shutdown
                3221225786,  # sigint (any interrupt)
            ]
        }
    if "retry-exit-status" in worker:
        task_def["payload"].setdefault("onExitStatus", {}).setdefault(
            "retry", []
        ).extend(worker["retry-exit-status"])
    if worker["os"] in ["linux-bitbar", "linux-lambda"]:
        task_def["payload"].setdefault("onExitStatus", {}).setdefault("retry", [])
        # exit code 4 is used to indicate an intermittent android device error
        if 4 not in task_def["payload"]["onExitStatus"]["retry"]:
            task_def["payload"]["onExitStatus"]["retry"].extend([4])

    env = worker.get("env", {})

    # Never enable sccache on the toolchains repo, as there is no benefit from it
    # because each push uses a different compiler.
    if task.get("use-sccache") and config.params["project"] != "toolchains":
        features["taskclusterProxy"] = True
        task_def["scopes"].append(
            "assume:project:taskcluster:{trust_domain}:level-{level}-sccache-buckets".format(
                trust_domain=config.graph_config["trust-domain"],
                level=config.params["level"],
            )
        )
        env["USE_SCCACHE"] = "1"
        worker["env"]["SCCACHE_GCS_PROJECT"] = SCCACHE_GCS_PROJECT
        # Disable sccache idle shutdown.
        env["SCCACHE_IDLE_TIMEOUT"] = "0"
    else:
        env["SCCACHE_DISABLE"] = "1"

    if env:
        task_def["payload"]["env"] = env

    artifacts = []

    for artifact in worker.get("artifacts", []):
        a = {
            "path": artifact["path"],
            "type": artifact["type"],
            "expires": {"relative-datestamp": artifact["expires-after"]},
        }
        if "name" in artifact:
            a["name"] = artifact["name"]
        artifacts.append(a)

    if artifacts:
        task_def["payload"]["artifacts"] = artifacts

    # Need to copy over mounts, but rename keys to respect naming convention
    #   * 'cache-name' -> 'cacheName'
    #   * 'task-id'    -> 'taskId'
    # All other key names are already suitable, and don't need renaming.
    mounts = deepcopy(worker.get("mounts", []))
    for mount in mounts:
        if "cache-name" in mount:
            mount["cacheName"] = "{trust_domain}-level-{level}-{name}".format(
                trust_domain=config.graph_config["trust-domain"],
                level=config.params["level"],
                name=mount.pop("cache-name"),
            )
            task_def["scopes"].append(
                "generic-worker:cache:{}".format(mount["cacheName"])
            )
        if "content" in mount:
            if "task-id" in mount["content"]:
                mount["content"]["taskId"] = mount["content"].pop("task-id")
            if "artifact" in mount["content"]:
                if not mount["content"]["artifact"].startswith("public/"):
                    task_def["scopes"].append(
                        "queue:get-artifact:{}".format(mount["content"]["artifact"])
                    )

    if mounts:
        task_def["payload"]["mounts"] = mounts

    if worker.get("os-groups"):
        task_def["payload"]["osGroups"] = worker["os-groups"]
        task_def["scopes"].extend(
            [
                "generic-worker:os-group:{}/{}".format(task["worker-type"], group)
                for group in worker["os-groups"]
            ]
        )

    if worker.get("chain-of-trust"):
        features["chainOfTrust"] = True

    if worker.get("taskcluster-proxy"):
        features["taskclusterProxy"] = True

    if worker.get("run-as-administrator", False):
        features["runAsAdministrator"] = True
        task_def["scopes"].append(
            "generic-worker:run-as-administrator:{}".format(task["worker-type"]),
        )

    if features:
        task_def["payload"]["features"] = features


@payload_builder(
    "scriptworker-signing",
    schema={
        # the maximum time to run, in seconds
        Required("max-run-time"): int,
        # list of artifact URLs for the artifacts that should be signed
        Required("upstream-artifacts"): [
            {
                # taskId of the task with the artifact
                Required("taskId"): taskref_or_string,
                # type of signing task (for CoT)
                Required("taskType"): str,
                # Paths to the artifacts to sign
                Required("paths"): [str],
                # Signing formats to use on each of the paths
                Required("formats"): [str],
                Optional("singleFileGlobs"): [str],
            }
        ],
        # behavior for mac iscript
        Optional("mac-behavior"): Any(
            "apple_notarization",
            "apple_notarization_stacked",
            "mac_sign_and_pkg",
            "mac_sign_and_pkg_hardened",
            "mac_geckodriver",
            "mac_notarize_geckodriver",
            "mac_single_file",
            "mac_notarize_single_file",
        ),
        Optional("entitlements-url"): str,
        Optional("requirements-plist-url"): str,
        Optional("provisioning-profile-config"): [
            {
                Required("profile_name"): str,
                Required("target_path"): str,
            }
        ],
        Optional("hardened-sign-config"): [
            {
                Optional("deep"): bool,
                Optional("runtime"): bool,
                Optional("force"): bool,
                Optional("entitlements"): str,
                Optional("requirements"): str,
                Required("globs"): [str],
            }
        ],
    },
)
def build_scriptworker_signing_payload(config, task, task_def):
    worker = task["worker"]

    task_def["payload"] = {
        "maxRunTime": worker["max-run-time"],
        "upstreamArtifacts": worker["upstream-artifacts"],
    }
    if worker.get("mac-behavior"):
        task_def["payload"]["behavior"] = worker["mac-behavior"]
        for attribute in (
            "entitlements-url",
            "requirements-plist-url",
            "hardened-sign-config",
            "provisioning-profile-config",
        ):
            if worker.get(attribute):
                task_def["payload"][attribute] = worker[attribute]

    artifacts = set(task.setdefault("attributes", {}).get("release_artifacts", []))
    for upstream_artifact in worker["upstream-artifacts"]:
        for path in upstream_artifact["paths"]:
            artifacts.update(
                get_signed_artifacts(
                    input=path,
                    formats=upstream_artifact["formats"],
                    behavior=worker.get("mac-behavior"),
                )
            )
    task["attributes"]["release_artifacts"] = sorted(list(artifacts))


@payload_builder(
    "beetmover",
    schema={
        # the maximum time to run, in seconds
        Required("max-run-time"): int,
        # locale key, if this is a locale beetmover job
        Optional("locale"): str,
        Required("release-properties"): {
            "app-name": str,
            "app-version": str,
            "branch": str,
            "build-id": str,
            "hash-type": str,
            "platform": str,
        },
        # list of artifact URLs for the artifacts that should be beetmoved
        Required("upstream-artifacts"): [
            {
                # taskId of the task with the artifact
                Required("taskId"): taskref_or_string,
                # type of signing task (for CoT)
                Required("taskType"): str,
                # Paths to the artifacts to sign
                Required("paths"): [str],
                # locale is used to map upload path and allow for duplicate simple names
                Required("locale"): str,
            }
        ],
        Optional("artifact-map"): object,
    },
)
def build_beetmover_payload(config, task, task_def):
    worker = task["worker"]
    release_config = get_release_config(config)
    release_properties = worker["release-properties"]

    task_def["payload"] = {
        "maxRunTime": worker["max-run-time"],
        "releaseProperties": {
            "appName": release_properties["app-name"],
            "appVersion": release_properties["app-version"],
            "branch": release_properties["branch"],
            "buildid": release_properties["build-id"],
            "hashType": release_properties["hash-type"],
            "platform": release_properties["platform"],
        },
        "upload_date": config.params["build_date"],
        "upstreamArtifacts": worker["upstream-artifacts"],
    }
    if worker.get("locale"):
        task_def["payload"]["locale"] = worker["locale"]
    if worker.get("artifact-map"):
        task_def["payload"]["artifactMap"] = worker["artifact-map"]
    if release_config:
        task_def["payload"].update(release_config)


@payload_builder(
    "beetmover-push-to-release",
    schema={
        # the maximum time to run, in seconds
        Required("max-run-time"): int,
        Required("product"): str,
    },
)
def build_beetmover_push_to_release_payload(config, task, task_def):
    worker = task["worker"]
    release_config = get_release_config(config)
    partners = [f"{p}/{s}" for p, s, _ in get_partners_to_be_published(config)]

    task_def["payload"] = {
        "maxRunTime": worker["max-run-time"],
        "product": worker["product"],
        "version": release_config["version"],
        "build_number": release_config["build_number"],
        "partners": partners,
    }


@payload_builder(
    "beetmover-import-from-gcs-to-artifact-registry",
    schema={
        Required("max-run-time"): int,
        Required("gcs-sources"): [str],
        Required("product"): str,
    },
)
def build_import_from_gcs_to_artifact_registry_payload(config, task, task_def):
    task_def["payload"] = {
        "product": task["worker"]["product"],
        "gcs_sources": task["worker"]["gcs-sources"],
    }


@payload_builder(
    "beetmover-maven",
    schema={
        Required("max-run-time"): int,
        Required("release-properties"): {
            "app-name": str,
            "app-version": str,
            "branch": str,
            "build-id": str,
            "artifact-id": str,
            "hash-type": str,
            "platform": str,
        },
        Required("upstream-artifacts"): [
            {
                Required("taskId"): taskref_or_string,
                Required("taskType"): str,
                Required("paths"): [str],
                Optional("zipExtract"): bool,
            }
        ],
        Optional("artifact-map"): object,
    },
)
def build_beetmover_maven_payload(config, task, task_def):
    build_beetmover_payload(config, task, task_def)

    task_def["payload"]["artifact_id"] = task["worker"]["release-properties"][
        "artifact-id"
    ]
    if task["worker"].get("artifact-map"):
        task_def["payload"]["artifactMap"] = task["worker"]["artifact-map"]

    task_def["payload"]["version"] = _compute_geckoview_version(
        task["worker"]["release-properties"]["app-version"],
        task["worker"]["release-properties"]["build-id"],
    )

    del task_def["payload"]["releaseProperties"]["hashType"]
    del task_def["payload"]["releaseProperties"]["platform"]


@payload_builder(
    "balrog",
    schema={
        Required("balrog-action"): Any(*BALROG_ACTIONS),
        Optional("product"): str,
        Optional("platforms"): [str],
        Optional("release-eta"): str,
        Optional("channel-names"): optionally_keyed_by("release-type", [str]),
        Optional("require-mirrors"): bool,
        Optional("publish-rules"): optionally_keyed_by(
            "release-type", "release-level", [int]
        ),
        Optional("rules-to-update"): optionally_keyed_by(
            "release-type", "release-level", [str]
        ),
        Optional("archive-domain"): optionally_keyed_by("release-level", str),
        Optional("download-domain"): optionally_keyed_by("release-level", str),
        Optional("blob-suffix"): str,
        Optional("complete-mar-filename-pattern"): str,
        Optional("complete-mar-bouncer-product-pattern"): str,
        Optional("update-line"): object,
        Optional("suffixes"): [str],
        Optional("background-rate"): optionally_keyed_by(
            "release-type", "beta-number", Any(int, None)
        ),
        Optional("force-fallback-mapping-update"): optionally_keyed_by(
            "release-type", "beta-number", bool
        ),
        Optional("pin-channels"): optionally_keyed_by(
            "release-type", "release-level", [str]
        ),
        # list of artifact URLs for the artifacts that should be beetmoved
        Optional("upstream-artifacts"): [
            {
                # taskId of the task with the artifact
                Required("taskId"): taskref_or_string,
                # type of signing task (for CoT)
                Required("taskType"): str,
                # Paths to the artifacts to sign
                Required("paths"): [str],
            }
        ],
    },
)
def build_balrog_payload(config, task, task_def):
    worker = task["worker"]
    release_config = get_release_config(config)
    beta_number = None
    if "b" in release_config["version"]:
        beta_number = release_config["version"].split("b")[-1]

    task_def["payload"] = {
        "behavior": worker["balrog-action"],
    }

    if (
        worker["balrog-action"] == "submit-locale"
        or worker["balrog-action"] == "v2-submit-locale"
    ):
        task_def["payload"].update(
            {
                "upstreamArtifacts": worker["upstream-artifacts"],
                "suffixes": worker["suffixes"],
            }
        )
    else:
        for prop in (
            "archive-domain",
            "channel-names",
            "download-domain",
            "publish-rules",
            "rules-to-update",
            "background-rate",
            "force-fallback-mapping-update",
            "pin-channels",
        ):
            if prop in worker:
                resolve_keyed_by(
                    worker,
                    prop,
                    task["description"],
                    **{
                        "release-type": config.params["release_type"],
                        "release-level": release_level(config.params["project"]),
                        "beta-number": beta_number,
                    },
                )
        task_def["payload"].update(
            {
                "build_number": release_config["build_number"],
                "product": worker["product"],
                "version": release_config["version"],
            }
        )
        for prop in (
            "blob-suffix",
            "complete-mar-filename-pattern",
            "complete-mar-bouncer-product-pattern",
            "pin-channels",
        ):
            if prop in worker:
                task_def["payload"][prop.replace("-", "_")] = worker[prop]
        if (
            worker["balrog-action"] == "submit-toplevel"
            or worker["balrog-action"] == "v2-submit-toplevel"
        ):
            task_def["payload"].update(
                {
                    "app_version": release_config["appVersion"],
                    "archive_domain": worker["archive-domain"],
                    "channel_names": worker["channel-names"],
                    "download_domain": worker["download-domain"],
                    "partial_versions": release_config.get("partial_versions", ""),
                    "platforms": worker["platforms"],
                    "rules_to_update": worker["rules-to-update"],
                    "require_mirrors": worker["require-mirrors"],
                    "update_line": worker["update-line"],
                }
            )
        else:  # schedule / ship
            task_def["payload"].update(
                {
                    "publish_rules": worker["publish-rules"],
                    "release_eta": worker.get(
                        "release-eta", config.params.get("release_eta")
                    )
                    or "",
                }
            )
            if worker.get("force-fallback-mapping-update"):
                task_def["payload"]["force_fallback_mapping_update"] = worker[
                    "force-fallback-mapping-update"
                ]
            if worker.get("background-rate"):
                task_def["payload"]["background_rate"] = worker["background-rate"]


@payload_builder(
    "bouncer-aliases",
    schema={
        Required("entries"): object,
    },
)
def build_bouncer_aliases_payload(config, task, task_def):
    worker = task["worker"]

    task_def["payload"] = {"aliases_entries": worker["entries"]}


@payload_builder(
    "bouncer-locations",
    schema={
        Required("implementation"): "bouncer-locations",
        Required("bouncer-products"): [str],
    },
)
def build_bouncer_locations_payload(config, task, task_def):
    worker = task["worker"]
    release_config = get_release_config(config)

    task_def["payload"] = {
        "bouncer_products": worker["bouncer-products"],
        "version": release_config["version"],
        "product": task["shipping-product"],
    }


@payload_builder(
    "bouncer-submission",
    schema={
        Required("locales"): [str],
        Required("entries"): object,
    },
)
def build_bouncer_submission_payload(config, task, task_def):
    worker = task["worker"]

    task_def["payload"] = {
        "locales": worker["locales"],
        "submission_entries": worker["entries"],
    }


@payload_builder(
    "push-flatpak",
    schema={
        Required("channel"): str,
        Required("upstream-artifacts"): [
            {
                Required("taskId"): taskref_or_string,
                Required("taskType"): str,
                Required("paths"): [str],
            }
        ],
    },
)
def build_push_flatpak_payload(config, task, task_def):
    worker = task["worker"]

    task_def["payload"] = {
        "channel": worker["channel"],
        "upstreamArtifacts": worker["upstream-artifacts"],
    }


@payload_builder(
    "push-msix",
    schema={
        Required("channel"): str,
        Optional("publish-mode"): str,
        Required("upstream-artifacts"): [
            {
                Required("taskId"): taskref_or_string,
                Required("taskType"): str,
                Required("paths"): [str],
            }
        ],
    },
)
def build_push_msix_payload(config, task, task_def):
    worker = task["worker"]

    task_def["payload"] = {
        "channel": worker["channel"],
        "upstreamArtifacts": worker["upstream-artifacts"],
    }
    if worker.get("publish-mode"):
        task_def["payload"]["publishMode"] = worker["publish-mode"]


@payload_builder(
    "shipit-update-product-channel-version",
    schema={
        Required("product"): str,
        Required("channel"): str,
        Required("version"): str,
    },
)
def build_ship_it_update_product_channel_version_payload(config, task, task_def):
    worker = task["worker"]
    task_def["payload"] = {
        "product": worker["product"],
        "version": worker["version"],
        "channel": worker["channel"],
    }


@payload_builder(
    "shipit-shipped",
    schema={
        Required("release-name"): str,
    },
)
def build_ship_it_shipped_payload(config, task, task_def):
    worker = task["worker"]

    task_def["payload"] = {"release_name": worker["release-name"]}


@payload_builder(
    "shipit-maybe-release",
    schema={
        Required("phase"): str,
    },
)
def build_ship_it_maybe_release_payload(config, task, task_def):
    # expect branch name, including path
    branch = config.params["head_repository"][len("https://hg.mozilla.org/") :]
    # 'version' is e.g. '71.0b13' (app_version doesn't have beta number)
    version = config.params["version"]

    task_def["payload"] = {
        "product": task["shipping-product"],
        "branch": branch,
        "phase": task["worker"]["phase"],
        "version": version,
        "cron_revision": config.params["head_rev"],
    }


@payload_builder(
    "push-addons",
    schema={
        Required("channel"): Any("listed", "unlisted"),
        Required("upstream-artifacts"): [
            {
                Required("taskId"): taskref_or_string,
                Required("taskType"): str,
                Required("paths"): [str],
            }
        ],
    },
)
def build_push_addons_payload(config, task, task_def):
    worker = task["worker"]

    task_def["payload"] = {
        "channel": worker["channel"],
        "upstreamArtifacts": worker["upstream-artifacts"],
    }


@payload_builder(
    "treescript",
    schema={
        Required("tags"): [Any("buildN", "release", None)],
        Required("bump"): bool,
        Optional("bump-files"): [str],
        Optional("repo-param-prefix"): str,
        Optional("dontbuild"): bool,
        Optional("ignore-closed-tree"): bool,
        Optional("force-dry-run"): bool,
        Optional("push"): bool,
        Optional("source-repo"): str,
        Optional("ssh-user"): str,
        Optional("l10n-bump-info"): [
            {
                Required("name"): str,
                Required("path"): str,
                Required("version-path"): str,
                Optional("l10n-repo-url"): str,
                Optional("l10n-repo-target-branch"): str,
                Optional("ignore-config"): object,
                Required("platform-configs"): [
                    {
                        Required("platforms"): [str],
                        Required("path"): str,
                        Optional("format"): str,
                    }
                ],
            }
        ],
        Optional("merge-info"): object,
        Optional("android-l10n-import-info"): {
            Required("from-repo-url"): str,
            Required("toml-info"): [
                {
                    Required("toml-path"): str,
                    Required("dest-path"): str,
                }
            ],
        },
        Optional("android-l10n-sync-info"): {
            Required("from-repo-url"): str,
            Required("toml-info"): [
                {
                    Required("toml-path"): str,
                }
            ],
        },
    },
)
def build_treescript_payload(config, task, task_def):
    worker = task["worker"]
    release_config = get_release_config(config)

    task_def["payload"] = {"actions": []}
    actions = task_def["payload"]["actions"]
    if worker["tags"]:
        tag_names = []
        product = task["shipping-product"].upper()
        version = release_config["version"].replace(".", "_")
        buildnum = release_config["build_number"]
        if "buildN" in worker["tags"]:
            tag_names.extend(
                [
                    f"{product}_{version}_BUILD{buildnum}",
                ]
            )
        if "release" in worker["tags"]:
            tag_names.extend([f"{product}_{version}_RELEASE"])
        tag_info = {
            "tags": tag_names,
            "revision": config.params[
                "{}head_rev".format(worker.get("repo-param-prefix", ""))
            ],
        }
        task_def["payload"]["tag_info"] = tag_info
        actions.append("tag")

    if worker["bump"]:
        if not worker["bump-files"]:
            raise Exception("Version Bump requested without bump-files")

        bump_info = {}
        bump_info["next_version"] = release_config["next_version"]
        bump_info["files"] = worker["bump-files"]
        task_def["payload"]["version_bump_info"] = bump_info
        actions.append("version_bump")

    if worker.get("l10n-bump-info"):
        l10n_bump_info = []
        l10n_repo_urls = set()
        for lbi in worker["l10n-bump-info"]:
            new_lbi = {}
            if "l10n-repo-url" in lbi:
                l10n_repo_urls.add(lbi["l10n-repo-url"])
            for k, v in lbi.items():
                new_lbi[k.replace("-", "_")] = lbi[k]
            l10n_bump_info.append(new_lbi)

        task_def["payload"]["l10n_bump_info"] = l10n_bump_info
        if len(l10n_repo_urls) > 1:
            raise Exception(
                "Must use the same l10n-repo-url for all files in the same task!"
            )
        elif len(l10n_repo_urls) == 1:
            if "github.com" in l10n_repo_urls.pop():
                actions.append("l10n_bump_github")
            else:
                actions.append("l10n_bump")

    if worker.get("merge-info"):
        merge_info = {
            merge_param_name.replace("-", "_"): merge_param_value
            for merge_param_name, merge_param_value in worker["merge-info"].items()
            if merge_param_name != "version-files"
        }
        merge_info["version_files"] = [
            {
                file_param_name.replace("-", "_"): file_param_value
                for file_param_name, file_param_value in file_entry.items()
            }
            for file_entry in worker["merge-info"]["version-files"]
        ]
        task_def["payload"]["merge_info"] = merge_info
        actions.append("merge_day")

    if worker.get("android-l10n-import-info"):
        android_l10n_import_info = {}
        for k, v in worker["android-l10n-import-info"].items():
            android_l10n_import_info[k.replace("-", "_")] = worker[
                "android-l10n-import-info"
            ][k]
        android_l10n_import_info["toml_info"] = [
            {
                param_name.replace("-", "_"): param_value
                for param_name, param_value in entry.items()
            }
            for entry in worker["android-l10n-import-info"]["toml-info"]
        ]
        task_def["payload"]["android_l10n_import_info"] = android_l10n_import_info
        actions.append("android_l10n_import")

    if worker.get("android-l10n-sync-info"):
        android_l10n_sync_info = {}
        for k, v in worker["android-l10n-sync-info"].items():
            android_l10n_sync_info[k.replace("-", "_")] = worker[
                "android-l10n-sync-info"
            ][k]
        android_l10n_sync_info["toml_info"] = [
            {
                param_name.replace("-", "_"): param_value
                for param_name, param_value in entry.items()
            }
            for entry in worker["android-l10n-sync-info"]["toml-info"]
        ]
        task_def["payload"]["android_l10n_sync_info"] = android_l10n_sync_info
        actions.append("android_l10n_sync")

    if worker["push"]:
        actions.append("push")

    if worker.get("force-dry-run"):
        task_def["payload"]["dry_run"] = True

    if worker.get("dontbuild"):
        task_def["payload"]["dontbuild"] = True

    if worker.get("ignore-closed-tree") is not None:
        task_def["payload"]["ignore_closed_tree"] = worker["ignore-closed-tree"]

    if worker.get("source-repo"):
        task_def["payload"]["source_repo"] = worker["source-repo"]

    if worker.get("ssh-user"):
        task_def["payload"]["ssh_user"] = worker["ssh-user"]


@payload_builder(
    "invalid",
    schema={
        # an invalid task is one which should never actually be created; this is used in
        # release automation on branches where the task just doesn't make sense
        Extra: object,
    },
)
def build_invalid_payload(config, task, task_def):
    task_def["payload"] = "invalid task - should never be created"


@payload_builder(
    "always-optimized",
    schema={
        Extra: object,
    },
)
@payload_builder("succeed", schema={})
def build_dummy_payload(config, task, task_def):
    task_def["payload"] = {}


transforms = TransformSequence()


@transforms.add
def set_implementation(config, tasks):
    """
    Set the worker implementation based on the worker-type alias.
    """
    for task in tasks:
        default_worker_implementation, default_os = worker_type_implementation(
            config.graph_config, config.params, task["worker-type"]
        )

        worker = task.setdefault("worker", {})
        tags = task.setdefault("tags", {})

        worker_implementation = worker.get(
            "implementation", default_worker_implementation
        )
        tag_worker_implementation = _get_worker_implementation_tag(
            config, task["worker-type"], worker_implementation
        )
        if worker_implementation:
            worker["implementation"] = worker_implementation
            tags["worker-implementation"] = tag_worker_implementation

        os = worker.get("os", default_os)
        if os:
            tags["os"] = os
            worker["os"] = os

        yield task


def _get_worker_implementation_tag(config, task_worker_type, worker_implementation):
    # Scriptworkers have different types of payload and each sets its own
    # worker-implementation. Per bug 1955941, we want to bundle them all in one category
    # through their tags.
    provisioner_id, _ = get_worker_type(
        config.graph_config,
        config.params,
        task_worker_type,
    )
    if provisioner_id in ("scriptworker-k8s", "scriptworker-prov-v1"):
        return "scriptworker"

    return worker_implementation


@transforms.add
def set_defaults(config, tasks):
    for task in tasks:
        task.setdefault("shipping-phase", None)
        task.setdefault("shipping-product", None)
        task.setdefault("always-target", False)
        task.setdefault("optimization", None)
        task.setdefault("use-sccache", False)

        worker = task["worker"]
        if worker["implementation"] in ("docker-worker",):
            worker.setdefault("chain-of-trust", False)
            worker.setdefault("taskcluster-proxy", False)
            worker.setdefault("allow-ptrace", True)
            worker.setdefault("loopback-video", False)
            worker.setdefault("loopback-audio", False)
            worker.setdefault("docker-in-docker", False)
            worker.setdefault("privileged", False)
            worker.setdefault("volumes", [])
            worker.setdefault("env", {})
            if "caches" in worker:
                for c in worker["caches"]:
                    c.setdefault("skip-untrusted", False)
        elif worker["implementation"] == "generic-worker":
            worker.setdefault("env", {})
            worker.setdefault("os-groups", [])
            if worker["os-groups"] and worker["os"] not in (
                "windows",
                "linux",
            ):
                raise Exception(
                    "os-groups feature of generic-worker is only supported on "
                    "Windows and Linux, not on {}".format(worker["os"])
                )
            worker.setdefault("chain-of-trust", False)
        elif worker["implementation"] in (
            "scriptworker-signing",
            "beetmover",
            "beetmover-push-to-release",
            "beetmover-maven",
            "beetmover-import-from-gcs-to-artifact-registry",
        ):
            worker.setdefault("max-run-time", 600)
        elif worker["implementation"] == "push-apk":
            worker.setdefault("commit", False)

        yield task


@transforms.add
def setup_raptor(config, tasks):
    """Add options that are specific to raptor jobs (identified by suite=raptor).

    This variant uses a separate set of transforms for manipulating the tests at the
    task-level. Currently only used for setting the taskcluster proxy setting and
    the scopes required for perftest secrets.
    """
    from gecko_taskgraph.transforms.test.raptor import (
        task_transforms as raptor_transforms,
    )

    for task in tasks:
        if task.get("extra", {}).get("suite", "") != "raptor":
            yield task
            continue

        yield from raptor_transforms(config, [task])


@transforms.add
def task_name_from_label(config, tasks):
    for task in tasks:
        taskname = task.pop("name", None)
        if "label" not in task:
            if taskname is None:
                raise Exception("task has neither a name nor a label")
            task["label"] = f"{config.kind}-{taskname}"
        yield task


UNSUPPORTED_SHIPPING_PRODUCT_ERROR = """\
The shipping product {product} is not in the list of configured products in
`taskcluster/config.yml'.
"""


def validate_shipping_product(config, product):
    if product not in config.graph_config["release-promotion"]["products"]:
        raise Exception(UNSUPPORTED_SHIPPING_PRODUCT_ERROR.format(product=product))


@transforms.add
def validate(config, tasks):
    for task in tasks:
        validate_schema(
            task_description_schema,
            task,
            "In task {!r}:".format(task.get("label", "?no-label?")),
        )
        validate_schema(
            payload_builders[task["worker"]["implementation"]].schema,
            task["worker"],
            "In task.run {!r}:".format(task.get("label", "?no-label?")),
        )
        if task["shipping-product"] is not None:
            validate_shipping_product(config, task["shipping-product"])
        yield task


@index_builder("generic")
def add_generic_index_routes(config, task):
    index = task.get("index")
    routes = task.setdefault("routes", [])

    verify_index(config, index)

    subs = config.params.copy()
    subs["job-name"] = index["job-name"]
    subs["build_date_long"] = time.strftime(
        "%Y.%m.%d.%Y%m%d%H%M%S", time.gmtime(config.params["build_date"])
    )
    subs["build_date"] = time.strftime(
        "%Y.%m.%d", time.gmtime(config.params["build_date"])
    )
    subs["product"] = index["product"]
    subs["trust-domain"] = config.graph_config["trust-domain"]
    subs["branch_rev"] = get_branch_rev(config)
    try:
        subs["branch_git_rev"] = get_branch_git_rev(config)
    except KeyError:
        pass

    project = config.params.get("project")

    for tpl in V2_ROUTE_TEMPLATES:
        try:
            routes.append(tpl.format(**subs))
        except KeyError:
            # Ignore errors that arise from branch_git_rev not being set.
            pass

    # Additionally alias all tasks for "trunk" repos into a common
    # namespace.
    if project and project in TRUNK_PROJECTS:
        for tpl in V2_TRUNK_ROUTE_TEMPLATES:
            routes.append(tpl.format(**subs))

    return task


@index_builder("shippable")
def add_shippable_index_routes(config, task):
    index = task.get("index")
    routes = task.setdefault("routes", [])

    verify_index(config, index)

    subs = config.params.copy()
    subs["job-name"] = index["job-name"]
    subs["build_date_long"] = time.strftime(
        "%Y.%m.%d.%Y%m%d%H%M%S", time.gmtime(config.params["build_date"])
    )
    subs["build_date"] = time.strftime(
        "%Y.%m.%d", time.gmtime(config.params["build_date"])
    )
    subs["product"] = index["product"]
    subs["trust-domain"] = config.graph_config["trust-domain"]
    subs["branch_rev"] = get_branch_rev(config)
    try:
        subs["branch_git_rev"] = get_branch_git_rev(config)
    except KeyError:
        pass

    for tpl in V2_SHIPPABLE_TEMPLATES:
        try:
            routes.append(tpl.format(**subs))
        except KeyError:
            # Ignore errors that arise from branch_git_rev not being set.
            pass

    # Also add routes for en-US
    task = add_shippable_l10n_index_routes(config, task, force_locale="en-US")

    return task


@index_builder("shippable-with-multi-l10n")
def add_shippable_multi_index_routes(config, task):
    task = add_shippable_index_routes(config, task)
    task = add_l10n_index_routes(config, task, force_locale="multi")
    return task


@index_builder("l10n")
def add_l10n_index_routes(config, task, force_locale=None):
    index = task.get("index")
    routes = task.setdefault("routes", [])

    verify_index(config, index)

    subs = config.params.copy()
    subs["job-name"] = index["job-name"]
    subs["build_date_long"] = time.strftime(
        "%Y.%m.%d.%Y%m%d%H%M%S", time.gmtime(config.params["build_date"])
    )
    subs["product"] = index["product"]
    subs["trust-domain"] = config.graph_config["trust-domain"]
    subs["branch_rev"] = get_branch_rev(config)

    locales = task["attributes"].get(
        "chunk_locales", task["attributes"].get("all_locales")
    )
    # Some tasks has only one locale set
    if task["attributes"].get("locale"):
        locales = [task["attributes"]["locale"]]

    if force_locale:
        # Used for en-US and multi-locale
        locales = [force_locale]

    if not locales:
        raise Exception("Error: Unable to use l10n index for tasks without locales")

    # If there are too many locales, we can't write a route for all of them
    # See Bug 1323792
    if len(locales) > 18:  # 18 * 3 = 54, max routes = 64
        return task

    for locale in locales:
        for tpl in V2_L10N_TEMPLATES:
            routes.append(tpl.format(locale=locale, **subs))

    return task


@index_builder("shippable-l10n")
def add_shippable_l10n_index_routes(config, task, force_locale=None):
    index = task.get("index")
    routes = task.setdefault("routes", [])

    verify_index(config, index)

    subs = config.params.copy()
    subs["job-name"] = index["job-name"]
    subs["build_date_long"] = time.strftime(
        "%Y.%m.%d.%Y%m%d%H%M%S", time.gmtime(config.params["build_date"])
    )
    subs["product"] = index["product"]
    subs["trust-domain"] = config.graph_config["trust-domain"]
    subs["branch_rev"] = get_branch_rev(config)

    locales = task["attributes"].get(
        "chunk_locales", task["attributes"].get("all_locales")
    )
    # Some tasks has only one locale set
    if task["attributes"].get("locale"):
        locales = [task["attributes"]["locale"]]

    if force_locale:
        # Used for en-US and multi-locale
        locales = [force_locale]

    if not locales:
        raise Exception("Error: Unable to use l10n index for tasks without locales")

    # If there are too many locales, we can't write a route for all of them
    # See Bug 1323792
    if len(locales) > 18:  # 18 * 3 = 54, max routes = 64
        return task

    for locale in locales:
        for tpl in V2_SHIPPABLE_L10N_TEMPLATES:
            routes.append(tpl.format(locale=locale, **subs))

    return task


def add_geckoview_index_routes(config, task):
    index = task.get("index")
    routes = task.setdefault("routes", [])
    geckoview_version = _compute_geckoview_version(
        config.params["app_version"], config.params["moz_build_date"]
    )

    subs = {
        "geckoview-version": geckoview_version,
        "job-name": index["job-name"],
        "product": index["product"],
        "project": config.params["project"],
        "trust-domain": config.graph_config["trust-domain"],
    }
    routes.append(V2_GECKOVIEW_RELEASE.format(**subs))

    return task


@index_builder("android-shippable")
def add_android_shippable_index_routes(config, task):
    task = add_shippable_index_routes(config, task)
    task = add_geckoview_index_routes(config, task)

    return task


@index_builder("android-shippable-with-multi-l10n")
def add_android_shippable_multi_index_routes(config, task):
    task = add_shippable_multi_index_routes(config, task)
    task = add_geckoview_index_routes(config, task)

    return task


@transforms.add
def add_index_routes(config, tasks):
    for task in tasks:
        index = task.get("index", {})

        # The default behavior is to rank tasks according to their tier
        extra_index = task.setdefault("extra", {}).setdefault("index", {})
        rank = index.get("rank", "by-tier")

        if rank == "by-tier":
            # rank is one for non-tier-1 tasks and based on pushid for others;
            # this sorts tier-{2,3} builds below tier-1 in the index, but above
            # eager-index
            tier = task.get("treeherder", {}).get("tier", 3)
            extra_index["rank"] = 1 if tier > 1 else int(config.params["build_date"])
        elif rank == "build_date":
            extra_index["rank"] = int(config.params["build_date"])
        else:
            extra_index["rank"] = rank

        if not index:
            yield task
            continue

        index_type = index.get("type", "generic")
        task = index_builders[index_type](config, task)

        del task["index"]
        yield task


@transforms.add
def try_task_config_env(config, tasks):
    """Set environment variables in the task."""
    env = config.params["try_task_config"].get("env")
    if not env:
        yield from tasks
        return

    # Find all implementations that have an 'env' key.
    implementations = {
        name
        for name, builder in payload_builders.items()
        if "env" in builder.schema.schema
    }
    for task in tasks:
        if task["worker"]["implementation"] in implementations:
            task["worker"]["env"].update(env)
        yield task


@transforms.add
def try_task_config_chemspill_prio(config, tasks):
    """Increase the priority from lowest and very-low -> low, but leave others unchanged."""
    chemspill_prio = config.params["try_task_config"].get("chemspill-prio")
    if not chemspill_prio:
        yield from tasks
        return

    for task in tasks:
        if task["priority"] in ("lowest", "very-low"):
            task["priority"] = "low"
        yield task


@transforms.add
def try_task_config_routes(config, tasks):
    """Set routes in the task."""
    routes = config.params["try_task_config"].get("routes")
    for task in tasks:
        if routes:
            task_routes = task.setdefault("routes", [])
            task_routes.extend(routes)
        yield task


@transforms.add
def set_task_and_artifact_expiry(config, jobs):
    """Set the default expiry for tasks and their artifacts.

    These values are read from ci/config.yml
    """
    now = datetime.datetime.utcnow()
    # We don't want any configuration leading to anything with an expiry longer
    # than 28 days on try.
    cap = "28 days" if is_try(config.params) else None
    cap_from_now = fromNow(cap, now) if cap else None
    if cap:
        for policy, expires in config.graph_config["expiration-policy"]["by-project"][
            "try"
        ].items():
            if fromNow(expires, now) > cap_from_now:
                raise Exception(
                    f'expiration-policy "{policy}" is larger than {cap} '
                    f'for {config.params["project"]}'
                )
    for job in jobs:
        expires = get_expiration(config, job.get("expiration-policy", "default"))
        job_expiry = job.setdefault("expires-after", expires)
        job_expiry_from_now = fromNow(job_expiry, now)
        if cap and job_expiry_from_now > cap_from_now:
            job_expiry, job_expiry_from_now = cap, cap_from_now
        # If the task has no explicit expiration-policy, but has an expires-after,
        # we use that as the default artifact expiry.
        artifact_expires = expires if "expiration-policy" in job else job_expiry

        for artifact in job["worker"].get("artifacts", ()):
            artifact_expiry = artifact.setdefault("expires-after", artifact_expires)

            # By using > instead of >=, there's a chance of mismatch
            #   where the artifact expires sooner than the task.
            #   There is no chance, however, of mismatch where artifacts
            #   expire _after_ the task.
            # Currently this leads to some build tasks having logs
            #   that expire in 1 year while the task expires in 3 years.
            if fromNow(artifact_expiry, now) > job_expiry_from_now:
                artifact["expires-after"] = job_expiry

        yield job


def group_name_variant(group_names, groupSymbol):
    # iterate through variants, allow for Base-[variant_list]
    # sorting longest->shortest allows for finding variants when
    # other variants have a suffix that is a subset
    variant_symbols = sorted(
        [
            (
                v,
                TEST_VARIANTS[v]["suffix"],
                TEST_VARIANTS[v].get("description", "{description}"),
            )
            for v in TEST_VARIANTS
            if TEST_VARIANTS[v].get("suffix", "")
        ],
        key=lambda tup: len(tup[1]),
        reverse=True,
    )

    # strip known variants
    # build a list of known variants
    base_symbol = groupSymbol
    found_variants = []
    for variant, suffix, description in variant_symbols:
        if f"-{suffix}" in base_symbol:
            base_symbol = base_symbol.replace(f"-{suffix}", "")
            found_variants.append((variant, description))

    if base_symbol not in group_names:
        return ""

    description = group_names[base_symbol]
    for variant, desc in found_variants:
        description = desc.format(description=description)

    return description


@transforms.add
def build_task(config, tasks):
    for task in tasks:
        level = str(config.params["level"])

        task_worker_type = task["worker-type"]
        worker_overrides = config.params["try_task_config"].get("worker-overrides", {})
        if task_worker_type in worker_overrides:
            worker_pool = worker_overrides[task_worker_type]
            provisioner_id, worker_type = worker_pool.split("/", 1)
        else:
            provisioner_id, worker_type = get_worker_type(
                config.graph_config,
                config.params,
                task_worker_type,
            )
        task["worker-type"] = "/".join([provisioner_id, worker_type])
        project = config.params["project"]

        routes = task.get("routes", [])
        scopes = [
            s.format(level=level, project=project) for s in task.get("scopes", [])
        ]

        # set up extra
        extra = task.get("extra", {})
        extra["parent"] = {"task-reference": "<decision>"}
        task_th = task.get("treeherder")
        if task_th:
            extra.setdefault("treeherder-platform", task_th["platform"])
            treeherder = extra.setdefault("treeherder", {})

            machine_platform, collection = task_th["platform"].split("/", 1)
            treeherder["machine"] = {"platform": machine_platform}
            treeherder["collection"] = {collection: True}

            group_names = config.graph_config["treeherder"]["group-names"]
            groupSymbol, symbol = split_symbol(task_th["symbol"])
            if groupSymbol != "?":
                treeherder["groupSymbol"] = groupSymbol
                description = group_names.get(
                    groupSymbol, group_name_variant(group_names, groupSymbol)
                )
                if not description:
                    path = os.path.join(config.path, task.get("task-from", ""))
                    raise Exception(UNKNOWN_GROUP_NAME.format(groupSymbol, path))
                treeherder["groupName"] = description
            treeherder["symbol"] = symbol
            if len(symbol) > 25 or len(groupSymbol) > 25:
                raise RuntimeError(
                    "Treeherder group and symbol names must not be longer than "
                    "25 characters: {} (see {})".format(
                        task_th["symbol"],
                        TC_TREEHERDER_SCHEMA_URL,
                    )
                )
            treeherder["jobKind"] = task_th["kind"]
            treeherder["tier"] = task_th["tier"]

            branch_rev = get_branch_rev(config)

            routes.append(
                "{}.v2.{}.{}".format(
                    TREEHERDER_ROUTE_ROOT,
                    config.params["project"],
                    branch_rev,
                )
            )

        if "deadline-after" not in task:
            task["deadline-after"] = "1 day"

        if "priority" not in task:
            task["priority"] = get_default_priority(
                config.graph_config, config.params["project"]
            )

        tags = task.get("tags", {})
        attributes = task.get("attributes", {})

        tags.update(
            {
                "createdForUser": config.params["owner"],
                "kind": config.kind,
                "label": task["label"],
                "retrigger": "true" if attributes.get("retrigger", False) else "false",
                "project": config.params["project"],
                "trust-domain": config.graph_config["trust-domain"],
            }
        )

        task_def = {
            "provisionerId": provisioner_id,
            "workerType": worker_type,
            "routes": routes,
            "created": {"relative-datestamp": "0 seconds"},
            "deadline": {"relative-datestamp": task["deadline-after"]},
            "expires": {"relative-datestamp": task["expires-after"]},
            "scopes": scopes,
            "metadata": {
                "description": task["description"],
                "name": task["label"],
                "owner": config.params["owner"],
                "source": config.params.file_url(config.path, pretty=True),
            },
            "extra": extra,
            "tags": tags,
            "priority": task["priority"],
        }

        if task.get("requires", None):
            task_def["requires"] = task["requires"]

        if task_th:
            # link back to treeherder in description
            th_job_link = (
                "https://treeherder.mozilla.org/#/jobs?repo={}&revision={}&selectedTaskRun=<self>"
            ).format(config.params["project"], branch_rev)
            task_def["metadata"]["description"] = {
                "task-reference": "{description} ([Treeherder job]({th_job_link}))".format(
                    description=task_def["metadata"]["description"],
                    th_job_link=th_job_link,
                )
            }

        # add the payload and adjust anything else as required (e.g., scopes)
        payload_builders[task["worker"]["implementation"]].builder(
            config, task, task_def
        )

        # Resolve run-on-projects
        build_platform = attributes.get("build_platform")
        resolve_keyed_by(
            task,
            "run-on-projects",
            item_name=task["label"],
            **{"build-platform": build_platform},
        )
        attributes["run_on_projects"] = task.get("run-on-projects", ["all"])
        attributes["always_target"] = task["always-target"]
        # This logic is here since downstream tasks don't always match their
        # upstream dependency's shipping_phase.
        # A text_type task['shipping-phase'] takes precedence, then
        # an existing attributes['shipping_phase'], then fall back to None.
        if task.get("shipping-phase") is not None:
            attributes["shipping_phase"] = task["shipping-phase"]
        else:
            attributes.setdefault("shipping_phase", None)
        # shipping_product will always match the upstream task's
        # shipping_product, so a pre-set existing attributes['shipping_product']
        # takes precedence over task['shipping-product']. However, make sure
        # we don't have conflicting values.
        if task.get("shipping-product") and attributes.get("shipping_product") not in (
            None,
            task["shipping-product"],
        ):
            raise Exception(
                "{} shipping_product {} doesn't match task shipping-product {}!".format(
                    task["label"],
                    attributes["shipping_product"],
                    task["shipping-product"],
                )
            )
        attributes.setdefault("shipping_product", task["shipping-product"])

        # Set MOZ_AUTOMATION on all jobs.
        if task["worker"]["implementation"] in (
            "generic-worker",
            "docker-worker",
        ):
            payload = task_def.get("payload")
            if payload:
                env = payload.setdefault("env", {})
                env["MOZ_AUTOMATION"] = "1"

        dependencies = task.get("dependencies", {})
        if_dependencies = task.get("if-dependencies", [])
        if if_dependencies:
            for i, dep in enumerate(if_dependencies):
                if dep in dependencies:
                    if_dependencies[i] = dependencies[dep]
                    continue

                raise Exception(
                    "{label} specifies '{dep}' in if-dependencies, "
                    "but {dep} is not a dependency!".format(
                        label=task["label"], dep=dep
                    )
                )

        yield {
            "label": task["label"],
            "description": task["description"],
            "task": task_def,
            "dependencies": dependencies,
            "if-dependencies": if_dependencies,
            "soft-dependencies": task.get("soft-dependencies", []),
            "attributes": attributes,
            "optimization": task.get("optimization", None),
        }


@transforms.add
def chain_of_trust(config, tasks):
    for task in tasks:
        if task["task"].get("payload", {}).get("features", {}).get("chainOfTrust"):
            image = task.get("dependencies", {}).get("docker-image")
            if image:
                cot = (
                    task["task"].setdefault("extra", {}).setdefault("chainOfTrust", {})
                )
                cot.setdefault("inputs", {})["docker-image"] = {
                    "task-reference": "<docker-image>"
                }
        yield task


@transforms.add
def check_task_identifiers(config, tasks):
    """Ensures that all tasks have well defined identifiers:
    ``^[a-zA-Z0-9_-]{1,38}$``
    """
    e = re.compile("^[a-zA-Z0-9_-]{1,38}$")
    for task in tasks:
        for attrib in ("workerType", "provisionerId"):
            if not e.match(task["task"][attrib]):
                raise Exception(
                    "task {}.{} is not a valid identifier: {}".format(
                        task["label"], attrib, task["task"][attrib]
                    )
                )
        yield task


@transforms.add
def check_task_dependencies(config, tasks):
    """Ensures that tasks don't have more than 100 dependencies."""
    for task in tasks:
        if len(task["dependencies"]) > MAX_DEPENDENCIES:
            raise Exception(
                "task {}/{} has too many dependencies ({} > {})".format(
                    config.kind,
                    task["label"],
                    len(task["dependencies"]),
                    MAX_DEPENDENCIES,
                )
            )
        yield task


def check_caches_are_volumes(task):
    """Ensures that all cache paths are defined as volumes.

    Caches and volumes are the only filesystem locations whose content
    isn't defined by the Docker image itself. Some caches are optional
    depending on the job environment. We want paths that are potentially
    caches to have as similar behavior regardless of whether a cache is
    used. To help enforce this, we require that all paths used as caches
    to be declared as Docker volumes. This check won't catch all offenders.
    But it is better than nothing.
    """
    volumes = {s for s in task["worker"]["volumes"]}
    paths = {c["mount-point"] for c in task["worker"].get("caches", [])}
    missing = paths - volumes

    if not missing:
        return

    raise Exception(
        "task %s (image %s) has caches that are not declared as "
        "Docker volumes: %s "
        "(have you added them as VOLUMEs in the Dockerfile?)"
        % (task["label"], task["worker"]["docker-image"], ", ".join(sorted(missing)))
    )


def check_required_volumes(task):
    """
    Ensures that all paths that are required to be volumes are defined as volumes.

    Performance of writing to files in poor in directories not marked as
    volumes, in docker. Ensure that paths that are often written to are marked
    as volumes.
    """
    volumes = set(task["worker"]["volumes"])
    paths = set(task["worker"].get("required-volumes", []))
    missing = paths - volumes

    if not missing:
        return

    raise Exception(
        "task %s (image %s) has paths that should be volumes for peformance "
        "that are not declared as Docker volumes: %s "
        "(have you added them as VOLUMEs in the Dockerfile?)"
        % (task["label"], task["worker"]["docker-image"], ", ".join(sorted(missing)))
    )


@transforms.add
def check_run_task_caches(config, tasks):
    """Audit for caches requiring run-task.

    run-task manages caches in certain ways. If a cache managed by run-task
    is used by a non run-task task, it could cause problems. So we audit for
    that and make sure certain cache names are exclusive to run-task.

    IF YOU ARE TEMPTED TO MAKE EXCLUSIONS TO THIS POLICY, YOU ARE LIKELY
    CONTRIBUTING TECHNICAL DEBT AND WILL HAVE TO SOLVE MANY OF THE PROBLEMS
    THAT RUN-TASK ALREADY SOLVES. THINK LONG AND HARD BEFORE DOING THAT.
    """
    re_reserved_caches = re.compile(
        """^
        (checkouts|tooltool-cache)
    """,
        re.VERBOSE,
    )

    re_sparse_checkout_cache = re.compile("^checkouts-sparse")

    cache_prefix = "{trust_domain}-level-{level}-".format(
        trust_domain=config.graph_config["trust-domain"],
        level=config.params["level"],
    )

    suffix = _run_task_suffix()

    for task in tasks:
        payload = task["task"].get("payload", {})
        command = payload.get("command") or [""]

        main_command = command[0] if isinstance(command[0], str) else ""
        run_task = main_command.endswith("run-task")

        require_sparse_cache = False
        have_sparse_cache = False

        if run_task:
            for arg in command[1:]:
                if not isinstance(arg, str):
                    continue

                if arg == "--":
                    break

                if arg.startswith("--gecko-sparse-profile"):
                    if "=" not in arg:
                        raise Exception(
                            "{} is specifying `--gecko-sparse-profile` to run-task "
                            "as two arguments. Unable to determine if the sparse "
                            "profile exists.".format(task["label"])
                        )
                    _, sparse_profile = arg.split("=", 1)
                    if not os.path.exists(os.path.join(GECKO, sparse_profile)):
                        raise Exception(
                            "{} is using non-existant sparse profile {}.".format(
                                task["label"], sparse_profile
                            )
                        )
                    require_sparse_cache = True
                    break

        for cache in payload.get("cache", {}):
            if not cache.startswith(cache_prefix):
                raise Exception(
                    "{} is using a cache ({}) which is not appropriate "
                    "for its trust-domain and level. It should start with {}.".format(
                        task["label"], cache, cache_prefix
                    )
                )

            cache = cache[len(cache_prefix) :]

            if re_sparse_checkout_cache.match(cache):
                have_sparse_cache = True

            if not re_reserved_caches.match(cache):
                continue

            if not run_task:
                raise Exception(
                    "%s is using a cache (%s) reserved for run-task "
                    "change the task to use run-task or use a different "
                    "cache name" % (task["label"], cache)
                )

            if not cache.endswith(suffix):
                raise Exception(
                    "%s is using a cache (%s) reserved for run-task "
                    "but the cache name is not dependent on the contents "
                    "of run-task; change the cache name to conform to the "
                    "naming requirements" % (task["label"], cache)
                )

        if require_sparse_cache and not have_sparse_cache:
            raise Exception(
                "%s is using a sparse checkout but not using "
                "a sparse checkout cache; change the checkout "
                "cache name so it is sparse aware" % task["label"]
            )

        yield task
