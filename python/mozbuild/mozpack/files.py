# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import bisect
import codecs
import errno
import inspect
import os
import platform
import shutil
import stat
import subprocess
import uuid
from collections import OrderedDict
from io import BytesIO
from itertools import chain, takewhile
from tarfile import TarFile, TarInfo
from tempfile import NamedTemporaryFile, mkstemp

import six
from jsmin import JavascriptMinify

import mozbuild.makeutil as makeutil
import mozpack.path as mozpath
from mozbuild.preprocessor import Preprocessor
from mozbuild.util import FileAvoidWrite, ensure_unicode, memoize
from mozpack.chrome.manifest import ManifestEntry, ManifestInterfaces
from mozpack.errors import ErrorMessage, errors
from mozpack.executables import elfhack, is_executable, may_elfhack, may_strip, strip
from mozpack.mozjar import JarReader

try:
    import hglib
except ImportError:
    hglib = None


# For clean builds, copying files on win32 using CopyFile through ctypes is
# ~2x as fast as using shutil.copyfile.
if platform.system() != "Windows":
    _copyfile = shutil.copyfile
else:
    import ctypes

    _kernel32 = ctypes.windll.kernel32
    _CopyFileA = _kernel32.CopyFileA
    _CopyFileW = _kernel32.CopyFileW

    def _copyfile(src, dest):
        # False indicates `dest` should be overwritten if it exists already.
        if isinstance(src, str) and isinstance(dest, str):
            _CopyFileW(src, dest, False)
        elif isinstance(src, str) and isinstance(dest, str):
            _CopyFileA(src, dest, False)
        else:
            raise TypeError("mismatched path types!")


# Helper function; ensures we always open files with the correct encoding when
# opening them in text mode.
def _open(path, mode="r"):
    if six.PY3 and "b" not in mode:
        return open(path, mode, encoding="utf-8")
    return open(path, mode)


class Dest:
    """
    Helper interface for BaseFile.copy. The interface works as follows:
      - read() and write() can be used to sequentially read/write from the underlying file.
      - a call to read() after a write() will re-open the underlying file and read from it.
      - a call to write() after a read() will re-open the underlying file, emptying it, and write to it.
    """

    def __init__(self, path):
        self.file = None
        self.mode = None
        self.path = ensure_unicode(path)

    @property
    def name(self):
        return self.path

    def read(self, length=-1):
        if self.mode != "r":
            self.file = _open(self.path, mode="rb")
            self.mode = "r"
        return self.file.read(length)

    def write(self, data):
        if self.mode != "w":
            self.file = _open(self.path, mode="wb")
            self.mode = "w"
        to_write = six.ensure_binary(data)
        return self.file.write(to_write)

    def exists(self):
        return os.path.exists(self.path)

    def close(self):
        if self.mode:
            self.mode = None
            self.file.close()
            self.file = None


