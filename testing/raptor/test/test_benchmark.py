import os
import subprocess
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


def _git_mocks():
    return (
        mock.patch("benchmark.subprocess.check_call"),
        mock.patch("benchmark.subprocess.check_output"),
    )


def _make_source(path, with_file=True):
    path.mkdir(parents=True)
    if with_file:
        (path / "index.html").write_text("hello")
    else:
        (path / "sub").mkdir()
    return path


@pytest.mark.parametrize(
    "config,expected",
    [
        ({}, {}),
        (
            {"benchmark_repository": "https://x/repo", "benchmark_revision": "abc"},
            {"repository": "https://x/repo", "repository_revision": "abc"},
        ),
        (
            {
                "benchmark_repository": "https://x/repo",
                "benchmark_revision": "abc",
                "benchmark_branch": "dev",
            },
            {
                "repository": "https://x/repo",
                "repository_revision": "abc",
                "branch": "dev",
            },
        ),
    ],
)
def test_init_applies_repository_overrides_and_serves(tmp_path, config, expected):
    """Check that the constructor applies benchmark repository overrides and starts serving."""
    config = dict(config, run_local=True)
    test = {"name": "sp3"}

    def fake_setup(self, topsrc, mozbuild, run_local):
        assert run_local is True
        self.bench_dir = tmp_path

    with mock.patch.object(Benchmark, "setup_benchmarks", fake_setup):
        with mock.patch.object(Benchmark, "start_http_server") as start:
            bench = Benchmark(config, test, debug_mode=True)

    start.assert_called_once_with()
    assert bench.debug_mode is True
    assert bench.httpd is None and bench.server_thread is None
    assert bench.bench_dir == tmp_path
    assert {k: v for k, v in test.items() if k != "name"} == expected


@pytest.mark.parametrize("port,expected", [(8123, 8123), (None, 5555)])
def test_start_http_server_uses_given_or_free_port(benchmark, port, expected):
    """Check that the server uses the test's benchmark_port or else a free port."""
    benchmark.config["host"] = "127.0.0.1"
    benchmark.test["benchmark_port"] = port
    httpd = mock.Mock()

    with mock.patch.object(Benchmark, "_pick_free_port", return_value=5555):
        with mock.patch.object(
            Benchmark, "setup_webserver", return_value=httpd
        ) as setup:
            with mock.patch("benchmark.threading.Thread") as thread:
                benchmark.start_http_server()

    setup.assert_called_once_with(f"127.0.0.1:{expected}")
    thread.assert_called_once_with(target=httpd.serve_forever)
    thread.return_value.start.assert_called_once_with()
    assert benchmark.httpd is httpd
    assert benchmark.port == expected


def test_setup_webserver_builds_handler(benchmark, tmp_path):
    """Check that the webserver binds the given address with a handler for bench_dir."""
    benchmark.bench_dir = tmp_path
    benchmark.debug_mode = False
    benchmark.config["verbose"] = True

    with mock.patch("benchmark.ThreadingHTTPServer") as server:
        result = benchmark.setup_webserver("127.0.0.1:8000")

    assert result is server.return_value
    (address, handler), _ = server.call_args
    assert address == ("127.0.0.1", 8000)
    assert benchmark.host == "127.0.0.1" and benchmark.port == "8000"
    assert handler.protocol_version == "HTTP/1.1"
    assert handler.doc_root == tmp_path
    assert handler.verbose is True


def _handler_class(benchmark, tmp_path, verbose):
    benchmark.bench_dir = tmp_path
    benchmark.debug_mode = verbose
    with mock.patch("benchmark.ThreadingHTTPServer") as server:
        benchmark.setup_webserver("localhost:1")
    return server.call_args[0][1]


def test_handler_serves_bench_dir(benchmark, tmp_path):
    """Check that the request handler serves files from the benchmark directory."""
    handler = _handler_class(benchmark, tmp_path, False)
    with mock.patch(
        "benchmark.SimpleHTTPRequestHandler.__init__", return_value=None
    ) as init:
        handler("request", "address", "server")
    init.assert_called_once_with("request", "address", "server", directory=tmp_path)


def test_handler_sends_cors_and_isolation_headers(benchmark, tmp_path):
    """Check that responses carry the CORS and cross-origin isolation headers."""
    handler = _handler_class(benchmark, tmp_path, False)
    instance = handler.__new__(handler)
    with mock.patch.object(handler, "send_header") as send_header:
        with mock.patch("benchmark.SimpleHTTPRequestHandler.end_headers") as end:
            instance.end_headers()

    assert send_header.call_args_list == [
        mock.call("Access-Control-Allow-Origin", "*"),
        mock.call("Cross-Origin-Opener-Policy", "same-origin"),
        mock.call("Cross-Origin-Embedder-Policy", "require-corp"),
    ]
    end.assert_called_once_with(instance)


