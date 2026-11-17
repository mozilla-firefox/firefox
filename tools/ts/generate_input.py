# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import sys
from types import SimpleNamespace

SUBSTS = {
    # extract_rfp_targets.py reads the update channel.
    "MOZ_UPDATE_CHANNEL": "default",
}


def main(topsrcdir, topobjdir, *args):
    sys.modules["buildconfig"] = SimpleNamespace(
        topsrcdir=topsrcdir,
        topobjdir=topobjdir,
        defines={},
        substs=SUBSTS,
        get_dependencies=lambda: [],
    )
    from mozbuild.action import file_generate

    return file_generate.main(list(args))


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
