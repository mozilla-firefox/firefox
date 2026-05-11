#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

here = os.path.split(os.path.abspath(__file__))[0]
sys.path.insert(0, os.path.join(here, "tests"))

from tools import localpaths  # noqa: F401, I001
from wptrunner import wptrunner

if __name__ == "__main__":
    rv = wptrunner.main()
    sys.exit(rv)
