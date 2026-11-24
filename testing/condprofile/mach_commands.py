# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.
import logging
import os
import sys
import tempfile

from mach.decorators import Command, CommandArgument
from mozbuild.base import BinaryNotFoundException

requirements = os.path.join(os.path.dirname(__file__), "requirements", "base.txt")


def _init(command_context):
    command_context.activate_virtualenv()
    command_context.virtualenv_manager.install_pip_requirements(
        requirements, require_hashes=False
    )


@Command("fetch-condprofile", category="testing")
@CommandArgument("--target-dir", default=None, help="Target directory")
@CommandArgument("--platform", default=None, help="Platform")
@CommandArgument("--scenario", default="full", help="Scenario")  # grab choices
@CommandArgument("--customization", default="default", help="Customization")  # same
@CommandArgument("--task-id", default=None, help="Task ID")
@CommandArgument("--download-cache", action="store_true", default=True)
@CommandArgument(
    "--repo",
    default="mozilla-central",
    choices=["mozilla-central", "try"],
    help="Repository",
)
def fetch(
    command_context,
    target_dir,
    platform,
    scenario,
    customization,
    task_id,
    download_cache,
    repo,
):
    _init(command_context)
    from condprof.client import get_profile
    from condprof.util import get_current_platform, get_version

    if platform is None:
        platform = get_current_platform()

    if target_dir is None:
        target_dir = tempfile.mkdtemp()

    version = get_version(command_context.get_binary_path())

    get_profile(
        target_dir,
        platform,
        scenario,
        customization,
        task_id,
        download_cache,
        repo,
        version,
    )
    print(f"Downloaded conditioned profile can be found at: {target_dir}")


@Command("run-condprofile", category="testing")
@CommandArgument("archive", help="Archives Dir", type=str, default=None)
@CommandArgument("--firefox", help="Firefox Binary", type=str, default=None)
@CommandArgument("--scenario", help="Scenario to use", type=str, default="all")
@CommandArgument("--profile", help="Existing profile Dir", type=str, default=None)
@CommandArgument(
    "--customization", help="Profile customization to use", type=str, default="all"
)
@CommandArgument(
    "--visible", help="Don't use headless mode", action="store_true", default=False
)
@CommandArgument(
    "--archives-dir", help="Archives local dir", type=str, default="/tmp/archives"
)
@CommandArgument(
    "--force-new", help="Create from scratch", action="store_true", default=False
)
@CommandArgument(
    "--strict",
    help="Errors out immediatly on a scenario failure",
    action="store_true",
    default=True,
)
@CommandArgument(
    "--geckodriver",
    help="Path to the geckodriver binary",
    type=str,
    default=sys.platform.startswith("win") and "geckodriver.exe" or "geckodriver",
)
@CommandArgument("--device-name", help="Name of the device", type=str, default=None)
def run(command_context, **kw):
    os.environ["MANUAL_MACH_RUN"] = "1"
    _init(command_context)

    if kw["firefox"] is None:
        try:
            kw["firefox"] = command_context.get_binary_path()
        except BinaryNotFoundException as e:
            command_context.log(
                logging.ERROR,
                "run-condprofile",
                {"error": str(e)},
                "ERROR: {error}",
            )
            command_context.log(
                logging.INFO, "run-condprofile", {"help": e.help()}, "{help}"
            )
            return 1

    from condprof.runner import run
    from mozrunner.devices.android_device import prepend_adb_dir_to_path

    prepend_adb_dir_to_path(command_context)
    run(**kw)
