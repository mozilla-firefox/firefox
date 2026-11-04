# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import importlib.util
import os
import sys

from mozpack import path as mozpath
from mozpack.files import FileFinder

_is_windows = sys.platform == "cygwin" or (sys.platform == "win32" and os.sep == "\\")


class FilterPath:
    """Helper class to make comparing and matching file paths easier."""

    def __init__(self, path):
        self.path = os.path.normpath(path)
        self._finder = None

    @property
    def finder(self):
        if self._finder:
            return self._finder
        self._finder = FileFinder(mozpath.normsep(self.path))
        return self._finder

    @property
    def ext(self):
        return os.path.splitext(self.path)[1].strip(".")

    @property
    def exists(self):
        return os.path.exists(self.path)

    @property
    def isfile(self):
        return os.path.isfile(self.path)

    @property
    def isdir(self):
        return os.path.isdir(self.path)

    def join(self, *args):
        return FilterPath(os.path.join(self.path, *args))

    def match(self, patterns):
        a = mozpath.normsep(self.path)
        for p in patterns:
            if isinstance(p, FilterPath):
                p = p.path
            p = mozpath.normsep(p)
            if mozpath.match(a, p):
                return True
        return False

    def contains(self, other):
        """Return True if other is a subdirectory of self or equals self."""
        if isinstance(other, FilterPath):
            other = other.path
        a = os.path.abspath(self.path)
        b = os.path.normpath(os.path.abspath(other))

        parts_a = a.split(os.sep)
        parts_b = b.split(os.sep)

        if len(parts_a) > len(parts_b):
            return False

        if _is_windows and parts_a:
            # Normalize case of drive letters, without invoking the file system.
            if parts_a[0].endswith(":"):
                parts_a[0] = parts_a[0].upper()
            if parts_b[0].endswith(":"):
                parts_b[0] = parts_b[0].upper()

        for i, part in enumerate(parts_a):
            if part != parts_b[i]:
                return False
        return True

    def __repr__(self):
        return repr(self.path)


def collapse(paths, base=None, dotfiles=False):
    """Given an iterable of paths, collapse them into the smallest possible set
    of paths that contain the original set (without containing any extra paths).

    For example, if directory 'a' contains two files b.txt and c.txt, calling:

        collapse(['a/b.txt', 'a/c.txt'])

    returns ['a']. But if a third file d.txt also exists, then it will return
    ['a/b.txt', 'a/c.txt'] since ['a'] would also include that extra file.

    :param paths: An iterable of paths (files and directories) to collapse.
    :returns: The smallest set of paths (files and directories) that contain
              the original set of paths and only the original set.
    """
    if not paths:
        if not base:
            return []

        # Need to test whether directory chain is empty. If it is then bubble
        # the base back up so that it counts as 'covered'.
        for _, _, names in os.walk(base):
            if names:
                return []
        return [base]

    if not base:
        paths = list(map(mozpath.abspath, paths))
        base = mozpath.commonprefix(paths).rstrip("/")

        # Make sure `commonprefix` factors in sibling directories that have the
        # same prefix in their basenames.
        parent = mozpath.dirname(base)
        same_prefix = [
            p for p in os.listdir(parent) if p.startswith(mozpath.basename(base))
        ]
        if not os.path.isdir(base) or len(same_prefix) > 1:
            base = parent

    if base in paths:
        return [base]

    covered = set()
    full = set()
    for name in os.listdir(base):
        if not dotfiles and name[0] == ".":
            continue

        path = mozpath.join(base, name)
        full.add(path)

        if path in paths:
            # This path was explicitly specified, so just bubble it back up
            # without recursing down into it (if it was a directory).
            covered.add(path)
        elif os.path.isdir(path):
            new_paths = [p for p in paths if p.startswith(path)]
            covered.update(collapse(new_paths, base=path, dotfiles=dotfiles))

    if full == covered:
        # Every file under this base was covered, so we can collapse them all
        # up into the base path.
        return [base]
    return list(covered)


