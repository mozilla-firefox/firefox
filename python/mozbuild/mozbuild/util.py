# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# This file contains miscellaneous utility functions that don't belong anywhere
# in particular.
import argparse
import collections
import collections.abc
import copy
import difflib
import functools
import hashlib
import itertools
import os
import re
import subprocess
import sys
from io import BytesIO, StringIO
from pathlib import Path

import six

from mozbuild.dirutils import ensureParentDir

try:
    import psutil
except Exception:
    psutil = None

MOZBUILD_METRICS_PATH = os.path.abspath(
    os.path.join(__file__, "..", "..", "metrics.yaml")
)

if sys.platform == "win32":
    system_encoding = "mbcs"
else:
    system_encoding = "utf-8"


def _open(path, mode):
    if "b" in mode:
        return open(path, mode)
    return open(path, mode, encoding="utf-8", newline="\n")


def hash_file(path, hasher=None):
    """Hashes a file specified by the path given and returns the hex digest."""

    # If the default hashing function changes, this may invalidate
    # lots of cached data.  Don't change it lightly.
    h = hasher or hashlib.sha1()

    with open(path, "rb") as fh:
        while True:
            data = fh.read(8192)

            if not len(data):
                break

            h.update(data)

    return h.hexdigest()


class EmptyValue(str):
    """A dummy type that behaves like an empty string and sequence.

    This type exists in order to support
    :py:class:`mozbuild.frontend.reader.EmptyConfig`. It should likely not be
    used elsewhere.
    """

    def __init__(self):
        super(EmptyValue, self).__init__()


class ReadOnlyNamespace:
    """A class for objects with immutable attributes set at initialization."""

    def __init__(self, **kwargs):
        for k, v in kwargs.items():
            super(ReadOnlyNamespace, self).__setattr__(k, v)

    def __delattr__(self, key):
        raise Exception("Object does not support deletion.")

    def __setattr__(self, key, value):
        raise Exception("Object does not support assignment.")

    def __ne__(self, other):
        return not (self == other)

    def __eq__(self, other):
        return self is other or (
            hasattr(other, "__dict__") and self.__dict__ == other.__dict__
        )

    def __repr__(self):
        return "<%s %r>" % (self.__class__.__name__, self.__dict__)


class ReadOnlyDict(dict):
    """A read-only dictionary."""

    def __init__(self, *args, **kwargs):
        dict.__init__(self, *args, **kwargs)

    def __delitem__(self, key):
        raise Exception("Object does not support deletion.")

    def __setitem__(self, key, value):
        raise Exception("Object does not support assignment.")

    def update(self, *args, **kwargs):
        raise Exception("Object does not support update.")

    def __copy__(self, *args, **kwargs):
        return ReadOnlyDict(**dict.copy(self, *args, **kwargs))

    def __deepcopy__(self, memo):
        result = {}
        for k, v in self.items():
            result[k] = copy.deepcopy(v, memo)

        return ReadOnlyDict(**result)

    def __reduce__(self, *args, **kwargs):
        """
        Support for `pickle`.
        """

        return (self.__class__, (dict(self),))


class undefined_default:
    """Represents an undefined argument value that isn't None."""


undefined = undefined_default()


class ReadOnlyDefaultDict(ReadOnlyDict):
    """A read-only dictionary that supports default values on retrieval."""

    def __init__(self, default_factory, *args, **kwargs):
        ReadOnlyDict.__init__(self, *args, **kwargs)
        self._default_factory = default_factory

    def __missing__(self, key):
        value = self._default_factory()
        dict.__setitem__(self, key, value)
        return value


def simple_diff(filename, old_lines, new_lines):
    """Returns the diff between old_lines and new_lines, in unified diff form,
    as a list of lines.

    old_lines and new_lines are lists of non-newline terminated lines to
    compare.
    old_lines can be None, indicating a file creation.
    new_lines can be None, indicating a file deletion.
    """

    old_name = "/dev/null" if old_lines is None else filename
    new_name = "/dev/null" if new_lines is None else filename

    return difflib.unified_diff(
        old_lines or [], new_lines or [], old_name, new_name, n=4, lineterm=""
    )


