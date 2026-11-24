#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import os
import subprocess
import sys

import xtalos


def start(
    xperf_path,
    xperf_providers,
    xperf_stackwalk,
    xperf_user_providers,
    etl_filename,
    debug=False,
):
    xperf_cmd = [
        xperf_path,
        "-on",
        "+".join(xperf_providers),
        "-stackwalk",
        "+".join(xperf_stackwalk),
        "-MaxBuffers",
        "1024",
        "-BufferSize",
        "1024",
        "-f",
        f"{etl_filename}.kernel",
        "-start",
        "talos_ses",
        "-on",
        "+".join(xperf_user_providers),
        "-MaxBuffers",
        "1024",
        "-BufferSize",
        "1024",
        "-f",
        f"{etl_filename}.user",
    ]
    if debug:
        print(f"executing '{subprocess.list2cmdline(xperf_cmd)}'")
    subprocess.call(xperf_cmd)


def start_from_config(config_file=None, debug=False, **kwargs):
    """start from a YAML config file"""

    # required options and associated error messages
    required = {
        "xperf_path": "xperf_path not given",
        "xperf_providers": "No xperf providers given",
        "xperf_user_providers": "No xperf user providers given",
        "xperf_stackwalk": "No xperf stackwalk options given",
        "etl_filename": "No etl_filename given",
    }
    for key in required:
        if key not in kwargs:
            kwargs[key] = None

    if config_file:
        # override options from YAML config file
        kwargs = xtalos.options_from_config(kwargs, config_file)

    # ensure the required options are given
    for key, msg in required.items():
        if not kwargs.get(key):
            raise xtalos.XTalosError(msg)

    # ensure path to xperf actually exists
    if not os.path.exists(kwargs["xperf_path"]):
        raise xtalos.XTalosError(
            f"ERROR: xperf_path '{kwargs['xperf_path']}' does not exist"
        )

    # make calling arguments
    args = dict([(key, kwargs[key]) for key in required.keys()])
    args["debug"] = debug

    # call start
    start(**args)


def main(args=sys.argv[1:]):
    # parse command line options
    parser = xtalos.XtalosOptions()
    args = parser.parse_args(args)

    # start xperf
    try:
        start_from_config(
            config_file=args.configFile,
            debug=args.debug_level >= xtalos.DEBUG_INFO,
            **args.__dict__,
        )
    except xtalos.XTalosError as e:
        parser.error(str(e))


if __name__ == "__main__":
    main()
