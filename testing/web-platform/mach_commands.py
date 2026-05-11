# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Integrates the web-platform-tests test runner with mach.

import os
import sys

from mach.decorators import Command
from mach_commands_base import (
    WebPlatformTestsRunner,
    create_parser_wpt,
    setup_environment,
)
from mozbuild.base import MachCommandConditions as conditions
from mozbuild.base import MozbuildObject

here = os.path.abspath(os.path.dirname(__file__))
INTEROP_REQUIREMENTS_PATH = os.path.join(here, "interop_requirements.txt")


class WebPlatformTestsRunnerSetup(MozbuildObject):
    default_log_type = "mach"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._here = os.path.join(self.topsrcdir, "testing", "web-platform")
        kwargs["tests_root"] = os.path.join(self._here, "tests")
        setup_environment(kwargs["tests_root"])
        build_path = os.path.join(self.topobjdir, "build")
        if build_path not in sys.path:
            sys.path.append(build_path)

    def kwargs_common(self, kwargs):
        """Setup kwargs relevant for all browser products"""

        tests_src_path = os.path.join(self._here, "tests")

        if kwargs["product"] in {"firefox", "firefox_android"}:
            if kwargs["specialpowers_path"] is None:
                kwargs["specialpowers_path"] = os.path.join(
                    self.distdir, "xpi-stage", "specialpowers@mozilla.org.xpi"
                )

        if kwargs["config"] is None:
            kwargs["config"] = os.path.join(
                self.topobjdir, "_tests", "web-platform", "wptrunner.local.ini"
            )

        if (
            kwargs["exclude"] is None
            and kwargs["include"] is None
            and not sys.platform.startswith("linux")
        ):
            kwargs["exclude"] = ["css"]

        if kwargs["ssl_type"] in (None, "pregenerated"):
            cert_root = os.path.join(tests_src_path, "tools", "certs")
            if kwargs["ca_cert_path"] is None:
                kwargs["ca_cert_path"] = os.path.join(cert_root, "cacert.pem")

            if kwargs["host_key_path"] is None:
                kwargs["host_key_path"] = os.path.join(
                    cert_root, "web-platform.test.key"
                )

            if kwargs["host_cert_path"] is None:
                kwargs["host_cert_path"] = os.path.join(
                    cert_root, "web-platform.test.pem"
                )

        if kwargs["reftest_screenshot"] is None:
            kwargs["reftest_screenshot"] = "fail"

        kwargs["capture_stdio"] = True

        return kwargs

    def kwargs_firefox(self, kwargs):
        """Setup kwargs specific to running Firefox and other gecko browsers"""
        from wptrunner import wptcommandline

        kwargs = self.kwargs_common(kwargs)

        if kwargs["binary"] is None:
            kwargs["binary"] = self.get_binary_path()

        if kwargs["webdriver_binary"] is None:
            kwargs["webdriver_binary"] = self.find_webdriver_binary()

        if kwargs["certutil_binary"] is None:
            kwargs["certutil_binary"] = self.get_binary_path("certutil")

        if kwargs["install_fonts"] is None:
            kwargs["install_fonts"] = False

        if kwargs["preload_browser"] is None:
            kwargs["preload_browser"] = False

        if kwargs["prefs_root"] is None:
            kwargs["prefs_root"] = os.path.join(self.topsrcdir, "testing", "profiles")

        if kwargs["stackfix_dir"] is None:
            kwargs["stackfix_dir"] = self.bindir

        if kwargs["symbols_path"] is None:
            kwargs["symbols_path"] = os.path.join(self.distdir, "crashreporter-symbols")

        kwargs["gmp_path"] = os.pathsep.join(
            os.path.join(self.distdir, "bin", p, "1.0")
            for p in ("gmp-fake", "gmp-fakeopenh264")
        )

        kwargs = wptcommandline.check_args(kwargs)

        return kwargs

    def kwargs_firefox_android(self, kwargs):
        from wptrunner import wptcommandline

        kwargs = self.kwargs_common(kwargs)

        # package_name may be different in the future
        package_name = kwargs["package_name"]
        if not package_name:
            kwargs["package_name"] = package_name = "org.mozilla.geckoview.test_runner"

        # Note that this import may fail in non-firefox-for-android trees
        from mozrunner.devices.android_device import (
            InstallIntent,
            get_adb_path,
            verify_android_device,
        )

        kwargs["adb_binary"] = get_adb_path(self)
        install = InstallIntent.NO if kwargs.pop("no_install") else InstallIntent.YES
        verify_android_device(
            self, install=install, verbose=False, xre=True, app=package_name
        )

        if kwargs["webdriver_binary"] is None:
            kwargs["webdriver_binary"] = self.find_webdriver_binary()

        if kwargs["certutil_binary"] is None:
            kwargs["certutil_binary"] = os.path.join(
                os.environ.get("MOZ_HOST_BIN"), "certutil"
            )

        if kwargs["install_fonts"] is None:
            kwargs["install_fonts"] = False

        if not kwargs["device_serial"]:
            kwargs["device_serial"] = ["emulator-5554"]

        kwargs = wptcommandline.check_args(kwargs)

        return kwargs

    def kwargs_wptrun(self, kwargs):
        """Setup kwargs for wpt-run which is only used for non-gecko browser products"""
        from tools.wpt import run, virtualenv

        kwargs = self.kwargs_common(kwargs)

        # Our existing kwargs corresponds to the wptrunner command line arguments.
        # `wpt run` extends this with some additional arguments that are consumed by
        # the frontend. Copy over the default values of these extra arguments so they
        # are present when we call into that frontend.
        run_parser = run.create_parser()
        run_kwargs = run_parser.parse_args([kwargs["product"], kwargs["test_list"]])

        for key, value in vars(run_kwargs).items():
            if key not in kwargs:
                kwargs[key] = value

        # Install the deps
        # We do this explicitly to avoid calling pip with options that aren't
        # supported in the in-tree version
        wptrunner_path = os.path.join(self._here, "tests", "tools", "wptrunner")
        browser_cls = run.product_setup[kwargs["product"]].browser_cls
        requirements = ["requirements.txt"]
        if (
            hasattr(browser_cls, "requirements")
            and browser_cls.requirements is not None
        ):
            requirements.append(browser_cls.requirements)

        for filename in requirements:
            path = os.path.join(wptrunner_path, filename)
            if os.path.exists(path):
                self.virtualenv_manager.install_pip_requirements(
                    path, require_hashes=False
                )

        venv = virtualenv.Virtualenv(
            self.virtualenv_manager.virtualenv_root, skip_virtualenv_setup=True
        )
        try:
            browser_cls, kwargs = run.setup_wptrunner(venv, **kwargs)
        except run.WptrunError as e:
            print(e, file=sys.stderr)
            sys.exit(1)

        # This is kind of a hack; override the metadata paths so we don't use
        # gecko metadata for non-gecko products
        for url_base, test_root in kwargs["test_paths"].items():
            meta_suffix = url_base.strip("/")
            meta_dir = os.path.join(
                self._here, "products", kwargs["product"].name, meta_suffix
            )
            test_root.metadata_path = meta_dir
            if not os.path.exists(meta_dir):
                os.makedirs(meta_dir)
        return kwargs

    def setup_fonts_firefox(self):
        # Ensure the Ahem font is available
        if not sys.platform.startswith("darwin"):
            font_path = os.path.join(os.path.dirname(self.get_binary_path()), "fonts")
        else:
            font_path = os.path.join(
                os.path.dirname(self.get_binary_path()),
                os.pardir,
                "Resources",
                "res",
                "fonts",
            )
        ahem_src = os.path.join(
            self.topsrcdir, "testing", "web-platform", "tests", "fonts", "Ahem.ttf"
        )
        ahem_dest = os.path.join(font_path, "Ahem.ttf")
        if not os.path.exists(ahem_dest) and os.path.exists(ahem_src):
            with open(ahem_src, "rb") as src, open(ahem_dest, "wb") as dest:
                dest.write(src.read())

    def find_webdriver_binary(self):
        ext = ".exe" if sys.platform in ["win32", "msys", "cygwin"] else ""
        try_paths = [
            self.get_binary_path("geckodriver", validate_exists=False),
            os.path.join(self.topobjdir, "dist", "host", "bin", f"geckodriver{ext}"),
        ]

        for build_type in ["release", "debug"]:
            try_paths.append(
                os.path.join(self.topsrcdir, "target", build_type, f"geckodriver{ext}")
            )
        found_paths = []
        for path in try_paths:
            if os.path.exists(path):
                found_paths.append(path)

        if found_paths:
            # Pick the most recently modified version
            found_paths.sort(key=os.path.getmtime)
            return found_paths[-1]