def _find_globs(base, patterns, skip):
    """Yield the paths under `base`, relative to it, that match one of
    `patterns`, without descending into a match or into a directory in
    `skip`. Dot files are ignored unless a component of a pattern names them.
    """
    patterns = [mozpath.normsep(p) for p in patterns]
    matches = mozpath.matcher(patterns)
    dotted = [part for p in patterns for part in p.split("/") if part.startswith(".")]
    stack = [(base, "")]
    while stack:
        dirpath, rel = stack.pop()
        try:
            entries = list(os.scandir(dirpath))
        except OSError:
            continue
        for entry in entries:
            if entry.name.startswith(".") and not any(
                mozpath.match(entry.name, d) for d in dotted
            ):
                continue
            p = f"{rel}/{entry.name}" if rel else entry.name
            if entry.is_dir():
                if entry.path in skip:
                    continue
                if matches(p):
                    yield p
                else:
                    stack.append((entry.path, p))
            elif matches(p):
                yield p


def filterpaths(
    root,
    paths,
    include,
    exclude=None,
    extensions=None,
    exclude_extensions=None,
    expand_excludes=True,
):
    """Filters a list of paths.

    Given a list of paths and some filtering rules, return the set of paths
    that should be linted. Note that at most one of extensions or
    exclude_extensions should be provided (ie not both).

    :param paths: A starting list of paths to possibly lint.
    :param include: A list of paths that should be included (required).
    :param exclude: A list of paths that should be excluded (optional).
    :param extensions: A list of file extensions which should be considered (optional).
    :param exclude_extensions: A list of file extensions which should not be considered (optional).
    :param expand_excludes: Whether to compute the list of paths to exclude.
                            Expanding glob excludes requires walking every
                            directory in `paths`, so callers that only need
                            the paths to lint should pass False (optional).
    :returns: A tuple containing a list of file paths to lint and a list of
              paths to exclude (empty if `expand_excludes` is False).
    """

    def normalize(path):
        if "*" not in path and not os.path.isabs(path):
            path = os.path.join(root, path)
        return FilterPath(path)

    # Includes are always paths and should always exist.
    include = list(map(normalize, include))

    # Exclude paths with and without globs will be handled separately,
    # pull them apart now.
    exclude = list(map(normalize, exclude or []))
    excludepaths = [p for p in exclude if p.exists]
    excludeglobs = [p.path for p in exclude if not p.exists]
    prunable = [e for e in excludepaths if not any(e.contains(i) for i in include)]
    excludedirs = {p.path for p in prunable if p.isdir}
    findglobs = [p for p in excludeglobs if "*" in p]

    keep = set()
    discard = set()
    for path in list(map(normalize, paths)):
        # Exclude bad file extensions
        if extensions and path.isfile and path.ext not in extensions:
            continue
        elif exclude_extensions and path.isfile and path.ext in exclude_extensions:
            continue

        if path.match(excludeglobs):
            continue

        # First handle include/exclude directives
        # that exist (i.e don't have globs)
        for inc in include:
            # If the include directive is a file and we're specifically linting
            # it, keep it.
            if inc.isfile and path.path == inc.path:
                keep.add(inc)
                continue

            # Only excludes that are subdirectories of the include
            # path matter.
            excs = [e for e in excludepaths if inc.contains(e)]

            if path.contains(inc):
                # If specified path is an ancestor of include path,
                # then lint the include path.
                keep.add(inc)

                # We can't apply these exclude paths without explicitly
                # including every sibling file. Rather than do that,
                # just return them and hope the underlying linter will
                # deal with them.
                discard.update(excs)

            elif inc.contains(path):
                # If the include path is an ancestor of the specified
                # path, then add the specified path only if there are
                # no exclude paths in-between them.
                if not any(e.contains(path) for e in excs):
                    keep.add(path)
                    discard.update([e for e in excs if path.contains(e)])

        if not expand_excludes:
            continue

        # Next expand excludes with globs in them so we can add them to
        # the set of files to discard.
        if findglobs and path.isdir and not any(e.contains(path) for e in prunable):
            for p in _find_globs(path.path, findglobs, excludedirs):
                discard.add(path.join(p))

    if expand_excludes:
        excludes = collapse([f.path for f in discard if f.exists])
    else:
        excludes = []

    return [f.path for f in keep if f.exists], excludes


