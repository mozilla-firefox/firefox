# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Package a staged extension directory into an XPI.

The file list comes from the install track for the staging area rather than
from whatever the directory happens to hold, so a file that is no longer
installed stops being packaged.
"""

import argparse
import sys

import mozpack.path as mozpath
from mozpack.copier import FileRegistry, Jarrer
from mozpack.files import FileFinder
from mozpack.manifests import InstallManifest

from mozbuild.dirutils import ensureParentDir
from mozbuild.makeutil import Makefile
from mozbuild.util import FileAvoidWrite


def _staged_entries(track, prefix):
    manifest = InstallManifest(path=track)
    registry = FileRegistry()
    manifest.populate_registry(registry)
    entries = []
    for recorded, installed in registry:
        dest = mozpath.normpath(recorded)
        if dest == prefix or not dest.startswith(prefix + "/"):
            continue
        entries.append((mozpath.relpath(dest, prefix), installed))
    return sorted(entries, key=lambda entry: entry[0])


def staged_members(track, prefix):
    """The paths the install step recorded under ``prefix``, relative to it."""
    return [member for member, _ in _staged_entries(track, prefix)]


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--track",
        required=True,
        metavar="PATH",
        help="Install track recording what was staged.",
    )
    parser.add_argument(
        "--stage-dir",
        required=True,
        metavar="DIR",
        help="Directory the extension was staged into.",
    )
    parser.add_argument(
        "--prefix",
        required=True,
        metavar="PATH",
        help="Path of the staging area within the track, which selects "
        "the entries belonging to this extension.",
    )
    parser.add_argument(
        "--dep-file",
        required=True,
        metavar="PATH",
        help="File to write make dependencies to.",
    )
    parser.add_argument(
        "--dep-target",
        required=True,
        metavar="TARGET",
        help="Make target the dependencies are for.",
    )
    parser.add_argument("output", help="XPI to write.")
    args = parser.parse_args(argv)

    entries = _staged_entries(args.track, mozpath.normpath(args.prefix))
    if not entries:
        print(
            f"{args.output}: the install track records nothing under {args.prefix}",
            file=sys.stderr,
        )
        return 1

    jarrer = Jarrer()
    finder = FileFinder(args.stage_dir)
    deps = {}
    missing = []
    for member, installed in entries:
        entry = finder.get(member)
        if entry is None:
            missing.append(member)
            continue
        jarrer.add(member, entry)
        deps[entry.path] = None
        deps.update(dict.fromkeys(installed.inputs()))

    if missing:
        # The track says these belong in the staging area, so an archive
        # missing one of them is incomplete, and that is an error.
        print(
            f"{args.output}: {len(missing)} file(s) the install recorded "
            f"are not in {args.stage_dir}:",
            file=sys.stderr,
        )
        for member in missing:
            print(f"  {member}", file=sys.stderr)
        return 1

    ensureParentDir(args.output)
    jarrer.copy(args.output)

    deps = list(deps)
    mk = Makefile()
    mk.create_rule([args.dep_target]).add_dependencies(deps)
    # An empty rule keeps make quiet when a packaged file goes away.
    mk.create_rule(deps)
    ensureParentDir(args.dep_file)
    with FileAvoidWrite(args.dep_file) as fh:
        mk.dump(fh)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