class BaseFile:
    """
    Base interface and helper for file copying. Derived class may implement
    their own copy function, or rely on BaseFile.copy using the open() member
    function and/or the path property.
    """

    @staticmethod
    def is_older(first, second):
        """
        Compares the modification time of two files, and returns whether the
        ``first`` file is older than the ``second`` file.
        """
        # os.path.getmtime returns a result in seconds with precision up to
        # the microsecond. But microsecond is too precise because
        # shutil.copystat only copies milliseconds, and seconds is not
        # enough precision.
        return int(os.path.getmtime(first) * 1000) <= int(
            os.path.getmtime(second) * 1000
        )

    @staticmethod
    def any_newer(dest, inputs):
        """
        Compares the modification time of ``dest`` to multiple input files, and
        returns whether any of the ``inputs`` is newer (has a later mtime) than
        ``dest``.
        """
        # os.path.getmtime returns a result in seconds with precision up to
        # the microsecond. But microsecond is too precise because
        # shutil.copystat only copies milliseconds, and seconds is not
        # enough precision.
        dest_mtime = int(os.path.getmtime(dest) * 1000)
        for input in inputs:
            try:
                src_mtime = int(os.path.getmtime(input) * 1000)
            except OSError as e:
                if e.errno == errno.ENOENT:
                    # If an input file was removed, we should update.
                    return True
                raise
            if dest_mtime < src_mtime:
                return True
        return False

    @staticmethod
    def normalize_mode(mode):
        # Normalize file mode:
        # - keep file type (e.g. S_IFREG)
        ret = stat.S_IFMT(mode)
        # - expand user read and execute permissions to everyone
        if mode & 0o0400:
            ret |= 0o0444
        if mode & 0o0100:
            ret |= 0o0111
        # - keep user write permissions
        if mode & 0o0200:
            ret |= 0o0200
        # - leave away sticky bit, setuid, setgid
        return ret

    def copy(self, dest, skip_if_older=True):
        """
        Copy the BaseFile content to the destination given as a string or a
        Dest instance. Avoids replacing existing files if the BaseFile content
        matches that of the destination, or in case of plain files, if the
        destination is newer than the original file. This latter behaviour is
        disabled when skip_if_older is False.
        Returns whether a copy was actually performed (True) or not (False).
        """
        if isinstance(dest, str):
            dest = Dest(dest)
        else:
            assert isinstance(dest, Dest)

        can_skip_content_check = False
        if not dest.exists():
            can_skip_content_check = True
        elif getattr(self, "path", None) and getattr(dest, "path", None):
            if skip_if_older and BaseFile.is_older(self.path, dest.path):
                return False
            elif os.path.getsize(self.path) != os.path.getsize(dest.path):
                can_skip_content_check = True

        if can_skip_content_check:
            if getattr(self, "path", None) and getattr(dest, "path", None):
                # The destination directory must exist, or CopyFile will fail.
                destdir = os.path.dirname(dest.path)
                os.makedirs(destdir, exist_ok=True)
                _copyfile(self.path, dest.path)
                shutil.copystat(self.path, dest.path)
            else:
                # Ensure the file is always created
                if not dest.exists():
                    dest.write(b"")
                shutil.copyfileobj(self.open(), dest)
            return True

        src = self.open()
        accumulated_src_content = []
        while True:
            dest_content = dest.read(32768)
            src_content = src.read(32768)
            accumulated_src_content.append(src_content)
            if len(dest_content) == len(src_content) == 0:
                break
            # If the read content differs between origin and destination,
            # write what was read up to now, and copy the remainder.
            if six.ensure_binary(dest_content) != six.ensure_binary(src_content):
                dest.write(b"".join(accumulated_src_content))
                shutil.copyfileobj(src, dest)
                break
        if hasattr(self, "path") and hasattr(dest, "path"):
            shutil.copystat(self.path, dest.path)
        return True

    def open(self):
        """
        Return a file-like object allowing to read() the content of the
        associated file. This is meant to be overloaded in subclasses to return
        a custom file-like object.
        """
        assert self.path is not None
        return open(self.path, "rb")

    def read(self):
        raise NotImplementedError("BaseFile.read() not implemented. Bug 1170329.")

    def size(self):
        """Returns size of the entry.

        Derived classes are highly encouraged to override this with a more
        optimal implementation.
        """
        return len(self.read())

    @property
    def mode(self):
        """
        Return the file's unix mode, or None if it has no meaning.
        """
        return None

    def inputs(self):
        """
        Return an iterable of the input file paths that impact this output file.
        """
        raise NotImplementedError("BaseFile.inputs() not implemented.")


class File(BaseFile):
    """
    File class for plain files.
    """

    def __init__(self, path):
        self.path = ensure_unicode(path)

    @property
    def mode(self):
        """
        Return the file's unix mode, as returned by os.stat().st_mode.
        """
        if platform.system() == "Windows":
            return None
        assert self.path is not None
        mode = os.stat(self.path).st_mode
        return self.normalize_mode(mode)

    def read(self):
        """Return the contents of the file."""
        with open(self.path, "rb") as fh:
            return fh.read()

    def size(self):
        return os.stat(self.path).st_size

    def inputs(self):
        return (self.path,)


class ExecutableFile(File):
    """
    File class for executable and library files on OS/2, OS/X and ELF systems.
    (see mozpack.executables.is_executable documentation).
    """

    def __init__(self, path):
        File.__init__(self, path)

    def copy(self, dest, skip_if_older=True):
        real_dest = dest
        if not isinstance(dest, str):
            fd, dest = mkstemp()
            os.close(fd)
            os.remove(dest)
        assert isinstance(dest, str)
        # If File.copy didn't actually copy because dest is newer, check the
        # file sizes. If dest is smaller, it means it is already stripped and
        # elfhacked, so we can skip.
        if not File.copy(self, dest, skip_if_older) and os.path.getsize(
            self.path
        ) > os.path.getsize(dest):
            return False
        try:
            if may_strip(dest):
                strip(dest)
            if may_elfhack(dest):
                elfhack(dest)
        except ErrorMessage:
            os.remove(dest)
            raise

        if real_dest != dest:
            f = File(dest)
            ret = f.copy(real_dest, skip_if_older)
            os.remove(dest)
            return ret
        return True


