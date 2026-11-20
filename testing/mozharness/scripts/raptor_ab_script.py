#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Run raptor_script.py twice in one task, for AB profiles."""

import os
import sys

# load modules from parent dir
sys.path.insert(1, os.path.dirname(sys.path[0]))

from mozharness.mozilla.testing.raptor_ab import run_ab_profiles

if __name__ == "__main__":
    sys.exit(run_ab_profiles())
