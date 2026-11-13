# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import subprocess
import sys
from pathlib import Path

import buildconfig


def action(fh, script, target_dir, *args):
    fh.close()
    os.unlink(fh.name)

    args = list(args)
    objdir = Path.cwd()
    topsrcdir = Path(buildconfig.topsrcdir)

    def make_absolute(base_path, p):
        return Path(base_path) / Path(p.lstrip("/"))

    try:
        abs_target_dir = str(make_absolute(objdir, target_dir))
        abs_script = make_absolute(topsrcdir, script)
        script = [str(abs_script)]
        if abs_script.suffix == ".py":
            script = [sys.executable] + script
        subprocess.check_call(script + args, cwd=abs_target_dir)
    except Exception:
        relative = os.path.relpath(__file__, topsrcdir)
        print(
            f"{relative}:action caught exception. params={json.dumps([script, target_dir] + args, indent=2)}\n"
        )
        raise
