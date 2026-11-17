# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

import yaml
from android_taskgraph import register as register_android_taskgraph
from mozilla_taskgraph import register as register_mozilla_taskgraph
from mozsystemmonitor.resourcemonitor import SystemResourceMonitor
from taskgraph import config as taskgraph_config
from taskgraph import generator
from taskgraph import morph as taskgraph_morph
from taskgraph.transforms.task import payload_builders
from taskgraph.util import docker, schema
from taskgraph.util import taskcluster as tc_util
from taskgraph.util.readonlydict import ReadOnlyDict

from gecko_taskgraph.config import GraphConfigSchema

GECKO = os.path.normpath(os.path.realpath(os.path.join(__file__, "..", "..", "..")))
TEST_CONFIGS = os.path.join(GECKO, "taskcluster", "test_configs")

# Overwrite Taskgraph's default graph_config_schema with a custom one.
taskgraph_config.graph_config_schema = GraphConfigSchema


def _represent_ro_dict(dumper, data):
    return dumper.represent_dict(dict(data))


# Teach the default SafeDumper how to serialize ReadOnlyDict. Registering here
# (rather than on a one-off Dumper subclass) ensures the representer is present
# in worker processes spawned by ProcessPoolExecutor, which on macOS use the
# "spawn" start method and therefore do not inherit registrations made in the
# parent process.
yaml.SafeDumper.add_representer(ReadOnlyDict, _represent_ro_dict)

# Overwrite Taskgraph's RUN_TASK_SNIPPET to place the binaries in Gecko
# specific locations.
docker.RUN_TASK_FILES = {
    f"run-task/{path}": os.path.join(docker.RUN_TASK_ROOT, path)
    for path in [
        "run-task",
        "fetch-content",
    ]
}
docker.RUN_TASK_FILES["run-task/run-task-setup.py"] = os.path.join(
    GECKO, "taskcluster", "scripts", "run-task-setup.py"
)
docker.RUN_TASK_FILES["run-task/run_task_resource_monitor.py"] = os.path.join(
    GECKO, "taskcluster", "scripts", "run_task_resource_monitor.py"
)
docker.RUN_TASK_FILES["run-task/basemonitor.py"] = os.path.join(
    GECKO,
    "testing",
    "mozbase",
    "mozsystemmonitor",
    "mozsystemmonitor",
    "basemonitor.py",
)
docker.RUN_TASK_SNIPPET = [
    "COPY run-task/run-task /builds/worker/bin/run-task-git\n",
    "COPY run-task/fetch-content /builds/worker/bin/fetch-content\n",
    "COPY run-task/run-task-setup.py /builds/worker/bin/run-task-setup.py\n",
    "COPY run-task/run_task_resource_monitor.py /builds/worker/bin/run_task_resource_monitor.py\n",
    "COPY run-task/basemonitor.py /builds/worker/bin/mozsystemmonitor/basemonitor.py\n",
]

# Don't use any of the upstream morphs.
# TODO Investigate merging our morphs with upstream.
taskgraph_morph.registered_morphs = []

# Default rootUrl to use if none is given in the environment; this should point
# to the production Taskcluster deployment used for CI.
tc_util.PRODUCTION_TASKCLUSTER_ROOT_URL = "https://firefox-ci-tc.services.mozilla.com"

# Schemas for YAML files should use dashed identifiers by default. If there are
# components of the schema for which there is a good reason to use another format,
# exceptions can be added here.
schema.EXCEPTED_SCHEMA_IDENTIFIERS.extend([
    "test_name",
    "json_location",
    "video_location",
    "profile_name",
    "target_path",
    "try_task_config",
])

# TODO: These are temporarily redefined in gecko_taskgraph. Remove them from
# upstream until they can be consolidated.
del payload_builders["beetmover"]
del payload_builders["docker-worker"]
del payload_builders["generic-worker"]


def record_phase_marker(name, start, end):
    """Record a taskgraph generation phase in the resource usage profile. The
    name of a phase that raised is None."""
    SystemResourceMonitor.record_marker(
        "Taskgraph", start, end, {"type": "Text", "text": name or "failed phase"}
    )


def register(graph_config):
    """Used to register Gecko specific extensions.

    Args:
        graph_config: The graph configuration object.
    """
    from gecko_taskgraph import (  # noqa
        filter_tasks,
        morph,
        target_tasks,  # trigger target task method registration
    )
    from gecko_taskgraph.parameters import register_parameters
    from gecko_taskgraph import task_context  # noqa - trigger custom task-context registration
    from gecko_taskgraph.util import (
        dependencies,  # noqa - trigger group_by registration
    )
    from gecko_taskgraph.kind import ProfiledKind
    from gecko_taskgraph.util.verify import profiled_verifications

    register_mozilla_taskgraph(graph_config)
    register_android_taskgraph(graph_config)

    # Don't use the upstream verifications, and replace them with our own.
    # TODO Investigate merging our verifications with upstream.
    generator.verifications = profiled_verifications

    # Workers loading kinds in parallel find ProfiledKind.load_tasks through
    # the kind instances they are sent, even when not forked.
    generator.Kind = ProfiledKind

    generator.phase_finished = record_phase_marker

    register_parameters()
