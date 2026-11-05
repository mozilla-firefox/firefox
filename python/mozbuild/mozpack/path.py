# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Like :py:mod:`os.path`, with a reduced set of functions, and with normalized path
separators (always use forward slashes).
Also contains a few additional utilities not found in :py:mod:`os.path`.
"""

import functools
import os
import posixpath
import re
import sys


def normsep(path):
    """
    Normalize path separators, by using forward slashes instead of whatever
    :py:const:`os.sep` is.
    """
    if os.sep != "/":
        # Python 2 is happy to do things like byte_string.replace(u'foo',
        # u'bar'), but not Python 3.
        if isinstance(path, bytes):
            path = path.replace(os.sep.encode("ascii"), b"/")
        else:
            path = path.replace(os.sep, "/")
    if os.altsep and os.altsep != "/":
        if isinstance(path, bytes):
            path = path.replace(os.altsep.encode("ascii"), b"/")
        else:
            path = path.replace(os.altsep, "/")
    return path


def strip_extended_length_prefix(path):
    """Strip the Windows extended-length path prefix and normalize.

    On Windows, some APIs return paths prefixed with ``\\\\?\\`` (or
    ``//?/`` after separator normalisation).  Strip that prefix so
    comparisons against regular paths succeed.
    """
    return os.path.normpath(path.removeprefix("\\\\?\\").removeprefix("//?/"))


@functools.cache
def relpath(path, start):
    path = normsep(path)
    start = normsep(start)
    if sys.platform == "win32":
        # os.path.relpath can't handle relative paths between UNC and non-UNC
        # paths, so strip an extended-length prefix if present (bug 1581248)
        path = strip_extended_length_prefix(path)
        start = strip_extended_length_prefix(start)
    try:
        rel = os.path.relpath(path, start)
    except ValueError:
        # On Windows this can throw a ValueError if the two paths are on
        # different drives. In that case, just return the path.
        return abspath(path)
    rel = normsep(rel)
    return "" if rel == "." else rel


def realpath(path):
    return normsep(os.path.realpath(path))


def abspath(path):
    return normsep(os.path.abspath(path))


def join(*paths):
    return normsep(os.path.join(*paths))


def normpath(path):
    return posixpath.normpath(normsep(path))


def dirname(path):
    return posixpath.dirname(normsep(path))


def commonprefix(paths):
    return posixpath.commonprefix([normsep(path) for path in paths])


def basename(path):
    return os.path.basename(path)


def splitext(path):
    return posixpath.splitext(normsep(path))


def split(path):
    """
    Return the normalized path as a list of its components.

        ``split('foo/bar/baz')`` returns ``['foo', 'bar', 'baz']``
    """
    return normsep(path).split("/")


def basedir(path, bases):
    """
    Given a list of directories (`bases`), return which one contains the given
    path. If several matches are found, the deepest base directory is returned.

        ``basedir('foo/bar/baz', ['foo', 'baz', 'foo/bar'])`` returns ``'foo/bar'``
        (`'foo'` and `'foo/bar'` both match, but `'foo/bar'` is the deepest match)
    """
    path = normsep(path)
    bases = [normsep(b) for b in bases]
    if path in bases:
        return path
    for b in sorted(bases, reverse=True):
        if b == "" or path.startswith(b + "/"):
            return b


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
    return _pattern_re(pattern).match(path) is not None


def _pattern_re(pattern):
    if pattern not in re_cache:
        p = re.escape(pattern)
        p = MATCH_STAR_STAR_RE.sub(r"\1(?:.+/)?", p)
        p = MATCH_STAR_STAR_END_RE.sub(r"(?:\1.+)?", p)
        p = p.replace(r"\*", "[^/]*") + "(?:/.*)?$"
        re_cache[pattern] = re.compile(p)
    return re_cache[pattern]


def matcher(patterns):
    """
    Return a function that returns whether a path matches any of the given
    patterns, following the same rules as ``match``. Testing many paths against
    the returned function is faster than calling ``match`` for each pattern.
    """
    if not patterns:
        return lambda path: False
    if not all(patterns):
        return lambda path: True
    literals = {p for p in patterns if "*" not in p}
    globs = [p for p in patterns if "*" in p]
    regex = (
        re.compile("|".join(f"(?:{_pattern_re(p).pattern})" for p in globs))
        if globs
        else None
    )

    def matches(path):
        if literals:
            end = path.find("/")
            while end != -1:
                if path[:end] in literals:
                    return True
                end = path.find("/", end + 1)
            if path in literals:
                return True
        return regex is not None and regex.match(path) is not None

    return matches


def rebase(oldbase, base, relativepath):
    """
    Return `relativepath` relative to `base` instead of `oldbase`.
    """
    if base == oldbase:
        return relativepath
    if len(base) < len(oldbase):
        assert basedir(oldbase, [base]) == base
        relbase = relpath(oldbase, base)
        result = join(relbase, relativepath)
    else:
        assert basedir(base, [oldbase]) == oldbase
        relbase = relpath(base, oldbase)
        result = relpath(relativepath, relbase)
    result = normpath(result)
    if relativepath.endswith("/") and not result.endswith("/"):
        result += "/"
    return result
