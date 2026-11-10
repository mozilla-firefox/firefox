import contextlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest.mock import MagicMock

from mozperftest import utils
from mozperftest.environment import MachEnvironment
from mozperftest.hooks import Hooks
from mozperftest.metadata import Metadata
from mozperftest.script import ScriptInfo

HERE = Path(__file__).parent
ROOT = Path(HERE, "..", "..", "..", "..").resolve()
EXAMPLE_TESTS_DIR = os.path.join(HERE, "data", "samples")
EXAMPLE_TEST = os.path.join(EXAMPLE_TESTS_DIR, "perftest_example.js")
EXAMPLE_DYNAMIC_TEST = os.path.join(EXAMPLE_TESTS_DIR, "perftest_example_dynamic.js")
EXAMPLE_XPCSHELL_TEST = Path(EXAMPLE_TESTS_DIR, "test_xpcshell.js")
EXAMPLE_XPCSHELL_TEST2 = Path(EXAMPLE_TESTS_DIR, "test_xpcshell_flavor2.js")
EXAMPLE_XPCSHELL_DESTRUCTURED_TEST = Path(
    EXAMPLE_TESTS_DIR, "test_xpcshell_destructured.js"
)
EXAMPLE_MOCHITEST_TEST = Path(EXAMPLE_TESTS_DIR, "test_mochitest.html")
EXAMPLE_MOCHITEST_TEST2 = Path(EXAMPLE_TESTS_DIR, "test_mochitest_multiscript.html")
EXAMPLE_SHELL_TEST = HERE / "data" / "samples" / "perftest_custom.sh"
BT_DATA = Path(HERE, "data", "browsertime-results", "browsertime.json")
BT_DATA_VIDEO = Path(HERE, "data", "browsertime-results-video", "browsertime.json")
DMG = Path(HERE, "data", "firefox.dmg")
MOZINFO = Path(HERE, "data", "mozinfo.json")
MOCHITEST_DATA = Path(
    HERE, "data", "perfherder-metrics-samples", "mochitest-metrics-sample.json"
)


@contextlib.contextmanager
def temp_file(name="temp", content=None):
    tempdir = tempfile.mkdtemp()
    path = os.path.join(tempdir, name)
    if content is not None:
        with open(path, "w") as f:
            f.write(content)
    try:
        yield path
    finally:
        shutil.rmtree(tempdir)


def get_running_env(**kwargs):
    from mozbuild.base import MozbuildObject

    config = MozbuildObject.from_environment()
    mach_cmd = MagicMock()

    def get_binary_path(*args):
        return ""

    def run_pip(args):
        subprocess.check_call(
            [sys.executable, "-m", "pip"] + args,
            stderr=subprocess.STDOUT,
            cwd=config.topsrcdir,
            universal_newlines=True,
        )

    mach_cmd.get_binary_path = get_binary_path
    mach_cmd.topsrcdir = config.topsrcdir
    mach_cmd.topobjdir = config.topobjdir
    mach_cmd.distdir = config.distdir
    mach_cmd.bindir = config.bindir
    mach_cmd._mach_context = MagicMock()
    mach_cmd._mach_context.state_dir = tempfile.mkdtemp()
    mach_cmd.run_process.return_value = 0
    mach_cmd.virtualenv_manager = MagicMock()
    mach_cmd.virtualenv_manager.python_path = sys.executable
    mach_cmd.virtualenv_manager.bin_path = Path(sys.executable).parent
    mach_cmd.virtualenv_manager._run_pip = run_pip

    mach_args = {
        "flavor": "desktop-browser",
        "test_objects": None,
        "resolve_tests": True,
        "browsertime-clobber": False,
        "browsertime-install-url": None,
    }
    mach_args.update(kwargs)
    hooks = Hooks(mach_cmd, mach_args.pop("hooks", None))
    tests = mach_args.get("tests", [])
    if len(tests) > 0:
        script = ScriptInfo(tests[0])
    else:
        script = None
    env = MachEnvironment(mach_cmd, hooks=hooks, **mach_args)
    metadata = Metadata(mach_cmd, env, "desktop-browser", script)
    return mach_cmd, metadata, env


def requests_content(chunks=None):
    if chunks is None:
        chunks = [b"some ", b"content"]

    def _content(*args, **kw):
        class Resp:
            def iter_content(self, **kw):
                yield from chunks

        return Resp()

    return _content


@contextlib.contextmanager
def running_on_try(on_try=True):
    old = utils.ON_TRY
    utils.ON_TRY = on_try
    try:
        if on_try:
            with utils.temporary_env(MOZ_AUTOMATION="1"):
                yield
        else:
            yield
    finally:
        utils.ON_TRY = old


class FakeDevice:
    def __init__(self, **args):
        self.apps = []
        self._logger = MagicMock()
        self._have_su = True
        self._have_android_su = True
        self._have_root_shell = True
        self.is_rooted = True

    def clear_logcat(self, *args, **kwargs):
        return True

    def shell_output(self, *args, **kwargs):
        return "A Fake Device"

    def shell_bool(self, *args, **kwargs):
        return True

    def uninstall_app(self, apk_name):
        return True

    def install_app(self, apk, replace=True):
        if apk not in self.apps:
            self.apps.append(apk)

    def install_app_baseline_profile(self, apk, replace=True):
        if apk not in self.apps:
            self.apps.append(apk)

    def is_app_installed(self, app_name):
        return True