class AbsoluteSymlinkFile(File):
    """File class that is copied by symlinking (if available).

    This class only works if the target path is absolute.
    """

    def __init__(self, path):
        if not os.path.isabs(path):
            raise ValueError("Symlink target not absolute: %s" % path)

        File.__init__(self, path)

    def copy(self, dest, skip_if_older=True):
        assert isinstance(dest, str)

        # The logic in this function is complicated by the fact that symlinks
        # aren't universally supported. So, where symlinks aren't supported, we
        # fall back to file copying. Keep in mind that symlink support is
        # per-filesystem, not per-OS.

        # Handle the simple case where symlinks are definitely not supported by
        # falling back to file copy.
        if not hasattr(os, "symlink"):
            return File.copy(self, dest, skip_if_older=skip_if_older)

        # Always verify the symlink target path exists.
        if not os.path.exists(self.path):
            errors.fatal("Symlink target path does not exist: %s" % self.path)

        st = None

        try:
            st = os.lstat(dest)
        except OSError as ose:
            if ose.errno != errno.ENOENT:
                raise

        # If the dest is a symlink pointing to us, we have nothing to do.
        # If it's the wrong symlink, the filesystem must support symlinks,
        # so we replace with a proper symlink.
        if st and stat.S_ISLNK(st.st_mode):
            link = os.readlink(dest)
            if link == self.path:
                return False

            os.remove(dest)
            os.symlink(self.path, dest)
            return True

        # If the destination doesn't exist, we try to create a symlink. If that
        # fails, we fall back to copy code.
        if not st:
            try:
                os.symlink(self.path, dest)
                return True
            except OSError:
                return File.copy(self, dest, skip_if_older=skip_if_older)

        # Now the complicated part. If the destination exists, we could be
        # replacing a file with a symlink. Or, the filesystem may not support
        # symlinks. We want to minimize I/O overhead for performance reasons,
        # so we keep the existing destination file around as long as possible.
        # A lot of the system calls would be eliminated if we cached whether
        # symlinks are supported. However, even if we performed a single
        # up-front test of whether the root of the destination directory
        # supports symlinks, there's no guarantee that all operations for that
        # dest (or source) would be on the same filesystem and would support
        # symlinks.
        #
        # Our strategy is to attempt to create a new symlink with a random
        # name. If that fails, we fall back to copy mode. If that works, we
        # remove the old destination and move the newly-created symlink into
        # its place.

        temp_dest = os.path.join(os.path.dirname(dest), str(uuid.uuid4()))
        try:
            os.symlink(self.path, temp_dest)
        # TODO Figure out exactly how symlink creation fails and only trap
        # that.
        except OSError:
            return File.copy(self, dest, skip_if_older=skip_if_older)

        # If removing the original file fails, don't forget to clean up the
        # temporary symlink.
        try:
            os.remove(dest)
        except OSError:
            os.remove(temp_dest)
            raise

        os.rename(temp_dest, dest)
        return True


class HardlinkFile(File):
    """File class that is copied by hard linking (if available)

    This is similar to the AbsoluteSymlinkFile, but with hard links. The symlink
    implementation requires paths to be absolute, because they are resolved at
    read time, which makes relative paths messy. Hard links resolve paths at
    link-creation time, so relative paths are fine.
    """

    def copy(self, dest, skip_if_older=True):
        assert isinstance(dest, str)

        if not hasattr(os, "link"):
            return super(HardlinkFile, self).copy(dest, skip_if_older=skip_if_older)

        try:
            path_st = os.stat(self.path)
        except OSError as e:
            if e.errno == errno.ENOENT:
                errors.fatal("Hard link target path does not exist: %s" % self.path)
            else:
                raise

        st = None
        try:
            st = os.lstat(dest)
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise

        if st:
            # The dest already points to the right place.
            if st.st_dev == path_st.st_dev and st.st_ino == path_st.st_ino:
                return False
            # The dest exists and it points to the wrong place
            os.remove(dest)

        # At this point, either the dest used to exist and we just deleted it,
        # or it never existed. We can now safely create the hard link.
        try:
            os.link(self.path, dest)
        except OSError:
            # If we can't hard link, fall back to copying
            return super(HardlinkFile, self).copy(dest, skip_if_older=skip_if_older)
        return True


