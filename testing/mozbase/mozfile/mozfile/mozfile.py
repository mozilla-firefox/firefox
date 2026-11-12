# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# We don't import all modules at the top for performance reasons. See Bug 1008943

import errno
import os
import re
import stat
import sys
import warnings
from contextlib import contextmanager
from textwrap import dedent
from urllib.parse import urlparse
from urllib.request import urlopen

__all__ = [
    "extract_tarball",
    "extract_zip",
    "extract",
    "is_url",
    "load",
    "load_source",
    "copy_contents",
    "match",
    "move",
    "remove",
    "rmtree",
    "tree",
    "which",
    "NamedTemporaryFile",
    "TemporaryDirectory",
]

# utilities for extracting archives


def extract_tarball(src, dest, ignore=None):
    """extract a .tar file"""

    import tarfile

    def _is_within_directory(directory, target):
        real_directory = os.path.realpath(directory)
        real_target = os.path.realpath(target)
        prefix = os.path.commonprefix([real_directory, real_target])
        return prefix == real_directory

    with tarfile.open(src) as bundle:
        namelist = []

        for m in bundle:
            # Mitigation for CVE-2007-4559, Python's tarfile library will allow
            # writing files outside of the intended destination.
            member_path = os.path.join(dest, m.name)
            if not _is_within_directory(dest, member_path):
                raise RuntimeError(
                    dedent(
                        f"""
                    Tar bundle '{src}' may be maliciously crafted to escape the destination!
                    The following path was detected:

                      {m.name}
                    """
                    )
                )
            if m.issym():
                link_path = os.path.join(os.path.dirname(member_path), m.linkname)
                if not _is_within_directory(dest, link_path):
                    raise RuntimeError(
                        dedent(
                            f"""
                    Tar bundle '{src}' may be maliciously crafted to escape the destination!
                    The following path was detected:

                      {m.name}
                        """
                        )
                    )

            if m.mode & (stat.S_ISUID | stat.S_ISGID):
                raise RuntimeError(
                    dedent(
                        f"""
                    Tar bundle '{src}' may be maliciously crafted to setuid/setgid!
                    The following path was detected:

                      {m.name}
                    """
                    )
                )

            if ignore and any(match(m.name, i) for i in ignore):
                continue
            bundle.extract(m, path=dest)
            namelist.append(m.name)

    return namelist


def extract_zip(src, dest, ignore=None):
    """extract a zip file"""

    import zipfile

    if isinstance(src, zipfile.ZipFile):
        bundle = src
    else:
        try:
            bundle = zipfile.ZipFile(src)
        except Exception:
            print("src: %s" % src)
            raise

    namelist = bundle.namelist()

    for name in namelist:
        if ignore and any(match(name, i) for i in ignore):
            continue

        bundle.extract(name, dest)
        filename = os.path.realpath(os.path.join(dest, name))
        mode = bundle.getinfo(name).external_attr >> 16 & 0x1FF
        # Only update permissions if attributes are set. Otherwise fallback to the defaults.
        if mode:
            os.chmod(filename, mode)
    bundle.close()
    return namelist


def extract(src, dest=None, ignore=None):
    """
    Takes in a tar or zip file and extracts it to dest

    If dest is not specified, extracts to os.path.dirname(src)

    Returns the list of top level files that were extracted
    """

    import tarfile
    import zipfile

    assert os.path.exists(src), "'%s' does not exist" % src

    if dest is None:
        dest = os.path.dirname(src)
    elif not os.path.isdir(dest):
        os.makedirs(dest)
    assert not os.path.isfile(dest), "dest cannot be a file"

    if tarfile.is_tarfile(src):
        namelist = extract_tarball(src, dest, ignore=ignore)
    elif zipfile.is_zipfile(src):
        namelist = extract_zip(src, dest, ignore=ignore)
    else:
        raise Exception("mozfile.extract: no archive format found for '%s'" % src)

    # namelist returns paths with forward slashes even in windows
    top_level_files = [
        os.path.join(dest, name.rstrip("/"))
        for name in namelist
        if len(name.rstrip("/").split("/")) == 1
    ]

    # namelist doesn't include folders, append these to the list
    for name in namelist:
        index = name.find("/")
        if index != -1:
            root = os.path.join(dest, name[:index])
            if root not in top_level_files:
                top_level_files.append(root)

    return top_level_files


# utilities for removal of files and directories


def rmtree(dir):
    """Deprecated wrapper method to remove a directory tree.

    Ensure to update your code to use mozfile.remove() directly

    :param dir: directory to be removed
    """

    warnings.warn(
        "mozfile.rmtree() is deprecated in favor of mozfile.remove()",
        PendingDeprecationWarning,
        stacklevel=2,
    )
    return remove(dir)


