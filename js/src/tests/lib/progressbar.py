# Text progress bar library, like curl or scp.

import math
import sys
from datetime import datetime, timedelta

if sys.platform.startswith("win"):
    from .terminal_win import Terminal
else:
    from .terminal_unix import Terminal


class NullProgressBar:
    def __init__(self):
        self.data = None
        self.t0 = datetime.now()

    def update(self, current, data):
        self.data = dict(data)

    def poke(self):
        pass

    def finish(self, complete=True):
        if not self.data:
            sys.stdout.write("Results: no tests run\n")
            return
        counts = ", ".join(f"{k}: {v}" for k, v in self.data.items())
        dt = (datetime.now() - self.t0).total_seconds()
        sys.stdout.write(f"Results: {counts} | {dt:.1f}s\n")

    def beginline(self):
        pass

    def message(self, msg):
        sys.stdout.write(msg + "\n")

    @staticmethod
    def update_granularity():
        return timedelta.max


class ProgressBar:
    def __init__(self, limit, fmt):
        assert self.conservative_isatty()

        self.prior = None
        self.atLineStart = True
        # [{str:str}] Describtion of how to lay out each field in the counters map.
        self.counters_fmt = fmt
        # int: The value of 'current' equal to 100%.
        self.limit = limit
        # int: max digits in limit
        self.limit_digits = int(math.ceil(math.log10(self.limit)))
        # datetime: The start time.
        self.t0 = datetime.now()
        # datetime: Optional, the last time update() ran.
        self.last_update_time = None

        # Compute the width of the counters and build the format string.
        self.counters_width = 1  # [
        for layout in self.counters_fmt:
            self.counters_width += self.limit_digits
            # | (or ']' for the last one)
            self.counters_width += 1

        self.barlen = 64 - self.counters_width

    @staticmethod
    def update_granularity():
        return timedelta(seconds=0.1)

    def update(self, current, data):
        # Record prior for poke.
        self.prior = (current, data)
        self.atLineStart = False

        # Build counters string.
        sys.stdout.write("\r[")
        for layout in self.counters_fmt:
            Terminal.set_color(layout["color"])
            sys.stdout.write(
                ("{:" + str(self.limit_digits) + "d}").format(data[layout["value"]])
            )
            Terminal.reset_color()
            if layout != self.counters_fmt[-1]:
                sys.stdout.write("|")
            else:
                sys.stdout.write("] ")

        # Build the bar.
        pct = int(100.0 * current / self.limit)
        sys.stdout.write(f"{pct:3d}% ")

        barlen = int(1.0 * self.barlen * current / self.limit) - 1
        bar = "=" * barlen + ">" + " " * (self.barlen - barlen - 1)
        sys.stdout.write(bar + "|")

        # Update the bar.
        now = datetime.now()
        dt = now - self.t0
        dt = dt.seconds + dt.microseconds * 1e-6
        sys.stdout.write(f"{dt:6.1f}s")
        Terminal.clear_right()

        # Force redisplay, since we didn't write a \n.
        sys.stdout.flush()

        self.last_update_time = now

    def poke(self):
        if not self.prior:
            return
        if datetime.now() - self.last_update_time < self.update_granularity():
            return
        self.update(*self.prior)

    def finish(self, complete=True):
        if not self.prior:
            sys.stdout.write(
                "No test run... You can try adding"
                " --run-slow-tests or --run-skipped to run more tests\n"
            )
            return
        final_count = self.limit if complete else self.prior[0]
        self.update(final_count, self.prior[1])
        sys.stdout.write("\n")

    def beginline(self):
        if not self.atLineStart:
            sys.stdout.write("\n")
            self.atLineStart = True

    def message(self, msg):
        self.beginline()
        sys.stdout.write(msg)
        sys.stdout.write("\n")

    @staticmethod
    def conservative_isatty():
        """
        Prefer erring on the side of caution and not using terminal commands if
        the current output stream may be a file.
        """
        return sys.stdout.isatty()
