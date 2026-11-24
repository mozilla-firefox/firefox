# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import threading
import traceback
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

import mozfile
from logger.logger import RaptorLogger

LOG = RaptorLogger(component="raptor-benchmark")
here = pathlib.Path(__file__).parent.resolve()


class Benchmark:
    """utility class for running benchmarks in raptor"""

    def __init__(self, config, test, debug_mode=False):
        self.config = config
        self.test = test
        self.debug_mode = debug_mode
        self.httpd = None
        self.server_thread = None

        # Note that we can only change the repository, revision, and branch through here.
        # The path to the test should remain constant. If it needs to be changed, make a
        # patch that changes it for the new test.
        if self.config.get("benchmark_repository", None):
            self.test["repository"] = self.config["benchmark_repository"]
            self.test["repository_revision"] = self.config["benchmark_revision"]

            if self.config.get("benchmark_branch", None):
                self.test["branch"] = self.config["benchmark_branch"]

        self.setup_benchmarks(
            os.getenv("MOZ_DEVELOPER_REPO_DIR"),
            os.getenv("MOZ_MOZBUILD_DIR"),
            run_local=self.config.get("run_local", False),
        )

        LOG.info(f"bench_dir: {self.bench_dir}")
        LOG.info("bench_dir contains:")
        LOG.info(list(self.bench_dir.iterdir()))

        # now have the benchmark source ready, go ahead and serve it up!
        self.start_http_server()

    def start_http_server(self):
        self.host = self.config["host"]
        self.port = int(self.test.get("benchmark_port") or self._pick_free_port())
        _webserver = f"{self.host}:{self.port}"

        self.httpd = self.setup_webserver(_webserver)
        self.server_thread = threading.Thread(target=self.httpd.serve_forever)
        self.server_thread.start()

    def setup_webserver(self, webserver):
        LOG.info(f"starting webserver on {webserver!r}")
        LOG.info(f"serving benchmarks from here: {self.bench_dir}")

        self.host, self.port = webserver.split(":")

        class CustomHandler(SimpleHTTPRequestHandler):
            protocol_version = "HTTP/1.1"  # allow connection re-use
            doc_root = self.bench_dir
            verbose = self.debug_mode or self.config.get("verbose", False)

            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs, directory=CustomHandler.doc_root)

            def log_message(self, *args):
                if CustomHandler.verbose:
                    super().log_message(*args)

            def end_headers(self):
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Cross-Origin-Opener-Policy", "same-origin")
                self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
                SimpleHTTPRequestHandler.end_headers(self)

        return ThreadingHTTPServer((self.host, int(self.port)), CustomHandler)

    def get_source_path_mapping(self):
        """Map the URLs of the benchmark server to the directory it serves.

        profiler-edit uses this to embed the benchmark's JS sources and their
        source maps into the profile, after the server has been stopped.
        """
        return {
            "urlPrefix": f"http://{self.host}:{self.port}/",
            "localPath": str(pathlib.Path(self.bench_dir).resolve()),
        }

    def stop_http_server(self):
        try:
            if self.httpd:
                self.httpd.shutdown()
        except Exception:
            LOG.warning(f"Failed to stop benchmark server: {traceback.format_exc()}")
        try:
            if self.server_thread:
                self.server_thread.join(5)
        except Exception:
            LOG.warning(f"Failed to stop benchmark server: {traceback.format_exc()}")

    def _pick_free_port(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(("", 0))
        port = sock.getsockname()[1]
        sock.close()
        return port

    def _full_clone(self, benchmark_repository, dest):
        subprocess.check_call([
            "git",
            "clone",
            "-c",
            "http.postBuffer=2147483648",
            "-c",
            "core.autocrlf=false",
            benchmark_repository,
            str(dest.resolve()),
        ])

    def _get_benchmark_folder(self, benchmark_dest, run_local):
        if not run_local:
            # If the test didn't specify a repo and we're in CI
            # then we'll find them here and we don't need to do anything else
            return pathlib.Path(benchmark_dest, "tests", "webkit", "PerformanceTests")
        return pathlib.Path(benchmark_dest, "testing", "raptor", "benchmarks")

    def _sparse_clone(self, benchmark_repository, dest):
        """Get a partial clone of the repo.

        This need git version 2.30+ so it's currently unused but it works.
        See bug 1804694. This method should only be used in CI, locally we
        can simply pull the whole repo.
        """
        subprocess.check_call([
            "git",
            "clone",
            "--depth",
            "1",
            "--filter",
            "blob:none",
            "--sparse",
            benchmark_repository,
            str(dest.resolve()),
        ])
        subprocess.check_call(
            [
                "git",
                "sparse-checkout",
                "set",
                self.test.get("repository_path", "benchmarks"),
            ],
            cwd=dest,
        )

    def _copy_or_link_files(
        self,
        benchmark_path,
        benchmark_dest,
        skip_files_and_hidden=True,
        host_from_parent=True,
    ):
        if not benchmark_dest.exists():
            benchmark_dest.mkdir(parents=True, exist_ok=True)

        dest = pathlib.Path(benchmark_dest, benchmark_path.name)
        if hasattr(os, "symlink") and os.name != "nt":
            if not dest.exists():
                os.symlink(benchmark_path, dest)
        else:
            # Clobber the benchmark in case a recent update removed any files.
            mozfile.remove(str(dest.resolve()))
            shutil.copytree(benchmark_path, dest)

        if host_from_parent and any(
            path.is_file() for path in benchmark_path.iterdir()
        ):
            # Host the parent of this directory to prevent hosting issues
            # (e.g. linked files ending up with different routes)
            host_folder = dest.parent
            self.test["test_url"] = self.test["test_url"].replace(
                "<port>/", f"<port>/{benchmark_path.name}/"
            )
            dest = host_folder

        return dest

    def _verify_benchmark_revision(self, benchmark_revision, external_repo_path):
        try:
            # Check if the given revision is valid
            subprocess.check_call(
                ["git", "rev-parse", "--verify", f"{benchmark_revision}^{{commit}}"],
                cwd=external_repo_path,
            )
            LOG.info("Given benchmark repository revision verified")
        except Exception:
            LOG.error(
                f"Given revision doesn't exist in this repository: {benchmark_revision}"
            )
            raise

    def _update_benchmark_repo(self, external_repo_path):
        default_branch = self.test.get("repository_branch", None)
        if default_branch is None:
            try:
                # Get the default branch name, and check it if's been updated
                default_branch = (
                    subprocess
                    .check_output(
                        ["git", "rev-parse", "--abbrev-ref", "origin/HEAD"],
                        cwd=external_repo_path,
                    )
                    .decode("utf-8")
                    .strip()
                    .split("/")[-1]
                )
                remote_default_branch = (
                    subprocess
                    .check_output(
                        ["git", "remote", "set-head", "origin", "-a"],
                        cwd=external_repo_path,
                    )
                    .decode("utf-8")
                    .strip()
                )
                if default_branch not in remote_default_branch:
                    default_branch = remote_default_branch.split()[-1]
            except Exception:
                LOG.critical("Failed to find the default branch of the repository!")
                raise
        else:
            LOG.info(f"Using non-default branch {default_branch}")
            try:
                subprocess.check_call(["git", "pull", "--all"], cwd=external_repo_path)
            except subprocess.CalledProcessError:
                LOG.info("Failed to pull new branches from remote")

        LOG.info(external_repo_path)
        subprocess.check_call(
            ["git", "checkout", default_branch], cwd=external_repo_path
        )
        subprocess.check_call(["git", "pull"], cwd=external_repo_path)

    def _setup_git_benchmarks(self, mozbuild_path, benchmark_dest, run_local=True):
        """Setup a benchmark from a github repository."""
        benchmark_repository = self.test["repository"]
        benchmark_revision = self.test["repository_revision"]

        # Specifies where we can find the benchmark within the cloned repo, this is the
        # folder that will be hosted to run the test. If it isn't given, we'll host the
        # root of the repository.
        benchmark_repo_path = self.test.get("repository_path", "")

        # Get the performance-tests cache (if it exists), otherwise create a temp folder
        if mozbuild_path is None:
            mozbuild_path = tempfile.mkdtemp()

        external_repo_path = pathlib.Path(
            mozbuild_path, "performance-tests", benchmark_repository.split("/")[-1]
        )

        try:
            subprocess.check_output(["git", "--version"])
        except Exception as ex:
            LOG.info(
                "Git is not available! Please install git and "
                "ensure it is included in the terminal path"
            )
            raise ex

        if not external_repo_path.is_dir():
            LOG.info(f"Cloning the benchmarks to {external_repo_path}")
            # Use sparse checkouts instead of full clones in CI. Locally, we can do
            # a full clone. At the moment this is necessary for the perf-automation
            # repo which hosts multiple benchmarks and we only need to sparse clone
            # the required benchmark. Other benchmarks are either vendored in-tree
            # or cloned directly (e.g. speedometer 3, motionmark 1.3)
            use_sparse_checkout = (
                self.test.get("sparse_checkout", False) and not run_local
            )

            if use_sparse_checkout:
                LOG.info("Performing a sparse clone...")
                self._sparse_clone(benchmark_repository, external_repo_path)
                LOG.info("Sparse clone successful")
            else:
                LOG.info("Performing a full clone...")
                self._full_clone(benchmark_repository, external_repo_path)
                LOG.info("Full clone successful")
        else:
            # Make sure that the repo origin wasn't changed
            url = (
                subprocess
                .check_output(
                    ["git", "config", "--get", "remote.origin.url"],
                    cwd=external_repo_path,
                )
                .decode("utf-8")
                .strip()
            )

            if url != benchmark_repository:
                LOG.info(
                    "Removing repo with a different remote origin before installing new one"
                )
                mozfile.remove(str(external_repo_path))
                self._full_clone(benchmark_repository, external_repo_path)
            else:
                self._update_benchmark_repo(external_repo_path)

        self._verify_benchmark_revision(benchmark_revision, external_repo_path)
        subprocess.check_call(
            ["git", "checkout", benchmark_revision], cwd=external_repo_path
        )

        benchmark_dest = pathlib.Path(
            self._get_benchmark_folder(benchmark_dest, run_local), self.test["name"]
        )
        benchmark_dest = self._copy_or_link_files(
            pathlib.Path(external_repo_path, benchmark_repo_path),
            benchmark_dest,
            skip_files_and_hidden=False,
            host_from_parent=self.test.get("host_from_parent", True),
        )

        return benchmark_dest

    def _setup_fetched_benchmark(self, benchmark_dest, run_local=True):
        """Setup a benchmark downloaded by a taskcluster fetch task.

        Returns None when the test doesn't declare a `fetch_path`, or when the
        fetch isn't present (e.g. running locally), so the caller can fall back
        to cloning the benchmark repository.
        """
        fetch_path = self.test.get("fetch_path", None)
        if not fetch_path:
            return None

        fetches_dir = os.environ.get("MOZ_FETCHES_DIR", None)
        if not fetches_dir:
            LOG.info("MOZ_FETCHES_DIR is unset, falling back to cloning the benchmark")
            return None

        benchmark_path = pathlib.Path(fetches_dir, fetch_path)
        if not benchmark_path.is_dir():
            LOG.info(
                f"No fetched benchmark at {benchmark_path}, "
                f"falling back to cloning the benchmark"
            )
            return None

        LOG.info(f"Using the fetched benchmark found at {benchmark_path}")
        benchmark_dest = pathlib.Path(
            self._get_benchmark_folder(benchmark_dest, run_local), self.test["name"]
        )

        return self._copy_or_link_files(
            benchmark_path,
            benchmark_dest,
            skip_files_and_hidden=False,
            host_from_parent=self.test.get("host_from_parent", True),
        )

    def _setup_in_tree_benchmarks(self, topsrc_path, benchmark_dest, run_local=True):
        """Setup a benchmakr that is found in-tree.

        This method will be deprecated once bug 1804578 is resolved (copying our
        in-tree benchmarks into a repo) to have a standard way of running benchmarks.
        """
        benchmark_dest = self._get_benchmark_folder(benchmark_dest, run_local)
        if not run_local:
            # If the test didn't specify a repo and we're in CI
            # then we'll find them here and we don't need to do anything else
            return benchmark_dest

        benchmark_dest = self._copy_or_link_files(
            pathlib.Path(topsrc_path, "third_party", "webkit", "PerformanceTests"),
            benchmark_dest,
        )

        return benchmark_dest

    def setup_benchmarks(
        self,
        topsrc_path,
        mozbuild_path,
        run_local=True,
    ):
        """Make sure benchmarks are linked to the proper location in the objdir.

        Benchmarks can either live in-tree or in an external repository. In the latter
        case also clone/update the repository if necessary.
        """
        # bench_dir is where we will download all mitmproxy required files
        # when running locally it comes from obj_path via mozharness/mach
        if self.config.get("obj_path", None) is not None:
            bench_dir = pathlib.Path(self.config.get("obj_path"))
        else:
            # in production it is ../tasks/task_N/build/tests/raptor/raptor/...
            # 'here' is that path, we can start with that
            bench_dir = pathlib.Path(here)

        fetched_bench_dir = self._setup_fetched_benchmark(
            bench_dir, run_local=run_local
        )

        if fetched_bench_dir is not None:
            # Setup benchmarks that a fetch task already downloaded for us
            bench_dir = fetched_bench_dir
        elif self.test.get("repository", None) is not None:
            # Setup benchmarks that are found on Github
            bench_dir = self._setup_git_benchmarks(
                mozbuild_path, bench_dir, run_local=run_local
            )
        else:
            # Setup the benchmarks that are available in-tree
            bench_dir = self._setup_in_tree_benchmarks(
                topsrc_path, bench_dir, run_local=run_local
            )

        self.bench_dir = bench_dir
