# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import os
import sys
import time

from mozpack.copier import FileCopier, FileRegistry
from mozpack.errors import errors
from mozpack.files import BaseFile, FileFinder
from mozpack.manifests import InstallManifest

COMPLETE = (
    "Elapsed: {elapsed:.2f}s; From {dest}: Kept {existing} existing; "
    "Added/updated {updated}; "
    "Removed {rm_files} files and {rm_dirs} directories."
)


def process_manifest(destdir, paths, track, no_symlinks=False):
    if os.path.exists(track):
        # We use the same format as install manifests for the tracking
        # data.
        manifest = InstallManifest(path=track)
        remove_unaccounted = FileRegistry()
        dummy_file = BaseFile()

        finder = FileFinder(destdir, find_dotfiles=True)
        for dest in manifest._dests:
            for p, f in finder.find(dest):
                remove_unaccounted.add(p, dummy_file)

        remove_empty_directories = True
        remove_all_directory_symlinks = True

    else:
        # If tracking is enabled and there is no file, we don't want to
        # be removing anything. An empty FileRegistry removes nothing and is
        # faster, because the copier then avoids walking the whole destination.
        remove_unaccounted = FileRegistry()
        remove_empty_directories = False
        remove_all_directory_symlinks = False

    manifest = InstallManifest()
    for path in paths:
        manifest |= InstallManifest(path=path)

    copier = FileCopier()
    link_policy = "copy" if no_symlinks else "symlink"
    manifest.populate_registry(copier, link_policy=link_policy)
    with errors.accumulate():
        result = copier.copy(
            destdir,
            remove_unaccounted=remove_unaccounted,
            remove_all_directory_symlinks=remove_all_directory_symlinks,
            remove_empty_directories=remove_empty_directories,
        )

    if track:
        # We should record files that we actually copied.
        # It is too late to expand wildcards when the track file is read.
        manifest.write(path=track, expand_pattern=True)

    return result


def main(argv):
    parser = argparse.ArgumentParser(description="Process install manifest files.")

    parser.add_argument("destdir", help="Destination directory.")
    parser.add_argument("manifests", nargs="+", help="Path to manifest file(s).")
    parser.add_argument(
        "--no-symlinks",
        action="store_true",
        help="Do not install symbolic links. Always copy files",
    )
    parser.add_argument(
        "--track",
        metavar="PATH",
        required=True,
        help="Use installed files tracking information from the given path.",
    )
    args = parser.parse_args(argv)

    start = time.monotonic()

    result = process_manifest(
        args.destdir,
        args.manifests,
        track=args.track,
        no_symlinks=args.no_symlinks,
    )

    elapsed = time.monotonic() - start

    if (
        result.updated_files_count
        + result.removed_files_count
        + result.removed_directories_count
        > 0
        or os.environ.get("BUILD_VERBOSE_LOG")
    ):
        print(
            COMPLETE.format(
                elapsed=elapsed,
                dest=args.destdir,
                existing=result.existing_files_count,
                updated=result.updated_files_count,
                rm_files=result.removed_files_count,
                rm_dirs=result.removed_directories_count,
            )
        )


if __name__ == "__main__":
    main(sys.argv[1:])