class FileAvoidWrite(BytesIO):
    """File-like object that buffers output and only writes if content changed.

    We create an instance from an existing filename. New content is written to
    it. When we close the file object, if the content in the in-memory buffer
    differs from what is on disk, then we write out the new content. Otherwise,
    the original file is untouched.

    Instances can optionally capture diffs of file changes. This feature is not
    enabled by default because it a) doesn't make sense for binary files b)
    could add unwanted overhead to calls.

    Additionally, there is dry run mode where the file is not actually written
    out, but reports whether the file was existing and would have been updated
    still occur, as well as diff capture if requested.
    """

    def __init__(self, filename, capture_diff=False, dry_run=False, readmode="r"):
        BytesIO.__init__(self)
        self.name = filename
        assert type(capture_diff) is bool
        assert type(dry_run) is bool
        assert "r" in readmode
        self._capture_diff = capture_diff
        self._write_to_file = not dry_run
        self.diff = None
        self.mode = readmode
        self._binary_mode = "b" in readmode

    def write(self, buf):
        BytesIO.write(self, six.ensure_binary(buf))

    def avoid_writing_to_file(self):
        self._write_to_file = False

    def close(self):
        """Stop accepting writes, compare file contents, and rewrite if needed.

        Returns a tuple of bools indicating what action was performed:

            (file existed, file updated)

        If ``capture_diff`` was specified at construction time and the
        underlying file was changed, ``.diff`` will be populated with the diff
        of the result.
        """
        # Use binary data if the caller explicitly asked for it.
        ensure = six.ensure_binary if self._binary_mode else six.ensure_text
        buf = ensure(self.getvalue())

        BytesIO.close(self)
        existed = False
        old_content = None

        try:
            existing = _open(self.name, self.mode)
            existed = True
        except OSError:
            pass
        else:
            try:
                old_content = existing.read()
                if old_content == buf:
                    return True, False
            except OSError:
                pass
            finally:
                existing.close()

        if self._write_to_file:
            ensureParentDir(self.name)
            # Maintain 'b' if specified.  'U' only applies to modes starting with
            # 'r', so it is dropped.
            writemode = "w"
            if self._binary_mode:
                writemode += "b"
                buf = six.ensure_binary(buf)
            else:
                buf = six.ensure_text(buf)
            path = Path(self.name)
            if path.is_symlink():
                # Migration to code autogeneration can encounter with existing symlinks, e.g. bug 1953858.
                path.unlink()
            with _open(path, writemode) as file:
                file.write(buf)

        self._generate_diff(buf, old_content)

        return existed, True

    def _generate_diff(self, new_content, old_content):
        """Generate a diff for the changed contents if `capture_diff` is True.

        If the changed contents could not be decoded as utf-8 then generate a
        placeholder message instead of a diff.

        Args:
            new_content: Str or bytes holding the new file contents.
            old_content: Str or bytes holding the original file contents. Should be
                None if no old content is being overwritten.
        """
        if not self._capture_diff:
            return

        try:
            if old_content is None:
                old_lines = None
            else:
                if self._binary_mode:
                    # difflib doesn't work with bytes.
                    old_content = old_content.decode("utf-8")

                old_lines = old_content.splitlines()

            if self._binary_mode:
                # difflib doesn't work with bytes.
                new_content = new_content.decode("utf-8")

            new_lines = new_content.splitlines()

            self.diff = simple_diff(self.name, old_lines, new_lines)
        # FileAvoidWrite isn't unicode/bytes safe. So, files with non-ascii
        # content or opened and written in different modes may involve
        # implicit conversion and this will make Python unhappy. Since
        # diffing isn't a critical feature, we just ignore the failure.
        # This can go away once FileAvoidWrite uses io.BytesIO and
        # io.StringIO. But that will require a lot of work.
        except (UnicodeDecodeError, UnicodeEncodeError):
            self.diff = ["Binary or non-ascii file changed: %s" % self.name]

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        if not self.closed:
            self.close()


def resolve_target_to_make(topobjdir, target):
    r"""
    Resolve `target` (a target, directory, or file) to a make target.

    `topobjdir` is the object directory; all make targets will be
    rooted at or below the top-level Makefile in this directory.

    Returns a pair `(reldir, target)` where `reldir` is a directory
    relative to `topobjdir` containing a Makefile and `target` is a
    make target (possibly `None`).

    A directory resolves to the nearest directory at or above
    containing a Makefile, and target `None`.

    A regular (non-Makefile) file resolves to the nearest directory at
    or above the file containing a Makefile, and an appropriate
    target.

    A Makefile resolves to the nearest parent strictly above the
    Makefile containing a different Makefile, and an appropriate
    target.
    """

    target = target.replace(os.sep, "/").lstrip("/")
    abs_target = os.path.join(topobjdir, target)

    # For directories, run |make -C dir|. If the directory does not
    # contain a Makefile, check parents until we find one. At worst,
    # this will terminate at the root.
    if os.path.isdir(abs_target):
        current = abs_target

        while True:
            make_path = os.path.join(current, "Makefile")
            if os.path.exists(make_path):
                return (current[len(topobjdir) + 1 :], None)

            current = os.path.dirname(current)

    # If it's not in a directory, this is probably a top-level make
    # target. Treat it as such.
    if "/" not in target:
        return (None, target)

    # We have a relative path within the tree. We look for a Makefile
    # as far into the path as possible. Then, we compute the make
    # target as relative to that directory.
    reldir = os.path.dirname(target)
    target = os.path.basename(target)

    while True:
        make_path = os.path.join(topobjdir, reldir, "Makefile")

        # We append to target every iteration, so the check below
        # happens exactly once.
        if target != "Makefile" and os.path.exists(make_path):
            return (reldir, target)

        target = os.path.join(os.path.basename(reldir), target)
        reldir = os.path.dirname(reldir)


