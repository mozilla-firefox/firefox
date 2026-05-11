# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

from mozlog import handlers
from mozlog.structuredlog import log_levels

here = os.path.abspath(os.path.dirname(__file__))


def setup_environment(tests_root=None):
    if tests_root is None:
        tests_root = os.path.join(here, "tests")
    if tests_root not in sys.path:
        sys.path.insert(0, tests_root)
        from tools import localpaths  # noqa: F401


def create_parser_wpt():
    setup_environment()

    from wptrunner import wptcommandline

    result = wptcommandline.create_parser()

    result.add_argument(
        "--no-install",
        action="store_true",
        default=False,
        help="Do not install test runner application",
    )
    return result


class CriticalLogBuffer(handlers.BaseHandler):
    """
    Buffer critical log entries
    """

    def __init__(self):
        self.log_entries = []

    def __call__(self, data):
        if (
            data["action"] == "log"
            and log_levels[data["level"]] <= log_levels["CRITICAL"]
        ):
            self.log_entries.append(data)


class WebPlatformTestsRunner:
    """Run web platform tests."""

    def __init__(self, setup):
        self.setup = setup

    def setup_logging(self, **kwargs):
        from tools.wpt import run

        return run.setup_logging(
            kwargs,
            {self.setup.default_log_type: sys.stdout},
            formatter_defaults={"screenshot": True},
        )

    def run(self, logger, **kwargs):
        from mozbuild.base import BinaryNotFoundException
        from wptrunner import wptrunner

        if kwargs["manifest_update"] is not False:
            self.update_manifest(logger)
        kwargs["manifest_update"] = False

        if kwargs["product"] == "firefox":
            try:
                kwargs = self.setup.kwargs_firefox(kwargs)
            except BinaryNotFoundException as e:
                logger.error(e)
                logger.info(e.help())
                return 1
        elif kwargs["product"] == "firefox_android":
            kwargs = self.setup.kwargs_firefox_android(kwargs)
        else:
            kwargs = self.setup.kwargs_wptrun(kwargs)

        log_buffer = CriticalLogBuffer()
        logger.add_handler(log_buffer)

        result = 1
        try:
            result = wptrunner.start(**kwargs)
        finally:
            if int(result) != 0:
                self._process_log_errors(logger, log_buffer, kwargs)
            logger.remove_handler(log_buffer)
        return int(result)

    def _process_log_errors(self, logger, log_buffer, kwargs):
        for item in log_buffer.log_entries:
            if (
                kwargs["webdriver_binary"] is None
                and "webdriver" in item["message"]
                and self.setup.topobjdir
            ):
                print(
                    "ERROR: Couldn't find geckodriver binary required to run wdspec tests, consider `ac_add_options --enable-geckodriver` in your build configuration"
                )

    def update_manifest(self, logger, **kwargs):
        import manifestupdate

        return manifestupdate.run(
            logger=logger,
            src_root=self.setup.topsrcdir,
            obj_root=self.setup.topobjdir,
            **kwargs,
        )
