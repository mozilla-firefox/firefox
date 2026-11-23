import os
import sys
from unittest import mock

import mozunit
import pytest

# need this so raptor imports work both from /raptor and via mach
here = os.path.abspath(os.path.dirname(__file__))

raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

from benchmark import Benchmark


@pytest.fixture(autouse=True)
def quiet_logger():
    """Raptor's logger isn't initialized in unit tests."""
    with mock.patch("logger.logger.RaptorLogger.info"):
        yield


@pytest.fixture
def benchmark():
    """A Benchmark with __init__ bypassed, so only setup is exercised."""
    bench = Benchmark.__new__(Benchmark)
    bench.config = {}
    bench.test = {"name": "jetstream3", "test_url": "http://<host>:<port>/"}
    return bench


def test_fetched_benchmark_is_used_when_present(benchmark, tmp_path):
    fetched = tmp_path / "pgo-extended-corpus" / "JetStream"
    fetched.mkdir(parents=True)
    (fetched / "index.html").write_text("")
    benchmark.test["fetch_path"] = "pgo-extended-corpus/JetStream"

    with mock.patch.dict(os.environ, {"MOZ_FETCHES_DIR": str(tmp_path)}):
        result = benchmark._setup_fetched_benchmark(tmp_path / "objdir", run_local=True)

    assert result is not None
    # the served folder is the parent, and the url points at the benchmark
    assert (result / "JetStream").is_symlink() or (result / "JetStream").is_dir()
    assert benchmark.test["test_url"] == "http://<host>:<port>/JetStream/"


def test_no_fetch_path_falls_back_to_clone(benchmark, tmp_path):
    with mock.patch.dict(os.environ, {"MOZ_FETCHES_DIR": str(tmp_path)}):
        assert benchmark._setup_fetched_benchmark(tmp_path, run_local=True) is None


def test_no_fetches_dir_falls_back_to_clone(benchmark, tmp_path):
    benchmark.test["fetch_path"] = "pgo-extended-corpus/JetStream"
    env = {k: v for k, v in os.environ.items() if k != "MOZ_FETCHES_DIR"}

    with mock.patch.dict(os.environ, env, clear=True):
        assert benchmark._setup_fetched_benchmark(tmp_path, run_local=True) is None


def test_missing_fetched_dir_falls_back_to_clone(benchmark, tmp_path):
    benchmark.test["fetch_path"] = "pgo-extended-corpus/JetStream"

    with mock.patch.dict(os.environ, {"MOZ_FETCHES_DIR": str(tmp_path)}):
        assert benchmark._setup_fetched_benchmark(tmp_path, run_local=True) is None


def test_source_path_mapping_points_at_the_served_directory(benchmark, tmp_path):
    benchmark.host = "127.0.0.1"
    benchmark.port = "62763"
    benchmark.bench_dir = tmp_path

    assert benchmark.get_source_path_mapping() == {
        "urlPrefix": "http://127.0.0.1:62763/",
        "localPath": str(tmp_path.resolve()),
    }


if __name__ == "__main__":
    mozunit.main()