class WebPlatformTestsServeRunner(MozbuildObject):
    def run(self, **kwargs):
        sys.path.insert(0, os.path.join(here, "tests"))
        sys.path.insert(0, os.path.join(here, "tests", "tools"))
        import logging

        import manifestupdate
        from serve import serve
        from wptrunner import wptcommandline

        logger = logging.getLogger("web-platform-tests")

        src_root = self.topsrcdir
        obj_root = self.topobjdir
        src_wpt_dir = os.path.join(src_root, "testing", "web-platform")

        config_path = manifestupdate.generate_config(
            logger,
            src_root,
            src_wpt_dir,
            os.path.join(obj_root, "_tests", "web-platform"),
            False,
        )

        test_paths = wptcommandline.get_test_paths(
            wptcommandline.config.read(config_path)
        )

        def get_route_builder(*args, **kwargs):
            route_builder = serve.get_route_builder(*args, **kwargs)

            for url_base, paths in test_paths.items():
                if url_base != "/":
                    route_builder.add_mount_point(url_base, paths.tests_path)

            return route_builder

        return 0 if serve.run(route_builder=get_route_builder, **kwargs) else 1


class WebPlatformTestsUpdater(MozbuildObject):
    """Update web platform tests."""

    def setup_logging(self, **kwargs):
        import update

        return update.setup_logging(kwargs, {"mach": sys.stdout})

    def update_manifest(self, logger, **kwargs):
        import manifestupdate

        return manifestupdate.run(
            logger=logger, src_root=self.topsrcdir, obj_root=self.topobjdir, **kwargs
        )

    def run_update(self, logger, **kwargs):
        import update
        from update import updatecommandline

        self.update_manifest(logger, **kwargs)

        if kwargs["config"] is None:
            kwargs["config"] = os.path.join(
                self.topobjdir, "_tests", "web-platform", "wptrunner.local.ini"
            )
        if kwargs["product"] is None:
            kwargs["product"] = "firefox"

        kwargs["store_state"] = False

        kwargs = updatecommandline.check_args(kwargs)

        try:
            update.run_update(logger, **kwargs)
        except Exception:
            import traceback

            traceback.print_exc()


