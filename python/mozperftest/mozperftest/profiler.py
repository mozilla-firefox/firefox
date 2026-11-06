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
        self.active_profilers = []
        for name in profilers:
            profiler_class = PROFILERS[name]
            if profiler_class.is_enabled():
                self.active_profilers.append(profiler_class.get_controller())

    def start(self, options=None):
        for profiler in self.active_profilers:
            profiler.start(options)

    def stop(self, output_path, index):
        for profiler in self.active_profilers:
            profiler.stop(output_path, index)
