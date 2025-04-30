# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import json
import os.path
import sys

import buildconfig
import mozpack.path as mozpath

from mozpack.copier import FileRegistry
from mozpack.manifests import InstallManifest


# A list of build manifests, and their relative base paths, from which to
# extract lists of install files. These vary depending on which backend we're
# using, so nonexistent manifests are ignored.
manifest_paths = (
    ("", "_build_manifests/install/dist_bin"),
    ("", "faster/install_dist_bin"),
    ("browser", "faster/install_dist_bin_browser"),
)


def get_registry(paths):
    used_paths = set()

    registry = FileRegistry()
    for base, path in paths:
        full_path = mozpath.join(buildconfig.topobjdir, path)
        if not os.path.exists(full_path):
            continue

        used_paths.add(full_path)

        reg = FileRegistry()
        InstallManifest(full_path).populate_registry(reg)

        for p, f in reg:
            path = mozpath.join(base, p)
            try:
                registry.add(path, f)
            except Exception:
                pass

    return registry, used_paths


def get_child(base, path):
    """Returns the nearest parent of `path` which is an immediate child of
    `base`"""

    dirname = mozpath.dirname(path)
    while dirname != base:
        path = dirname
        dirname = mozpath.dirname(path)
    return path


def main(output, *args):
    parser = argparse.ArgumentParser(
        description="Produces a JSON manifest of built-in add-ons"
    )
    parser.add_argument(
        "--builtin-addons",
        type=str,
        dest="builtinsdir",
        action="store",
        help=(
            "The build sub-directory containing builtin add-ons to include in the omni.ja"
        ),
    )
    args = parser.parse_args(args)

    registry, inputs = get_registry(manifest_paths)

    dicts = {}
    for path in registry.match("dictionaries/*.dic"):
        base, ext = os.path.splitext(mozpath.basename(path))
        dicts[base] = path

    listing = {
        "dictionaries": dicts,
    }

    if args.builtinsdir:
        builtins = list()
        for p in registry.match("%s/*/manifest.json" % args.builtinsdir):
            dirname = mozpath.basename(get_child(args.builtinsdir, p))
            builtins_entry = dict()
            builtins_entry["res_url"] = f"resource://builtin-addons/{dirname}/"
            # collect addon id and version from each of the builtins manifest.json files.
            webext_manifest = json.loads(registry[p].read())
            builtins_entry["addon_id"] = webext_manifest["browser_specific_settings"][
                "gecko"
            ]["id"]
            builtins_entry["addon_version"] = webext_manifest["version"]
            builtins.append(builtins_entry)

        def sort_by_dirname(entry):
            return entry["res_url"]

        listing["builtins"] = sorted(builtins, key=sort_by_dirname)

    json.dump(listing, output, sort_keys=True)

    return inputs


if __name__ == "__main__":
    main(sys.stdout, *sys.argv[1:])