class WebPlatformTestsUnittestRunner(MozbuildObject):
    def run(self, **kwargs):
        import unittestrunner

        return unittestrunner.run(self.topsrcdir, **kwargs)


class WebPlatformTestsTestPathsRunner(MozbuildObject):
    """Update web platform tests."""

    def run(self, **kwargs):
        setup_environment()
        import logging

        import manifestupdate
        from manifest import testpaths
        from wptrunner import wptcommandline

        logger = logging.getLogger("web-platform-tests")

        src_root = self.topsrcdir
        obj_root = self.topobjdir
        src_wpt_dir = os.path.join(src_root, "testing", "web-platform")

        config_path = manifestupdate.generate_config(
            logger,
            src_root,
            src_wpt_dir,
            os.path.join(obj_root, "_tests", "web-platform"),
            False,
        )

        test_paths = wptcommandline.get_test_paths(
            wptcommandline.config.read(config_path)
        )
        results = {}
        for url_base, paths in test_paths.items():
            results.update(
                testpaths.get_paths(
                    path=paths.manifest_path,
                    src_root=src_root,
                    tests_root=paths.tests_path,
                    update=kwargs["update"],
                    rebuild=kwargs["rebuild"],
                    url_base=url_base,
                    cache_root=kwargs["cache_root"],
                    test_ids=kwargs["test_ids"],
                )
            )
        testpaths.write_output(results, kwargs["json"])
        return True


