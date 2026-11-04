# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import types
from fnmatch import fnmatch

import mozunit
import pytest

from mozlint import pathutils

here = os.path.abspath(os.path.dirname(__file__))
root = os.path.join(here, "filter")
definition = os.path.join(here, "linters", "external.yml")


def assert_paths(a, b):
    def normalize(p):
        if not os.path.isabs(p):
            p = os.path.join(root, p)
        return os.path.normpath(p)

    assert set(map(normalize, a)) == set(map(normalize, b))


@pytest.mark.parametrize(
    "test",
    (
        {
            "paths": ["a.js", "subdir1/subdir3/d.js"],
            "include": ["."],
            "exclude": ["subdir1"],
            "expected": ["a.js"],
        },
        {
            "paths": ["a.js", "subdir1/subdir3/d.js"],
            "include": ["subdir1/subdir3"],
            "exclude": ["subdir1"],
            "expected": ["subdir1/subdir3/d.js"],
        },
        {
            "paths": ["subdir1/subdir3"],
            "include": ["subdir1/subdir3"],
            "exclude": ["subdir1", "**/*.py"],
            "expected": ["subdir1/subdir3"],
            "expected_exclude": ["subdir1/subdir3/d.py"],
        },
        {
            "paths": ["."],
            "include": ["."],
            "exclude": ["**/c.py", "subdir1/subdir3"],
            "extensions": ["py"],
            "expected": ["."],
            "expected_exclude": ["subdir2/c.py", "subdir1/subdir3"],
        },
        {
            "paths": [
                "a.py",
                "a.js",
                "subdir1/b.py",
                "subdir2/c.py",
                "subdir1/subdir3/d.py",
            ],
            "include": ["."],
            "exclude": ["**/c.py", "subdir1/subdir3"],
            "extensions": ["py"],
            "expected": ["a.py", "subdir1/b.py"],
        },
        pytest.param(
            {
                "paths": [
                    "a.py",
                    "a.js",
                    "subdir1/b.py",
                    "subdir2/c.py",
                    "subdir1/subdir3/d.py",
                ],
                "include": ["."],
                "exclude": [],
                "exclude_extensions": ["py"],
                "expected": ["a.js"],
            },
            id="Excluding .py should only return .js file.",
        ),
        pytest.param(
            {
                "paths": [
                    "a.py",
                    "a.js",
                    "subdir1/b.py",
                    "subdir2/c.py",
                    "subdir1/subdir3/d.py",
                ],
                "include": ["."],
                "exclude": [],
                "exclude_extensions": ["js"],
                "expected": [
                    "a.py",
                    "subdir1/b.py",
                    "subdir2/c.py",
                    "subdir1/subdir3/d.py",
                ],
            },
            id="Excluding .js should only return .py files.",
        ),
        {
            "paths": ["a.py", "a.js", "subdir2"],
            "include": ["."],
            "exclude": [],
            "extensions": ["py"],
            "expected": ["a.py", "subdir2"],
        },
        {
            "paths": ["subdir1"],
            "include": ["."],
            "exclude": ["subdir1/subdir3"],
            "extensions": ["py"],
            "expected": ["subdir1"],
            "expected_exclude": ["subdir1/subdir3"],
        },
        {
            "paths": ["docshell"],
            "include": ["docs"],
            "exclude": [],
            "expected": [],
        },
        {
            "paths": ["does/not/exist"],
            "include": ["."],
            "exclude": [],
            "expected": [],
        },
        {
            "paths": ["a.py"],
            "include": ["a.js"],
            "exclude": [],
            "expected": [],
        },
    ),
)
def test_filterpaths(test):
    expected = test.pop("expected")
    expected_exclude = test.pop("expected_exclude", [])

    paths, exclude = pathutils.filterpaths(root, **test)
    assert_paths(paths, expected)
    assert_paths(exclude, expected_exclude)

    paths, exclude = pathutils.filterpaths(root, expand_excludes=False, **test)
    assert_paths(paths, expected)
    assert exclude == []


