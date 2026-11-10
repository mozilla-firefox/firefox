# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import contextlib
import functools
import glob
import importlib
import inspect
import logging
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from collections import defaultdict
from datetime import date, datetime, timedelta
from io import StringIO
from pathlib import Path

import requests
from redo import retry
from requests.packages.urllib3.util.retry import Retry

RETRY_SLEEP = 10
API_ROOT = "https://firefox-ci-tc.services.mozilla.com/api/index/v1"
MULTI_REVISION_ROOT = f"{API_ROOT}/namespaces"
MULTI_TASK_ROOT = f"{API_ROOT}/tasks"
ON_TRY = "MOZ_AUTOMATION" in os.environ
DOWNLOAD_TIMEOUT = 30
PERF_METRICS_MATCHER = re.compile(r"(perfMetrics.*)")
EVAL_DATA_MATCHER = re.compile(r"(evalDataPayload.*)")
PRETTY_APP_NAMES = {
    "org.mozilla.fenix": "fenix",
    "org.mozilla.firefox": "fenix",
    "org.mozilla.geckoview_example": "geckoview",
}

FIREFOX_MOBILE_APPS = ["fenix", "geckoview", "focus", "refbrow", "fennec"]
CHROME_MOBILE_APPS = ["chrome-m"]
MOBILE_APPS = FIREFOX_MOBILE_APPS + CHROME_MOBILE_APPS

FIREFOX_DESKTOP_APPS = ["firefox"]
CHROME_DESKTOP_APPS = ["chrome"]
DESKTOP_APPS = FIREFOX_DESKTOP_APPS + CHROME_DESKTOP_APPS


class NoPerfMetricsError(Exception):
    """Raised when perfMetrics were not found, or were not output
    during a test run."""

    def __init__(self, flavor):
        super().__init__(
            f"No perftest results were found in the {flavor} test. Results must be "
            'reported using:\n info("perfMetrics", { metricName: metricValue });'
        )


class NoEvalDataError(Exception):
    """Raised when evalDataPayload was not found, or were not output
    during a test run."""

    def __init__(self, flavor):
        super().__init__(
            f"No eval data was found in the {flavor} test. Results must be "
            'reported using:\n info("evalDataPayload", { evalData: evalValue });'
        )


class LogProcessor:
    def __init__(self, matcher):
        self.buf = ""
        self.stdout = sys.__stdout__
        self.matcher = matcher
        self._match = []

    @property
    def match(self):
        return self._match

    def write(self, buf):
        while buf:
            try:
                newline_index = buf.index("\n")
            except ValueError:
                # No newline, wait for next call
                self.buf += buf
                break

            # Get data up to next newline and combine with previously buffered data
            data = self.buf + buf[: newline_index + 1]
            buf = buf[newline_index + 1 :]

            # Reset buffer then output line
            self.buf = ""
            if data.strip() == "":
                continue
            self.stdout.write(data.strip("\n") + "\n")

            match = self.matcher.search(data)
            if match:
                self._match.append(match.group(1))

    def flush(self):
        pass


@contextlib.contextmanager
def silence(layer=None):
    if layer is None:
        to_patch = (MachLogger,)
    else:
        to_patch = (MachLogger, layer)

    meths = ("info", "debug", "warning", "error", "log")
    patched = defaultdict(dict)

    oldout, olderr = sys.stdout, sys.stderr
    sys.stdout, sys.stderr = StringIO(), StringIO()

    def _vacuum(*args, **kw):
        sys.stdout.write(str(args))

    for obj in to_patch:
        for meth in meths:
            if not hasattr(obj, meth):
                continue
            patched[obj][meth] = getattr(obj, meth)
            setattr(obj, meth, _vacuum)

    stdout = stderr = None
    try:
        sys.stdout.buffer = sys.stdout
        sys.stderr.buffer = sys.stderr
        sys.stdout.fileno = sys.stderr.fileno = lambda: -1
        try:
            yield sys.stdout, sys.stderr
        except Exception:
            sys.stdout.seek(0)
            stdout = sys.stdout.read()
            sys.stderr.seek(0)
            stderr = sys.stderr.read()
            raise
    finally:
        sys.stdout, sys.stderr = oldout, olderr
        for obj, meths in patched.items():
            for name, old_func in meths.items():
                try:
                    setattr(obj, name, old_func)
                except Exception:
                    pass
        if stdout is not None:
            print(stdout)
        if stderr is not None:
            print(stderr)