class ExistingFile(BaseFile):
    """
    File class that represents a file that may exist but whose content comes
    from elsewhere.

    This purpose of this class is to account for files that are installed via
    external means. It is typically only used in manifests or in registries to
    account for files.

    When asked to copy, this class does nothing because nothing is known about
    the source file/data.

    Instances of this class come in two flavors: required and optional. If an
    existing file is required, it must exist during copy() or an error is
    raised.
    """

    def __init__(self, required):
        self.required = required

    def copy(self, dest, skip_if_older=True):
        if isinstance(dest, str):
            dest = Dest(dest)
        else:
            assert isinstance(dest, Dest)

        if not self.required:
            return

        if not dest.exists():
            errors.fatal("Required existing file doesn't exist: %s" % dest.path)

    def inputs(self):
        return ()


class PreprocessedFile(BaseFile):
    """
    File class for a file that is preprocessed. PreprocessedFile.copy() runs
    the preprocessor on the file to create the output.
    """

    def __init__(
        self,
        path,
        depfile_path,
        marker,
        defines,
        extra_depends=None,
        silence_missing_directive_warnings=False,
    ):
        self.path = ensure_unicode(path)
        self.depfile = ensure_unicode(depfile_path)
        self.marker = marker
        self.defines = defines
        self.extra_depends = list(extra_depends or [])
        self.silence_missing_directive_warnings = silence_missing_directive_warnings

    def inputs(self):
        pp = Preprocessor(defines=self.defines, marker=self.marker)
        pp.setSilenceDirectiveWarnings(self.silence_missing_directive_warnings)

        with _open(self.path, "r") as input:
            with _open(os.devnull, "w") as output:
                pp.processFile(input=input, output=output)

        # This always yields at least self.path.
        return pp.includes

    def copy(self, dest, skip_if_older=True):
        """
        Invokes the preprocessor to create the destination file.
        """
        if isinstance(dest, str):
            dest = Dest(dest)
        else:
            assert isinstance(dest, Dest)

        # We have to account for the case where the destination exists and is a
        # symlink to something. Since we know the preprocessor is certainly not
        # going to create a symlink, we can just remove the existing one. If the
        # destination is not a symlink, we leave it alone, since we're going to
        # overwrite its contents anyway.
        # If symlinks aren't supported at all, we can skip this step.
        if hasattr(os, "symlink") and os.path.islink(dest.path):
            os.remove(dest.path)

        pp_deps = set(self.extra_depends)

        # If a dependency file was specified, and it exists, add any
        # dependencies from that file to our list.
        if self.depfile and os.path.exists(self.depfile):
            target = mozpath.normpath(dest.name)
            with _open(self.depfile, "rt") as fileobj:
                for rule in makeutil.read_dep_makefile(fileobj):
                    if target in rule.targets():
                        pp_deps.update(rule.dependencies())

        skip = False
        if dest.exists() and skip_if_older:
            # If a dependency file was specified, and it doesn't exist,
            # assume that the preprocessor needs to be rerun. That will
            # regenerate the dependency file.
            if self.depfile and not os.path.exists(self.depfile):
                skip = False
            else:
                skip = not BaseFile.any_newer(dest.path, pp_deps)

        if skip:
            return False

        deps_out = None
        if self.depfile:
            deps_out = FileAvoidWrite(self.depfile)
        pp = Preprocessor(defines=self.defines, marker=self.marker)
        pp.setSilenceDirectiveWarnings(self.silence_missing_directive_warnings)

        with _open(self.path, "r") as input:
            pp.processFile(input=input, output=dest, depfile=deps_out)

        dest.close()
        if self.depfile:
            deps_out.close()

        return True


class GeneratedFile(BaseFile):
    """
    File class for content with no previous existence on the filesystem.
    """

    def __init__(self, content):
        self._content = content

    @property
    def content(self):
        if inspect.isfunction(self._content):
            self._content = self._content()
        return six.ensure_binary(self._content)

    @content.setter
    def content(self, content):
        self._content = content

    def open(self):
        return BytesIO(self.content)

    def read(self):
        return self.content

    def size(self):
        return len(self.content)

    def inputs(self):
        return ()


class DeflatedFile(BaseFile):
    """
    File class for members of a jar archive. DeflatedFile.copy() effectively
    extracts the file from the jar archive.
    """

    def __init__(self, file):
        from mozpack.mozjar import JarFileReader

        assert isinstance(file, JarFileReader)
        self.file = file

    def open(self):
        self.file.seek(0)
        return self.file