class List(list):
    """A list specialized for moz.build environments.

    We overload the assignment and append operations to require that the
    appended thing is a list. This avoids bad surprises coming from appending
    a string to a list, which would just add each letter of the string.
    """

    def __init__(self, iterable=None, **kwargs):
        if iterable is None:
            iterable = []
        if not isinstance(iterable, list):
            raise ValueError("List can only be created from other list instances.")

        self._kwargs = kwargs
        super(List, self).__init__(iterable)

    def extend(self, l):
        if not isinstance(l, list):
            raise ValueError("List can only be extended with other list instances.")

        return super(List, self).extend(l)

    def __setitem__(self, key, val):
        if isinstance(key, slice):
            if not isinstance(val, list):
                raise ValueError(
                    "List can only be sliced with other list " "instances."
                )
            if key.step:
                raise ValueError("List cannot be sliced with a nonzero step " "value")
            # Python 2 and Python 3 do this differently for some reason.
            if six.PY2:
                return super(List, self).__setslice__(key.start, key.stop, val)
            else:
                return super(List, self).__setitem__(key, val)
        return super(List, self).__setitem__(key, val)

    def __setslice__(self, i, j, sequence):
        return self.__setitem__(slice(i, j), sequence)

    def __add__(self, other):
        # Allow None and EmptyValue is a special case because it makes undefined
        # variable references in moz.build behave better.
        other = [] if isinstance(other, (type(None), EmptyValue)) else other
        if not isinstance(other, list):
            raise ValueError("Only lists can be appended to lists.")

        new_list = self.__class__(self, **self._kwargs)
        new_list.extend(other)
        return new_list

    def __iadd__(self, other):
        other = [] if isinstance(other, (type(None), EmptyValue)) else other
        if not isinstance(other, list):
            raise ValueError("Only lists can be appended to lists.")

        return super(List, self).__iadd__(other)


class UnsortedError(Exception):
    def __init__(self, srtd, original):
        assert len(srtd) == len(original)

        self.sorted = srtd
        self.original = original

        for i, orig in enumerate(original):
            s = srtd[i]

            if orig != s:
                self.i = i
                break

    def __str__(self):
        s = StringIO()

        s.write("An attempt was made to add an unsorted sequence to a list. ")
        s.write("The incoming list is unsorted starting at element %d. " % self.i)
        s.write(
            'We expected "%s" but got "%s"'
            % (self.sorted[self.i], self.original[self.i])
        )

        return s.getvalue()


class StrictOrderingOnAppendList(List):
    """A list specialized for moz.build environments.

    We overload the assignment and append operations to require that incoming
    elements be ordered. This enforces cleaner style in moz.build files.
    """

    @staticmethod
    def ensure_sorted(l):
        if isinstance(l, StrictOrderingOnAppendList):
            return

        def _first_element(e):
            # If the list entry is a tuple, we sort based on the first element
            # in the tuple.
            return e[0] if isinstance(e, tuple) else e

        srtd = sorted(l, key=lambda x: _first_element(x).lower())

        if srtd != l:
            raise UnsortedError(srtd, l)

    def __init__(self, iterable=None, **kwargs):
        if iterable is None:
            iterable = []

        StrictOrderingOnAppendList.ensure_sorted(iterable)

        super(StrictOrderingOnAppendList, self).__init__(iterable, **kwargs)

    def extend(self, l):
        StrictOrderingOnAppendList.ensure_sorted(l)

        return super(StrictOrderingOnAppendList, self).extend(l)

    def __setitem__(self, key, val):
        if isinstance(key, slice):
            StrictOrderingOnAppendList.ensure_sorted(val)
        return super(StrictOrderingOnAppendList, self).__setitem__(key, val)

    def __add__(self, other):
        StrictOrderingOnAppendList.ensure_sorted(other)

        return super(StrictOrderingOnAppendList, self).__add__(other)

    def __iadd__(self, other):
        StrictOrderingOnAppendList.ensure_sorted(other)

        return super(StrictOrderingOnAppendList, self).__iadd__(other)