def simple_platform():
    plat = host_platform()

    if plat.startswith("win"):
        return "win"
    elif plat.startswith("linux"):
        return "linux"
    else:
        return "mac"


def host_platform():
    is_64bits = sys.maxsize > 2**32

    if sys.platform.startswith("win"):
        if is_64bits:
            return "win64"
    elif sys.platform.startswith("linux"):
        if is_64bits:
            return "linux64"
    elif sys.platform.startswith("darwin"):
        return "darwin"

    raise ValueError(f"platform not yet supported: {sys.platform}")


class MachLogger:
    """Wrapper around the mach logger to make logging simpler."""

    def __init__(self, mach_cmd):
        self._logger = mach_cmd.log

    @property
    def log(self):
        return self._logger

    def info(self, msg, name="mozperftest", **kwargs):
        self._logger(logging.INFO, name, kwargs, msg)

    def debug(self, msg, name="mozperftest", **kwargs):
        self._logger(logging.DEBUG, name, kwargs, msg)

    def warning(self, msg, name="mozperftest", **kwargs):
        self._logger(logging.WARNING, name, kwargs, msg)

    def error(self, msg, name="mozperftest", **kwargs):
        self._logger(logging.ERROR, name, kwargs, msg)

    def group_start(self, msg=None, name="mozperftest", **kwargs):
        self._logger(logging.INFO, name, kwargs, msg or "")

    def group_end(self, msg=None, name="mozperftest", **kwargs):
        self._logger(logging.INFO, name, kwargs, msg or "")


def install_package(virtualenv_manager, package, ignore_failure=False):
    """Installs a package using the virtualenv manager.

    Makes sure the package is really installed when the user already has it
    in their local installation.

    Returns True on success, or re-raise the error. If ignore_failure
    is set to True, ignore the error and return False
    """
    from pip._internal.req.constructors import install_req_from_line

    # Ensure that we are looking in the right places for packages. This
    # is required in CI because pip installs in an area that is not in
    # the search path.
    venv_site_lib = str(Path(virtualenv_manager.bin_path, "..", "lib").resolve())
    venv_site_packages = str(
        Path(
            venv_site_lib,
            f"python{sys.version_info.major}.{sys.version_info.minor}",
            "site-packages",
        )
    )
    if venv_site_packages not in sys.path and ON_TRY:
        sys.path.insert(0, venv_site_packages)

    req = install_req_from_line(package)
    req.check_if_exists(use_user_site=False)
    # already installed, check if it's in our venv
    if req.satisfied_by is not None:
        site_packages = os.path.abspath(req.satisfied_by.location)
        if site_packages.startswith(venv_site_lib):
            # already installed in this venv, we can skip
            return True
    with silence():
        try:
            subprocess.check_call([
                virtualenv_manager.python_path,
                "-m",
                "pip",
                "install",
                package,
            ])
            return True
        except Exception:
            if not ignore_failure:
                raise
    return False


def install_requirements_file(
    virtualenv_manager, requirements_file, ignore_failure=False
):
    """Installs a package using the virtualenv manager.

    Makes sure the package is really installed when the user already has it
    in their local installation.

    Returns True on success, or re-raise the error. If ignore_failure
    is set to True, ignore the error and return False
    """

    # Ensure that we are looking in the right places for packages. This
    # is required in CI because pip installs in an area that is not in
    # the search path.
    venv_site_lib = str(Path(virtualenv_manager.bin_path, "..", "lib").resolve())
    venv_site_packages = Path(
        venv_site_lib,
        f"python{sys.version_info.major}.{sys.version_info.minor}",
        "site-packages",
    )
    if not venv_site_packages.exists():
        venv_site_packages = Path(
            venv_site_lib,
            "site-packages",
        )

    venv_site_packages = str(venv_site_packages)
    if venv_site_packages not in sys.path and ON_TRY:
        sys.path.insert(0, venv_site_packages)

    with silence():
        cwd = os.getcwd()
        try:
            os.chdir(Path(requirements_file).parent)
            subprocess.check_call([
                virtualenv_manager.python_path,
                "-m",
                "pip",
                "install",
                "-r",
                requirements_file,
            ])
            return True
        except Exception:
            if not ignore_failure:
                raise
        finally:
            os.chdir(cwd)
    return False