class ExtractedTarFile(GeneratedFile):
    """
    File class for members of a tar archive. Contents of the underlying file
    are extracted immediately and stored in memory.
    """

    def __init__(self, tar, info):
        assert isinstance(info, TarInfo)
        assert isinstance(tar, TarFile)
        GeneratedFile.__init__(self, tar.extractfile(info).read())
        self._unix_mode = self.normalize_mode(info.mode)

    @property
    def mode(self):
        return self._unix_mode

    def read(self):
        return self.content


class ManifestFile(BaseFile):
    """
    File class for a manifest file. It takes individual manifest entries (using
    the add() and remove() member functions), and adjusts them to be relative
    to the base path for the manifest, given at creation.
    Example:
        There is a manifest entry "content foobar foobar/content/" relative
        to "foobar/chrome". When packaging, the entry will be stored in
        jar:foobar/omni.ja!/chrome/chrome.manifest, which means the entry
        will have to be relative to "chrome" instead of "foobar/chrome". This
        doesn't really matter when serializing the entry, since this base path
        is not written out, but it matters when moving the entry at the same
        time, e.g. to jar:foobar/omni.ja!/chrome.manifest, which we don't do
        currently but could in the future.
    """

    def __init__(self, base, entries=None):
        self._base = base
        self._entries = []
        self._interfaces = []
        for e in entries or []:
            self.add(e)

    def add(self, entry):
        """
        Add the given entry to the manifest. Entries are rebased at open() time
        instead of add() time so that they can be more easily remove()d.
        """
        assert isinstance(entry, ManifestEntry)
        if isinstance(entry, ManifestInterfaces):
            self._interfaces.append(entry)
        else:
            self._entries.append(entry)

    def remove(self, entry):
        """
        Remove the given entry from the manifest.
        """
        assert isinstance(entry, ManifestEntry)
        if isinstance(entry, ManifestInterfaces):
            self._interfaces.remove(entry)
        else:
            self._entries.remove(entry)

    def open(self):
        """
        Return a file-like object allowing to read() the serialized content of
        the manifest.
        """
        content = "".join(
            "%s\n" % e.rebase(self._base)
            for e in chain(self._entries, self._interfaces)
        )
        return BytesIO(six.ensure_binary(content))

    def __iter__(self):
        """
        Iterate over entries in the manifest file.
        """
        return chain(self._entries, self._interfaces)

    def isempty(self):
        """
        Return whether there are manifest entries to write
        """
        return len(self._entries) + len(self._interfaces) == 0


class MinifiedCommentStripped(BaseFile):
    """
    File class for content minified by stripping comments. This wraps around a
    BaseFile instance, and removes lines starting with a # from its content.
    """

    def __init__(self, file):
        assert isinstance(file, BaseFile)
        self._file = file

    def open(self):
        """
        Return a file-like object allowing to read() the minified content of
        the underlying file.
        """
        content = "".join(
            l
            for l in [six.ensure_text(s) for s in self._file.open().readlines()]
            if not l.startswith("#")
        )
        return BytesIO(six.ensure_binary(content))


class MinifiedJavaScript(BaseFile):
    """
    File class for minifying JavaScript files.
    """

    def __init__(self, file, verify_command=None):
        assert isinstance(file, BaseFile)
        self._file = file
        self._verify_command = verify_command

    def open(self):
        output = six.StringIO()
        minify = JavascriptMinify(
            codecs.getreader("utf-8")(self._file.open()), output, quote_chars="'\"`"
        )
        minify.minify()
        output.seek(0)
        output_source = six.ensure_binary(output.getvalue())
        output = BytesIO(output_source)

        if not self._verify_command:
            return output

        input_source = self._file.open().read()

        with NamedTemporaryFile("wb+") as fh1, NamedTemporaryFile("wb+") as fh2:
            fh1.write(input_source)
            fh2.write(output_source)
            fh1.flush()
            fh2.flush()

            try:
                args = list(self._verify_command)
                args.extend([fh1.name, fh2.name])
                subprocess.check_output(
                    args, stderr=subprocess.STDOUT, universal_newlines=True
                )
            except subprocess.CalledProcessError as e:
                errors.warn(
                    "JS minification verification failed for %s:"
                    % (getattr(self._file, "path", "<unknown>"))
                )
                # Prefix each line with "Warning:" so mozharness doesn't
                # think these error messages are real errors.
                for line in e.output.splitlines():
                    errors.warn(line)

                return self._file.open()

        return output


