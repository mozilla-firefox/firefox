# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import concurrent.futures as futures
import errno
import os
import stat
import sys
from collections import Counter, OrderedDict, defaultdict

import mozpack.path as mozpath
from mozpack.errors import errors
from mozpack.files import BaseFile, Dest


class FileRegistry:
    """
    Generic container to keep track of a set of BaseFile instances. It
    preserves the order under which the files are added, but doesn't keep
    track of empty directories (directories are not stored at all).
    The paths associated with the BaseFile instances are relative to an
    unspecified (virtual) root directory.

        registry = FileRegistry()
        registry.add('foo/bar', file_instance)
    """

    def __init__(self):
        self._files = OrderedDict()
        self._required_directories = Counter()
        self._partial_paths_cache = {}

    def _partial_paths(self, path):
        """
        Turn "foo/bar/baz/zot" into ["foo/bar/baz", "foo/bar", "foo"].
        """
        dir_name = path.rpartition("/")[0]
        if not dir_name:
            return []

        partial_paths = self._partial_paths_cache.get(dir_name)
        if partial_paths:
            return partial_paths

        partial_paths = [dir_name] + self._partial_paths(dir_name)

        self._partial_paths_cache[dir_name] = partial_paths
        return partial_paths

    def add(self, path, content):
        """
        Add a BaseFile instance to the container, under the given path.
        """
        assert isinstance(content, BaseFile)
        if path in self._files:
            return errors.error("%s already added" % path)
        if self._required_directories[path] > 0:
            return errors.error("Can't add %s: it is a required directory" % path)
        # Check whether any parent of the given path is already stored
        partial_paths = self._partial_paths(path)
        for partial_path in partial_paths:
            if partial_path in self._files:
                return errors.error("Can't add %s: %s is a file" % (path, partial_path))
        self._files[path] = content
        self._required_directories.update(partial_paths)

    def match(self, pattern):
        """
        Return the list of paths, stored in the container, matching the
        given pattern. See the mozpack.path.match documentation for a
        description of the handled patterns.
        """
        if "*" in pattern:
            return [p for p in self.paths() if mozpath.match(p, pattern)]
        if pattern == "":
            return self.paths()
        if pattern in self._files:
            return [pattern]
        return [p for p in self.paths() if mozpath.basedir(p, [pattern]) == pattern]

    def remove(self, pattern):
        """
        Remove paths matching the given pattern from the container. See the
        mozpack.path.match documentation for a description of the handled
        patterns.
        """
        items = self.match(pattern)
        if not items:
            return errors.error(
                "Can't remove %s: %s"
                % (pattern, "not matching anything previously added")
            )
        for i in items:
            del self._files[i]
            self._required_directories.subtract(self._partial_paths(i))

    def paths(self):
        """
        Return all paths stored in the container, in the order they were added.
        """
        return list(self._files)

    def __len__(self):
        """
        Return number of paths stored in the container.
        """
        return len(self._files)

    def __contains__(self, pattern):
        raise RuntimeError(
            "'in' operator forbidden for %s. Use contains()." % self.__class__.__name__
        )

    def contains(self, pattern):
        """
        Return whether the container contains paths matching the given
        pattern. See the mozpack.path.match documentation for a description of
        the handled patterns.
        """
        return len(self.match(pattern)) > 0

    def __getitem__(self, path):
        """
        Return the BaseFile instance stored in the container for the given
        path.
        """
        return self._files[path]

    def __iter__(self):
        """
        Iterate over all (path, BaseFile instance) pairs from the container.
            for path, file in registry:
                (...)
        """
        return iter(self._files.items())

    def required_directories(self):
        """
        Return the set of directories required by the paths in the container,
        in no particular order.  The returned directories are relative to an
        unspecified (virtual) root directory (and do not include said root
        directory).
        """
        return set(k for k, v in self._required_directories.items() if v > 0)

    def output_to_inputs_tree(self):
        """
        Return a dictionary mapping each output path to the set of its
        required input paths.

        All paths are normalized.
        """
        tree = {}
        for output, file in self:
            output = mozpath.normpath(output)
            tree[output] = set(mozpath.normpath(f) for f in file.inputs())
        return tree

    def input_to_outputs_tree(self):
        """
        Return a dictionary mapping each input path to the set of
        impacted output paths.

        All paths are normalized.
        """
        tree = defaultdict(set)
        for output, file in self:
            output = mozpath.normpath(output)
            for input in file.inputs():
                input = mozpath.normpath(input)
                tree[input].add(output)
        return dict(tree)