# on try, we create tests packages where tests, like
# xpcshell tests, don't have the same path.
# see - python/mozbuild/mozbuild/action/test_archive.py
# this mapping will map paths when running there.
# The key is the source path, and the value the list of candidate ci paths,
# tried in order. A single source directory can hold tests of several flavors,
# which end up in different subdirectories of the test package.
_TRY_MAPPING = {
    Path("accessible"): [Path("mochitest", "browser", "accessible")],
    Path("browser"): [Path("mochitest", "browser", "browser")],
    Path("netwerk"): [Path("xpcshell", "tests", "netwerk")],
    Path("dom"): [
        Path("mochitest", "tests", "dom"),
        Path("mochitest", "browser", "dom"),
    ],
    Path("toolkit"): [Path("mochitest", "browser", "toolkit")],
}


def build_test_list(tests):
    """Collects tests given a list of directories, files and URLs.

    Returns a tuple containing the list of tests found and a temp dir for tests
    that were downloaded from an URL.
    """
    temp_dir = None

    if isinstance(tests, str):
        tests = [tests]
    res = []
    for test in tests:
        if test.isdigit():
            res.append(str(test))
            continue

        if test.startswith("http"):
            if temp_dir is None:
                temp_dir = tempfile.mkdtemp()
            target = Path(temp_dir, test.split("/")[-1])
            download_file(test, target)
            res.append(str(target))
            continue

        p_test = Path(test)
        if ON_TRY and not p_test.resolve().exists():
            # until we have pathlib.Path.is_relative_to() (3.9)
            for src_path, ci_paths in _TRY_MAPPING.items():
                src_path = str(src_path)
                if not test.startswith(src_path):
                    continue
                candidates = [
                    Path(test.replace(src_path, str(ci_path), 1))
                    for ci_path in ci_paths
                ]
                p_test = next(
                    (c for c in candidates if c.resolve().exists()), candidates[0]
                )
                break

        resolved_test = p_test.resolve()

        if resolved_test.is_file():
            res.append(str(resolved_test))
        elif resolved_test.is_dir():
            for file in resolved_test.rglob("perftest_*.js"):
                res.append(str(file))
            for file in resolved_test.rglob("eval_*.js"):
                res.append(str(file))
        else:
            raise FileNotFoundError(str(resolved_test))
    res.sort()
    return res, temp_dir


def download_file(url, target, retry_sleep=RETRY_SLEEP, attempts=3):
    """Downloads a file, given an URL in the target path.

    The function will attempt several times on failures.
    """

    def _download_file(url, target):
        req = requests.get(url, stream=True, timeout=30)
        target_dir = target.parent.resolve()
        if str(target_dir) != "":
            target_dir.mkdir(exist_ok=True)

        with target.open("wb") as f:
            for chunk in req.iter_content(chunk_size=1024):
                if not chunk:
                    continue
                f.write(chunk)
                f.flush()
        return target

    return retry(
        _download_file,
        args=(url, target),
        attempts=attempts,
        sleeptime=retry_sleep,
        jitter=0,
    )


@contextlib.contextmanager
def temporary_env(**env):
    old = {}
    for key, value in env.items():
        old[key] = os.environ.get(key)
        if value is None and key in os.environ:
            del os.environ[key]
        elif value is not None:
            os.environ[key] = value
    try:
        yield
    finally:
        for key, value in old.items():
            if value is None and key in os.environ:
                del os.environ[key]
            elif value is not None:
                os.environ[key] = value