class BaseFinder:
    def __init__(
        self, base, minify=False, minify_js=False, minify_js_verify_command=None
    ):
        """
        Initializes the instance with a reference base directory.

        The optional minify argument specifies whether minification of code
        should occur. minify_js is an additional option to control minification
        of JavaScript. It requires minify to be True.

        minify_js_verify_command can be used to optionally verify the results
        of JavaScript minification. If defined, it is expected to be an iterable
        that will constitute the first arguments to a called process which will
        receive the filenames of the original and minified JavaScript files.
        The invoked process can then verify the results. If minification is
        rejected, the process exits with a non-0 exit code and the original
        JavaScript source is used. An example value for this argument is
        ('/path/to/js', '/path/to/verify/script.js').
        """
        if minify_js and not minify:
            raise ValueError("minify_js requires minify.")

        self.base = mozpath.normsep(base)
        self._minify = minify
        self._minify_js = minify_js
        self._minify_js_verify_command = minify_js_verify_command

    def find(self, pattern):
        """
        Yield path, BaseFile_instance pairs for all files under the base
        directory and its subdirectories that match the given pattern. See the
        mozpack.path.match documentation for a description of the handled
        patterns.
        """
        while pattern.startswith("/"):
            pattern = pattern[1:]
        for p, f in self._find(pattern):
            yield p, self._minify_file(p, f)

    def get(self, path):
        """Obtain a single file.

        Where ``find`` is tailored towards matching multiple files, this method
        is used for retrieving a single file. Use this method when performance
        is critical.

        Returns a ``BaseFile`` if at most one file exists or ``None`` otherwise.
        """
        files = list(self.find(path))
        if len(files) != 1:
            return None
        return files[0][1]

    def __iter__(self):
        """
        Iterates over all files under the base directory (excluding files
        starting with a '.' and files at any level under a directory starting
        with a '.').
            for path, file in finder:
                ...
        """
        return self.find("")

    def __contains__(self, pattern):
        raise RuntimeError(
            "'in' operator forbidden for %s. Use contains()." % self.__class__.__name__
        )

    def contains(self, pattern):
        """
        Return whether some files under the base directory match the given
        pattern. See the mozpack.path.match documentation for a description of
        the handled patterns.
        """
        return any(self.find(pattern))

    def _minify_file(self, path, file):
        """
        Return an appropriate MinifiedSomething wrapper for the given BaseFile
        instance (file), according to the file type (determined by the given
        path), if the FileFinder was created with minification enabled.
        Otherwise, just return the given BaseFile instance.
        """
        if not self._minify or isinstance(file, ExecutableFile):
            return file

        if path.endswith((".ftl", ".properties")):
            return MinifiedCommentStripped(file)

        if self._minify_js and path.endswith((".js", ".jsm", ".mjs")):
            return MinifiedJavaScript(file, self._minify_js_verify_command)

        return file

    def _find_helper(self, pattern, files, file_getter):
        """Generic implementation of _find.

        A few *Finder implementations share logic for returning results.
        This function implements the custom logic.

        The ``file_getter`` argument is a callable that receives a path
        that is known to exist. The callable should return a ``BaseFile``
        instance.
        """
        if "*" in pattern:
            for p in files:
                if mozpath.match(p, pattern):
                    yield p, file_getter(p)
        elif pattern == "":
            for p in files:
                yield p, file_getter(p)
        elif pattern in files:
            yield pattern, file_getter(pattern)
        else:
            for p in files:
                if mozpath.basedir(p, [pattern]) == pattern:
                    yield p, file_getter(p)