def create_parser_update():
    from update import updatecommandline

    return updatecommandline.create_parser()


def create_parser_manifest_update():
    import manifestupdate

    return manifestupdate.create_parser()


def create_parser_metadata_summary():
    import metasummary

    return metasummary.create_parser()


def create_parser_metadata_merge():
    import metamerge

    return metamerge.get_parser()


def create_parser_serve():
    setup_environment()
    from tools import serve

    return serve.serve.get_parser()


def create_parser_unittest():
    import unittestrunner

    return unittestrunner.get_parser()


def create_parser_fetch_logs():
    import interop

    return interop.get_parser_fetch_logs()


def create_parser_interop_score():
    import interop

    return interop.get_parser_interop_score()


def create_parser_testpaths():
    import argparse

    from mach.util import get_state_dir

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--no-update",
        dest="update",
        action="store_false",
        default=True,
        help="Don't update manifest before continuing",
    )
    parser.add_argument(
        "-r",
        "--rebuild",
        action="store_true",
        default=False,
        help="Force a full rebuild of the manifest.",
    )
    parser.add_argument(
        "--cache-root",
        action="store",
        default=os.path.join(get_state_dir(), "cache", "wpt"),
        help="Path in which to store any caches (default <tests_root>/.wptcache/)",
    )
    parser.add_argument(
        "test_ids", action="store", nargs="+", help="Test ids for which to get paths"
    )
    parser.add_argument(
        "--json", action="store_true", default=False, help="Output as JSON"
    )
    return parser


@Command(
    "web-platform-tests",
    category="testing",
    conditions=[conditions.is_firefox_or_android],
    description="Run web-platform-tests.",
    parser=create_parser_wpt,
    virtualenv_name="wpt",
)
def run_web_platform_tests(command_context, **params):
    if params["product"] is None:
        if conditions.is_android(command_context):
            params["product"] = "firefox_android"
        else:
            params["product"] = "firefox"
    if "test_objects" in params:
        include = []
        test_types = set()
        for item in params["test_objects"]:
            include.append(item["name"])
            test_types.add(item.get("subsuite"))
        if None not in test_types:
            params["test_types"] = list(test_types)
        params["include"] = include
        del params["test_objects"]
        # subsuite coming from `mach test` means something more like `test type`, so remove that argument
        if "subsuite" in params:
            del params["subsuite"]
    if params.get("debugger", None):
        import mozdebug

        if not mozdebug.get_debugger_info(params.get("debugger")):
            sys.exit(1)

    wpt_setup = command_context._spawn(WebPlatformTestsRunnerSetup)
    wpt_setup._mach_context = command_context._mach_context
    wpt_setup._virtualenv_name = command_context._virtualenv_name
    wpt_runner = WebPlatformTestsRunner(wpt_setup)

    logger = wpt_runner.setup_logging(**params)
    # wptrunner already handles setting any log parameter from
    # mach test to the logger, so it's OK to remove that argument now
    if "log" in params:
        del params["log"]

    if (
        conditions.is_android(command_context)
        and params["product"] != "firefox_android"
    ):
        logger.warning("Must specify --product=firefox_android in Android environment.")

    return wpt_runner.run(logger, **params)


@Command(
    "wpt",
    category="testing",
    conditions=[conditions.is_firefox_or_android],
    description="Run web-platform-tests.",
    parser=create_parser_wpt,
    virtualenv_name="wpt",
)
def run_wpt(command_context, **params):
    return run_web_platform_tests(command_context, **params)