class ImmutableStrictOrderingOnAppendList(StrictOrderingOnAppendList):
    """Like StrictOrderingOnAppendList, but not allowing mutations of the value."""

    def append(self, elt):
        raise Exception("cannot use append on this type")

    def extend(self, iterable):
        raise Exception("cannot use extend on this type")

    def __setslice__(self, i, j, iterable):
        raise Exception("cannot assign to slices on this type")

    def __setitem__(self, i, elt):
        raise Exception("cannot assign to indexes on this type")

    def __iadd__(self, other):
        raise Exception("cannot use += on this type")


class StrictOrderingOnAppendListWithAction(StrictOrderingOnAppendList):
    """An ordered list that accepts a callable to be applied to each item.

    A callable (action) passed to the constructor is run on each item of input.
    The result of running the callable on each item will be stored in place of
    the original input, but the original item must be used to enforce sortedness.
    """

    def __init__(self, iterable=(), action=None):
        if not callable(action):
            raise ValueError(
                "A callable action is required to construct "
                "a StrictOrderingOnAppendListWithAction"
            )

        self._action = action
        if not isinstance(iterable, (tuple, list)):
            raise ValueError(
                "StrictOrderingOnAppendListWithAction can only be initialized "
                "with another list"
            )
        iterable = [self._action(i) for i in iterable]
        super(StrictOrderingOnAppendListWithAction, self).__init__(
            iterable, action=action
        )

    def extend(self, l):
        if not isinstance(l, list):
            raise ValueError(
                "StrictOrderingOnAppendListWithAction can only be extended "
                "with another list"
            )
        l = [self._action(i) for i in l]
        return super(StrictOrderingOnAppendListWithAction, self).extend(l)

    def __setitem__(self, key, val):
        if isinstance(key, slice):
            if not isinstance(val, list):
                raise ValueError(
                    "StrictOrderingOnAppendListWithAction can only be sliced "
                    "with another list"
                )
            val = [self._action(item) for item in val]
        return super(StrictOrderingOnAppendListWithAction, self).__setitem__(key, val)

    def __add__(self, other):
        if not isinstance(other, list):
            raise ValueError(
                "StrictOrderingOnAppendListWithAction can only be added with "
                "another list"
            )
        return super(StrictOrderingOnAppendListWithAction, self).__add__(other)

    def __iadd__(self, other):
        if not isinstance(other, list):
            raise ValueError(
                "StrictOrderingOnAppendListWithAction can only be added with "
                "another list"
            )
        other = [self._action(i) for i in other]
        return super(StrictOrderingOnAppendListWithAction, self).__iadd__(other)


class MozbuildDeletionError(Exception):
    pass


def FlagsFactory(flags):
    """Returns a class which holds optional flags for an item in a list.

    The flags are defined in the dict given as argument, where keys are
    the flag names, and values the type used for the value of that flag.

    The resulting class is used by the various <TypeName>WithFlagsFactory
    functions below.
    """
    assert isinstance(flags, dict)
    assert all(isinstance(v, type) for v in flags.values())

    class Flags:
        __slots__ = flags.keys()
        _flags = flags

        def update(self, **kwargs):
            for k, v in kwargs.items():
                setattr(self, k, v)

        def __getattr__(self, name):
            if name not in self.__slots__:
                raise AttributeError(
                    "'%s' object has no attribute '%s'"
                    % (self.__class__.__name__, name)
                )
            try:
                return object.__getattr__(self, name)
            except AttributeError:
                value = self._flags[name]()
                self.__setattr__(name, value)
                return value

        def __setattr__(self, name, value):
            if name not in self.__slots__:
                raise AttributeError(
                    "'%s' object has no attribute '%s'"
                    % (self.__class__.__name__, name)
                )
            if not isinstance(value, self._flags[name]):
                raise TypeError(
                    "'%s' attribute of class '%s' must be '%s'"
                    % (name, self.__class__.__name__, self._flags[name].__name__)
                )
            return object.__setattr__(self, name, value)

        def __delattr__(self, name):
            raise MozbuildDeletionError("Unable to delete attributes for this object")

    return Flags


