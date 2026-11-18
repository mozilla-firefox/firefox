# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
from argparse import Namespace
from functools import partial

from mach.decorators import Command

here = os.path.abspath(os.path.dirname(__file__))
parser = None
logger = None


def run_test(context, **kwargs):
    from mochitest_options import ALL_FLAVORS
    from mozlog.commandline import setup_logging

    if not kwargs.get("log"):
        kwargs["log"] = setup_logging("mochitest", kwargs, {"mach": sys.stdout})
    global logger
    logger = kwargs["log"]

    flavor = kwargs.get("flavor") or "mochitest"
    if flavor not in ALL_FLAVORS:
        for fname, fobj in ALL_FLAVORS.items():
            if flavor in fobj["aliases"]:
                flavor = fname
                break
    fobj = ALL_FLAVORS[flavor]
    kwargs.update(fobj.get("extra_args", {}))

    if kwargs.get("allow_software_gl_layers"):
        os.environ["MOZ_LAYERS_ALLOW_SOFTWARE_GL"] = "1"
        del kwargs["allow_software_gl_layers"]
    if kwargs.get("mochitest_suite"):
        suite = kwargs["mochitest_suite"]
        del kwargs["mochitest_suite"]
    elif kwargs.get("test_suite"):
        suite = kwargs["test_suite"]
        del kwargs["test_suite"]
    if kwargs.get("no_run_tests"):
        del kwargs["no_run_tests"]

    args = Namespace(**kwargs)
    args.e10s = context.mozharness_config.get("e10s", args.e10s)
    args.certPath = context.certs_dir

    # subsuite mapping from mozharness configs
    subsuites = {
        # --mochitest-suite -> --subsuite
        "mochitest-chrome-gpu": "gpu",
        "mochitest-plain-gpu": "gpu",
        "mochitest-media": "media",
        "mochitest-browser-screenshots": "screenshots",
        "mochitest-webgl1-core": "webgl1-core",
        "mochitest-webgl1-ext": "webgl1-ext",
        "mochitest-webgl2-core": "webgl2-core",
        "mochitest-webgl2-ext": "webgl2-ext",
        "mochitest-webgl2-deqp": "webgl2-deqp",
        "mochitest-webgpu": "webgpu",
        "mochitest-devtools-chrome": "devtools",
        "mochitest-browser-a11y": "a11y",
        "mochitest-remote": "remote",
        "mochitest-browser-media": "media-bc",
        "mochitest-browser-chrome-ml-models": "ml-models",
    }
    args.subsuite = subsuites.get(suite)
    if args.subsuite == "devtools":
        args.flavor = "browser"
    if args.subsuite == "a11y":
        args.flavor = "browser"
    if args.subsuite == "media-bc":
        args.flavor = "browser"
    if args.subsuite == "ml-models":
        args.flavor = "browser"

    if not args.test_paths:
        mh_test_paths = json.loads(os.environ.get("MOZHARNESS_TEST_PATHS", '""'))
        if mh_test_paths:
            logger.info("Found MOZHARNESS_TEST_PATHS:")
            logger.info(str(mh_test_paths))
            args.test_paths = []
            for k in mh_test_paths:
                args.test_paths.extend(mh_test_paths[k])
    if args.test_paths:
        install_subdir = fobj.get("install_subdir", fobj["suite"])
        test_root = os.path.join(context.package_root, "mochitest", install_subdir)
        normalize = partial(context.normalize_test_path, test_root)
        # pylint --py3k: W1636
        args.test_paths = list(map(normalize, args.test_paths))

    import mozinfo

    if mozinfo.info.get("buildapp") == "mobile/android":
        return run_mochitest_android(context, args)
    return run_mochitest_desktop(context, args)


def run_mochitest_desktop(context, args):
    args.app = args.app or context.firefox_bin
    args.utilityPath = context.bin_dir
    args.extraProfileFiles.append(os.path.join(context.bin_dir, "plugins"))
    args.extraPrefs.append("webgl.force-enabled=true")
    args.quiet = True
    args.useTestMediaDevices = True
    args.screenshotOnFail = True
    args.cleanupCrashes = True
    args.marionette_startup_timeout = "180"
    args.sandboxReadWhitelist.append(context.mozharness_workdir)

    from runtests import run_test_harness

    logger.info("mach calling runtests with args: " + str(args))
    return run_test_harness(parser, args)


def set_android_args(context, args):
    args.app = args.app or "org.mozilla.geckoview.test_runner"
    args.utilityPath = context.hostutils
    args.xrePath = context.hostutils
    config = context.mozharness_config
    if config:
        host = os.environ.get("HOST_IP", "10.0.2.2")
        args.remoteWebServer = config.get("remote_webserver", host)
        args.httpPort = config.get("http_port", 8854)
        args.sslPort = config.get("ssl_port", 4454)
        args.adbPath = config["exes"]["adb"] % {
            "abs_work_dir": context.mozharness_workdir
        }
        args.deviceSerial = (
            os.environ.get("ANDROID_SERIAL")
            or os.environ.get("DEVICE_SERIAL")
            or "emulator-5554"
        )
    return args


def run_mochitest_android(context, args):
    args = set_android_args(context, args)
    args.extraProfileFiles.append(
        os.path.join(context.package_root, "mochitest", "fonts")
    )

    from runtestsremote import run_test_harness

    logger.info("mach calling runtestsremote with args: " + str(args))
    return run_test_harness(parser, args)


def add_global_arguments(parser):
    parser.add_argument("--test-suite")
    parser.add_argument("--mochitest-suite")
    parser.add_argument("--download-symbols")
    parser.add_argument("--allow-software-gl-layers", action="store_true")
    parser.add_argument("--no-run-tests", action="store_true")


def setup_mochitest_argument_parser():
    import mozinfo

    mozinfo.find_and_update_from_json(here)
    app = "generic"
    if mozinfo.info.get("buildapp") == "mobile/android":
        app = "android"

    from mochitest_options import MochitestArgumentParser

    global parser
    parser = MochitestArgumentParser(app=app)
    add_global_arguments(parser)
    return parser


@Command(
    "mochitest",
    category="testing",
    description="Run the mochitest harness.",
    parser=setup_mochitest_argument_parser,
)
def mochitest(command_context, **kwargs):
    command_context._mach_context.activate_mozharness_venv()
    return run_test(command_context._mach_context, **kwargs)