class FileFinder(BaseFinder):
    """
    Helper to get appropriate BaseFile instances from the file system.
    """

    def __init__(
        self,
        base,
        find_executables=False,
        ignore=(),
        ignore_broken_symlinks=False,
        find_dotfiles=False,
        **kargs
    ):
        """
        Create a FileFinder for files under the given base directory.

        The find_executables argument determines whether the finder needs to
        try to guess whether files are executables. Disabling this guessing
        when not necessary can speed up the finder significantly.

        ``ignore`` accepts an iterable of patterns to ignore. Entries are
        strings that match paths relative to ``base`` using
        ``mozpath.match()``. This means if an entry corresponds
        to a directory, all files under that directory will be ignored. If
        an entry corresponds to a file, that particular file will be ignored.
        ``ignore_broken_symlinks`` is passed by the packager to work around an
        issue with the build system not cleaning up stale files in some common
        cases. See bug 1297381.
        """
        BaseFinder.__init__(self, base, **kargs)
        self.find_dotfiles = find_dotfiles
        self.find_executables = find_executables
        self.ignore = tuple(mozpath.normsep(path) for path in ignore)
        self.ignore_broken_symlinks = ignore_broken_symlinks

    def _find(self, pattern):
        """
        Actual implementation of FileFinder.find(), dispatching to specialized
        member functions depending on what kind of pattern was given.
        Note all files with a name starting with a '.' are ignored when
        scanning directories, but are not ignored when explicitely requested.
        """
        if "*" in pattern:
            return self._find_glob("", mozpath.split(pattern))
        elif os.path.isdir(os.path.join(self.base, pattern)):
            return self._find_dir(pattern)
        else:
            f = self.get(pattern)
            return ((pattern, f),) if f else ()

    def _find_dir(self, path):
        """
        Actual implementation of FileFinder.find() when the given pattern
        corresponds to an existing directory under the base directory.
        Ignores file names starting with a '.' under the given path. If the
        path itself has leafs starting with a '.', they are not ignored.
        """
        for p in self.ignore:
            if mozpath.match(path, p):
                return

        # The sorted makes the output idempotent. Otherwise, we are
        # likely dependent on filesystem implementation details, such as
        # inode ordering.
        for p in sorted(os.listdir(os.path.join(self.base, path))):
            if p.startswith("."):
                if p in (".", ".."):
                    continue
                if not self.find_dotfiles:
                    continue
            for p_, f in self._find(mozpath.join(path, p)):
                yield p_, f

    def get(self, path):
        srcpath = os.path.join(self.base, path)
        if not os.path.lexists(srcpath):
            return None

        if self.ignore_broken_symlinks and not os.path.exists(srcpath):
            return None

        for p in self.ignore:
            if mozpath.match(path, p):
                return None

        if self.find_executables and is_executable(srcpath):
            return ExecutableFile(srcpath)
        else:
            return File(srcpath)

    def _find_glob(self, base, pattern):
        """
        Actual implementation of FileFinder.find() when the given pattern
        contains globbing patterns ('*' or '**'). This is meant to be an
        equivalent of:
            for p, f in self:
                if mozpath.match(p, pattern):
                    yield p, f
        but avoids scanning the entire tree.
        """
        if not pattern:
            for p, f in self._find(base):
                yield p, f
        elif pattern[0] == "**":
            for p, f in self._find(base):
                if mozpath.match(p, mozpath.join(*pattern)):
                    yield p, f
        elif "*" in pattern[0]:
            if not os.path.exists(os.path.join(self.base, base)):
                return

            for p in self.ignore:
                if mozpath.match(base, p):
                    return

            # See above comment w.r.t. sorted() and idempotent behavior.
            for p in sorted(os.listdir(os.path.join(self.base, base))):
                if p.startswith(".") and not pattern[0].startswith("."):
                    continue
                if mozpath.match(p, pattern[0]):
                    for p_, f in self._find_glob(mozpath.join(base, p), pattern[1:]):
                        yield p_, f
        else:
            for p, f in self._find_glob(mozpath.join(base, pattern[0]), pattern[1:]):
                yield p, f


class JarFinder(BaseFinder):
    """
    Helper to get appropriate DeflatedFile instances from a JarReader.
    """

    def __init__(self, base, reader, **kargs):
        """
        Create a JarFinder for files in the given JarReader. The base argument
        is used as an indication of the Jar file location.
        """
        assert isinstance(reader, JarReader)
        BaseFinder.__init__(self, base, **kargs)
        self._files = OrderedDict((f.filename, f) for f in reader)

    def _find(self, pattern):
        """
        Actual implementation of JarFinder.find(), dispatching to specialized
        member functions depending on what kind of pattern was given.
        """
        return self._find_helper(
            pattern, self._files, lambda x: DeflatedFile(self._files[x])
        )


class TarFinder(BaseFinder):
    """
    Helper to get files from a TarFile.
    """

    def __init__(self, base, tar, **kargs):
        """
        Create a TarFinder for files in the given TarFile. The base argument
        is used as an indication of the Tar file location.
        """
        assert isinstance(tar, TarFile)
        self._tar = tar
        BaseFinder.__init__(self, base, **kargs)
        self._files = OrderedDict((f.name, f) for f in tar if f.isfile())

    def _find(self, pattern):
        """
        Actual implementation of TarFinder.find(), dispatching to specialized
        member functions depending on what kind of pattern was given.
        """
        return self._find_helper(
            pattern, self._files, lambda x: ExtractedTarFile(self._tar, self._files[x])
        )