def _call_windows_retry(func, args=(), retry_max=5, retry_delay=0.5):
    """
    It's possible to see spurious errors on Windows due to various things
    keeping a handle to the directory open (explorer, virus scanners, etc)
    So we try a few times if it fails with a known error.
    retry_delay is doubled after each failed attempt to increase the
    likelihood of success in subsequent attempts.
    """
    import redo

    error = None
    for _ in redo.retrier(
        attempts=retry_max + 1, sleeptime=retry_delay, sleepscale=2, jitter=0
    ):
        try:
            return func(*args)
        except OSError as e:
            # Error codes are defined in:
            # http://docs.python.org/2/library/errno.html#module-errno
            if e.errno not in (errno.EACCES, errno.ENOTEMPTY):
                raise
            error = e
            print(
                '%s() failed for "%s". Reason: %s (%s). Retrying...'
                % (func.__name__, args, e.strerror, e.errno)
            )
    raise error


def remove(path):
    """Removes the specified file, link, or directory tree.

    This is a replacement for shutil.rmtree that works better under
    windows. It does the following things:

     - check path access for the current user before trying to remove
     - retry operations on some known errors due to various things keeping
       a handle on file paths - like explorer, virus scanners, etc. The
       known errors are errno.EACCES and errno.ENOTEMPTY, and it will
       retry up to 5 five times with a delay of (failed_attempts * 0.5) seconds
       between each attempt.

    Note that no error will be raised if the given path does not exists.

    :param path: path to be removed
    """

    import shutil

    def _call_with_windows_retry(*args, **kwargs):
        try:
            _call_windows_retry(*args, **kwargs)
        except OSError as e:
            # The file or directory to be removed doesn't exist anymore
            if e.errno != errno.ENOENT:
                raise

    def _update_permissions(path):
        """Sets specified pemissions depending on filetype"""
        if os.path.islink(path):
            # Path is a symlink which we don't have to modify
            # because it should already have all the needed permissions
            return

        stats = os.stat(path)

        if os.path.isfile(path):
            mode = stats.st_mode | stat.S_IWUSR
        elif os.path.isdir(path):
            mode = stats.st_mode | stat.S_IWUSR | stat.S_IXUSR
        else:
            # Not supported type
            return

        _call_with_windows_retry(os.chmod, (path, mode))

    if not os.path.lexists(path):
        return

    """
    On Windows, adds '\\\\?\\' to paths which match ^[A-Za-z]:\\.* to access
    files or directories that exceed MAX_PATH(260) limitation or that ends
    with a period.
    """
    if (
        sys.platform in ("win32", "cygwin")
        and len(path) >= 3
        and path[1] == ":"
        and path[2] == "\\"
    ):
        path = "\\\\?\\%s" % path

    if os.path.isfile(path) or os.path.islink(path):
        # Verify the file or link is read/write for the current user
        _update_permissions(path)
        _call_with_windows_retry(os.remove, (path,))

    elif os.path.isdir(path):
        # Verify the directory is read/write/execute for the current user
        _update_permissions(path)

        # We're ensuring that every nested item has writable permission.
        for root, dirs, files in os.walk(path):
            for entry in dirs + files:
                _update_permissions(os.path.join(root, entry))
        _call_with_windows_retry(shutil.rmtree, (path,))


def copy_contents(srcdir, dstdir, ignore_dangling_symlinks=False):
    """
    Copy the contents of the srcdir into the dstdir, preserving
    subdirectories.

    If an existing file of the same name exists in dstdir, it will be overwritten.
    """
    import shutil

    # On Windows, use the verbose mechanism because _call_with_windows_retry
    # doesn't allow named arguments (dirs_exist_ok) to be passed.
    if os.name == "nt":
        names = os.listdir(srcdir)
        if not os.path.isdir(dstdir):
            os.makedirs(dstdir)
        errors = []
        for name in names:
            srcname = os.path.join(srcdir, name)
            dstname = os.path.join(dstdir, name)
            try:
                if os.path.islink(srcname):
                    linkto = os.readlink(srcname)
                    os.symlink(linkto, dstname)
                elif os.path.isdir(srcname):
                    copy_contents(
                        srcname,
                        dstname,
                        ignore_dangling_symlinks=ignore_dangling_symlinks,
                    )
                else:
                    _call_windows_retry(shutil.copy2, (srcname, dstname))
            except OSError as why:
                errors.append((srcname, dstname, str(why)))
            except Exception as err:
                errors.extend(err)
        try:
            _call_windows_retry(shutil.copystat, (srcdir, dstdir))
        except OSError as why:
            if why.winerror is None:
                errors.extend((srcdir, dstdir, str(why)))
        if errors:
            raise Exception(errors)
    else:

        def ignore_dangling(dirname, names):
            return [
                name
                for name in names
                if os.path.islink(path := os.path.join(dirname, name))
                and not os.path.exists(path)
            ]

        shutil.copytree(
            srcdir,
            dstdir,
            ignore=ignore_dangling if ignore_dangling_symlinks else None,
            dirs_exist_ok=True,
        )