@Command(
    "web-platform-tests-update",
    category="testing",
    description="Update web-platform-test metadata.",
    parser=create_parser_update,
    virtualenv_name="wpt",
)
def update_web_platform_tests(command_context, **params):
    wpt_updater = command_context._spawn(WebPlatformTestsUpdater)
    logger = wpt_updater.setup_logging(**params)
    return wpt_updater.run_update(logger, **params)


@Command(
    "wpt-update",
    category="testing",
    description="Update web-platform-test metadata.",
    parser=create_parser_update,
    virtualenv_name="wpt",
)
def update_wpt(command_context, **params):
    return update_web_platform_tests(command_context, **params)


@Command(
    "wpt-manifest-update",
    category="testing",
    description="Update web-platform-test manifests.",
    parser=create_parser_manifest_update,
    virtualenv_name="wpt",
)
def wpt_manifest_update(command_context, **params):
    wpt_setup = command_context._spawn(WebPlatformTestsRunnerSetup)
    wpt_runner = WebPlatformTestsRunner(wpt_setup)
    logger = wpt_runner.setup_logging(**params)
    logger.warning(
        "The wpt manifest is now automatically updated, "
        "so running this command is usually unnecessary"
    )
    return 0 if wpt_runner.update_manifest(logger, **params) else 1


@Command(
    "wpt-serve",
    category="testing",
    description="Run the wpt server",
    parser=create_parser_serve,
    virtualenv_name="wpt",
)
def wpt_serve(command_context, **params):
    import logging

    logger = logging.getLogger("web-platform-tests")
    logger.addHandler(logging.StreamHandler(sys.stdout))
    wpt_serve = command_context._spawn(WebPlatformTestsServeRunner)
    return wpt_serve.run(**params)


@Command(
    "wpt-metadata-summary",
    category="testing",
    description="Create a json summary of the wpt metadata",
    parser=create_parser_metadata_summary,
    virtualenv_name="wpt",
)
def wpt_summary(command_context, **params):
    import metasummary

    wpt_setup = command_context._spawn(WebPlatformTestsRunnerSetup)
    return metasummary.run(wpt_setup.topsrcdir, wpt_setup.topobjdir, **params)


@Command(
    "wpt-metadata-merge",
    category="testing",
    parser=create_parser_metadata_merge,
    virtualenv_name="wpt",
)
def wpt_meta_merge(command_context, **params):
    import metamerge

    if params["dest"] is None:
        params["dest"] = params["current"]
    return metamerge.run(**params)


@Command(
    "wpt-unittest",
    category="testing",
    description="Run the wpt tools and wptrunner unit tests",
    parser=create_parser_unittest,
    virtualenv_name="wpt",
)
def wpt_unittest(command_context, **params):
    runner = command_context._spawn(WebPlatformTestsUnittestRunner)
    return 0 if runner.run(**params) else 1


@Command(
    "wpt-test-paths",
    category="testing",
    description="Get a mapping from test ids to files",
    parser=create_parser_testpaths,
    virtualenv_name="wpt",
)
def wpt_test_paths(command_context, **params):
    runner = command_context._spawn(WebPlatformTestsTestPathsRunner)
    runner.run(**params)
    return 0


@Command(
    "wpt-fetch-logs",
    category="testing",
    description="Fetch wptreport.json logs from taskcluster",
    parser=create_parser_fetch_logs,
    virtualenv_name="wpt-interop",
)
def wpt_fetch_logs(command_context, **params):
    import interop

    interop.fetch_logs(**params)
    return 0


@Command(
    "wpt-interop-score",
    category="testing",
    description="Score a run according to Interop 2023",
    parser=create_parser_interop_score,
    virtualenv_name="wpt-interop",
)
def wpt_interop_score(command_context, **params):
    import interop

    interop.score_runs(**params)
    return 0