@pytest.mark.parametrize("verbose", [True, False])
def test_handler_logs_only_when_verbose(benchmark, tmp_path, verbose):
    """Check that requests are logged only in verbose or debug mode."""
    handler = _handler_class(benchmark, tmp_path, verbose)
    instance = handler.__new__(handler)
    with mock.patch("benchmark.SimpleHTTPRequestHandler.log_message") as log:
        instance.log_message("%s", "x")
    assert log.called is verbose


def test_stop_http_server_shuts_down_and_joins(benchmark):
    """Check that stopping the server shuts it down and joins its thread."""
    benchmark.httpd = mock.Mock()
    benchmark.server_thread = mock.Mock()
    benchmark.stop_http_server()
    benchmark.httpd.shutdown.assert_called_once_with()
    benchmark.server_thread.join.assert_called_once_with(5)


def test_stop_http_server_without_server(benchmark):
    """Check that stopping is a no-op when no server was started."""
    benchmark.httpd = None
    benchmark.server_thread = None
    benchmark.stop_http_server()


def test_stop_http_server_logs_failures(benchmark):
    """Check that shutdown and join failures are logged as warnings, not raised."""
    benchmark.httpd = mock.Mock(**{"shutdown.side_effect": OSError("boom")})
    benchmark.server_thread = mock.Mock(**{"join.side_effect": RuntimeError("x")})
    with mock.patch("logger.logger.RaptorLogger.warning") as warning:
        benchmark.stop_http_server()
    assert warning.call_count == 2
    assert "boom" in warning.call_args_list[0][0][0]


def test_pick_free_port(benchmark):
    """Check that a free port is picked by binding to port 0."""
    with mock.patch("benchmark.socket.socket") as sock:
        sock.return_value.getsockname.return_value = ("0.0.0.0", 4321)
        assert benchmark._pick_free_port() == 4321
    sock.return_value.bind.assert_called_once_with(("", 0))
    sock.return_value.close.assert_called_once_with()


def test_full_clone(benchmark, tmp_path):
    """Check that a full clone disables autocrlf and clones into the resolved destination."""
    with mock.patch("benchmark.subprocess.check_call") as call:
        benchmark._full_clone("https://x/repo", tmp_path / "dest")
    cmd = call.call_args[0][0]
    assert cmd[:2] == ["git", "clone"]
    assert "core.autocrlf=false" in cmd
    assert cmd[-2:] == ["https://x/repo", str((tmp_path / "dest").resolve())]


@pytest.mark.parametrize("repo_path,expected", [(None, "benchmarks"), ("sp", "sp")])
def test_sparse_clone(benchmark, tmp_path, repo_path, expected):
    """Check that a sparse clone checks out repository_path, defaulting to benchmarks."""
    if repo_path:
        benchmark.test["repository_path"] = repo_path
    dest = tmp_path / "dest"
    with mock.patch("benchmark.subprocess.check_call") as call:
        benchmark._sparse_clone("https://x/repo", dest)
    clone, sparse = call.call_args_list
    assert "--sparse" in clone[0][0]
    assert clone[0][0][-2:] == ["https://x/repo", str(dest.resolve())]
    assert sparse == mock.call(["git", "sparse-checkout", "set", expected], cwd=dest)


@pytest.mark.parametrize(
    "run_local,parts",
    [
        (False, ("tests", "webkit", "PerformanceTests")),
        (True, ("testing", "raptor", "benchmarks")),
    ],
)
def test_get_benchmark_folder(benchmark, tmp_path, run_local, parts):
    """Check that benchmarks live under tests/webkit in CI and testing/raptor locally."""
    assert benchmark._get_benchmark_folder(tmp_path, run_local) == tmp_path.joinpath(
        *parts
    )


@pytest.mark.skipif(os.name == "nt", reason="symlinks are only used off Windows")
def test_copy_or_link_files_symlinks_and_hosts_parent(benchmark, tmp_path):
    """Check that benchmarks are symlinked once and their parent is hosted when it has files."""
    src = _make_source(tmp_path / "src" / "Bench")
    dest_root = tmp_path / "out" / "jetstream3"

    result = benchmark._copy_or_link_files(src, dest_root)

    assert result == dest_root
    assert (dest_root / "Bench").is_symlink()
    assert (dest_root / "Bench").resolve() == src.resolve()
    assert benchmark.test["test_url"] == "http://<host>:<port>/Bench/"

    result = benchmark._copy_or_link_files(src, dest_root, host_from_parent=False)
    assert result == dest_root / "Bench"


