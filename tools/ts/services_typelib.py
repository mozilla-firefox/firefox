# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import sys
from types import SimpleNamespace

import buildconfig
import mozpack.path as mozpath
from mozfile import json

sys.path.append(mozpath.join(buildconfig.topsrcdir, "xpcom", "components"))
from gen_static_components import read_manifest

# components.conf files compare these to strings, so unlike the other
# settings they cannot default to True.
PLATFORM_SUBSTS = {
    "darwin": {
        "CC_TYPE": "clang",
        "MOZ_DEVTOOLS": "all",
        "MOZ_WIDGET_TOOLKIT": "cocoa",
        "OS_ARCH": "Darwin",
        "OS_TARGET": "Darwin",
    },
    "linux": {
        "CC_TYPE": "clang",
        "MOZ_DEVTOOLS": "all",
        "MOZ_WIDGET_TOOLKIT": "gtk",
        "OS_ARCH": "Linux",
        "OS_TARGET": "Linux",
    },
    "win32": {
        "CC_TYPE": "clang-cl",
        "MOZ_DEVTOOLS": "all",
        "MOZ_WIDGET_TOOLKIT": "windows",
        "OS_ARCH": "WINNT",
        "OS_TARGET": "WINNT",
    },
}


class StandInSubsts:
    """Used in place of ``buildconfig.substs`` while a ``components.conf`` is read.

    ``values`` holds the settings that pick a platform, such as
    ``MOZ_WIDGET_TOOLKIT``. Any other setting reads as ``True``, so a
    component behind a build option counts as built."""

    def __init__(self, values):
        self._values = values

    def __contains__(self, name):
        return True

    def __getitem__(self, name):
        return self._values.get(name, True)

    def get(self, name, default=None):
        return self[name]


def services_typelib(topsrcdir, confs):
    """Return a dict that maps each interface name to its ``Services`` name.

    ``confs`` lists ``components.conf`` paths relative to ``topsrcdir``. An
    interface is included if any class gives it a ``js_name`` on any
    platform in ``PLATFORM_SUBSTS``. Raises if one interface gets two
    names."""
    services = {}
    for path in confs:
        for substs in PLATFORM_SUBSTS.values():
            manifest = read_manifest(
                mozpath.join(topsrcdir, path),
                SimpleNamespace(substs=StandInSubsts(substs)),
            )
            for cls in manifest.get("Classes", []):
                if not (js_name := cls.get("js_name")):
                    continue
                for iface in cls.get("interfaces", []):
                    if (other := services.setdefault(iface, js_name)) != js_name:
                        raise Exception(
                            f"{iface} is exposed as both Services.{other} and "
                            f"Services.{js_name}"
                        )
    return services


def main(output, conf_list):
    with open(conf_list, encoding="utf-8") as fh:
        confs = fh.read().splitlines()
    json.dump(
        services_typelib(buildconfig.topsrcdir, confs),
        output,
        indent=2,
        sort_keys=True,
    )
    output.write("\n")