def move(src, dst):
    """
    Move a file or directory path.

    This is a replacement for shutil.move that works better under windows,
    retrying operations on some known errors due to various things keeping
    a handle on file paths.
    """
    import shutil

    _call_windows_retry(shutil.move, (src, dst))


def depth(directory):
    """returns the integer depth of a directory or path relative to '/'"""

    directory = os.path.abspath(directory)
    level = 0
    while True:
        directory, remainder = os.path.split(directory)
        level += 1
        if not remainder:
            break
    return level


def tree(directory, sort_key=lambda x: x.lower()):
    """Display tree directory structure for `directory`."""
    vertical_line = "│"
    item_marker = "├"
    last_child = "└"

    retval = []
    indent = []
    last = {}
    top = depth(directory)

    for dirpath, dirnames, filenames in os.walk(directory, topdown=True):
        abspath = os.path.abspath(dirpath)
        basename = os.path.basename(abspath)
        parent = os.path.dirname(abspath)
        level = depth(abspath) - top

        # sort articles of interest
        for resource in (dirnames, filenames):
            resource[:] = sorted(resource, key=sort_key)

        if level > len(indent):
            indent.append(vertical_line)
        indent = indent[:level]

        if dirnames:
            files_end = item_marker
            last[abspath] = dirnames[-1]
        else:
            files_end = last_child

        if last.get(parent) == os.path.basename(abspath):
            # last directory of parent
            dirpath_mark = last_child
            indent[-1] = " "
        elif not indent:
            dirpath_mark = ""
        else:
            dirpath_mark = item_marker

        # append the directory and piece of tree structure
        # if the top-level entry directory, print as passed
        retval.append(
            "%s%s%s"
            % ("".join(indent[:-1]), dirpath_mark, basename if retval else directory)
        )
        # add the files
        if filenames:
            last_file = filenames[-1]
            retval.extend([
                (
                    "%s%s%s"
                    % (
                        "".join(indent),
                        files_end if filename == last_file else item_marker,
                        filename,
                    )
                )
                for index, filename in enumerate(filenames)
            ])

    return "\n".join(retval)


def which(cmd, mode=os.F_OK | os.X_OK, path=None, exts=None, extra_search_dirs=()):
    """A wrapper around `shutil.which` to make the behavior on Windows
    consistent with other platforms.

    On non-Windows platforms, this is a direct call to `shutil.which`. On
    Windows, this:

    * Ensures that `cmd` without an extension will be found. Previously it was
      only found if it had an extension in `PATHEXT`.
    * Ensures the absolute path to the binary is returned. Previously if the
      binary was found in `cwd`, a relative path was returned.
    * Checks the Windows registry if shutil.which doesn't come up with anything.

    The arguments are the same as the ones in `shutil.which`. In addition there
    is an `exts` argument that only has an effect on Windows. This is used to
    set a custom value for PATHEXT and is formatted as a list of file
    extensions.

    extra_search_dirs is a convenience argument. If provided, the strings in
    the sequence will be appended to the END of the given `path`.
    """
    from shutil import which as shutil_which

    if isinstance(path, (list, tuple)):
        path = os.pathsep.join(path)

    if not path:
        path = os.environ.get("PATH", os.defpath)

    if extra_search_dirs:
        path = os.pathsep.join([path] + list(extra_search_dirs))

    if sys.platform != "win32":
        return shutil_which(cmd, mode=mode, path=path)

    oldexts = os.environ.get("PATHEXT", "")
    if not exts:
        exts = oldexts.split(os.pathsep)

    os.environ["PATHEXT"] = os.pathsep.join(exts)
    try:
        path = shutil_which(cmd, mode=mode, path=path)
        if path:
            return os.path.abspath(path.rstrip("."))
    finally:
        if oldexts:
            os.environ["PATHEXT"] = oldexts
        else:
            del os.environ["PATHEXT"]

    # If we've gotten this far, we need to check for registered executables
    # before giving up.
    try:
        import winreg
    except ImportError:
        import _winreg as winreg
    if not cmd.lower().endswith(".exe"):
        cmd += ".exe"
    try:
        ret = winreg.QueryValue(
            winreg.HKEY_LOCAL_MACHINE,
            r"SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\%s" % cmd,
        )
        return os.path.abspath(ret) if ret else None
    except winreg.error:
        return None


# utilities for temporary resources


