# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Templates provide a way of modifying the task definition of selected tasks.
They are added to 'try_task_config.json' and processed by the transforms.
"""

import calendar
import json
import os
import pathlib
import re
import subprocess
import sys
import time
from abc import ABCMeta, abstractmethod
from argparse import SUPPRESS, Action
from textwrap import dedent

import mozpack.path as mozpath
import requests
from mozbuild.base import BuildEnvironmentNotFoundException, MozbuildObject
from mozbuild.util import set_taskcluster_root_url

from . import push
from .tasks import resolve_tests_by_suite
from .util.ssh import get_ssh_user

here = pathlib.Path(__file__).parent
build = MozbuildObject.from_environment(cwd=str(here))

# Set from mach settings in mach_commands.init()
SKIP_ARTIFACT_BUILD_CHECK = False

# Suffixes of files that are never compiled, so a patch touching only files
# with these suffixes can safely be tested with artifact builds.
NON_COMPILED_SUFFIXES = (
    "^headers^",
    ".build",
    ".cjs",
    ".css",
    ".headers",
    ".html",
    ".ico",
    ".js",
    ".json",
    ".jsx",
    ".mjs",
    ".mn",
    ".png",
    ".scss",
    ".sjs",
    ".svg",
    ".toml",
    ".ts",
    ".tsx",
    ".xhtml",
)

# `./mach try` is often left unattended, so the artifact build prompt below
# only blocks for this long before falling back to not using artifact builds.
ARTIFACT_PROMPT_TIMEOUT = 30


def prompt_with_timeout(message, timeout):
    """Ask a question on stdin, waiting at most `timeout` seconds for an answer.

    Returns the answer, or None if stdin isn't interactive or nothing was
    entered in time. Callers should then fall back to a default, so that
    scripted and unattended pushes are never blocked on an answer.
    """
    if not sys.stdin or not sys.stdin.isatty():
        return None

    print(message, end="", flush=True)

    if sys.platform == "win32":
        import msvcrt

        deadline = time.monotonic() + timeout
        while not msvcrt.kbhit():
            if time.monotonic() > deadline:
                print()
                return None
            time.sleep(0.05)
    else:
        # `select` can wait on stdin on POSIX platforms, but on Windows it only
        # supports sockets, hence the `msvcrt` polling above.
        import select

        if not select.select([sys.stdin], [], [], timeout)[0]:
            print()
            return None

    return sys.stdin.readline().strip()


class ParameterConfig:
    __metaclass__ = ABCMeta

    def __init__(self):
        self.dests = set()

    def add_arguments(self, parser):
        for cli, kwargs in self.arguments:
            action = parser.add_argument(*cli, **kwargs)
            self.dests.add(action.dest)

    @property
    @abstractmethod
    def arguments(self):
        pass

    @abstractmethod
    def get_parameters(self, **kwargs) -> dict:
        pass

    def validate(self, **kwargs):
        pass


class TargetTasksMethod(ParameterConfig):
    arguments = [
        [
            ["--target-tasks-method"],
            {"help": "Custom target tasks method to use."},
        ],
    ]

    def get_parameters(self, target_tasks_method: str, **kwargs):
        if target_tasks_method:
            return {"target_tasks_method": target_tasks_method}


class PushDate(ParameterConfig):
    arguments = [
        [
            ["--pushdate"],
            {
                "default": None,
                "help": "Override the build date (format: YYYYMMDDHHMMSS) used for this try push.",
            },
        ],
    ]

    def get_parameters(self, pushdate: str, **kwargs):
        if pushdate is not None:
            try:
                build_date = int(
                    calendar.timegm(time.strptime(pushdate, "%Y%m%d%H%M%S"))
                )
            except ValueError:
                print(
                    f"error: --pushdate must be in YYYYMMDDHHMMSS format, got: {pushdate}",
                    file=sys.stderr,
                )
                sys.exit(1)
            return {
                "build_date": build_date,
                "moz_build_date": pushdate,
                "pushdate": build_date,
            }


class TryConfig(ParameterConfig):
    @abstractmethod
    def try_config(self, **kwargs) -> dict:
        pass

    def get_parameters(self, **kwargs):
        result = self.try_config(**kwargs)
        if not result:
            return None
        return {"try_task_config": result}


class Artifact(TryConfig):
    arguments = [
        [
            ["--artifact"],
            {"action": "store_true", "help": "Force artifact builds where possible."},
        ],
        [
            ["--no-artifact"],
            {
                "action": "store_true",
                "help": "Disable artifact builds even if being used locally.",
            },
        ],
    ]

    def add_arguments(self, parser):
        group = parser.add_mutually_exclusive_group()
        return super().add_arguments(group)

    @classmethod
    def is_artifact_build(cls):
        try:
            return build.substs.get("MOZ_ARTIFACT_BUILDS", False)
        except BuildEnvironmentNotFoundException:
            return False

    @classmethod
    def is_non_compiled_patch(cls):
        try:
            changed_files = list(push.vcs.get_outgoing_files())
        except Exception:
            return False

        return bool(changed_files) and all(
            f.endswith(NON_COMPILED_SUFFIXES) for f in changed_files
        )

    def try_config(self, artifact, no_artifact, **kwargs):
        if artifact:
            return {"use-artifact-builds": True, "disable-pgo": True}

        if no_artifact:
            return

        # If 'try.noartifact' is set in mach settings, default to non-artifact
        # try instead of checking current build environment.
        if SKIP_ARTIFACT_BUILD_CHECK:
            return

        if self.is_artifact_build():
            print("Artifact builds enabled, pass --no-artifact to disable")
            return {"use-artifact-builds": True, "disable-pgo": True}

        if self.is_non_compiled_patch():
            while True:
                answer = prompt_with_timeout(
                    "This patch only touches files that don't require a "
                    "compiled build (JS/TS/TOML/...). Use artifact builds? "
                    f"[Y/n] (no answer within {ARTIFACT_PROMPT_TIMEOUT}s "
                    "means 'n'): ",
                    ARTIFACT_PROMPT_TIMEOUT,
                )
                if answer is None:
                    print("No answer, not using artifact builds.")
                    break
                answer = answer.lower()
                if answer in ("", "y", "yes"):
                    return {"use-artifact-builds": True, "disable-pgo": True}
                if answer in ("n", "no"):
                    break
                print(f"Invalid answer: '{answer}'")


class Pernosco(TryConfig):
    arguments = [
        [
            ["--pernosco"],
            {
                "action": "store_true",
                "default": None,
                "help": "Opt-in to analysis by the Pernosco debugging service.",
            },
        ],
        [
            ["--no-pernosco"],
            {
                "dest": "pernosco",
                "action": "store_false",
                "default": None,
                "help": "Opt-out of the Pernosco debugging service (if you are on the include list).",
            },
        ],
    ]

    def add_arguments(self, parser):
        group = parser.add_mutually_exclusive_group()
        return super().add_arguments(group)

    def try_config(self, pernosco, **kwargs):
        pernosco = pernosco or os.environ.get("MOZ_USE_PERNOSCO")
        if pernosco is None:
            return

        if pernosco:
            try:
                # The Pernosco service currently requires a Mozilla e-mail address to
                # log in. Prevent people with non-Mozilla addresses from using this
                # flag so they don't end up consuming time and resources only to
                # realize they can't actually log in and see the reports.
                address = get_ssh_user()
                if not address.endswith("@mozilla.com"):
                    print(
                        dedent(
                            f"""\
                        Pernosco requires a Mozilla e-mail address to view its reports. Please
                        push to try with an @mozilla.com address to use --pernosco.

                            Current user: {address}
                    """
                        )
                    )
                    sys.exit(1)

            except (subprocess.CalledProcessError, IndexError):
                print("warning: failed to detect current user for 'hg.mozilla.org'")
                print("Pernosco requires a Mozilla e-mail address to view its reports.")
                while True:
                    answer = input(
                        "Do you have an @mozilla.com address? [Y/n]: "
                    ).lower()
                    if answer == "n":
                        sys.exit(1)
                    elif answer == "y":
                        break

        return {
            "pernosco": True,
            # TODO Bug 1907076: Remove the env below once Pernosco consumers
            # are using the `pernosco-v1` task routes.
            "env": {
                "PERNOSCO": str(int(pernosco)),
            },
        }

    def validate(self, **kwargs):
        try_config = kwargs["try_config_params"].get("try_task_config") or {}
        if try_config.get("use-artifact-builds"):
            print(
                "Pernosco does not support artifact builds at this time. "
                "Please try again with '--no-artifact'."
            )
            sys.exit(1)


class Path(TryConfig):
    arguments = [
        [
            ["paths"],
            {
                "nargs": "*",
                "default": [],
                "help": "Run tasks containing tests under the specified path(s).",
            },
        ],
        [
            ["--allow-testfile-path"],
            {
                "dest": "allow_testfile_path",
                "action": "store_true",
                "default": None,
                "help": "Opt in to pass a specific testfile path (ie not only a folder)",
            },
        ],
    ]

    def try_config(self, paths, allow_testfile_path, **kwargs):
        if not paths:
            return

        for i, p in enumerate(paths):
            if not os.path.exists(p):
                print(f"error: '{p}' is not a valid path.", file=sys.stderr)
                sys.exit(1)

            # Passing paths to specific tests doesn't work with the Treeherder
            # test path filter or test-verify. Re-write it to the containing
            # directory to avoid confusion.
            if os.path.isfile(p) and not allow_testfile_path:
                parent = os.path.dirname(p)
                print(
                    f"warning: paths to individual tests may not work, re-writing to {parent}. Pass --allow-testfile-path to override"
                )
                paths[i] = parent

            paths[i] = mozpath.relpath(
                mozpath.join(os.getcwd(), paths[i]), build.topsrcdir
            )

        return {
            "env": {
                "MOZHARNESS_TEST_PATHS": json.dumps(resolve_tests_by_suite(paths)),
            }
        }


class Tag(TryConfig):
    arguments = [
        [
            ["--tag"],
            {
                "action": "append",
                "default": [],
                "help": "Run tests matching the specified tag.",
            },
        ],
    ]

    def try_config(self, tag, **kwargs):
        if not tag:
            return

        return {
            "env": {
                "MOZHARNESS_TEST_TAG": json.dumps(tag),
            }
        }


class Environment(TryConfig):
    arguments = [
        [
            ["--env"],
            {
                "action": "append",
                "default": None,
                "help": "Set an environment variable, of the form FOO=BAR. "
                "Can be passed in multiple times.",
            },
        ],
        [
            ["--record"],
            {
                "action": "store_true",
                "help": "Get a screen recording of the tests where possible.",
            },
        ],
        [
            ["--profiler"],
            {
                "action": "store_true",
                "help": "Enable the profiler with the normal feature set, "
                "overriding the low-overhead defaults used by the test harness.",
            },
        ],
    ]

    def try_config(self, env, record, profiler, **kwargs):
        if env is None:
            env = []
        if record:
            env.append("MOZ_RECORD_TEST=1")
        if profiler:
            env.append("MOZ_PROFILER_STARTUP=1")
            # Override the low-overhead feature set and sampling interval
            # that the test harness enables by default, so pushes with
            # --profiler get a normally configured profiler.
            env.append("MOZ_PROFILER_STARTUP_FEATURES=default")
            env.append("MOZ_PROFILER_STARTUP_INTERVAL=1")
        if not env:
            return
        return {
            "env": dict(e.split("=", 1) for e in env),
        }


class Extensions(TryConfig):
    AMO_API_ADDON_URL = "https://addons.mozilla.org/api/v5/addons/addon/{addon_id}/"

    arguments = [
        [
            ["--extension"],
            {
                "action": "append",
                "default": [],
                "dest": "extensions",
                "metavar": "ADDON_ID",
                "help": "Install an AMO webextension (by addon GUID/slug) into the "
                "test profile of raptor/browsertime and mozperftest tasks. May be "
                "specified multiple times. Has no effect on other tasks.",
            },
        ],
    ]

    def resolve_amo_addon(self, addon_id):
        """Resolve an AMO addon GUID/slug to its current pinned .xpi download URL.

        Resolving at submit time fails fast on a bad id and pins the exact version
        that the push will install.
        """
        api_url = self.AMO_API_ADDON_URL.format(addon_id=addon_id)
        try:
            resp = requests.get(api_url, headers={"User-Agent": "mach-try"}, timeout=30)
            resp.raise_for_status()
            data = resp.json()
        except Exception as e:
            raise Exception(
                f"Could not resolve addon {addon_id!r} from AMO ({api_url}): {e}"
            )

        current_version = data.get("current_version") or {}
        # AMO API v5 exposes a single `file`; older shapes used a `files` list.
        addon_file = current_version.get("file")
        if addon_file is None:
            files = current_version.get("files") or []
            addon_file = files[0] if files else None

        if not addon_file or not addon_file.get("url"):
            raise Exception(f"No downloadable file found for addon {addon_id!r} on AMO")

        return addon_file["url"]

    def try_config(self, extensions, **kwargs):
        if not extensions:
            return

        urls = []
        for entry in extensions:
            if entry.startswith(("http://", "https://")):
                try:
                    requests.head(
                        entry, allow_redirects=True, timeout=30
                    ).raise_for_status()
                except Exception as e:
                    raise Exception(f"Could not reach extension URL {entry!r}: {e}")
                urls.append(entry)
            elif entry.endswith(".xpi") or "/" in entry or "\\" in entry:
                raise Exception(
                    f"--extension does not accept local paths: {entry!r}. "
                    f"Use an AMO addon GUID/slug or a .xpi URL."
                )
            else:
                urls.append(self.resolve_amo_addon(entry))

        return {
            "env": {
                "PERF_FLAGS": "install-extension=" + ",".join(urls),
            },
        }


class ExistingTasks(ParameterConfig):
    TREEHERDER_PUSH_ENDPOINT = (
        "https://treeherder.mozilla.org/api/project/try/push/?count=1&author={user}"
    )
    TREEHERDER_PUSH_URL = (
        "https://treeherder.mozilla.org/jobs?repo={branch}&revision={revision}"
    )

    arguments = [
        [
            ["-E", "--use-existing-tasks"],
            {
                "const": "last_try_push",
                "default": None,
                "nargs": "?",
                "help": """
                    Use existing tasks from a previous push. Without args this
                uses your most recent try push. You may also specify
                `rev=<revision>` where <revision> is the head revision of the
                try push, `project=<project>` to use the latest push to
                <project> (e.g. mozilla-central), or `task-id=<task id>` where
                <task id> is the Decision task id of the push. These last two
                methods even work for non-try branches.
                """,
            },
        ],
        [
            ["--use-indexed-tasks"],
            {
                "action": "append",
                "dest": "use_indexed_tasks",
                "default": [],
                "metavar": "LABEL=INDEX_PATH",
                "help": """
                    Use the task at INDEX_PATH in place of the task LABEL, e.g.
                the latest signed mozilla-central build. Only completed tasks
                are indexed. Unlike --use-existing-tasks, which reuses every
                task of a push, this only replaces LABEL. Can be specified
                multiple times.
                """,
            },
        ],
        [
            ["--existing-tasks-include"],
            {
                "action": "append",
                "dest": "existing_tasks_include",
                "default": [],
                "metavar": "REGEX",
                "help": """
                    Only reuse tasks from --use-existing-tasks whose label
                matches REGEX, e.g. '^(docker-image|fetch|toolchain)-'. Can be
                specified multiple times.
                """,
            },
        ],
        [
            ["--existing-tasks-exclude"],
            {
                "action": "append",
                "dest": "existing_tasks_exclude",
                "default": [],
                "metavar": "REGEX",
                "help": """
                    Don't reuse tasks from --use-existing-tasks whose label
                matches REGEX. Can be specified multiple times.
                """,
            },
        ],
    ]

    def find_decision_task(self, use_existing_tasks):
        from taskgraph.util import taskcluster

        set_taskcluster_root_url()

        branch = "try"
        if use_existing_tasks == "last_try_push":
            # Use existing tasks from user's previous try push.
            user = get_ssh_user()
            url = self.TREEHERDER_PUSH_ENDPOINT.format(user=user)
            res = requests.get(url, headers={"User-Agent": "gecko-mach-try/1.0"})
            res.raise_for_status()
            data = res.json()
            if data["meta"]["count"] == 0:
                raise Exception(f"Could not find a try push for '{user}'!")
            revision = data["results"][0]["revision"]

        elif use_existing_tasks.startswith("rev="):
            revision = use_existing_tasks[len("rev=") :]

        elif use_existing_tasks.startswith("project="):
            project = use_existing_tasks[len("project=") :]
            index_path = f"gecko.v2.{project}.latest.taskgraph.decision"
            task_id = taskcluster.find_task_id(index_path)
            print(
                f"Using existing tasks from the latest {project} push "
                f"(decision task {task_id})"
            )
            return task_id

        else:
            raise Exception(f"Unable to parse '{use_existing_tasks}'!")

        url = self.TREEHERDER_PUSH_URL.format(branch=branch, revision=revision)
        print(f"Using existing tasks from: {url}")
        index_path = f"gecko.v2.{branch}.revision.{revision}.taskgraph.decision"
        return taskcluster.find_task_id(index_path)

    def get_parameters(
        self,
        use_existing_tasks,
        use_indexed_tasks,
        existing_tasks_include=(),
        existing_tasks_exclude=(),
        **kwargs,
    ):
        if (existing_tasks_include or existing_tasks_exclude) and not (
            use_existing_tasks
        ):
            print(
                "error: --existing-tasks-include and --existing-tasks-exclude "
                "require --use-existing-tasks",
                file=sys.stderr,
            )
            sys.exit(1)

        if not use_existing_tasks and not use_indexed_tasks:
            return

        from taskgraph.util import taskcluster

        set_taskcluster_root_url()

        existing_tasks = {}
        if use_existing_tasks:
            if use_existing_tasks.startswith("task-id="):
                decision_task_id = use_existing_tasks[len("task-id=") :]
            else:
                decision_task_id = self.find_decision_task(use_existing_tasks)

            existing_tasks = taskcluster.get_artifact(
                decision_task_id, "public/label-to-taskid.json"
            )
            include = [re.compile(r) for r in existing_tasks_include]
            exclude = [re.compile(r) for r in existing_tasks_exclude]
            existing_tasks = {
                label: task_id
                for label, task_id in existing_tasks.items()
                if (not include or any(r.search(label) for r in include))
                and not any(r.search(label) for r in exclude)
            }

        for label_and_index in use_indexed_tasks:
            label, sep, index_path = label_and_index.partition("=")
            if not sep or not label or not index_path:
                print(
                    "error: --use-indexed-tasks must be LABEL=INDEX_PATH, "
                    f"got '{label_and_index}'",
                    file=sys.stderr,
                )
                sys.exit(1)
            task_id = taskcluster.find_task_id(index_path)
            print(f"Using indexed task {task_id} for {label} (via {index_path})")
            existing_tasks[label] = task_id

        return {"existing_tasks": existing_tasks}


class RangeAction(Action):
    def __init__(self, min, max, *args, **kwargs):
        self.min = min
        self.max = max
        kwargs["metavar"] = f"[{self.min}-{self.max}]"
        super().__init__(*args, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        name = option_string or self.dest
        if values < self.min:
            parser.error(f"{name} can not be less than {self.min}")
        if values > self.max:
            parser.error(f"{name} can not be more than {self.max}")
        setattr(namespace, self.dest, values)


class Rebuild(TryConfig):
    arguments = [
        [
            ["--rebuild"],
            {
                "action": RangeAction,
                "min": 1,
                "max": 20,
                "default": None,
                "type": int,
                "help": "Rebuild all selected tasks the specified number of times.",
            },
        ],
    ]

    def try_config(self, rebuild, **kwargs):
        if not rebuild:
            return

        if not kwargs.get("new_test_config", False):
            if rebuild == 1:
                print(
                    "warning: setting --rebuild to 1 is the same as not specifying it."
                )
            elif kwargs.get("full") and rebuild > 3:
                print(
                    "warning: limiting --rebuild to 3 when using --full. "
                    "Use custom push actions to add more."
                )
                rebuild = 3

        return {
            "rebuild": rebuild,
        }


class Routes(TryConfig):
    arguments = [
        [
            ["--route"],
            {
                "action": "append",
                "dest": "routes",
                "help": (
                    "Additional route to add to the tasks "
                    "(note: these will not be added to the decision task)"
                ),
            },
        ],
    ]

    def try_config(self, routes, **kwargs):
        if routes:
            return {
                "routes": routes,
            }


class ChemspillPrio(TryConfig):
    arguments = [
        [
            ["--chemspill", "--chemspill-priority"],
            {
                "action": "store_true",
                "dest": "chemspill",
                "help": "Run at a higher priority than most try jobs (chemspills only).",
            },
        ],
    ]

    def try_config(self, chemspill, **kwargs):
        if chemspill:
            # Despite being "low", this is still higher than other tasks on
            # try, the equivalent of a push to autoland.
            return {"priority": "low"}


class GeckoProfile(TryConfig):
    arguments = [
        [
            ["--gecko-profile"],
            {
                "dest": "profile",
                "action": "store_true",
                "default": False,
                "help": (
                    "Create and upload a gecko profile during talos/raptor tasks. "
                    "Copy paste the parameters used in this profiling run directly from about:profiling in Nightly."
                ),
            },
        ],
        [
            ["--gecko-profile-interval"],
            {
                "dest": "gecko_profile_interval",
                "type": float,
                "help": "How frequently to take samples (ms)",
            },
        ],
        [
            ["--gecko-profile-entries"],
            {
                "dest": "gecko_profile_entries",
                "type": int,
                "help": "How many samples to take with the profiler",
            },
        ],
        [
            ["--gecko-profile-features"],
            {
                "dest": "gecko_profile_features",
                "type": str,
                "default": None,
                "help": "Set the features enabled for the profiler.",
            },
        ],
        [
            ["--gecko-profile-threads"],
            {
                "dest": "gecko_profile_threads",
                "type": str,
                "help": "Comma-separated list of threads to sample.",
            },
        ],
        # For backwards compatibility
        [
            ["--talos-profile"],
            {
                "dest": "profile",
                "action": "store_true",
                "default": False,
                "help": SUPPRESS,
            },
        ],
        # This is added for consistency with the old 'syntax' selector
        [
            ["--geckoProfile"],
            {
                "dest": "profile",
                "action": "store_true",
                "default": False,
                "help": SUPPRESS,
            },
        ],
    ]

    def try_config(
        self,
        profile,
        gecko_profile_interval,
        gecko_profile_entries,
        gecko_profile_features,
        gecko_profile_threads,
        **kwargs,
    ):
        if profile or not all(
            s is None for s in (gecko_profile_features, gecko_profile_threads)
        ):
            cfg = {
                "gecko-profile": True,
                "gecko-profile-interval": gecko_profile_interval,
                "gecko-profile-entries": gecko_profile_entries,
                "gecko-profile-features": gecko_profile_features,
                "gecko-profile-threads": gecko_profile_threads,
            }
            return {key: value for key, value in cfg.items() if value is not None}


class Browsertime(TryConfig):
    arguments = [
        [
            ["--browsertime"],
            {
                "action": "store_true",
                "help": "Use browsertime during Raptor tasks.",
            },
        ],
    ]

    def try_config(self, browsertime, **kwargs):
        if browsertime:
            return {
                "browsertime": True,
            }


class DisablePgo(TryConfig):
    arguments = [
        [
            ["--disable-pgo"],
            {
                "action": "store_true",
                "help": "Don't run PGO builds",
            },
        ],
    ]

    def try_config(self, disable_pgo, **kwargs):
        if disable_pgo:
            return {
                "disable-pgo": True,
            }


class NewConfig(TryConfig):
    arguments = [
        [
            ["--new-test-config"],
            {
                "action": "store_true",
                "help": "When a test fails (mochitest only) restart the browser and start from the next test",
            },
        ],
    ]

    def try_config(self, new_test_config, **kwargs):
        if new_test_config:
            return {
                "new-test-config": True,
            }


class DoNotOptimize(ParameterConfig):
    arguments = [
        [
            ["--do-not-optimize"],
            {
                "action": "append",
                "dest": "do_not_optimize",
                "default": None,
                "help": (
                    "Task labels to not optimize. These tasks will always be built "
                    "instead of being replaced by indexed tasks. Can be specified multiple times."
                ),
            },
        ],
    ]

    def get_parameters(self, do_not_optimize, **kwargs):
        if do_not_optimize:
            return {"do_not_optimize": do_not_optimize}


class BuildCar(ParameterConfig):
    arguments = [
        [
            ["--build-car"],
            {
                "action": "store_true",
                "help": "Force rebuild of custom-car toolchains instead of reusing mozilla-central artifacts.",
            },
        ],
    ]

    CUSTOM_CAR_LABELS = [
        "toolchain-linux64-custom-car",
        "toolchain-win64-custom-car",
        "toolchain-macosx64-aarch64-custom-car",
        "toolchain-android-custom-car",
    ]

    def get_parameters(self, build_car, **kwargs):
        if build_car:
            return {"do_not_optimize": self.CUSTOM_CAR_LABELS}


class WorkerOverrides(TryConfig):
    arguments = [
        [
            ["--worker-override"],
            {
                "action": "append",
                "dest": "worker_overrides",
                "help": (
                    "Override the worker pool used for a given taskgraph worker alias. "
                    "The argument should be `<alias>=<worker-pool>`. "
                    "Can be specified multiple times."
                ),
            },
        ],
        [
            ["--worker-suffix"],
            {
                "action": "append",
                "dest": "worker_suffixes",
                "help": (
                    "Override the worker pool used for a given taskgraph worker alias, "
                    "by appending a suffix to the work-pool. "
                    "The argument should be `<alias>=<suffix>`. "
                    "Can be specified multiple times."
                ),
            },
        ],
        [
            ["--worker-type"],
            {
                "action": "append",
                "dest": "worker_types",
                "default": [],
                "help": "Select tasks that only run on the specified worker.",
            },
        ],
    ]

    def try_config(self, worker_overrides, worker_suffixes, worker_types, **kwargs):
        from gecko_taskgraph.util.workertypes import get_worker_type
        from taskgraph.config import load_graph_config

        root = build.topsrcdir
        root = os.path.join(root, "taskcluster")
        graph_config = load_graph_config(root)

        overrides = {}
        if worker_overrides:
            for override in worker_overrides:
                alias, worker_pool = override.split("=", 1)
                if alias in overrides:
                    print(
                        f"Can't override worker alias {alias} more than once. "
                        f"Already set to use {overrides[alias]}, but also asked to use {worker_pool}."
                    )
                    sys.exit(1)
                overrides[alias] = worker_pool

                try:
                    provisioner, worker_type = get_worker_type(
                        graph_config, worker_type=alias, parameters={"level": "1"}
                    )
                except KeyError:
                    print(
                        f"Invalid worker type {alias}, use a value that matches below (limited to b-*, t-*, win*):"
                    )
                    root_alias = alias.strip("gecko-")
                    root_alias = root_alias.strip("t-")
                    root_alias = root_alias.split("-")[0]
                    possible_matches = []
                    for item in graph_config["workers"]["aliases"]:
                        if root_alias in item:
                            possible_matches.append(item)
                        if (
                            item.startswith("t-")
                            or item.startswith("b-")
                            or item.startswith("win")
                        ):
                            print(f"{item}")

                    print("")
                    if len(possible_matches) == 1:
                        print(f"did you mean: {possible_matches[0]}")
                    elif len(possible_matches) > 1:
                        print(f"did you mean one of these {possible_matches}")
                    sys.exit(1)

        if worker_suffixes:
            for worker_suffix in worker_suffixes:
                alias, suffix = worker_suffix.split("=", 1)
                if alias in overrides:
                    print(
                        f"Can't override worker alias {alias} more than once. "
                        f"Already set to use {overrides[alias]}, but also asked "
                        f"to add suffix {suffix}."
                    )
                    sys.exit(1)
                provisioner, worker_type = get_worker_type(
                    graph_config, worker_type=alias, parameters={"level": "1"}
                )
                overrides[alias] = f"{provisioner}/{worker_type}{suffix}"

        retVal = {}
        if worker_types:
            retVal["worker-types"] = list(overrides.keys()) + worker_types

        if overrides:
            retVal["worker-overrides"] = overrides
        return retVal


class RepeatTests(TryConfig):
    arguments = [
        [
            ["--repeat-tests"],
            {
                "action": RangeAction,
                "min": 1,
                "max": 20,
                "default": None,
                "type": int,
                "help": "Run each test the specified number of times in each task "
                "(mochitest, reftest, crashtest, xpcshell and web-platform-tests only).",
            },
        ],
    ]

    def try_config(self, repeat_tests, **kwargs):
        if not repeat_tests:
            return

        if repeat_tests == 1:
            print(
                "warning: setting --repeat-tests to 1 is the same as not specifying it."
            )

        return {
            "repeat-tests": repeat_tests,
        }


all_task_configs = {
    "artifact": Artifact,
    "browsertime": Browsertime,
    "build-car": BuildCar,
    "chemspill-prio": ChemspillPrio,
    "disable-pgo": DisablePgo,
    "do-not-optimize": DoNotOptimize,
    "env": Environment,
    "existing-tasks": ExistingTasks,
    "extensions": Extensions,
    "gecko-profile": GeckoProfile,
    "new-test-config": NewConfig,
    "path": Path,
    "pushdate": PushDate,
    "test-tag": Tag,
    "pernosco": Pernosco,
    "rebuild": Rebuild,
    "repeat-tests": RepeatTests,
    "routes": Routes,
    "target-tasks-method": TargetTasksMethod,
    "worker-overrides": WorkerOverrides,
}