class StrictOrderingOnAppendListWithFlags(StrictOrderingOnAppendList):
    """A list with flags specialized for moz.build environments.

    Each subclass has a set of typed flags; this class lets us use `isinstance`
    for natural testing.
    """


def StrictOrderingOnAppendListWithFlagsFactory(flags):
    """Returns a StrictOrderingOnAppendList-like object, with optional
    flags on each item.

    The flags are defined in the dict given as argument, where keys are
    the flag names, and values the type used for the value of that flag.

    Example:

    .. code-block:: python

        FooList = StrictOrderingOnAppendListWithFlagsFactory({
            'foo': bool, 'bar': unicode
        })
        foo = FooList(['a', 'b', 'c'])
        foo['a'].foo = True
        foo['b'].bar = 'bar'
    """

    class StrictOrderingOnAppendListWithFlagsSpecialization(
        StrictOrderingOnAppendListWithFlags
    ):
        def __init__(self, iterable=None):
            if iterable is None:
                iterable = []
            StrictOrderingOnAppendListWithFlags.__init__(self, iterable)
            self._flags_type = FlagsFactory(flags)
            self._flags = dict()

        def __getitem__(self, name):
            if name not in self._flags:
                if name not in self:
                    raise KeyError("'%s'" % name)
                self._flags[name] = self._flags_type()
            return self._flags[name]

        def __setitem__(self, name, value):
            if not isinstance(name, slice):
                raise TypeError(
                    "'%s' object does not support item assignment"
                    % self.__class__.__name__
                )
            result = super(
                StrictOrderingOnAppendListWithFlagsSpecialization, self
            ).__setitem__(name, value)
            # We may have removed items.
            for k in set(self._flags.keys()) - set(self):
                del self._flags[k]
            if isinstance(value, StrictOrderingOnAppendListWithFlags):
                self._update_flags(value)
            return result

        def _update_flags(self, other):
            if self._flags_type._flags != other._flags_type._flags:
                raise ValueError(
                    "Expected a list of strings with flags like %s, not like %s"
                    % (self._flags_type._flags, other._flags_type._flags)
                )
            intersection = set(self._flags.keys()) & set(other._flags.keys())
            if intersection:
                raise ValueError(
                    "Cannot update flags: both lists of strings with flags configure %s"
                    % intersection
                )
            self._flags.update(other._flags)

        def extend(self, l):
            result = super(
                StrictOrderingOnAppendListWithFlagsSpecialization, self
            ).extend(l)
            if isinstance(l, StrictOrderingOnAppendListWithFlags):
                self._update_flags(l)
            return result

        def __add__(self, other):
            result = super(
                StrictOrderingOnAppendListWithFlagsSpecialization, self
            ).__add__(other)
            if isinstance(other, StrictOrderingOnAppendListWithFlags):
                # Result has flags from other but not from self, since
                # internally we duplicate self and then extend with other, and
                # only extend knows about flags.  Since we don't allow updating
                # when the set of flag keys intersect, which we instance we pass
                # to _update_flags here matters.  This needs to be correct but
                # is an implementation detail.
                result._update_flags(self)
            return result

        def __iadd__(self, other):
            result = super(
                StrictOrderingOnAppendListWithFlagsSpecialization, self
            ).__iadd__(other)
            if isinstance(other, StrictOrderingOnAppendListWithFlags):
                self._update_flags(other)
            return result

    return StrictOrderingOnAppendListWithFlagsSpecialization