def findobject(path, definition, linter_paths=None):
    """
    Find a Python object given a path of the form <modulepath>:<objectpath>.
    Conceptually equivalent to

        def find_object(modulepath, objectpath):
            import <modulepath> as mod
            return mod.<objectpath>

    except that <modulepath> is loaded from the file it names next to the
    linter definition, so a module of the same name elsewhere on `sys.path`
    or already in `sys.modules` is never picked up.

    :param path: The <modulepath>:<objectpath> to resolve.
    :param definition: Path to the linter definition naming the object. Its
                       directory is searched first.
    :param linter_paths: Additional directories to search, for definitions
                         that live apart from the modules they name.
    """
    if path.count(":") != 1:
        raise ValueError(f'python path {path!r} does not have the form "module:object"')

    modulepath, objectpath = path.split(":")
    roots = [os.path.dirname(definition)]
    roots.extend(r for r in (linter_paths or []) if r not in roots)
    obj = _load_module(modulepath, roots)
    for a in objectpath.split("."):
        obj = getattr(obj, a)
    return obj


def _find_module_file(modulepath, roots):
    for root in roots:
        base = os.path.join(root, *modulepath.split("."))
        if os.path.isfile(f"{base}.py"):
            return f"{base}.py", None
        if os.path.isfile(os.path.join(base, "__init__.py")):
            return os.path.join(base, "__init__.py"), [base]

    raise ModuleNotFoundError(
        f"No module named {modulepath!r} under {', '.join(roots)}"
    )


def _load_module(modulepath, roots):
    name = f"mozlint.linters.{modulepath}"
    if name in sys.modules:
        return sys.modules[name]

    location, search_locations = _find_module_file(modulepath, roots)
    spec = importlib.util.spec_from_file_location(
        name, location, submodule_search_locations=search_locations
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    try:
        spec.loader.exec_module(module)
    except BaseException:
        del sys.modules[name]
        raise
    return module


def ancestors(path):
    while path:
        yield path
        (path, child) = os.path.split(path)
        if child == "":
            break


def get_ancestors_by_name(name, path, root):
    """Returns a list of files called `name` in `path`'s ancestors,
    sorted from closest->furthest. This can be useful for finding
    relevant configuration files.
    """
    configs = []
    for ancestor_path in ancestors(path):
        config = os.path.join(ancestor_path, name)
        if os.path.isfile(config):
            configs.append(config)
        if ancestor_path == root:
            break
    return configs


def expand_exclusions(paths, config, root):
    """Returns all files that match patterns and aren't excluded.

    This is used by some external linters who receive 'batch' files (e.g dirs)
    but aren't capable of applying their own exclusions. There is an argument
    to be made that this step should just apply to all linters no matter what.

    Args:
        paths (list): List of candidate paths to lint.
        config (dict): Linter's config object.
        root (str): Root of the repository.

    Returns:
        Generator which generates list of paths that weren't excluded.
    """
    extensions = {f".{e}" for e in config.get("extensions", [])}
    exclude_extensions = [e.lstrip(".") for e in config.get("exclude_extensions", [])]
    if extensions and exclude_extensions:
        raise ValueError("Can't specify both extensions and exclude_extensions.")
    find_dotfiles = config.get("find-dotfiles", False)

    def normalize(path):
        path = mozpath.normpath(path)
        if os.path.isabs(path):
            return path
        return mozpath.join(root, path)

    exclude = list(map(normalize, config.get("exclude", [])))
    # We need excluded extensions in both the ignore for the FileFinder and in
    # the exclusion set. If we don't put it in the exclusion set, we would
    # return files that are passed explicitly and whose extensions are in the
    # exclusion set. If we don't put it in the ignore set, the FileFinder
    # would return files in (sub)directories passed to us.
    base_ignore = [f"**/*.{ext}" for ext in exclude_extensions]
    exclude += base_ignore
    for path in paths:
        path = mozpath.normsep(path)
        if os.path.isfile(path):
            if any(path.startswith(e) for e in exclude if "*" not in e):
                continue

            if any(mozpath.match(path, e) for e in exclude if "*" in e):
                continue

            yield path
            continue

        # This is a directory. Check we don't have excludes for ancestors of
        # this path. Mess with slashes to avoid "foo/bar" matching "foo/barry".
        parent_path = os.path.dirname(path.rstrip("/")) + "/"
        assert not any(parent_path.startswith(e.rstrip("/") + "/") for e in exclude)

        ignore = base_ignore + [
            e[len(path) :].lstrip("/")
            for e in exclude
            if mozpath.commonprefix((path, e)) == path
        ]

        finder = FileFinder(path, ignore=ignore, find_dotfiles=find_dotfiles)
        for p, f in finder.find("**"):
            if extensions and os.path.splitext(p)[1] not in extensions:
                continue
            yield os.path.join(path, p)
