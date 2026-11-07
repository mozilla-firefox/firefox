# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from mozperftest.system.geckoprofiler import GeckoProfiler
from mozperftest.system.simpleperf import SimpleperfProfiler

PROFILERS = {
    "simpleperf": SimpleperfProfiler,
    "geckoprofiler": GeckoProfiler,
}


class ProfilingMediator:
    """Used to start and stop any profilers setup through the system later."""

    def __init__(self, profilers=None):
        if profilers is None:
            profilers = list(PROFILERS.keys())
        enabled = [name for name in profilers if PROFILERS[name].is_enabled()]
        # When both profilers run, the Gecko profile only provides the markers,
        # which get merged into the simpleperf profile, and Gecko writes the
        # jitdump files which let simpleperf profile JS.
        combined = "simpleperf" in enabled and "geckoprofiler" in enabled
        self.active_profilers = []
        for name in enabled:
            if name == "geckoprofiler":
                controller = PROFILERS[name].get_controller(markers_only=combined)
            elif name == "simpleperf":
                controller = PROFILERS[name].get_controller(jit_frames=combined)
            else:
                controller = PROFILERS[name].get_controller()
            self.active_profilers.append(controller)

    def start(self, options=None):
        for profiler in self.active_profilers:
            profiler.start(options)

    def stop(self, output_path, index):
        for profiler in self.active_profilers:
            profiler.stop(output_path, index)