class HierarchicalStringList:
    """A hierarchy of lists of strings.

    Each instance of this object contains a list of strings, which can be set or
    appended to. A sub-level of the hierarchy is also an instance of this class,
    can be added by appending to an attribute instead.

    For example, the moz.build variable EXPORTS is an instance of this class. We
    can do:

    EXPORTS += ['foo.h']
    EXPORTS.mozilla.dom += ['bar.h']

    In this case, we have 3 instances (EXPORTS, EXPORTS.mozilla, and
    EXPORTS.mozilla.dom), and the first and last each have one element in their
    list.
    """

    __slots__ = ("_strings", "_children")

    def __init__(self):
        # Please change ContextDerivedTypedHierarchicalStringList in context.py
        # if you make changes here.
        self._strings = StrictOrderingOnAppendList()
        self._children = {}

    class StringListAdaptor(collections.abc.Sequence):
        def __init__(self, hsl):
            self._hsl = hsl

        def __getitem__(self, index):
            return self._hsl._strings[index]

        def __len__(self):
            return len(self._hsl._strings)

    def walk(self):
        """Walk over all HierarchicalStringLists in the hierarchy.

        This is a generator of (path, sequence).

        The path is '' for the root level and '/'-delimited strings for
        any descendants.  The sequence is a read-only sequence of the
        strings contained at that level.
        """

        if self._strings:
            path_to_here = ""
            yield path_to_here, self.StringListAdaptor(self)

        for k, l in sorted(self._children.items()):
            for p, v in l.walk():
                path_to_there = "%s/%s" % (k, p)
                yield path_to_there.strip("/"), v

    def __setattr__(self, name, value):
        if name in self.__slots__:
            return object.__setattr__(self, name, value)

        # __setattr__ can be called with a list when a simple assignment is
        # used:
        #
        # EXPORTS.foo = ['file.h']
        #
        # In this case, we need to overwrite foo's current list of strings.
        #
        # However, __setattr__ is also called with a HierarchicalStringList
        # to try to actually set the attribute. We want to ignore this case,
        # since we don't actually create an attribute called 'foo', but just add
        # it to our list of children (using _get_exportvariable()).
        self._set_exportvariable(name, value)

    def __getattr__(self, name):
        if name.startswith("__"):
            return object.__getattr__(self, name)
        return self._get_exportvariable(name)

    def __delattr__(self, name):
        raise MozbuildDeletionError("Unable to delete attributes for this object")

    def __iadd__(self, other):
        if isinstance(other, HierarchicalStringList):
            self._strings += other._strings
            for c in other._children:
                self[c] += other[c]
        else:
            self._check_list(other)
            self._strings += other
        return self

    def __getitem__(self, name):
        return self._get_exportvariable(name)

    def __setitem__(self, name, value):
        self._set_exportvariable(name, value)

    def _get_exportvariable(self, name):
        # Please change ContextDerivedTypedHierarchicalStringList in context.py
        # if you make changes here.
        child = self._children.get(name)
        if not child:
            child = self._children[name] = HierarchicalStringList()
        return child

    def _set_exportvariable(self, name, value):
        if name in self._children:
            if value is self._get_exportvariable(name):
                return
            raise KeyError("global_ns", "reassign", "<some variable>.%s" % name)

        exports = self._get_exportvariable(name)
        exports._check_list(value)
        exports._strings += value

    def _check_list(self, value):
        if not isinstance(value, list):
            raise ValueError("Expected a list of strings, not %s" % type(value))
        for v in value:
            if not isinstance(v, str):
                raise ValueError(
                    "Expected a list of strings, not an element of %s" % type(v)
                )


class KeyedDefaultDict(dict):
    """Like a defaultdict, but the default_factory function takes the key as
    argument"""

    def __init__(self, default_factory, *args, **kwargs):
        dict.__init__(self, *args, **kwargs)
        self._default_factory = default_factory

    def __missing__(self, key):
        value = self._default_factory(key)
        dict.__setitem__(self, key, value)
        return value


class ReadOnlyKeyedDefaultDict(KeyedDefaultDict, ReadOnlyDict):
    """Like KeyedDefaultDict, but read-only."""


class memoize(dict):
    """A decorator to memoize the results of function calls depending
    on its arguments.
    Both functions and instance methods are handled, although in the
    instance method case, the results are cache in the instance itself.
    """

    def __init__(self, func):
        self.func = func
        functools.update_wrapper(self, func)

    def __call__(self, *args):
        if args not in self:
            self[args] = self.func(*args)
        return self[args]

    def method_call(self, instance, *args):
        name = "_%s" % self.func.__name__
        if not hasattr(instance, name):
            setattr(instance, name, {})
        cache = getattr(instance, name)
        if args not in cache:
            cache[args] = self.func(instance, *args)
        return cache[args]

    def __get__(self, instance, cls):
        return functools.update_wrapper(
            functools.partial(self.method_call, instance), self.func
        )


class memoized_property:
    """A specialized version of the memoize decorator that works for
    class instance properties.
    """

    def __init__(self, func):
        self.func = func

    def __get__(self, instance, cls):
        name = "_%s" % self.func.__name__
        if not hasattr(instance, name):
            setattr(instance, name, self.func(instance))
        return getattr(instance, name)


