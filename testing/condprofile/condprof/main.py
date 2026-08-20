# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Script that launches profiles creation."""

import argparse
import os
import sys
import traceback

# easier than setting PYTHONPATH in various platforms
if __name__ == "__main__":
    sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

from condprof.check_install import check  # NOQA

if "MANUAL_MACH_RUN" not in os.environ:
    check()

from condprof import patch  # noqa


def main(args=sys.argv[1:]):
    parser = argparse.ArgumentParser(description="Profile Creator")
    parser.add_argument("archive", help="Archives Dir", type=str, default=None)
    parser.add_argument("--firefox", help="Firefox Binary", type=str, default=None)
    parser.add_argument("--scenario", help="Scenario to use", type=str, default="all")
    parser.add_argument(
        "--profile", help="Existing profile Dir", type=str, default=None
    )
    parser.add_argument(
        "--customization",
        help="Profile customization to use",
        type=str,
        default="default",
    )
    parser.add_argument(
        "--visible", help="Don't use headless mode", action="store_true", default=False
    )
    parser.add_argument(
        "--archives-dir", help="Archives local dir", type=str, default="/tmp/archives"
    )
    parser.add_argument(
        "--force-new", help="Create from scratch", action="store_true", default=False
    )
    parser.add_argument(
        "--strict",
        help="Errors out immediatly on a scenario failure",
        action="store_true",
        default=False,
    )
    parser.add_argument(
        "--geckodriver",
        help="Path to the geckodriver binary",
        type=str,
        default=sys.platform.startswith("win") and "geckodriver.exe" or "geckodriver",
    )

    parser.add_argument(
        "--device-name", help="Name of the device", type=str, default=None
    )

    args = parser.parse_args(args=args)
    os.environ["CONDPROF_RUNNER"] = "1"

    from condprof.runner import run  # NOQA

    try:
        run(
            args.archive,
            args.firefox,
            args.scenario,
            args.profile,
            args.customization,
            args.visible,
            args.archives_dir,
            args.force_new,
            args.strict,
            args.geckodriver,
            args.device_name,
        )
    except Exception:
        print(f"Failed to run condprof: {traceback.format_exc()}")
        sys.exit(4)  # TBPL_RETRY


if __name__ == "__main__":
    main()