def test_copy_or_link_files_copies_without_symlink(benchmark, tmp_path, monkeypatch):
    """Check that benchmarks are copied over a clobbered dest when symlinks are unavailable."""
    monkeypatch.delattr(os, "symlink", raising=False)
    src = _make_source(tmp_path / "src" / "Bench")
    dest_root = tmp_path / "out"
    stale = dest_root / "Bench" / "stale.js"
    stale.parent.mkdir(parents=True)
    stale.write_text("")

    result = benchmark._copy_or_link_files(src, dest_root)

    assert result == dest_root
    assert not (dest_root / "Bench").is_symlink()
    assert (dest_root / "Bench" / "index.html").read_text() == "hello"
    assert not stale.exists()


def test_copy_or_link_files_hosts_dir_without_files(benchmark, tmp_path):
    """Check that a benchmark with only subfolders is hosted itself without a URL rewrite."""
    src = _make_source(tmp_path / "src" / "Bench", with_file=False)
    result = benchmark._copy_or_link_files(src, tmp_path / "out")
    assert result == tmp_path / "out" / "Bench"
    assert benchmark.test["test_url"] == "http://<host>:<port>/"


def test_verify_benchmark_revision(benchmark, tmp_path):
    """Check that the benchmark revision is verified as a commit."""
    with mock.patch("benchmark.subprocess.check_call") as call:
        benchmark._verify_benchmark_revision("abc", tmp_path)
    call.assert_called_once_with(
        ["git", "rev-parse", "--verify", "abc^{commit}"], cwd=tmp_path
    )


def test_verify_benchmark_revision_raises_on_bad_revision(benchmark, tmp_path):
    """Check that an unknown revision is logged as an error and re-raised."""
    err = subprocess.CalledProcessError(128, "git")
    with mock.patch("benchmark.subprocess.check_call", side_effect=err):
        with mock.patch("logger.logger.RaptorLogger.error") as error:
            with pytest.raises(
                subprocess.CalledProcessError, match="non-zero exit status 128"
            ):
                benchmark._verify_benchmark_revision("bad", tmp_path)
    assert "bad" in error.call_args[0][0]


@pytest.mark.parametrize(
    "set_head,expected",
    [
        (b"origin/HEAD set to main\n", "main"),
        (b"origin/HEAD set to trunk\n", "trunk"),
    ],
)
def test_update_benchmark_repo_default_branch(benchmark, tmp_path, set_head, expected):
    """Check that the repo is updated on the remote default branch, even after it changed."""
    check_call, check_output = _git_mocks()
    with check_call as call, check_output as output:
        output.side_effect = [b"origin/main\n", set_head]
        benchmark._update_benchmark_repo(tmp_path)

    assert output.call_args_list == [
        mock.call(["git", "rev-parse", "--abbrev-ref", "origin/HEAD"], cwd=tmp_path),
        mock.call(["git", "remote", "set-head", "origin", "-a"], cwd=tmp_path),
    ]
    assert call.call_args_list == [
        mock.call(["git", "checkout", expected], cwd=tmp_path),
        mock.call(["git", "pull"], cwd=tmp_path),
    ]


def test_update_benchmark_repo_default_branch_failure(benchmark, tmp_path):
    """Check that failing to find the default branch is logged and re-raised."""
    check_call, check_output = _git_mocks()
    with check_call as call:
        with check_output as output:
            with mock.patch("logger.logger.RaptorLogger.critical") as critical:
                with pytest.raises(
                    subprocess.CalledProcessError, match="non-zero exit status 1"
                ):
                    output.side_effect = subprocess.CalledProcessError(1, "git")
                    benchmark._update_benchmark_repo(tmp_path)
    critical.assert_called_once()
    call.assert_not_called()