class FileRegistrySubtree:
    """A proxy class to give access to a subtree of an existing FileRegistry.

    Note this doesn't implement the whole FileRegistry interface."""

    def __new__(cls, base, registry):
        if not base:
            return registry
        return object.__new__(cls)

    def __init__(self, base, registry):
        self._base = base
        self._registry = registry

    def _get_path(self, path):
        # mozpath.join will return a trailing slash if path is empty, and we
        # don't want that.
        return mozpath.join(self._base, path) if path else self._base

    def add(self, path, content):
        return self._registry.add(self._get_path(path), content)

    def match(self, pattern):
        return [
            mozpath.relpath(p, self._base)
            for p in self._registry.match(self._get_path(pattern))
        ]

    def remove(self, pattern):
        return self._registry.remove(self._get_path(pattern))

    def paths(self):
        return [p for p, f in self]

    def __len__(self):
        return len(self.paths())

    def contains(self, pattern):
        return self._registry.contains(self._get_path(pattern))

    def __getitem__(self, path):
        return self._registry[self._get_path(path)]

    def __iter__(self):
        for p, f in self._registry:
            if mozpath.basedir(p, [self._base]):
                yield mozpath.relpath(p, self._base), f


class FileCopyResult:
    """Represents results of a FileCopier.copy operation."""

    def __init__(self):
        self.updated_files = set()
        self.existing_files = set()
        self.removed_files = set()
        self.removed_directories = set()

    @property
    def updated_files_count(self):
        return len(self.updated_files)

    @property
    def existing_files_count(self):
        return len(self.existing_files)

    @property
    def removed_files_count(self):
        return len(self.removed_files)

    @property
    def removed_directories_count(self):
        return len(self.removed_directories)


