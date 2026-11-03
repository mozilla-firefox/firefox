# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
from contextlib import contextmanager

import mozpack.path as mozpath
from mozbuild.util import FileAvoidWrite

from .files import (
    AbsoluteSymlinkFile,
    ExistingFile,
    File,
    FileFinder,
    GeneratedFile,
    HardlinkFile,
    PreprocessedFile,
)


# This probably belongs in a more generic module. Where?
@contextmanager
def _auto_fileobj(path, fileobj, mode="r"):
    if path and fileobj:
        raise AssertionError("Only 1 of path or fileobj may be defined.")

    if not path and not fileobj:
        raise AssertionError("Must specified 1 of path or fileobj.")

    if path:
        fileobj = (
            FileAvoidWrite(path) if "w" in mode else open(path, mode, encoding="utf-8")
        )

    try:
        yield fileobj
    finally:
        if path:
            fileobj.close()


class UnreadableInstallManifest(Exception):
    """Raised when an invalid install manifest is parsed."""


class InstallManifest:
    """Describes actions to be used with a copier.FileCopier instance.

    This class facilitates serialization and deserialization of data used to
    construct a copier.FileCopier and to perform copy operations.

    The manifest defines source paths, destination paths, and a mechanism by
    which the destination file should come into existence.

    Entries in the manifest correspond to the following types:

      copy -- The file specified as the source path will be copied to the
          destination path.

      link -- The destination path will be a symlink or hardlink to the source
          path. If symlinks are not supported, a copy will be performed.

      exists -- The destination path is accounted for and won't be deleted by
          the FileCopier. If the destination path doesn't exist, an error is
          raised.

      optional -- The destination path is accounted for and won't be deleted by
          the FileCopier. No error is raised if the destination path does not
          exist.

      patternlink -- Paths matched by the expression in the source path
          will be symlinked or hardlinked to the destination directory.

      patterncopy -- Similar to patternlink except files are copied, not
          symlinked/hardlinked.

      preprocess -- The file specified at the source path will be run through
          the preprocessor, and the output will be written to the destination
          path.

      content -- The destination file will be created with the given content.

    Version 1 of the manifest was the initial version.
    Version 2 added optional path support
    Version 3 added support for pattern entries.
    Version 4 added preprocessed file support.
    Version 5 added content support.
    """

    CURRENT_VERSION = 5

    FIELD_SEPARATOR = "\x1f"

    # Negative values are reserved for non-actionable items, that is, metadata
    # that doesn't describe files in the destination.
    LINK = 1
    COPY = 2
    REQUIRED_EXISTS = 3
    OPTIONAL_EXISTS = 4
    PATTERN_LINK = 5
    PATTERN_COPY = 6
    PREPROCESS = 7
    CONTENT = 8

    def __init__(self, path=None, fileobj=None):
        """Create a new InstallManifest entry.

        If path is defined, the manifest will be populated with data from the
        file path.

        If fileobj is defined, the manifest will be populated with data read
        from the specified file object.

        Both path and fileobj cannot be defined.
        """
        self._dests = {}
        self._source_files = set()

        if path or fileobj:
            with _auto_fileobj(path, fileobj, "r") as fh:
                self._source_files.add(fh.name)
                self._load_from_fileobj(fh)

    def _load_from_fileobj(self, fileobj):
        version = fileobj.readline().rstrip()
        if version not in ("1", "2", "3", "4", "5"):
            raise UnreadableInstallManifest("Unknown manifest version: %s" % version)

        for line in fileobj:
            # Explicitly strip on \n so we don't strip out the FIELD_SEPARATOR
            # as well.
            line = line.rstrip("\n")

            fields = line.split(self.FIELD_SEPARATOR)

            record_type = int(fields[0])

            if record_type == self.LINK:
                dest, source = fields[1:]
                self.add_link(source, dest)
                continue

            if record_type == self.COPY:
                dest, source = fields[1:]
                self.add_copy(source, dest)
                continue

            if record_type == self.REQUIRED_EXISTS:
                _, path = fields
                self.add_required_exists(path)
                continue

            if record_type == self.OPTIONAL_EXISTS:
                _, path = fields
                self.add_optional_exists(path)
                continue

            if record_type == self.PATTERN_LINK:
                _, base, pattern, dest = fields[1:]
                self.add_pattern_link(base, pattern, dest)
                continue

            if record_type == self.PATTERN_COPY:
                _, base, pattern, dest = fields[1:]
                self.add_pattern_copy(base, pattern, dest)
                continue

            if record_type == self.PREPROCESS:
                dest, source, deps, marker, defines, warnings = fields[1:]

                self.add_preprocess(
                    source,
                    dest,
                    deps,
                    marker,
                    self._decode_field_entry(defines),
                    silence_missing_directive_warnings=bool(int(warnings)),
                )
                continue

            if record_type == self.CONTENT:
                dest, content = fields[1:]

                deserialized_content = self._decode_field_entry(content)
                if not isinstance(deserialized_content, str):
                    raise TypeError(
                        "not expecting type '%s'" % type(deserialized_content)
                    )
                self.add_content(deserialized_content, dest)
                continue

            # Don't fail for non-actionable items, allowing
            # forward-compatibility with those we will add in the future.
            if record_type >= 0:
                raise UnreadableInstallManifest("Unknown record type: %d" % record_type)

    def __len__(self):
        return len(self._dests)

    def __contains__(self, item):
        return item in self._dests

    def __eq__(self, other):
        return isinstance(other, InstallManifest) and self._dests == other._dests

    def __neq__(self, other):
        return not self.__eq__(other)

    def __ior__(self, other):
        if not isinstance(other, InstallManifest):
            raise ValueError("Can only | with another instance of InstallManifest.")

        self.add_entries_from(other)

        return self

    def _encode_field_entry(self, data):
        """Converts an object into a format that can be stored in the manifest file.

        Complex data types, such as ``dict``, need to be converted into a text
        representation before they can be written to a file.
        """
        return json.dumps(data, sort_keys=True)

    def _decode_field_entry(self, data):
        """Restores an object from a format that can be stored in the manifest file.

        Complex data types, such as ``dict``, need to be converted into a text
        representation before they can be written to a file.
        """
        return json.loads(data)

    def write(self, path=None, fileobj=None, expand_pattern=False):
        """Serialize this manifest to a file or file object.

        If path is specified, that file will be written to, and left untouched
        when the serialized content matches what is already there. If fileobj is
        specified, the serialized content will be written to that file object.

        It is an error if both are specified.
        """
        with _auto_fileobj(path, fileobj, "wt") as fh:
            fh.write("%d\n" % self.CURRENT_VERSION)

            for dest in sorted(self._dests):
                entry = self._dests[dest]

                if expand_pattern and entry[0] in (
                    self.PATTERN_LINK,
                    self.PATTERN_COPY,
                ):
                    type, base, pattern, dest = entry
                    type = self.LINK if type == self.PATTERN_LINK else self.COPY
                    finder = FileFinder(base)
                    paths = sorted(f[0] for f in finder.find(pattern))
                    for file_path in paths:
                        source = mozpath.join(base, file_path)
                        parts = [f"{type}", mozpath.join(dest, file_path), source]
                        fh.write(f"{self.FIELD_SEPARATOR.join(parts)}\n")
                else:
                    parts = [f"{entry[0]}", dest]
                    parts.extend(entry[1:])
                    fh.write(f"{self.FIELD_SEPARATOR.join(parts)}\n")

    def add_link(self, source, dest):
        """Add a link to this manifest.

        dest will be either a symlink or hardlink to source.
        """
        self._add_entry(dest, (self.LINK, source))

    def add_copy(self, source, dest):
        """Add a copy to this manifest.

        source will be copied to dest.
        """
        self._add_entry(dest, (self.COPY, source))

    def add_required_exists(self, dest):
        """Record that a destination file must exist.

        This effectively prevents the listed file from being deleted.
        """
        self._add_entry(dest, (self.REQUIRED_EXISTS,))

    def add_optional_exists(self, dest):
        """Record that a destination file may exist.

        This effectively prevents the listed file from being deleted. Unlike a
        "required exists" file, files of this type do not raise errors if the
        destination file does not exist.
        """
        self._add_entry(dest, (self.OPTIONAL_EXISTS,))

    def add_pattern_link(self, base, pattern, dest):
        """Add a pattern match that results in links being created.

        A ``FileFinder`` will be created with its base set to ``base``
        and ``FileFinder.find()`` will be called with ``pattern`` to discover
        source files. Each source file will be either symlinked or hardlinked
        under ``dest``.

        Filenames under ``dest`` are constructed by taking the path fragment
        after ``base`` and concatenating it with ``dest``. e.g.

           <base>/foo/bar.h -> <dest>/foo/bar.h
        """
        self._add_entry(
            mozpath.join(dest, pattern), (self.PATTERN_LINK, base, pattern, dest)
        )

    def add_pattern_copy(self, base, pattern, dest):
        """Add a pattern match that results in copies.

        See ``add_pattern_link()`` for usage.
        """
        self._add_entry(
            mozpath.join(dest, pattern), (self.PATTERN_COPY, base, pattern, dest)
        )

    def add_preprocess(
        self,
        source,
        dest,
        deps,
        marker="#",
        defines={},
        silence_missing_directive_warnings=False,
    ):
        """Add a preprocessed file to this manifest.

        ``source`` will be passed through preprocessor.py, and the output will be
        written to ``dest``.
        """
        self._add_entry(
            dest,
            (
                self.PREPROCESS,
                source,
                deps,
                marker,
                self._encode_field_entry(defines),
                "1" if silence_missing_directive_warnings else "0",
            ),
        )

    def add_content(self, content, dest):
        """Add a file with the given content."""
        self._add_entry(
            dest,
            (
                self.CONTENT,
                self._encode_field_entry(content),
            ),
        )

    def _add_entry(self, dest, entry):
        if dest in self._dests:
            raise ValueError("Item already in manifest: %s" % dest)

        self._dests[dest] = entry

    def add_entries_from(self, other, base=""):
        """
        Copy data from another mozpack.copier.InstallManifest
        instance, adding an optional base prefix to the destination.

        This allows to merge two manifests into a single manifest, or
        two take the tagged union of two manifests.
        """
        # We must copy source files to ourselves so extra dependencies from
        # the preprocessor are taken into account. Ideally, we would track
        # which source file each entry came from. However, this is more
        # complicated and not yet implemented. The current implementation
        # will result in over invalidation, possibly leading to performance
        # loss.
        self._source_files |= other._source_files

        for dest in sorted(other._dests):
            new_dest = mozpath.join(base, dest) if base else dest
            entry = other._dests[dest]
            if entry[0] in (self.PATTERN_LINK, self.PATTERN_COPY):
                entry_type, entry_base, entry_pattern, entry_dest = entry
                new_entry_dest = mozpath.join(base, entry_dest) if base else entry_dest
                new_entry = (entry_type, entry_base, entry_pattern, new_entry_dest)
            else:
                new_entry = tuple(entry)

            self._add_entry(new_dest, new_entry)

    def populate_registry(self, registry, link_policy="symlink"):
        """Populate a mozpack.copier.FileRegistry instance with data from us.

        The caller supplied a FileRegistry instance (or at least something that
        conforms to its interface) and that instance is populated with data
        from this manifest.

        The caller can set a link policy. This determines whether symlinks,
        hardlinks, or copies are used for LINK and PATTERN_LINK.
        """
        assert link_policy in ("symlink", "hardlink", "copy")
        for dest in sorted(self._dests):
            entry = self._dests[dest]
            install_type = entry[0]

            if install_type == self.LINK:
                if link_policy == "symlink":
                    cls = AbsoluteSymlinkFile
                elif link_policy == "hardlink":
                    cls = HardlinkFile
                else:
                    cls = File
                registry.add(dest, cls(entry[1]))
                continue

            if install_type == self.COPY:
                registry.add(dest, File(entry[1]))
                continue

            if install_type == self.REQUIRED_EXISTS:
                registry.add(dest, ExistingFile(required=True))
                continue

            if install_type == self.OPTIONAL_EXISTS:
                registry.add(dest, ExistingFile(required=False))
                continue

            if install_type in (self.PATTERN_LINK, self.PATTERN_COPY):
                _, base, pattern, dest = entry
                finder = FileFinder(base)
                paths = [f[0] for f in finder.find(pattern)]

                if install_type == self.PATTERN_LINK:
                    if link_policy == "symlink":
                        cls = AbsoluteSymlinkFile
                    elif link_policy == "hardlink":
                        cls = HardlinkFile
                    else:
                        cls = File
                else:
                    cls = File

                for path in paths:
                    source = mozpath.join(base, path)
                    registry.add(mozpath.join(dest, path), cls(source))

                continue

            if install_type == self.PREPROCESS:
                registry.add(
                    dest,
                    PreprocessedFile(
                        entry[1],
                        depfile_path=entry[2],
                        marker=entry[3],
                        defines=self._decode_field_entry(entry[4]),
                        extra_depends=self._source_files,
                        silence_missing_directive_warnings=bool(int(entry[5])),
                    ),
                )

                continue

            if install_type == self.CONTENT:
                # GeneratedFile expect the buffer interface, which the unicode
                # type doesn't have, so encode to a str.
                content = self._decode_field_entry(entry[1]).encode("utf-8")
                registry.add(dest, GeneratedFile(content))
                continue

            raise Exception(
                "Unknown install type defined in manifest: %d" % install_type
            )