@pytest.mark.parametrize("pull_fails", [False, True])
def test_update_benchmark_repo_explicit_branch(benchmark, tmp_path, pull_fails):
    """Check that an explicit branch is checked out whether or not pulling all branches fails."""
    benchmark.test["repository_branch"] = "dev"
    effects = [
        subprocess.CalledProcessError(1, "git") if pull_fails else None,
        None,
        None,
    ]
    check_call, check_output = _git_mocks()
    with check_call as call, check_output as output:
        call.side_effect = effects
        benchmark._update_benchmark_repo(tmp_path)

    output.assert_not_called()
    assert call.call_args_list == [
        mock.call(["git", "pull", "--all"], cwd=tmp_path),
        mock.call(["git", "checkout", "dev"], cwd=tmp_path),
        mock.call(["git", "pull"], cwd=tmp_path),
    ]


@pytest.fixture
def git_benchmark(benchmark, tmp_path):
    benchmark.test.update({
        "repository": "https://github.com/WebKit/Speedometer",
        "repository_revision": "abc123",
    })
    return benchmark


@pytest.mark.parametrize(
    "sparse,run_local,clone",
    [
        (False, True, "_full_clone"),
        (True, True, "_full_clone"),
        (True, False, "_sparse_clone"),
    ],
)
def test_setup_git_benchmarks_clones_when_missing(
    git_benchmark, tmp_path, sparse, run_local, clone
):
    """Check that a missing repo is cloned, sparsely only in CI, at the given revision."""
    git_benchmark.test["sparse_checkout"] = sparse
    repo = tmp_path / "mozbuild" / "performance-tests" / "Speedometer"
    check_call, check_output = _git_mocks()
    with check_call as call:
        with check_output:
            with mock.patch.object(
                Benchmark, clone, side_effect=lambda r, d: _make_source(d)
            ) as do_clone:
                result = git_benchmark._setup_git_benchmarks(
                    str(tmp_path / "mozbuild"), tmp_path / "obj", run_local=run_local
                )

    do_clone.assert_called_once_with(git_benchmark.test["repository"], repo)
    assert call.call_args_list[-1] == mock.call(["git", "checkout", "abc123"], cwd=repo)
    expected = git_benchmark._get_benchmark_folder(tmp_path / "obj", run_local)
    assert result == expected / "jetstream3"
    assert (result / "Speedometer" / "index.html").exists()
    assert git_benchmark.test["test_url"] == "http://<host>:<port>/Speedometer/"


def test_setup_git_benchmarks_uses_repository_path(git_benchmark, tmp_path):
    """Check that repository_path selects the hosted folder inside the repo."""
    git_benchmark.test["repository_path"] = "Bench"
    git_benchmark.test["host_from_parent"] = False
    check_call, check_output = _git_mocks()
    with check_call:
        with check_output:
            with mock.patch.object(
                Benchmark,
                "_full_clone",
                side_effect=lambda r, d: _make_source(d / "Bench"),
            ):
                result = git_benchmark._setup_git_benchmarks(
                    str(tmp_path / "mb"), tmp_path / "obj", run_local=True
                )
    assert result.name == "Bench"
    assert (result / "index.html").exists()


def test_setup_git_benchmarks_updates_existing_repo(git_benchmark, tmp_path):
    """Check that an existing repo with the same origin is updated, not recloned."""
    repo = _make_source(tmp_path / "performance-tests" / "Speedometer")
    check_call, check_output = _git_mocks()
    with check_call:
        with check_output as output:
            with mock.patch.object(Benchmark, "_update_benchmark_repo") as update:
                with mock.patch.object(Benchmark, "_full_clone") as clone:
                    output.side_effect = [
                        b"git 2.40",
                        b"https://github.com/WebKit/Speedometer\n",
                    ]
                    git_benchmark._setup_git_benchmarks(
                        str(tmp_path), tmp_path / "obj", run_local=True
                    )
    update.assert_called_once_with(repo)
    clone.assert_not_called()


def test_setup_git_benchmarks_reclones_on_origin_change(git_benchmark, tmp_path):
    """Check that an existing repo with a different origin is removed and recloned."""
    repo = _make_source(tmp_path / "performance-tests" / "Speedometer")
    (repo / "old.js").write_text("")
    check_call, check_output = _git_mocks()
    with check_call:
        with check_output as output:
            with mock.patch.object(Benchmark, "_update_benchmark_repo") as update:
                with mock.patch.object(
                    Benchmark, "_full_clone", side_effect=lambda r, d: _make_source(d)
                ) as clone:
                    output.side_effect = [
                        b"git 2.40",
                        b"https://elsewhere/Speedometer\n",
                    ]
                    git_benchmark._setup_git_benchmarks(
                        str(tmp_path), tmp_path / "obj", run_local=True
                    )
    update.assert_not_called()
    clone.assert_called_once_with(git_benchmark.test["repository"], repo)
    assert not (repo / "old.js").exists()