def convert_day(day):
    if day in ("yesterday", "today"):
        curr = date.today()
        if day == "yesterday":
            curr = curr - timedelta(1)
        day = curr.strftime("%Y.%m.%d")
    else:
        # verify that the user provided string is in the expected format
        # if it can't parse it, it'll raise a value error
        datetime.strptime(day, "%Y.%m.%d")

    return day


def get_revision_namespace_url(route, day="yesterday"):
    """Builds a URL to obtain all the namespaces of a given build route for a single day."""
    day = convert_day(day)
    return f"""{MULTI_REVISION_ROOT}/{route}.{day}.revision"""


def get_multi_tasks_url(route, revision, day="yesterday"):
    """Builds a URL to obtain all the tasks of a given build route for a single day.

    If previous is true, then we get builds from the previous day,
    otherwise, we look at the current day.
    """
    day = convert_day(day)
    return f"""{MULTI_TASK_ROOT}/{route}.{day}.revision.{revision}"""


def strtobool(val):
    if isinstance(val, (bool, int)):
        return bool(val)
    if not isinstance(bool, str):
        raise ValueError(val)
    val = val.lower()
    if val in ("y", "yes", "t", "true", "on", "1"):
        return 1
    elif val in ("n", "no", "f", "false", "off", "0"):
        return 0
    else:
        raise ValueError(f"invalid truth value {val!r}")


@contextlib.contextmanager
def temp_dir():
    tempdir = tempfile.mkdtemp()
    try:
        yield tempdir
    finally:
        shutil.rmtree(tempdir)


def load_class(path):
    """Loads a class given its path and returns it.

    The path is a string of the form `package.module:class` that points
    to the class to be imported.

    If if can't find it, or if the path is malformed,
    an ImportError is raised.
    """
    if ":" not in path:
        raise ImportError(f"Malformed path '{path}'")
    elmts = path.split(":")
    if len(elmts) != 2:
        raise ImportError(f"Malformed path '{path}'")
    mod_name, klass_name = elmts
    try:
        mod = importlib.import_module(mod_name)
    except ModuleNotFoundError:
        raise ImportError(f"Can't find '{mod_name}'")
    try:
        klass = getattr(mod, klass_name)
    except AttributeError:
        raise ImportError(f"Can't find '{klass_name}' in '{mod_name}'")
    return klass