def TypedNamedTuple(name, fields):
    """Factory for named tuple types with strong typing.

    Arguments are an iterable of 2-tuples. The first member is the
    the field name. The second member is a type the field will be validated
    to be.

    Construction of instances varies from ``collections.namedtuple``.

    First, if a single tuple argument is given to the constructor, this is
    treated as the equivalent of passing each tuple value as a separate
    argument into __init__. e.g.::

        t = (1, 2)
        TypedTuple(t) == TypedTuple(1, 2)

    This behavior is meant for moz.build files, so vanilla tuples are
    automatically cast to typed tuple instances.

    Second, fields in the tuple are validated to be instances of the specified
    type. This is done via an ``isinstance()`` check. To allow multiple types,
    pass a tuple as the allowed types field.
    """
    cls = collections.namedtuple(name, (name for name, typ in fields))

    class TypedTuple(cls):
        __slots__ = ()

        def __new__(klass, *args, **kwargs):
            if len(args) == 1 and not kwargs and isinstance(args[0], tuple):
                args = args[0]

            return super(TypedTuple, klass).__new__(klass, *args, **kwargs)

        def __init__(self, *args, **kwargs):
            for i, (fname, ftype) in enumerate(self._fields):
                value = self[i]

                if not isinstance(value, ftype):
                    raise TypeError(
                        "field in tuple not of proper type: %s; "
                        "got %s, expected %s" % (fname, type(value), ftype)
                    )

    TypedTuple._fields = fields

    return TypedTuple


@memoize
def TypedList(type, base_class=List):
    """A list with type coercion.

    The given ``type`` is what list elements are being coerced to. It may do
    strict validation, throwing ValueError exceptions.

    A ``base_class`` type can be given for more specific uses than a List. For
    example, a Typed StrictOrderingOnAppendList can be created with:

       TypedList(unicode, StrictOrderingOnAppendList)
    """

    class _TypedList(base_class):
        @staticmethod
        def normalize(e):
            if not isinstance(e, type):
                e = type(e)
            return e

        def _ensure_type(self, l):
            if isinstance(l, self.__class__):
                return l

            return [self.normalize(e) for e in l]

        def __init__(self, iterable=None, **kwargs):
            if iterable is None:
                iterable = []
            iterable = self._ensure_type(iterable)

            super(_TypedList, self).__init__(iterable, **kwargs)

        def extend(self, l):
            l = self._ensure_type(l)

            return super(_TypedList, self).extend(l)

        def __setitem__(self, key, val):
            val = self._ensure_type(val)

            return super(_TypedList, self).__setitem__(key, val)

        def __add__(self, other):
            other = self._ensure_type(other)

            return super(_TypedList, self).__add__(other)

        def __iadd__(self, other):
            other = self._ensure_type(other)

            return super(_TypedList, self).__iadd__(other)

        def append(self, other):
            self += [other]

    return _TypedList


def group_unified_files(files, unified_prefix, unified_suffix, files_per_unified_file):
    """Return an iterator of (unified_filename, source_filenames) tuples.

    We compile most C and C++ files in "unified mode"; instead of compiling
    ``a.cpp``, ``b.cpp``, and ``c.cpp`` separately, we compile a single file
    that looks approximately like::

       #include "a.cpp"
       #include "b.cpp"
       #include "c.cpp"

    This function handles the details of generating names for the unified
    files, and determining which original source files go in which unified
    file."""

    # Our last returned list of source filenames may be short, and we
    # don't want the fill value inserted by zip_longest to be an
    # issue.  So we do a little dance to filter it out ourselves.
    dummy_fill_value = ("dummy",)

    def filter_out_dummy(iterable):
        return six.moves.filter(lambda x: x != dummy_fill_value, iterable)

    # From the itertools documentation, slightly modified:
    def grouper(n, iterable):
        "grouper(3, 'ABCDEFG', 'x') --> ABC DEF Gxx"
        args = [iter(iterable)] * n
        return six.moves.zip_longest(fillvalue=dummy_fill_value, *args)

    for i, unified_group in enumerate(grouper(files_per_unified_file, files)):
        just_the_filenames = list(filter_out_dummy(unified_group))
        yield "%s%d.%s" % (unified_prefix, i, unified_suffix), just_the_filenames


def pair(iterable):
    """Given an iterable, returns an iterable pairing its items.

    For example,
        list(pair([1,2,3,4,5,6]))
    returns
        [(1,2), (3,4), (5,6)]
    """
    i = iter(iterable)
    return six.moves.zip_longest(i, i)


def pairwise(iterable):
    """Given an iterable, returns an iterable of overlapped pairs of
    its items. Based on the Python itertools documentation.

    For example,
        list(pairwise([1,2,3,4,5,6]))
    returns
        [(1,2), (2,3), (3,4), (4,5), (5,6)]
    """
    a, b = itertools.tee(iterable)
    next(b, None)
    return zip(a, b)


VARIABLES_RE = re.compile(r"\$\((\w+)\)")


