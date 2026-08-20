# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
These transforms construct a task description to run the given test, based on a
test description.  The implementation here is shared among all test kinds, but
contains specific support for how we run tests in Gecko (via mozharness,
invoked in particular ways).

This is a good place to translate a test-description option such as
`single-core: true` to the implementation of that option in a task description
(worker options, mozharness commandline, environment variables, etc.)

The test description should be fully formed by the time it reaches these
transforms, and these transforms should not embody any specific knowledge about
what should run where. this is the wrong place for special-casing platforms,
for example - use `all_tests.py` instead.
"""

import logging
from importlib import import_module
from typing import Literal, Union
from typing import Optional as TOptional

from mozbuild.schedules import INCLUSIVE_COMPONENTS
from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import (
    Schema,
    optionally_keyed_by,
    resolve_keyed_by,
)

from gecko_taskgraph.optimize.schema import (
    OptimizationSchema,
)
from gecko_taskgraph.transforms.job import JobDescriptionSchema
from gecko_taskgraph.transforms.job.run_task import RunTaskSchema
from gecko_taskgraph.transforms.test import linux_perf_platform_restrictions
from gecko_taskgraph.transforms.test.other import get_mobile_project
from gecko_taskgraph.util.chunking import manifest_loaders

logger = logging.getLogger(__name__)
transforms = TransformSequence()


# Schema for a test description
#
# *****WARNING*****
#
# This is a great place for baffling cruft to accumulate, and that makes
# everyone move more slowly.  Be considerate of your fellow hackers!
# See the warnings in taskcluster/docs/how-tos.rst
#
# *****WARNING*****
class SuiteSchema(Schema, kw_only=True):
    category: TOptional[str] = None
    name: TOptional[  # type: ignore
        optionally_keyed_by("variant", str, use_msgspec=True)
    ] = None


class MozharnessSchema(Schema, kw_only=True):
    # the mozharness script used to run this task
    script: optionally_keyed_by("test-platform", str, use_msgspec=True)  # type: ignore
    # the config files required for the task
    config: optionally_keyed_by("test-platform", list[str], use_msgspec=True)  # type: ignore
    # mochitest flavor for mochitest runs
    mochitest_flavor: TOptional[str] = None
    # any additional actions to pass to the mozharness command
    actions: TOptional[list[str]] = None
    # additional command-line options for mozharness, beyond those
    # automatically added
    extra_options: optionally_keyed_by(  # type: ignore
        "test-platform", "variant", "subtest", "app", list[str], use_msgspec=True
    )
    # the artifact name (including path) to test on the build task; this is
    # generally set in a per-kind transformation
    build_artifact_name: TOptional[str] = None
    installer_url: TOptional[str] = None
    # If not false, tooltool downloads will be enabled via relengAPIProxy
    # for either just public files, or all files.  Not supported on Windows
    tooltool_downloads: Union[bool, Literal["public", "internal"]] = False
    # Add --blob-upload-branch=<project> mozharness parameter
    include_blob_upload_branch: TOptional[bool] = None
    # The setting for --download-symbols (if omitted, the option will not
    # be passed to mozharness)
    download_symbols: TOptional[Union[bool, Literal["ondemand"]]] = None
    # If set, then MOZ_NODE_PATH=/usr/local/bin/node is included in the
    # environment.  This is more than just a helpful path setting -- it
    # causes xpcshell tests to start additional servers, and runs
    # additional tests.
    set_moz_node_path: bool = False
    # If true, include chunking information in the command even if the number
    # of chunks is 1
    chunked: optionally_keyed_by("test-platform", bool, use_msgspec=True)  # type: ignore
    requires_signed_builds: optionally_keyed_by(  # type: ignore
        "test-platform", "variant", bool, use_msgspec=True
    )

    def __post_init__(self):
        super().__post_init__()
        if self.download_symbols is False:
            raise ValueError("download-symbols must be True or 'ondemand'")
        if self.tooltool_downloads is True:
            raise ValueError(
                "tooltool-downloads must be False, 'public', or 'internal'"
            )


class DockerImageSchema(Schema, kw_only=True):
    in_tree: TOptional[str] = None
    indexed: TOptional[str] = None


class TargetIndexSchema(Schema, kw_only=True):
    index: str
    name: str


class TargetUpstreamTaskSchema(Schema, kw_only=True):
    upstream_task: str
    name: str


class TestManifestsSchema(Schema, kw_only=True):
    active: list[str]
    skipped: list[str]


class TestDescriptionSchema(Schema, kw_only=True):
    # description of the suite, for the task metadata
    description: str
    # test suite category and name
    suite: TOptional[  # type: ignore
        Union[
            optionally_keyed_by("variant", str, use_msgspec=True),
            SuiteSchema,
        ]
    ] = None
    # base work directory used to set up the task.
    workdir: TOptional[  # type: ignore
        optionally_keyed_by(
            "test-platform", Union[str, Literal["default"]], use_msgspec=True
        )
    ] = None
    # the name by which this test suite is addressed in try syntax; defaults to
    # the test-name.  This will translate to the `unittest_try_name` or
    # `talos_try_name` attribute.
    try_name: TOptional[str] = None
    # additional tags to mark up this type of test
    tags: TOptional[dict[str, object]] = None
    # the symbol, or group(symbol), under which this task should appear in
    # treeherder.
    treeherder_symbol: str
    # the value to place in task.extra.treeherder.machine.platform; ideally
    # this is the same as build-platform, and that is the default, but in
    # practice it's not always a match.
    treeherder_machine_platform: TOptional[str] = None
    # attributes to appear in the resulting task (later transforms will add the
    # common attributes)
    attributes: TOptional[dict[str, object]] = None
    # relative path (from config.path) to the file task was defined in
    task_from: TOptional[str] = None
    # The `run_on_projects` attribute, defaulting to "all".  This dictates the
    # projects on which this task should be included in the target task set.
    # See the attributes documentation for details.
    #
    # Note that the special case 'built-projects', the default, uses the parent
    # build task's run-on-projects, meaning that tests run only on platforms
    # that are built.
    run_on_projects: TOptional[  # type: ignore
        optionally_keyed_by(
            "app",
            "subtest",
            "test-platform",
            "test-name",
            "variant",
            Union[list[str], Literal["built-projects"]],
            use_msgspec=True,
        )
    ] = None
    # Whether tasks should run on only a specific type of repository.
    run_on_repo_type: TOptional[
        JobDescriptionSchema.__annotations__["run_on_repo_type"]
    ] = None  # type: ignore
    # Whether tasks should run on specified Git branches.
    run_on_git_branches: TOptional[
        JobDescriptionSchema.__annotations__["run_on_git_branches"]
    ] = None  # type: ignore
    # When set only run on projects where the build would already be running.
    # This ensures tasks where this is True won't be the cause of the build
    # running on a project it otherwise wouldn't have.
    built_projects_only: TOptional[bool] = None
    # the sheriffing tier for this task (default: set based on test platform)
    tier: TOptional[  # type: ignore
        optionally_keyed_by(
            "test-platform",
            "variant",
            "app",
            "subtest",
            Union[int, Literal["default"]],
            use_msgspec=True,
        )
    ] = None
    # number of chunks to create for this task.  This can be keyed by test
    # platform by passing a dictionary in the `by-test-platform` key.  If the
    # test platform is not found, the key 'default' will be tried.
    chunks: optionally_keyed_by(  # type: ignore
        "test-platform", "variant", Union[int, Literal["dynamic"]], use_msgspec=True
    )
    default_chunks: TOptional[  # type: ignore
        optionally_keyed_by("test-platform", "variant", int, use_msgspec=True)
    ] = None
    # Timeout multiplier to apply to default test timeout values. Can be keyed
    # by test platform.
    timeoutfactor: TOptional[  # type: ignore
        optionally_keyed_by("test-platform", Union[int, float], use_msgspec=True)
    ] = None
    # Custom 'test_manifest_loader' to use, overriding the one configured in the
    # parameters. When 'null', no test chunking will be performed. Can also
    # be used to disable "manifest scheduling".
    test_manifest_loader: TOptional[  # type: ignore
        optionally_keyed_by(
            "test-platform",
            TOptional[Union[Literal[tuple(manifest_loaders)]]],  # type: ignore
            use_msgspec=True,
        )
    ] = None
    # the time (with unit) after which this task is deleted; default depends on
    # the branch (see below)
    expires_after: TOptional[str] = None
    # The different configurations that should be run against this task, defined
    # in the TEST_VARIANTS object in the variant.py transforms.
    variants: TOptional[list[str]] = None
    # Whether to run this task without any variants applied.
    run_without_variant: optionally_keyed_by("test-platform", bool, use_msgspec=True)  # type: ignore
    # The EC2 instance size to run these tests on.
    instance_size: optionally_keyed_by(  # type: ignore
        "test-platform",
        "variant",
        Literal[
            "default",
            "large-legacy",
            "large",
            "large-noscratch",
            "xlarge",
            "xlarge-noscratch",
            "highcpu",
        ],
        use_msgspec=True,
    )
    # type of virtualization or hardware required by test.
    virtualization: optionally_keyed_by(  # type: ignore
        "test-platform",
        Literal["virtual", "virtual-with-gpu", "hardware"],
        use_msgspec=True,
    )
    # Whether the task requires loopback audio or video (whatever that may mean
    # on the platform)
    loopback_audio: bool
    loopback_video: bool
    # Whether the test can run using a software GL implementation on Linux
    # using the GL compositor. May not be used with "legacy" sized instances
    # due to poor LLVMPipe performance (bug 1296086).  Defaults to true for
    # unit tests on linux platforms and false otherwise
    allow_software_gl_layers: TOptional[bool] = None
    # For tasks that will run in docker-worker, this is the
    # name of the docker image or in-tree docker image to run the task in.  If
    # in-tree, then a dependency will be created automatically.  This is
    # generally `desktop-test`, or an image that acts an awful lot like it.
    docker_image: optionally_keyed_by(  # type: ignore
        "test-platform",
        Union[str, DockerImageSchema],
        use_msgspec=True,
    )
    # seconds of runtime after which the task will be killed.  Like 'chunks',
    # this can be keyed by test platform, but also variant.
    max_run_time: optionally_keyed_by(  # type: ignore
        "test-platform", "subtest", "variant", "app", int, use_msgspec=True
    )
    # the exit status code that indicates the task should be retried
    retry_exit_status: TOptional[list[int]] = None
    # Whether to perform a gecko checkout.
    checkout: bool
    # Wheter to perform a machine reboot after test is done
    reboot: TOptional[Union[bool, Literal["always", "on-exception", "on-failure"]]] = (
        None
    )
    # What to run
    mozharness: MozharnessSchema
    # The set of test manifests to run.
    test_manifests: TOptional[Union[list[str], TestManifestsSchema]] = None
    # flag to determine if this is a confirm failure task
    confirm_failure: TOptional[bool] = None
    # The current chunk (if chunking is enabled).
    this_chunk: TOptional[int] = None
    # os user groups for test task workers; required scopes, will be
    # added automatically
    os_groups: TOptional[  # type: ignore
        optionally_keyed_by("test-platform", list[str], use_msgspec=True)
    ] = None
    run_as_administrator: TOptional[  # type: ignore
        optionally_keyed_by("test-platform", bool, use_msgspec=True)
    ] = None
    # -- values supplied by the task-generation infrastructure
    # the platform of the build this task is testing
    build_platform: str
    # the label of the build task generating the materials to test
    build_label: str
    # the label of the signing task generating the materials to test.
    # Signed builds are used in xpcshell tests on Windows, for instance.
    build_signing_label: TOptional[  # type: ignore
        optionally_keyed_by("variant", str, use_msgspec=True)
    ] = None
    # the build's attributes
    build_attributes: dict[str, object]
    # the platform on which the tests will run
    test_platform: str
    # limit the test-platforms (as defined in test-platforms.yml)
    # that the test will run on
    limit_platforms: TOptional[  # type: ignore
        optionally_keyed_by("app", "subtest", list[str], use_msgspec=True)
    ] = None
    # the name of the test (the key in tests.yml)
    test_name: str
    # the product name, defaults to firefox
    product: TOptional[str] = None
    # conditional files to determine when these tests should be run
    # Exclusive("when", "optimization") — mutually exclusive with 'optimization' and 'schedules-component'
    when: TOptional[dict[str, list[str]]] = None
    # Optimization to perform on this task during the optimization phase.
    # Optimizations are defined in taskcluster/gecko_taskgraph/optimize.py.
    # Exclusive("optimization", "optimization") — mutually exclusive with 'when' and 'schedules-component'
    optimization: TOptional[OptimizationSchema] = None
    # The SCHEDULES component for this task; this defaults to the suite
    # (not including the flavor) but can be overridden here.
    # Exclusive("schedules-component", "optimization") — mutually exclusive with 'when' and 'optimization'
    schedules_component: TOptional[Union[str, list[str]]] = None
    worker_type: TOptional[  # type: ignore
        optionally_keyed_by(
            "test-platform", "variant", TOptional[str], use_msgspec=True
        )
    ] = None
    # Whether the build being tested requires extensions be signed.
    require_signed_extensions: TOptional[  # type: ignore
        optionally_keyed_by("release-type", "test-platform", bool, use_msgspec=True)
    ] = None
    # The target name, specifying the build artifact to be tested.
    # If None or not specified, a transform sets the target based on OS:
    # target.dmg (Mac), target.apk (Android), target.tar.xz (Linux),
    # or target.zip (Windows).
    target: TOptional[  # type: ignore
        optionally_keyed_by(
            "app",
            "test-platform",
            "variant",
            Union[str, None, TargetIndexSchema, TargetUpstreamTaskSchema],
            use_msgspec=True,
        )
    ] = None
    # Information for indexing this build so its artifacts can be discovered;
    # see the docstring on `IndexSchema` in 'task.py' transforms.
    index: TOptional[JobDescriptionSchema.__annotations__["index"]] = None  # type: ignore
    # A list of artifacts to install from 'fetch' tasks. Validation deferred
    # to 'job' transforms.
    fetches: TOptional[object] = None
    # A list of extra dependencies
    dependencies: TOptional[object] = None
    # Raptor / browsertime specific keys, defer validation to 'raptor.py'
    # transform.
    raptor: TOptional[object] = None
    # Raptor / browsertime specific keys that need to be here since 'raptor' schema
    # is evluated *before* test_description_schema
    app: TOptional[str] = None
    subtest: TOptional[str] = None
    # Define if a given task supports artifact builds or not, see bug 1695325.
    supports_artifact_builds: TOptional[bool] = None
    # Version of python used to run the task
    use_python: TOptional[  # type: ignore
        optionally_keyed_by(
            "test-platform",
            JobDescriptionSchema.__annotations__["use_python"],
            use_msgspec=True,
        )
    ] = None
    # Fetch uv binary and add it to PATH
    use_uv: TOptional[bool] = None
    # Cache mounts / volumes to set up
    use_caches: TOptional[  # type: ignore
        optionally_keyed_by(
            "test-platform",
            RunTaskSchema.__annotations__["use_caches"],
            use_msgspec=True,
        )
    ] = None

    def __post_init__(self):
        super().__post_init__()
        # Exclusive: optimization, when, schedules-component are mutually exclusive
        exclusive_count = sum([
            self.optimization is not None,
            self.when is not None,
            self.schedules_component is not None,
        ])
        if exclusive_count > 1:
            raise ValueError(
                "'optimization', 'when', and 'schedules-component' are mutually exclusive"
            )


@transforms.add
def handle_keyed_by_mozharness(config, tasks):
    """Resolve a mozharness field if it is keyed by something"""
    fields = [
        "mozharness",
        "mozharness.chunked",
        "mozharness.config",
        "mozharness.script",
    ]
    for task in tasks:
        for field in fields:
            resolve_keyed_by(
                task,
                field,
                item_name=task["test-name"],
                enforce_single_match=False,
            )
        yield task


@transforms.add
def set_defaults(config, tasks):
    for task in tasks:
        build_platform = task["build-platform"]
        if build_platform.startswith("android"):
            # all Android test tasks download internal objects from tooltool
            task["mozharness"]["tooltool-downloads"] = "internal"
            task["mozharness"]["actions"] = ["get-secrets"]

            # loopback-video is always true for Android, but false for other
            # platform phyla
            task["loopback-video"] = True
        task["mozharness"]["set-moz-node-path"] = True

        # software-gl-layers is only meaningful on linux unittests, where it defaults to True
        if task["test-platform"].startswith("linux") and task["suite"] not in [
            "talos",
            "raptor",
        ]:
            task.setdefault("allow-software-gl-layers", True)
        else:
            task["allow-software-gl-layers"] = False

        task.setdefault("try-name", task["test-name"])
        task.setdefault("os-groups", [])
        task.setdefault("run-as-administrator", False)
        task.setdefault("chunks", 1)
        task.setdefault("run-on-projects", "built-projects")
        task.setdefault("built-projects-only", False)
        task.setdefault("instance-size", "default")
        task.setdefault("max-run-time", 3600)
        task.setdefault("reboot", False)
        task.setdefault("virtualization", "virtual")
        task.setdefault("loopback-audio", False)
        task.setdefault("loopback-video", False)
        task.setdefault("limit-platforms", [])
        task.setdefault("docker-image", {"in-tree": "ubuntu1804-test"})
        task.setdefault("checkout", False)
        task.setdefault("require-signed-extensions", False)
        task.setdefault("run-without-variant", True)
        task.setdefault("variants", [])
        task.setdefault("supports-artifact-builds", True)
        task.setdefault("use-python", "system")
        task.setdefault("use-uv", True)
        task.setdefault("use-caches", ["checkout", "pip", "uv"])

        task["mozharness"].setdefault("extra-options", [])
        task["mozharness"].setdefault("requires-signed-builds", False)
        task["mozharness"].setdefault("tooltool-downloads", "public")
        task["mozharness"].setdefault("set-moz-node-path", False)
        task["mozharness"].setdefault("chunked", False)
        yield task


transforms.add_validate(TestDescriptionSchema)


@transforms.add
def run_variant_transforms(config, tasks):
    """Variant transforms are run as soon as possible to allow other transforms
    to key by variant."""
    for task in tasks:
        xforms = TransformSequence()
        mod = import_module("gecko_taskgraph.transforms.test.variant")
        xforms.add(mod.transforms)

        yield from xforms(config, [task])


@transforms.add
def resolve_keys(config, tasks):
    keys = (
        "require-signed-extensions",
        "run-without-variant",
        "suite",
        "suite.name",
        "test-manifest-loader",
        "timeoutfactor",
        "use-caches",
        "use-python",
    )
    for task in tasks:
        for key in keys:
            resolve_keyed_by(
                task,
                key,
                item_name=task["test-name"],
                enforce_single_match=False,
                **{
                    "release-type": config.params["release_type"],
                    "variant": task["attributes"].get("unittest_variant"),
                },
            )
        yield task


@transforms.add
def run_remaining_transforms(config, tasks):
    """Runs other transform files next to this module."""
    # List of modules to load transforms from in order.
    transform_modules = (
        ("raptor", lambda t: t["suite"] == "raptor"),
        ("other", None),
        ("worker", None),
        ("confirm_failure", None),
        ("pernosco", lambda t: t["build-platform"].startswith("linux64")),
        ("os_integration", None),
        # These transforms should run last as there is never any difference in
        # configuration from one chunk to another (other than chunk number).
        ("chunk", None),
    )

    for task in tasks:
        xforms = TransformSequence()
        for name, filterfn in transform_modules:
            if filterfn and not filterfn(task):
                continue

            mod = import_module(f"gecko_taskgraph.transforms.test.{name}")
            xforms.add(mod.transforms)

        yield from xforms(config, [task])


@transforms.add
def define_tags(config, tasks):
    for task in tasks:
        tags = task.setdefault("tags", {})
        tags.setdefault("test-suite", task["suite"])
        tags.setdefault("test-platform", task["test-platform"])
        variant = task.get("attributes", {}).get("unittest_variant")
        if variant:
            tags.setdefault("test-variant", variant)

        yield task


# Restrict most perf tests to Ubuntu 24.04, keeping only allowed exceptions on 18.04.
transforms.add(linux_perf_platform_restrictions.restrict_tests_to_2404)
# Apply platform restrictions for tests failing on Ubuntu 24.04.
transforms.add(linux_perf_platform_restrictions.restrict_failing_tests_to_1804)


@transforms.add
def make_job_description(config, tasks):
    """Convert *test* descriptions to *job* descriptions (input to
    gecko_taskgraph.transforms.job)"""

    for task in tasks:
        attributes = task.get("attributes", {})

        mobile = get_mobile_project(task)
        if mobile and (mobile not in task["test-name"]):
            label = "test-{}-{}-{}".format(
                task["test-platform"], mobile, task["test-name"]
            )
        else:
            label = "test-{}-{}".format(task["test-platform"], task["test-name"])

        try_name = task["try-name"]
        variant_suffix = ""
        if attributes.get("unittest_variant"):
            variant_suffix = task.pop("variant-suffix")
            label += variant_suffix
            try_name += variant_suffix

        if task["chunks"] > 1:
            label += "-{}".format(task["this-chunk"])

        if task.get("confirm-failure", False):
            label += "-cf"

        build_label = task["build-label"]

        if task["suite"] == "talos":
            attr_try_name = "talos_try_name"
        elif task["suite"] == "raptor":
            attr_try_name = "raptor_try_name"
        else:
            attr_try_name = "unittest_try_name"

        attr_build_platform, attr_build_type = task["build-platform"].split("/", 1)
        attributes.update({
            "build_platform": attr_build_platform,
            "build_type": attr_build_type,
            "test_platform": task["test-platform"],
            "test_chunk": str(task["this-chunk"]),
            "supports-artifact-builds": task["supports-artifact-builds"],
            attr_try_name: try_name,
        })

        if "test-manifests" in task:
            attributes["test_manifests"] = task["test-manifests"]

        jobdesc = {}
        name = "{}-{}".format(task["test-platform"], task["test-name"])
        jobdesc["name"] = name
        jobdesc["label"] = label
        jobdesc["description"] = task["description"]
        jobdesc["attributes"] = attributes
        jobdesc["dependencies"] = {"build": build_label}
        jobdesc["task-from"] = task["task-from"]

        if task.get("fetches"):
            jobdesc["fetches"] = task["fetches"]

        if task["mozharness"]["requires-signed-builds"] is True:
            jobdesc["dependencies"]["build-signing"] = task["build-signing-label"]

        if "dependencies" in task:
            jobdesc["dependencies"].update(task["dependencies"])

        if "expires-after" in task:
            jobdesc["expires-after"] = task["expires-after"]

        jobdesc["routes"] = task.get("routes", [])
        # The confirm-failure ('-cf') copy of a task is a retrigger helper that
        # never runs on real branches, so it must not claim the shared index.
        if "index" in task and not task.get("confirm-failure"):
            index = dict(task["index"])
            # The same job runs on multiple test platforms, so disambiguate the
            # index path to avoid the per-platform tasks clobbering each other.
            # The test-platform is "<platform>/<build-type>"; '/' isn't allowed
            # in an index name, so join the two with a '-'.
            platform = task["test-platform"].replace("/", "-")
            index["job-name"] = "{}.{}{}".format(
                index["job-name"], platform, variant_suffix
            )
            jobdesc["index"] = index
        jobdesc["run-on-repo-type"] = sorted(task["run-on-repo-type"])
        jobdesc["run-on-projects"] = sorted(task["run-on-projects"])
        jobdesc["scopes"] = []
        jobdesc["tags"] = task.get("tags", {})
        jobdesc["extra"] = {
            "chunks": {
                "current": task["this-chunk"],
                "total": task["chunks"],
            },
            "suite": attributes["unittest_suite"],
            "test-setting": task.pop("test-setting"),
        }
        jobdesc["treeherder"] = {
            "symbol": task["treeherder-symbol"],
            "kind": "test",
            "tier": task["tier"],
            "platform": task.get("treeherder-machine-platform", task["build-platform"]),
        }

        schedules = task.get("schedules-component", [])
        if task.get("when"):
            # This may still be used by comm-central.
            jobdesc["when"] = task["when"]
        elif "optimization" in task:
            jobdesc["optimization"] = task["optimization"]
        elif set(schedules) & set(INCLUSIVE_COMPONENTS):
            jobdesc["optimization"] = {"test-inclusive": schedules}
        else:
            jobdesc["optimization"] = {"test": schedules}

        run = jobdesc["run"] = {}
        run["using"] = "mozharness-test"
        run["clone-with"] = "hg"
        run["test"] = task

        if "workdir" in task:
            run["workdir"] = task.pop("workdir")

        jobdesc["worker-type"] = task.pop("worker-type")

        if "worker" in task:
            jobdesc["worker"] = task.pop("worker")

        if task.get("fetches"):
            jobdesc["fetches"] = task.pop("fetches")

        jobdesc["use-python"] = task.pop("use-python")
        jobdesc["use-uv"] = task.pop("use-uv")

        yield jobdesc


def normpath(path):
    return path.replace("/", "\\")


def get_firefox_version():
    with open("browser/config/version.txt") as f:
        return f.readline().strip()
