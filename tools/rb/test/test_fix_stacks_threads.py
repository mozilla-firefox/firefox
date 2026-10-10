# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import threading
import time
import unittest
from subprocess import PIPE, Popen

import mozunit

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import fix_stacks

ECHO = "import sys\nfor line in sys.stdin:\n    sys.stdout.write(line)\n    sys.stdout.flush()\n"


class TestFixStacksThreads(unittest.TestCase):
    """Checks that concurrent fixSymbols callers each get the answer to their own line.

    An echo process stands in for `fix-stacks`, so a correct answer is the line itself.
    """

    def setUp(self):
        self.proc = Popen(
            [sys.executable, "-u", "-c", ECHO],
            stdin=PIPE,
            stdout=PIPE,
            universal_newlines=True,
        )
        fix_stacks.fix_stacks = self.proc

    def tearDown(self):
        self.proc.kill()
        self.proc.wait()
        for t in getattr(self, "threads", []):
            t.join()
        fix_stacks.fix_stacks = None

    def test_concurrent_callers_get_their_own_answers(self):
        threads_count = 8
        lines = 500
        barrier = threading.Barrier(threads_count)
        wrong = []

        def worker(tid):
            barrier.wait()
            for i in range(lines):
                line = f"#01: ???[libxul.so +0x{tid:x}{i:06x}]"
                out = fix_stacks.fixSymbols(line)
                if not out:
                    return
                if out != line:
                    wrong.append((line, out))

        self.threads = [
            threading.Thread(target=worker, args=(t,), daemon=True)
            for t in range(threads_count)
        ]
        for t in self.threads:
            t.start()
        deadline = time.monotonic() + 30
        for t in self.threads:
            t.join(timeout=max(0, deadline - time.monotonic()))

        stuck = sum(t.is_alive() for t in self.threads)
        self.assertEqual(stuck, 0, "threads never got an answer")
        self.assertEqual(wrong, [], "threads got answers for other lines")


if __name__ == "__main__":
    mozunit.main()