def expand_variables(s, variables):
    """Given a string with $(var) variable references, replace those references
    with the corresponding entries from the given `variables` dict.

    If a variable value is not a string, it is iterated and its items are
    joined with a whitespace."""
    result = ""
    for s, name in pair(VARIABLES_RE.split(s)):
        result += s
        value = variables.get(name)
        if not value:
            continue
        if not isinstance(value, str):
            value = " ".join(value)
        result += value
    return result


class ForwardingArgumentParser(argparse.ArgumentParser):
    """
    An argument parser with customized help generation when forwarding
    arguments.
    """

    def add_forwarding_group(
        self, title, dest, help, forwarding_help, default_type=list, **kwargs
    ):
        """
        Add a group that captures all remaining arguments in order to pass them
        down to another program.
        """
        group = self.add_argument_group(
            title, description=f"-- --help {forwarding_help}", **kwargs
        )

        group.add_argument(
            dest,
            nargs=argparse.REMAINDER,
            default=default_type(),
            metavar=f"[--] {dest}...",
            help=help,
        )
        return group


class DefinesAction(argparse.Action):
    """An ArgumentParser action to handle -Dvar[=value] type of arguments."""

    def __call__(self, parser, namespace, values, option_string):
        defines = getattr(namespace, self.dest)
        if defines is None:
            defines = {}
        values = values.split("=", 1)
        if len(values) == 1:
            name, value = values[0], 1
        else:
            name, value = values
            if value.isdigit():
                value = int(value)
        defines[name] = value
        setattr(namespace, self.dest, defines)


class EnumStringComparisonError(Exception):
    pass


class EnumString(str):
    """A string type that only can have a limited set of values, similarly to
    an Enum, and can only be compared against that set of values.

    The class is meant to be subclassed, where the subclass defines
    POSSIBLE_VALUES. The `subclass` method is a helper to create such
    subclasses.
    """

    POSSIBLE_VALUES = ()

    def __init__(self, value):
        if value not in self.POSSIBLE_VALUES:
            raise ValueError(
                "'%s' is not a valid value for %s" % (value, self.__class__.__name__)
            )

    def __eq__(self, other):
        if other not in self.POSSIBLE_VALUES:
            raise EnumStringComparisonError(
                "Can only compare with %s"
                % ", ".join("'%s'" % v for v in self.POSSIBLE_VALUES)
            )
        return super(EnumString, self).__eq__(other)

    def __ne__(self, other):
        return not (self == other)

    def __hash__(self):
        return super(EnumString, self).__hash__()

    def __repr__(self):
        return f"{self.__class__.__name__}({str(self)!r})"


def _escape_char(c):
    # str.encode('unicode_espace') doesn't escape quotes, presumably because
    # quoting could be done with either ' or ".
    if c == "'":
        return "\\'"
    return str(c.encode("unicode_escape"))


def ensure_bytes(value, encoding="utf-8"):
    if isinstance(value, str):
        return value.encode(encoding)
    return value


def ensure_unicode(value, encoding="utf-8"):
    if isinstance(value, bytes):
        return value.decode(encoding)
    return value


def hexdump(buf):
    """
    Returns a list of hexdump-like lines corresponding to the given input buffer.
    """
    assert six.PY3
    off_format = f"%0{len(str(len(buf)))}x "
    lines = []
    for off in range(0, len(buf), 16):
        line = off_format % off
        chunk = buf[off : min(off + 16, len(buf))]
        for n, byte in enumerate(chunk):
            line += " %02x" % byte
            if n == 7:
                line += " "
        for n in range(len(chunk), 16):
            line += "   "
            if n == 7:
                line += " "
        line += "  |"
        for byte in chunk:
            if byte < 127 and byte >= 32:
                line += chr(byte)
            else:
                line += "."
        for n in range(len(chunk), 16):
            line += " "
        line += "|\n"
        lines.append(line)
    return lines


def cpu_count():
    """
    Returns the number of CPUs available to us. This may be different than
    `os.cpu_count()` because of affinity.

    See the Python documentation for `os.cpu_count()`.
    """
    try:
        return len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        pass
    if psutil:
        try:
            return len(psutil.Process().cpu_affinity())
        except (AttributeError, OSError):
            pass
    return os.cpu_count()


def macos_performance_cores():
    """
    Returns the number of performance cores on Mac OS

    See the Python documentation for `os.cpu_count()`.
    """
    proc = subprocess.run(
        ["sysctl", "-n", "hw.perflevel0.logicalcpu_max"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if proc.returncode != 0:
        return -1
    return int(proc.stdout.decode("ascii", "replace").strip())