class NamedTemporaryFile:
    """
    Like tempfile.NamedTemporaryFile except it works on Windows
    in the case where you open the created file a second time.

    This behaves very similarly to tempfile.NamedTemporaryFile but may
    not behave exactly the same. For example, this function does not
    prevent fd inheritance by children.

    Example usage:

    with NamedTemporaryFile() as fh:
        fh.write(b'foobar')

        print('Filename: %s' % fh.name)

    see https://bugzilla.mozilla.org/show_bug.cgi?id=821362
    """

    def __init__(
        self, mode="w+b", bufsize=-1, suffix="", prefix="tmp", dir=None, delete=True
    ):
        import tempfile

        fd, path = tempfile.mkstemp(suffix, prefix, dir, "t" in mode)
        os.close(fd)

        self.file = open(path, mode)
        self._path = path
        self._delete = delete
        self._unlinked = False

    def __getattr__(self, k):
        return getattr(self.__dict__["file"], k)

    def __iter__(self):
        return self.__dict__["file"]

    def __enter__(self):
        self.file.__enter__()
        return self

    def __exit__(self, exc, value, tb):
        self.file.__exit__(exc, value, tb)
        if self.__dict__["_delete"]:
            os.unlink(self.__dict__["_path"])
            self._unlinked = True

    def __del__(self):
        if self.__dict__["_unlinked"]:
            return
        self.file.__exit__(None, None, None)
        if self.__dict__["_delete"]:
            os.unlink(self.__dict__["_path"])


@contextmanager
def TemporaryDirectory():
    """
    create a temporary directory using tempfile.mkdtemp, and then clean it up.

    Example usage:
    with TemporaryDirectory() as tmp:
       open(os.path.join(tmp, "a_temp_file"), "w").write("data")

    """

    import shutil
    import tempfile

    tempdir = tempfile.mkdtemp()
    try:
        yield tempdir
    finally:
        shutil.rmtree(tempdir)


# utilities dealing with URLs


def is_url(thing):
    """
    Return True if thing looks like a URL.
    """

    parsed = urlparse(thing)
    if "scheme" in parsed:
        return len(parsed.scheme) >= 2
    else:
        return len(parsed[0]) >= 2


def load(resource):
    """
    open a file or URL for reading.  If the passed resource string is not a URL,
    or begins with 'file://', return a ``file``.  Otherwise, return the
    result of urllib.urlopen()
    """

    # handle file URLs separately due to python stdlib limitations
    if resource.startswith("file://"):
        resource = resource[len("file://") :]

    if not is_url(resource):
        # if no scheme is given, it is a file path
        return open(resource)

    return urlopen(resource)


# see https://docs.python.org/3/whatsnew/3.12.html#imp
def load_source(modname, filename):
    import importlib.machinery
    import importlib.util

    loader = importlib.machinery.SourceFileLoader(modname, filename)
    spec = importlib.util.spec_from_file_location(modname, filename, loader=loader)
    module = importlib.util.module_from_spec(spec)
    sys.modules[module.__name__] = module
    loader.exec_module(module)
    return module


# We can't depend on mozpack.path here, so copy the 'match' function over.

re_cache = {}
# Python versions < 3.7 return r'\/' for re.escape('/').
if re.escape("/") == "/":
    MATCH_STAR_STAR_RE = re.compile(r"(^|/)\\\*\\\*/")
    MATCH_STAR_STAR_END_RE = re.compile(r"(^|/)\\\*\\\*$")
else:
    MATCH_STAR_STAR_RE = re.compile(r"(^|\\\/)\\\*\\\*\\\/")
    MATCH_STAR_STAR_END_RE = re.compile(r"(^|\\\/)\\\*\\\*$")


def match(path, pattern):
    """
    Return whether the given path matches the given pattern.
    An asterisk can be used to match any string, including the null string, in
    one part of the path:

        ``foo`` matches ``*``, ``f*`` or ``fo*o``

    However, an asterisk matching a subdirectory may not match the null string:

        ``foo/bar`` does *not* match ``foo/*/bar``

    If the pattern matches one of the ancestor directories of the path, the
    patch is considered matching:

        ``foo/bar`` matches ``foo``

    Two adjacent asterisks can be used to match files and zero or more
    directories and subdirectories.

        ``foo/bar`` matches ``foo/**/bar``, or ``**/bar``
    """
    if not pattern:
        return True
    if pattern not in re_cache:
        p = re.escape(pattern)
        p = MATCH_STAR_STAR_RE.sub(r"\1(?:.+/)?", p)
        p = MATCH_STAR_STAR_END_RE.sub(r"(?:\1.+)?", p)
        p = p.replace(r"\*", "[^/]*") + "(?:/.*)?$"
        re_cache[pattern] = re.compile(p)
    return re_cache[pattern].match(path) is not None