@pytest.mark.parametrize(
    "test",
    (
        {
            "paths": ["subdir1/b.js"],
            "config": {
                "exclude": ["subdir1"],
                "extensions": ["js"],
            },
            "expected": [],
        },
        {
            "paths": ["subdir1"],
            "config": {
                "exclude": ["subdir1"],
                "extensions": ["js"],
            },
            "expected": [],
        },
        pytest.param(
            {
                "paths": ["a.py", "subdir1"],
                "config": {
                    "exclude": ["subdir1"],
                    "exclude_extensions": ["gob"],
                },
                "expected": ["a.py"],
            },
            id="Excluding both subdirs and nonsense extensions returns other files.",
        ),
        pytest.param(
            {
                "paths": ["a.py", "a.js", "subdir1"],
                "config": {
                    "exclude": [],
                    "exclude_extensions": ["py"],
                },
                "expected": ["a.js", "subdir1/subdir3/d.js", "subdir1/b.js"],
            },
            id="Excluding .py files returns only non-.py files, also from subdirs.",
        ),
        pytest.param(
            {
                "paths": ["subdir2"],
                "config": {},
                "expected": ["subdir2/c.js", "subdir2/c.py", "subdir2/noext"],
            },
            id="A directory expands to all of its files when nothing filters it.",
        ),
        pytest.param(
            {
                "paths": ["subdir2"],
                "config": {"exclude": ["subdir2/c.py"]},
                "expected": ["subdir2/c.js", "subdir2/noext"],
            },
            id="Excludes apply to a directory that nothing else filters.",
        ),
        pytest.param(
            {
                "paths": ["subdir2"],
                "config": {"exclude_extensions": ["py"]},
                "expected": ["subdir2/c.js", "subdir2/noext"],
            },
            id="A file with no extension survives exclude_extensions.",
        ),
        pytest.param(
            {
                "paths": ["missing"],
                "config": {},
                "expected": [],
            },
            id="A path that does not exist expands to nothing.",
        ),
        pytest.param(
            {
                "paths": ["subdir1", "subdir2"],
                "config": {"global_exclude": ["subdir1/subdir3", "**/*.py"]},
                "expected": ["subdir1/b.js", "subdir2/c.js", "subdir2/noext"],
            },
            id="Global excludes apply to a directory nothing else filters.",
        ),
        pytest.param(
            {
                "paths": ["subdir1"],
                "config": {"global_exclude": ["**/subdir3/"]},
                "expected": ["subdir1/b.js", "subdir1/b.py"],
            },
            id="A global exclude with a trailing slash applies to a directory walk.",
        ),
        pytest.param(
            {
                "paths": ["subdir1/b.js", "subdir2/c.js"],
                "config": {"global_exclude": ["subdir1"]},
                "expected": ["subdir2/c.js"],
            },
            id="Global excludes apply to files passed explicitly.",
        ),
        pytest.param(
            {
                "paths": ["subdir1/b.js"],
                "config": {"global_exclude": ["subdir1/b"]},
                "expected": ["subdir1/b.js"],
            },
            id="An exclude does not match a file whose name only starts with it.",
        ),
    ),
)
def test_expand_exclusions(test):
    expected = test.pop("expected", [])

    input_paths = [os.path.join(root, p) for p in test["paths"]]
    paths = list(pathutils.expand_exclusions(input_paths, test["config"], root))
    assert_paths(paths, expected)


@pytest.mark.parametrize(
    "paths,expected",
    [
        (["subdir1/*"], ["subdir1"]),
        (["subdir2/*"], ["subdir2"]),
        (["subdir1/*.*", "subdir1/subdir3/*", "subdir2/*"], ["subdir1", "subdir2"]),
        ([root + "/*", "subdir1/*.*", "subdir1/subdir3/*", "subdir2/*"], [root]),
        (["subdir1/b.py", "subdir1/subdir3"], ["subdir1/b.py", "subdir1/subdir3"]),
        (["subdir1/b.py", "subdir1/b.js"], ["subdir1/b.py", "subdir1/b.js"]),
        (["subdir1/subdir3"], ["subdir1/subdir3"]),
        (
            [
                "foo",
                "foobar",
            ],
            ["foo", "foobar"],
        ),
    ],
)
def test_collapse(paths, expected):
    os.chdir(root)

    inputs = []
    for path in paths:
        base, name = os.path.split(path)
        if "*" in name:
            for n in os.listdir(base):
                if not fnmatch(n, name):
                    continue
                inputs.append(os.path.join(base, n))
        else:
            inputs.append(path)

    print(f"inputs: {inputs}")
    assert_paths(pathutils.collapse(inputs), expected)


def test_find_globs_symlink(tmp_path):
    (tmp_path / "real").mkdir()
    (tmp_path / "real" / "skip.py").write_text("")
    try:
        (tmp_path / "link").symlink_to(tmp_path / "real", target_is_directory=True)
    except OSError:
        pytest.skip("symlinks are not supported")
    found = pathutils._find_globs(str(tmp_path), ["**/skip.py"], set())
    assert sorted(found) == ["link/skip.py", "real/skip.py"]


def test_find_globs_dot_dir(tmp_path):
    for d in (".gen", ".other"):
        (tmp_path / d).mkdir()
        (tmp_path / d / "bad.py").write_text("")
    found = pathutils._find_globs(str(tmp_path), [".gen/*.py", "**/*.py"], set())
    assert sorted(found) == [".gen/bad.py"]


def test_find_globs_skip(tmp_path):
    for path in ("keep/a.py", "pruned/b.py", "pruned/sub/c.py"):
        (tmp_path / path).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / path).write_text("")
    skip = {os.path.join(str(tmp_path), "pruned")}
    found = pathutils._find_globs(str(tmp_path), ["**/*.py"], skip)
    assert sorted(found) == ["keep/a.py"]


def test_findobject_loads_from_root(monkeypatch):
    monkeypatch.setitem(sys.modules, "external", types.ModuleType("external"))
    monkeypatch.delitem(sys.modules, "mozlint.linters.external", raising=False)

    func = pathutils.findobject("external:external", definition)
    assert func.__module__ == "mozlint.linters.external"
    assert os.path.samefile(
        func.__code__.co_filename, os.path.join(here, "linters", "external.py")
    )
    assert pathutils.findobject("external:external", definition) is func


def test_findobject_loads_from_linter_paths(monkeypatch):
    monkeypatch.setitem(sys.modules, "external", types.ModuleType("external"))
    monkeypatch.delitem(sys.modules, "mozlint.linters.external", raising=False)

    elsewhere = os.path.join(here, "filter", "external.yml")
    func = pathutils.findobject(
        "external:external", elsewhere, [os.path.join(here, "linters")]
    )
    assert os.path.samefile(
        func.__code__.co_filename, os.path.join(here, "linters", "external.py")
    )


def test_findobject_missing_module():
    with pytest.raises(ImportError):
        pathutils.findobject("does_not_exist:lint", definition)

    with pytest.raises(ImportError):
        pathutils.findobject("does_not_exist:lint", definition, [here])


def test_findobject_invalid_path():
    with pytest.raises(ValueError):
        pathutils.findobject("external.external", definition)


if __name__ == "__main__":
    mozunit.main()