def test_setup_git_benchmarks_without_mozbuild_uses_tempdir(git_benchmark, tmp_path):
    """Check that a temporary directory is used when no mozbuild path is given."""
    check_call, check_output = _git_mocks()
    with check_call:
        with check_output:
            with mock.patch(
                "benchmark.tempfile.mkdtemp", return_value=str(tmp_path)
            ) as mk:
                with mock.patch.object(
                    Benchmark, "_full_clone", side_effect=lambda r, d: _make_source(d)
                ) as clone:
                    git_benchmark._setup_git_benchmarks(
                        None, tmp_path / "obj", run_local=True
                    )
    mk.assert_called_once_with()
    assert clone.call_args[0][1] == tmp_path / "performance-tests" / "Speedometer"


def test_setup_git_benchmarks_requires_git(git_benchmark, tmp_path):
    """Check that setup fails before cloning when git is unavailable."""
    with mock.patch("benchmark.subprocess.check_output", side_effect=OSError("nogit")):
        with mock.patch.object(Benchmark, "_full_clone") as clone:
            with pytest.raises(OSError, match="nogit"):
                git_benchmark._setup_git_benchmarks(
                    str(tmp_path), tmp_path, run_local=True
                )
    clone.assert_not_called()


def test_setup_in_tree_benchmarks_in_ci(benchmark, tmp_path):
    """Check that in-tree benchmarks are used in place in CI."""
    with mock.patch.object(Benchmark, "_copy_or_link_files") as copy:
        result = benchmark._setup_in_tree_benchmarks(
            tmp_path / "src", tmp_path, run_local=False
        )
    copy.assert_not_called()
    assert result == tmp_path / "tests" / "webkit" / "PerformanceTests"


def test_setup_in_tree_benchmarks_locally(benchmark, tmp_path):
    """Check that in-tree benchmarks are linked into the objdir when running locally."""
    src = _make_source(
        tmp_path / "src" / "third_party" / "webkit" / "PerformanceTests",
        with_file=False,
    )
    result = benchmark._setup_in_tree_benchmarks(
        tmp_path / "src", tmp_path / "obj", run_local=True
    )
    benchmarks = tmp_path / "obj" / "testing" / "raptor" / "benchmarks"
    assert result == benchmarks / "PerformanceTests"
    assert (result / "sub").is_dir()
    assert src.is_dir()


@pytest.mark.parametrize(
    "fetched,repository,used",
    [
        (True, "https://x/repo", "fetched"),
        (False, "https://x/repo", "git"),
        (False, None, "in_tree"),
    ],
)
def test_setup_benchmarks_dispatch(benchmark, tmp_path, fetched, repository, used):
    """Check that fetched benchmarks win over git ones, which win over in-tree ones."""
    benchmark.config["obj_path"] = str(tmp_path)
    if repository:
        benchmark.test["repository"] = repository
    results = {"fetched": tmp_path / "f", "git": tmp_path / "g", "in_tree": tmp_path}

    with mock.patch.object(
        Benchmark,
        "_setup_fetched_benchmark",
        return_value=results["fetched"] if fetched else None,
    ) as fetch:
        with mock.patch.object(
            Benchmark, "_setup_git_benchmarks", return_value=results["git"]
        ) as git:
            with mock.patch.object(
                Benchmark, "_setup_in_tree_benchmarks", return_value=results["in_tree"]
            ) as in_tree:
                benchmark.setup_benchmarks("topsrc", "mozbuild", run_local=False)

    fetch.assert_called_once_with(tmp_path, run_local=False)
    assert git.called is (used == "git")
    assert in_tree.called is (used == "in_tree")
    if used == "git":
        git.assert_called_once_with("mozbuild", tmp_path, run_local=False)
    if used == "in_tree":
        in_tree.assert_called_once_with("topsrc", tmp_path, run_local=False)
    assert benchmark.bench_dir == results[used]


def test_setup_benchmarks_defaults_to_module_dir(benchmark):
    """Check that the module directory is the base when no obj_path is configured."""
    import benchmark as benchmark_module

    with mock.patch.object(Benchmark, "_setup_fetched_benchmark", return_value=None):
        with mock.patch.object(
            Benchmark, "_setup_in_tree_benchmarks", side_effect=lambda t, d, **k: d
        ):
            benchmark.setup_benchmarks("topsrc", None)
    assert benchmark.bench_dir == benchmark_module.here


if __name__ == "__main__":
    mozunit.main()