class ComposedFinder(BaseFinder):
    """
    Composes multiple File Finders in some sort of virtual file system.

    A ComposedFinder is initialized from a dictionary associating paths
    to `*Finder instances.`

    Note this could be optimized to be smarter than getting all the files
    in advance.
    """

    def __init__(self, finders):
        # Can't import globally, because of the dependency of mozpack.copier
        # on this module.
        from mozpack.copier import FileRegistry

        self.files = FileRegistry()

        for base, finder in sorted(finders.items()):
            if self.files.contains(base):
                self.files.remove(base)
            for p, f in finder.find(""):
                self.files.add(mozpath.join(base, p), f)

    def find(self, pattern):
        for p in self.files.match(pattern):
            yield p, self.files[p]


class MercurialFile(BaseFile):
    """File class for holding data from Mercurial."""

    def __init__(self, client, rev, path):
        self._content = client.cat(
            [six.ensure_binary(path)], rev=six.ensure_binary(rev)
        )

    def open(self):
        return BytesIO(six.ensure_binary(self._content))

    def read(self):
        return self._content


class MercurialRevisionFinder(BaseFinder):
    """A finder that operates on a specific Mercurial revision."""

    def __init__(self, repo, rev=".", recognize_repo_paths=False, **kwargs):
        """Create a finder attached to a specific revision in a repository.

        If no revision is given, open the parent of the working directory.

        ``recognize_repo_paths`` will enable a mode where ``.get()`` will
        recognize full paths that include the repo's path. Typically Finder
        instances are "bound" to a base directory and paths are relative to
        that directory. This mode changes that. When this mode is activated,
        ``.find()`` will not work! This mode exists to support the moz.build
        reader, which uses absolute paths instead of relative paths. The reader
        should eventually be rewritten to use relative paths and this hack
        should be removed (TODO bug 1171069).
        """
        if not hglib:
            raise Exception("hglib package not found")

        super(MercurialRevisionFinder, self).__init__(base=repo, **kwargs)

        self._root = mozpath.normpath(repo).rstrip("/")
        self._recognize_repo_paths = recognize_repo_paths

        # We change directories here otherwise we have to deal with relative
        # paths.
        oldcwd = os.getcwd()
        os.chdir(self._root)
        try:
            self._client = hglib.open(path=repo, encoding=b"utf-8")
        finally:
            os.chdir(oldcwd)
        self._rev = rev if rev is not None else "."
        self._files = OrderedDict()

        # Immediately populate the list of files in the repo since nearly every
        # operation requires this list.
        out = self._client.rawcommand(
            [
                b"files",
                b"--rev",
                six.ensure_binary(self._rev),
            ]
        )
        for relpath in out.splitlines():
            # Mercurial may use \ as path separator on Windows. So use
            # normpath().
            self._files[six.ensure_text(mozpath.normpath(relpath))] = None

    def _find(self, pattern):
        if self._recognize_repo_paths:
            raise NotImplementedError("cannot use find with recognize_repo_path")

        return self._find_helper(pattern, self._files, self._get)

    def get(self, path):
        path = mozpath.normpath(path)
        if self._recognize_repo_paths:
            if not path.startswith(self._root):
                raise ValueError(
                    "lookups in recognize_repo_paths mode must be "
                    "prefixed with repo path: %s" % path
                )
            path = path[len(self._root) + 1 :]

        try:
            return self._get(path)
        except KeyError:
            return None

    def _get(self, path):
        # We lazy populate self._files because potentially creating tens of
        # thousands of MercurialFile instances for every file in the repo is
        # inefficient.
        f = self._files[path]
        if not f:
            f = MercurialFile(self._client, self._rev, path)
            self._files[path] = f

        return f


class FileListFinder(BaseFinder):
    """Finder for a literal list of file names."""

    def __init__(self, files):
        self._files = sorted(files)

    @memoize
    def _match(self, pattern):
        """Return a sorted list of all files matching the given pattern."""
        # We don't use the utility _find_helper method because it's not tuned
        # for performance in the way that we would like this class to be. That's
        # a possible avenue for refactoring here.
        ret = []
        # We do this as an optimization to figure out where in the sorted list
        # to search and where to stop searching.
        components = pattern.split("/")
        prefix = "/".join(takewhile(lambda s: "*" not in s, components))
        start = bisect.bisect_left(self._files, prefix)
        for i in range(start, len(self._files)):
            f = self._files[i]
            if not f.startswith(prefix):
                break
            # Skip hidden files while scanning.
            if "/." in f[len(prefix) :]:
                continue
            if mozpath.match(f, pattern):
                ret.append(f)
        return ret

    def find(self, pattern):
        pattern = pattern.strip("/")
        for path in self._match(pattern):
            yield path, File(path)
