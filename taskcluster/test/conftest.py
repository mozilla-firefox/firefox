# Any copyright is dedicated to the public domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import json
import logging
import os

import pytest
from gecko_taskgraph.util.bugbug import BUGBUG_BASE_URL
from responses import RequestsMock
from responses import logger as rsps_logger
from taskgraph.generator import TaskGraphGenerator
from taskgraph.parameters import parameters_loader

here = os.path.abspath(os.path.dirname(__file__))
ROOT_URL = "https://firefox-ci-tc.services.mozilla.com"


@pytest.fixture(scope="session")
def responses():
    rsps_logger.setLevel(logging.WARNING)
    with RequestsMock(assert_all_requests_are_fired=False) as rsps:
        yield rsps


@pytest.fixture(scope="session")
def datadir():
    return os.path.join(here, "data")


@pytest.fixture(scope="session")
def create_tgg(responses, datadir):
    def inner(parameters=None, overrides=None):
        params = parameters_loader(parameters, strict=False, overrides=overrides)
        tgg = TaskGraphGenerator(None, params)

        # Mock out certain requests as they may depend on a revision that does
        # not exist on hg.mozilla.org.
        mock_requests = {}

        # bugbug /push/schedules
        url = BUGBUG_BASE_URL + "/push/{project}/{head_rev}/schedules".format(
            **tgg.parameters
        )
        mock_requests[url] = "bugbug-push-schedules.json"

        for url, filename in mock_requests.items():
            with open(os.path.join(datadir, filename)) as fh:
                responses.add(
                    responses.GET,
                    url,
                    json=json.load(fh),
                    status=200,
                )

        # Still allow other real requests.
        responses.add_passthru("https://hg.mozilla.org")
        responses.add_passthru("https://firefoxci.taskcluster-artifacts.net")
        responses.add_passthru("https://product-details.mozilla.org")
        responses.add_passthru(ROOT_URL)
        if proxy_url := os.environ.get("TASKCLUSTER_PROXY_URL"):
            responses.add_passthru(proxy_url)
        return tgg

    return inner


@pytest.fixture(scope="module")
def tgg(request, create_tgg):
    if not hasattr(request.module, "PARAMS"):
        pytest.fail("'tgg' fixture requires a module-level 'PARAMS' variable")

    tgg = create_tgg(overrides=request.module.PARAMS)
    return tgg


@pytest.fixture(scope="module")
def tgg_new_config(request, create_tgg):
    if not hasattr(request.module, "PARAMS_NEW_CONFIG"):
        pytest.fail(
            "'tgg_new_config' fixture requires a module-level 'PARAMS' variable"
        )

    tgg = create_tgg(overrides=request.module.PARAMS_NEW_CONFIG)
    return tgg


@pytest.fixture(scope="module")
def params(tgg):
    return tgg.parameters


@pytest.fixture(scope="module")
def full_task_graph(tgg):
    return tgg.full_task_graph


@pytest.fixture(scope="module")
def optimized_task_graph(full_task_graph, tgg):
    return tgg.optimized_task_graph


@pytest.fixture(scope="module")
def full_task_graph_new_config(tgg_new_config):
    return tgg_new_config.full_task_graph


@pytest.fixture(scope="module")
def optimized_task_graph_new_config(full_task_graph, tgg_new_config):
    return tgg_new_config.optimized_task_graph


@pytest.fixture(scope="session")
def filter_tasks():
    def inner(graph, func):
        return filter(func, graph.tasks.values())

    return inner


@pytest.fixture(scope="session")
def print_dependents():
    def inner(graph, label, indent=""):
        if indent == "":
            print(f"Dependent graph for {label}:")

        dependents = set()
        task_id = next(t.task_id for t in graph.tasks.values() if t.label == label)
        for task in graph.tasks.values():
            if task_id in task.dependencies.values():
                dependents.add(task.label)

        print(f"{indent}{label}")
        if dependents:
            for dep in sorted(dependents):
                inner(graph, dep, indent=indent + "  ")

    return inner
