# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os

HERE = os.path.dirname(__file__)


def get_customizations():
    for f in os.listdir(HERE):
        if not f.endswith("json"):
            continue
        yield os.path.join(HERE, f)


def find_customization(path_or_name):
    if not path_or_name.endswith(".json"):
        path_or_name += ".json"
    if not os.path.exists(path_or_name):
        # trying relative
        rpath = os.path.join(HERE, path_or_name)
        if not os.path.exists(rpath):
            return None
        path_or_name = rpath
    return path_or_name


def get_customization(path_or_name):
    path = find_customization(path_or_name)
    if path is None:
        raise OSError(f"Can't find the customization file {path_or_name!r}")
    with open(path) as f:
        return json.loads(f.read())