def load_class_from_path(klass_name, path):
    """This function returns a Transformer class with the given path.

    :param str path: The path points to the custom transformer.
    :param bool ret_members: If true then return inspect.getmembers().
    :return Transformer if not ret_members else inspect.getmembers().
    """
    file = pathlib.Path(path)

    if not file.exists():
        raise ImportError(f"The class path {path} does not exist.")

    # Importing a source file directly
    spec = importlib.util.spec_from_file_location(name=file.name, location=path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    members = inspect.getmembers(
        module,
        lambda c: inspect.isclass(c) and c.__name__ == klass_name,
    )

    if not members:
        raise ImportError(f"The path {path} was found but it was not a valid class.")

    return members[0][-1]


def run_script(cmd, cmd_args=None, verbose=False, display=False, label=None):
    """Used to run a command in a subprocess."""
    if isinstance(cmd, str):
        cmd = shlex.split(cmd)
    try:
        joiner = shlex.join
    except AttributeError:
        # Python < 3.8
        joiner = subprocess.list2cmdline

    if label is None:
        label = joiner(cmd)
    sys.stdout.write(f"=> {label} ")
    if cmd_args is None:
        args = cmd
    else:
        args = cmd + list(cmd_args)
    sys.stdout.flush()
    try:
        if verbose:
            sys.stdout.write(f"\nRunning {' '.join(args)}\n")
            sys.stdout.flush()
        output = subprocess.check_output(args)
        if display:
            sys.stdout.write("\n")
            for line in output.split(b"\n"):
                sys.stdout.write(line.decode("utf8") + "\n")
        sys.stdout.write("[OK]\n")
        sys.stdout.flush()
        return True, output
    except subprocess.CalledProcessError as e:
        for line in e.output.split(b"\n"):
            sys.stdout.write(line.decode("utf8") + "\n")
        sys.stdout.write("[FAILED]\n")
        sys.stdout.flush()
        return False, e


def run_python_script(
    virtualenv_manager,
    module,
    module_args=None,
    verbose=False,
    display=False,
    label=None,
):
    """Used to run a Python script in isolation."""
    if label is None:
        label = module
    cmd = [virtualenv_manager.python_path, "-m", module]
    return run_script(cmd, module_args, verbose=verbose, display=display, label=label)


def checkout_script(cmd, cmd_args=None, verbose=False, display=False, label=None):
    return run_script(cmd, cmd_args, verbose, display, label)[0]


def checkout_python_script(
    virtualenv_manager,
    module,
    module_args=None,
    verbose=False,
    display=False,
    label=None,
):
    return run_python_script(
        virtualenv_manager, module, module_args, verbose, display, label
    )[0]


_URL = (
    "{0}/secrets/v1/secret/project"
    "{1}releng{1}gecko{1}build{1}level-{2}{1}conditioned-profiles"
)
_DEFAULT_SERVER = "https://firefox-ci-tc.services.mozilla.com"


@functools.cache
def get_tc_secret():
    """Returns the Taskcluster secret.

    Raises an OSError when not running on try
    """
    if not ON_TRY:
        raise OSError("Not running in Taskcluster")
    session = requests.Session()
    retry = Retry(total=5, backoff_factor=0.1, status_forcelist=[500, 502, 503, 504])
    http_adapter = requests.adapters.HTTPAdapter(max_retries=retry)
    session.mount("https://", http_adapter)
    session.mount("http://", http_adapter)
    secrets_url = _URL.format(
        os.environ.get("TASKCLUSTER_PROXY_URL", _DEFAULT_SERVER),
        "%2F",
        os.environ.get("MOZ_SCM_LEVEL", "1"),
    )
    res = session.get(secrets_url, timeout=DOWNLOAD_TIMEOUT)
    res.raise_for_status()
    return res.json()["secret"]


def get_output_dir(output, folder=None):
    if output is None:
        raise Exception("Output path was not provided.")

    result_dir = Path(output)
    if folder is not None:
        result_dir = Path(result_dir, folder)

    result_dir.mkdir(parents=True, exist_ok=True)
    result_dir = result_dir.resolve()

    return result_dir


def create_path(path):
    if path.exists():
        return path
    else:
        create_path(path.parent)
        path.mkdir(exist_ok=True)
        return path


def get_pretty_app_name(app):
    # XXX See bug 1712337, we need a singluar point of entry
    # for the binary to allow us to get the version/app info
    # so that we can get a pretty name on desktop.
    return PRETTY_APP_NAMES[app]


def archive_folder(folder_to_archive, output_path, archive_name=None):
    """Archives the specified folder into a tar.gz file."""
    if not archive_name:
        archive_name = folder_to_archive.name

    full_archive_path = output_path / (archive_name + ".tgz")
    with tarfile.open(str(full_archive_path), "w:gz") as tar:
        tar.add(folder_to_archive, arcname=archive_name)

    return full_archive_path


def archive_files(
    files, output_dir, archive_name, prefix="", sort_key=None, base_dir=None
):
    """Archives individual files into a zip file, with optional append mode.

    Args:
        files: List of Path objects to archive
        output_dir: Path object - directory where the archive should be created
        archive_name: Name for the archive
        prefix: Optional subdirectory within the archive for files (ignored if base_dir is set)
        sort_key: Optional sorting key function for ordering files
        base_dir: Optional base directory to compute relative paths from

    Returns:
        Path to the archive if created/updated, None otherwise
    """
    output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = output_dir / f"{archive_name}.zip"

    mode = "a" if archive_path.exists() else "w"

    sorted_files = sorted(files, key=sort_key) if sort_key else files

    with zipfile.ZipFile(archive_path, mode, compression=zipfile.ZIP_DEFLATED) as zf:
        for file_path in sorted_files:
            if base_dir:
                archive_name = str(file_path.relative_to(base_dir))
            elif prefix:
                archive_name = f"{prefix}/{file_path.name}"
            else:
                archive_name = file_path.name

            print(f"Adding {archive_name} to archive")
            zf.write(file_path, arcname=archive_name)

    return archive_path


def extract_tgz_and_find_files(output_dir, tgz_name, patterns):
    """Extract TGZ file if on CI and find files matching patterns.

    Args:
        output_dir: Path object - directory where files are located or where TGZ should be extracted
        tgz_name: Name of the TGZ file (without extension)
        patterns: List of patterns for file extensions (e.g., ["*.data", "*.json.gz"])

    Returns:
        Tuple of (files, search_dir, work_dir) where:
            - files: list of found file paths
            - search_dir: base directory where files were found (for relative path computation)
            - work_dir: temp directory to clean up (or None if not on CI)
    """
    work_dir = None
    search_dir = output_dir

    if ON_TRY:
        tgz_path = output_dir / f"{tgz_name}.tgz"
        if tgz_path.exists():
            work_dir = Path(tempfile.mkdtemp())
            with tarfile.open(tgz_path, "r:gz") as tar:
                tar.extractall(path=work_dir)
            search_dir = work_dir

    found_files = []
    for pattern in patterns:
        found_files.extend(search_dir.rglob(pattern))

    valid_files = [f for f in found_files if f.is_file()]

    return (valid_files, search_dir, work_dir)


def get_adb_device_or_emu(verbose=False):
    from mozdevice import ADBDeviceFactory, ADBError

    try:
        return ADBDeviceFactory(verbose=verbose)
    except Exception as e:
        if "No ready devices found." not in str(e):
            raise e
        from mozbuild.base import MozbuildObject
        from mozrunner.devices.android_device import AndroidEmulator

        try:
            build_obj = MozbuildObject.from_environment()
            substs = build_obj.substs
        except Exception:
            substs = None
        emulator = AndroidEmulator("*", substs=substs, verbose=True)
        if emulator.is_available():
            try:
                response = input(
                    "No Android devices connected. Start an emulator? (Y/n) "
                ).strip()
            except EOFError:
                raise EOFError("Could not get input on non-interective output")
            if response.lower().startswith("y") or response == "":
                if not emulator.check_avd():
                    print("Android AVD not found, please run |mach bootstrap|")
                    raise ADBError("No ready devices found.")
                print(f"Starting emulator running {emulator.get_avd_description()}...")
                emulator.start()
                emulator.wait_for_start()
                return ADBDeviceFactory(verbose=True)
            else:
                raise ADBError("No emulator started and android device not found")


def _android_sdk_root():
    """Return the fetched Android SDK root, or None if it is not set."""
    return os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")


def _prepend_to_path(directory):
    """Prepend an existing directory to the PATH environment variable, once."""
    if not os.path.isdir(directory):
        return
    entries = os.environ.get("PATH", "").split(os.pathsep)
    if directory not in entries:
        os.environ["PATH"] = os.pathsep.join([directory, *entries])


def get_adb_path():
    """Return the path to the adb executable.

    Prefer the SDK pointed to by ANDROID_SDK_ROOT/ANDROID_HOME, so adb is found
    in automation where it is not on PATH; fall back to ``adb`` otherwise.
    """
    sdk_root = _android_sdk_root()
    if sdk_root:
        adb_path = os.path.join(sdk_root, "platform-tools", "adb")
        if os.path.exists(adb_path):
            return adb_path
    return "adb"


def ensure_adb_on_path():
    """Put the fetched SDK's platform-tools on PATH so ``adb`` resolves by name.

    Tests and tools spawned as subprocesses invoke ``adb`` directly; in
    automation the SDK is fetched but not on PATH.
    """
    sdk_root = _android_sdk_root()
    if sdk_root:
        _prepend_to_path(os.path.join(sdk_root, "platform-tools"))


def ensure_profgen_on_path():
    """Put the fetched SDK's cmdline-tools bin on PATH so ``profgen`` resolves.

    Installing a Fenix APK with its baseline profile shells out to ``profgen``,
    which ships in a versioned ``cmdline-tools/<version>/bin`` directory of the
    SDK rather than on PATH.
    """
    sdk_root = _android_sdk_root()
    if not sdk_root:
        return
    for bin_dir in glob.glob(os.path.join(sdk_root, "cmdline-tools", "*", "bin")):
        _prepend_to_path(bin_dir)


def ensure_java_on_path():
    """Set JAVA_HOME and put its bin on PATH from the fetched JDK.

    ``profgen`` (used to extract Fenix baseline profiles) is a Java tool, so it
    needs a JRE. In automation the JDK is fetched to ``MOZ_FETCHES_DIR/jdk`` but
    is not on PATH and JAVA_HOME is unset. The macOS JDK nests the runtime under
    ``<version>/Contents/Home``; other platforms use ``<version>`` directly.
    """
    fetches_dir = os.environ.get("MOZ_FETCHES_DIR")
    if not fetches_dir:
        return
    candidates = glob.glob(
        os.path.join(fetches_dir, "jdk", "*", "Contents", "Home")
    ) + glob.glob(os.path.join(fetches_dir, "jdk", "*"))
    for java_home in candidates:
        if os.path.isfile(os.path.join(java_home, "bin", "java")):
            os.environ["JAVA_HOME"] = java_home
            _prepend_to_path(os.path.join(java_home, "bin"))
            return


def start_test_emulator(verbose=False):
    """Start the Mozilla test emulator when no Android device is connected.

    Intended as a fallback when connecting to a device fails. The emulator is
    started unattended in automation (with software rendering, which headless
    CI workers require) or after a prompt when run interactively.

    Returns True if an emulator was started, False if none was (e.g. the
    emulator or AVD is unavailable, or the user declined the prompt).
    """
    from mozdevice import ADBError
    from mozrunner.devices.android_device import AndroidEmulator

    emulator = None
    for avd_type in ("arm64", "x86_64", "arm"):
        candidate = AndroidEmulator(avd_type, verbose=verbose)
        if candidate.is_available() and candidate.check_avd():
            emulator = candidate
            break
    if emulator is None:
        return False

    interactive = not ON_TRY and sys.stdin is not None and sys.stdin.isatty()
    if interactive:
        response = input(
            "No Android devices connected. Start an emulator? (Y/n) "
        ).strip()
        if response and not response.lower().startswith("y"):
            return False
    else:
        # Use host GPU rendering: the macOS perf workers have
        # a window server, and swiftshader_indirect produces a corrupt guest
        # framebuffer that shows up as static in the screen recordings.
        # These mirror emulator_extra_args in raptor's
        # android_emulator_macosx_config.py; keep them in sync except -gpu, which
        # differs by design (mozperftest records the guest framebuffer, raptor
        # uses the Firefox window recorder).
        os.environ.setdefault(
            "MOZ_EMULATOR_COMMAND_ARGS",
            "-gpu host -skip-adb-auth -verbose -show-kernel "
            "-ranchu -selinux permissive -memory 3072 -cores 4 -skin 800x1280 "
            "-no-snapstorage -no-snapshot -prop ro.test_harness=true",
        )

    print(f"Starting emulator running {emulator.get_avd_description()}...")
    emulator.start()
    if emulator.wait_for_start() is False:
        raise ADBError("The Android emulator failed to start.")
    return True


def ensure_android_device(verbose=False):
    """Make a usable Android device available, starting the emulator if needed.

    Centralizes Android device readiness for mozperftest: it makes adb reachable
    (see :func:`ensure_adb_on_path`) and, when no device is connected, starts the
    Mozilla test emulator, unattended in automation or after a prompt
    interactively (see :func:`start_test_emulator`). It does not open a
    connection; the AndroidDevice layer owns the (logged) device connection.

    :raises ADBError: when no device is connected and no emulator could be
        started.
    """
    from mozdevice import ADBError, ADBHost

    ensure_adb_on_path()
    adb_path = get_adb_path()

    adbhost = ADBHost(adb=adb_path, verbose=verbose)
    ready_devices = [d for d in adbhost.devices() if d.get("state") == "device"]
    if not ready_devices and not start_test_emulator(verbose=verbose):
        raise ADBError("No Android device connected and no emulator could be started.")