class FileCopier(FileRegistry):
    """
    FileRegistry with the ability to copy the registered files to a separate
    directory.
    """

    def copy(
        self,
        destination,
        skip_if_older=True,
        remove_unaccounted=True,
        remove_all_directory_symlinks=True,
        remove_empty_directories=True,
    ):
        """
        Copy all registered files to the given destination path. The given
        destination can be an existing directory, or not exist at all. It
        can't be e.g. a file.
        The copy process acts a bit like rsync: files are not copied when they
        don't need to (see mozpack.files for details on file.copy).

        By default, files in the destination directory that aren't
        registered are removed and empty directories are deleted. In
        addition, all directory symlinks in the destination directory
        are deleted: this is a conservative approach to ensure that we
        never accidently write files into a directory that is not the
        destination directory. In the worst case, we might have a
        directory symlink in the object directory to the source
        directory.

        To disable removing of unregistered files, pass
        remove_unaccounted=False. To disable removing empty
        directories, pass remove_empty_directories=False. In rare
        cases, you might want to maintain directory symlinks in the
        destination directory (at least those that are not required to
        be regular directories): pass
        remove_all_directory_symlinks=False. Exercise caution with
        this flag: you almost certainly do not want to preserve
        directory symlinks.

        Returns a FileCopyResult that details what changed.
        """
        assert isinstance(destination, str)
        assert not os.path.exists(destination) or os.path.isdir(destination)

        result = FileCopyResult()
        have_symlinks = hasattr(os, "symlink")
        destination = os.path.normpath(destination)

        # We create the destination directory specially. We can't do this as
        # part of the loop doing mkdir() below because that loop munges
        # symlinks and permissions and parent directories of the destination
        # directory may have their own weird schema. The contract is we only
        # manage children of destination, not its parents.
        os.makedirs(destination, exist_ok=True)

        # Because we could be handling thousands of files, code in this
        # function is optimized to minimize system calls. We prefer CPU time
        # in Python over possibly I/O bound filesystem calls to stat() and
        # friends.

        required_dirs = set([destination])
        required_dirs |= set(
            os.path.normpath(os.path.join(destination, d))
            for d in self.required_directories()
        )

        # Ensure destination directories are in place and proper.
        #
        # The "proper" bit is important. We need to ensure that directories
        # have appropriate permissions or we will be unable to discover
        # and write files. Furthermore, we need to verify directories aren't
        # symlinks.
        #
        # Symlinked directories (a symlink whose target is a directory) are
        # incompatible with us because our manifest talks in terms of files,
        # not directories. If we leave symlinked directories unchecked, we
        # would blindly follow symlinks and this might confuse file
        # installation. For example, if an existing directory is a symlink
        # to directory X and we attempt to install a symlink in this directory
        # to a file in directory X, we may create a recursive symlink!
        for d in sorted(required_dirs, key=len):
            os.makedirs(d, exist_ok=True)

            # We allow the destination to be a symlink because the caller
            # is responsible for managing the destination and we assume
            # they know what they are doing.
            if have_symlinks and d != destination:
                st = os.lstat(d)
                if stat.S_ISLNK(st.st_mode):
                    # While we have remove_unaccounted, it doesn't apply
                    # to directory symlinks because if it did, our behavior
                    # could be very wrong.
                    os.remove(d)
                    os.mkdir(d)

            if not os.access(d, os.W_OK):
                umask = os.umask(0o077)
                os.umask(umask)
                os.chmod(d, 0o777 & ~umask)

        if isinstance(remove_unaccounted, FileRegistry):
            existing_files = set(
                os.path.normpath(os.path.join(destination, p))
                for p in remove_unaccounted.paths()
            )
            existing_dirs = set(
                os.path.normpath(os.path.join(destination, p))
                for p in remove_unaccounted.required_directories()
            )
            existing_dirs |= {os.path.normpath(destination)}
        else:
            # While we have remove_unaccounted, it doesn't apply to empty
            # directories because it wouldn't make sense: an empty directory
            # is empty, so removing it should have no effect.
            existing_dirs = set()
            existing_files = set()
            for root, dirs, files in os.walk(destination):
                # We need to perform the same symlink detection as above.
                # os.walk() doesn't follow symlinks into directories by
                # default, so we need to check dirs (we can't wait for root).
                if have_symlinks:
                    filtered = []
                    for d in dirs:
                        full = os.path.join(root, d)
                        st = os.lstat(full)
                        if stat.S_ISLNK(st.st_mode):
                            # This directory symlink is not a required
                            # directory: any such symlink would have been
                            # removed and a directory created above.
                            if remove_all_directory_symlinks:
                                os.remove(full)
                                result.removed_files.add(os.path.normpath(full))
                            else:
                                existing_files.add(os.path.normpath(full))
                        else:
                            filtered.append(d)

                    dirs[:] = filtered

                existing_dirs.add(os.path.normpath(root))

                for d in dirs:
                    existing_dirs.add(os.path.normpath(os.path.join(root, d)))

                for f in files:
                    existing_files.add(os.path.normpath(os.path.join(root, f)))

        # Now we reconcile the state of the world against what we want.
        dest_files = set()

        # Install files.
        #
        # Creating/appending new files on Windows/NTFS is slow. So we use a
        # thread pool to speed it up significantly. The performance of this
        # loop is so critical to common build operations on Linux that the
        # overhead of the thread pool is worth avoiding, so we have 2 code
        # paths. We also employ a low water mark to prevent thread pool
        # creation if number of files is too small to benefit.
        copy_results = []
        if sys.platform == "win32" and len(self) > 100:
            with futures.ThreadPoolExecutor(4) as e:
                fs = []
                for p, f in self:
                    destfile = os.path.normpath(os.path.join(destination, p))
                    fs.append((destfile, e.submit(f.copy, destfile, skip_if_older)))

            copy_results = [(path, f.result) for path, f in fs]
        else:
            for p, f in self:
                destfile = os.path.normpath(os.path.join(destination, p))
                copy_results.append((destfile, f.copy(destfile, skip_if_older)))

        for destfile, copy_result in copy_results:
            dest_files.add(destfile)
            if copy_result:
                result.updated_files.add(destfile)
            else:
                result.existing_files.add(destfile)

        # Remove files no longer accounted for.
        if remove_unaccounted:
            for f in existing_files - dest_files:
                # Windows requires write access to remove files.
                if os.name == "nt" and not os.access(f, os.W_OK):
                    # It doesn't matter what we set permissions to since we
                    # will remove this file shortly.
                    os.chmod(f, 0o600)

                os.remove(f)
                result.removed_files.add(f)

        if not remove_empty_directories:
            return result

        # Figure out which directories can be removed. This is complicated
        # by the fact we optionally remove existing files. This would be easy
        # if we walked the directory tree after installing files. But, we're
        # trying to minimize system calls.

        # Start with the ideal set.
        remove_dirs = existing_dirs - required_dirs

        # Then don't remove directories if we didn't remove unaccounted files
        # and one of those files exists.
        if not remove_unaccounted:
            parents = set()
            pathsep = os.path.sep
            for f in existing_files:
                path = f
                while True:
                    # All the paths are normalized and relative by this point,
                    # so os.path.dirname would only do extra work.
                    dirname = path.rpartition(pathsep)[0]
                    if dirname in parents:
                        break
                    parents.add(dirname)
                    path = dirname
            remove_dirs -= parents

        # Remove empty directories that aren't required.
        for d in sorted(remove_dirs, key=len, reverse=True):
            try:
                try:
                    os.rmdir(d)
                except OSError as e:
                    if e.errno in (errno.EPERM, errno.EACCES):
                        # Permissions may not allow deletion. So ensure write
                        # access is in place before attempting to rmdir again.
                        os.chmod(d, 0o700)
                        os.rmdir(d)
                    else:
                        raise
            except OSError as e:
                # If remove_unaccounted is a # FileRegistry, then we have a
                # list of directories that may not be empty, so ignore rmdir
                # ENOTEMPTY errors for them.
                if (
                    isinstance(remove_unaccounted, FileRegistry)
                    and e.errno == errno.ENOTEMPTY
                ):
                    continue
                raise
            result.removed_directories.add(d)

        return result


