import os
import sys
from unittest import mock

import mozunit
import pytest

# need this so the raptor unit tests can find output & filter classes
here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

from results import RaptorResultsHandler

PERFHERDER_DATA = {"framework": {"name": "browsertime"}, "suites": []}
LABEL = "test-windows11-64-24h2-shippable/opt-browsertime-benchmark-firefox-sp3"


@pytest.fixture
def ci_env(monkeypatch):
    monkeypatch.setenv("RAPTOR_TASK_LABEL", LABEL)
    monkeypatch.setenv(
        "GECKO_HEAD_REPOSITORY", "https://hg.mozilla.org/integration/autoland/"
    )


@pytest.fixture
def output():
    return mock.Mock(summarized_results=PERFHERDER_DATA)


@pytest.fixture
def xtreme_regression():
    with mock.patch("results.XtremeRegression") as xtreme_regression:
        xtreme_regression.return_value.detect.return_value = [
            {"suite": "speedometer3", "test": None, "exceeds_variance": True},
            {"suite": "speedometer3", "test": "a", "exceeds_variance": False},
            {"suite": "speedometer3", "test": "b", "exceeds_variance": None},
        ]
        yield xtreme_regression


def test_detect_xtreme_regressions(ci_env, output, xtreme_regression):
    handler = RaptorResultsHandler()
    handler._detect_xtreme_regressions(output, 1)

    xtreme_regression.assert_called_once_with(
        "windows11-64-24h2-shippable", revision_repository="autoland"
    )
    xtreme_regression.return_value.detect.assert_called_once_with(PERFHERDER_DATA)
    assert handler.xtreme_regressions == [
        {"suite": "speedometer3", "test": None, "exceeds_variance": True}
    ]


def test_detect_xtreme_regressions_outside_of_ci(
    monkeypatch, output, xtreme_regression
):
    monkeypatch.delenv("RAPTOR_TASK_LABEL", raising=False)
    handler = RaptorResultsHandler()
    handler._detect_xtreme_regressions(output, 1)

    xtreme_regression.assert_not_called()
    assert handler.xtreme_regressions == []


def test_detect_xtreme_regressions_outside_of_autoland(
    ci_env, monkeypatch, output, xtreme_regression
):
    monkeypatch.setenv("GECKO_HEAD_REPOSITORY", "https://hg.mozilla.org/try/")
    handler = RaptorResultsHandler()
    handler._detect_xtreme_regressions(output, 1)

    xtreme_regression.assert_not_called()
    assert handler.xtreme_regressions == []


def test_detect_xtreme_regressions_without_perfherder_data(
    ci_env, output, xtreme_regression
):
    handler = RaptorResultsHandler()
    handler._detect_xtreme_regressions(output, 0)

    xtreme_regression.assert_not_called()


def test_detect_xtreme_regressions_failure(ci_env, output, xtreme_regression):
    xtreme_regression.return_value.detect.side_effect = ConnectionError(
        "Treeherder is down"
    )
    handler = RaptorResultsHandler()
    handler._detect_xtreme_regressions(output, 1)

    assert handler.xtreme_regressions == []


if __name__ == "__main__":
    mozunit.main()
