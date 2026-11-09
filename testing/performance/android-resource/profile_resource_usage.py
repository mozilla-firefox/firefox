# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import sys
import time
import traceback
from pathlib import Path

from mozperftest.profiler import ProfilingMediator


def stop_profiler(profiler):
    try:
        profiler.stop(os.environ["TESTING_DIR"], 0)
        return True
    except Exception:
        traceback.print_exc()
        return False


def main():
    test_start = Path(os.environ["TESTING_DIR"], "test_start.signal")
    profiler = ProfilingMediator(["simpleperf"])
    profiler.start("-g -f 1000 --trace-offcpu -e cpu-clock -a")
    # The test stays in the harness's process group, so that the harness's
    # killpg on a timeout also reaches the test and everything it started.
    process = subprocess.Popen(["bash", sys.argv[1]])
    try:
        try:
            while not test_start.exists():
                if process.poll() is not None:
                    break
                time.sleep(0.1)
            deadline = time.monotonic() + 30
            while process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.1)
        finally:
            profiler_stopped = stop_profiler(profiler)

        returncode = process.wait()
    except BaseException:
        if process.poll() is None:
            process.terminate()
        process.wait()
        raise

    if returncode == 0 and not profiler_stopped:
        return 1
    return returncode


if __name__ == "__main__":
    sys.exit(main())