class Jarrer(FileRegistry, BaseFile):
    """
    FileRegistry with the ability to copy and pack the registered files as a
    jar file. Also acts as a BaseFile instance, to be copied with a FileCopier.
    """

    def __init__(self, compress=True):
        """
        Create a Jarrer instance. See mozpack.mozjar.JarWriter documentation
        for details on the compress argument.
        """
        self.compress = compress
        self._preload = []
        self._compress_options = {}  # Map path to compress boolean option.
        FileRegistry.__init__(self)

    def add(self, path, content, compress=None):
        FileRegistry.add(self, path, content)
        if compress is not None:
            self._compress_options[path] = compress

    def copy(self, dest, skip_if_older=True):
        """
        Pack all registered files in the given destination jar. The given
        destination jar may be a path to jar file, or a Dest instance for
        a jar file.
        If the destination jar file exists, its (compressed) contents are used
        instead of the registered BaseFile instances when appropriate.
        """

        class DeflaterDest(Dest):
            """
            Dest-like class, reading from a file-like object initially, but
            switching to a Deflater object if written to.

                dest = DeflaterDest(original_file)
                dest.read()      # Reads original_file
                dest.write(data) # Creates a Deflater and write data there
                dest.read()      # Re-opens the Deflater and reads from it
            """

            def __init__(self, orig=None, compress=True):
                self.mode = None
                self.deflater = orig
                self.compress = compress

            def read(self, length=-1):
                if self.mode != "r":
                    assert self.mode is None
                    self.mode = "r"
                return self.deflater.read(length)

            def write(self, data):
                if self.mode != "w":
                    from mozpack.mozjar import Deflater

                    self.deflater = Deflater(self.compress)
                    self.mode = "w"
                self.deflater.write(data)

            def exists(self):
                return self.deflater is not None

        if isinstance(dest, str):
            dest = Dest(dest)
        assert isinstance(dest, Dest)

        from mozpack.mozjar import JarReader, JarWriter

        try:
            old_jar = JarReader(fileobj=dest)
        except Exception:
            old_jar = []

        old_contents = dict([(f.filename, f) for f in old_jar])

        with JarWriter(fileobj=dest, compress=self.compress) as jar:
            for path, file in self:
                compress = self._compress_options.get(path, self.compress)
                if path in old_contents:
                    deflater = DeflaterDest(old_contents[path], compress)
                else:
                    deflater = DeflaterDest(compress=compress)
                file.copy(deflater, skip_if_older)
                jar.add(path, deflater.deflater, mode=file.mode, compress=compress)
            if self._preload:
                jar.preload(self._preload)

    def open(self):
        raise RuntimeError("unsupported")

    def preload(self, paths):
        """
        Add the given set of paths to the list of preloaded files. See
        mozpack.mozjar.JarWriter documentation for details on jar preloading.
        """
        self._preload.extend(paths)
