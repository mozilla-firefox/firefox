# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import itertools
import json
import os

import pytest
from mozunit import main

from mach import func_cache


@pytest.fixture
def topsrcdir(tmp_path, monkeypatch):
    for path in (
        "a/x.gradle",
        "a/b/y.gradle",
        "a/b/.hidden/z.gradle",
        "a/b/gradle.properties",
        "a/b/c/settings.gradle.kts",
        "a/b/c/x.gradle.pyc",
        "a/d.gradle/inner.txt",
        "a/other.txt",
        "e/w.gradle",
    ):
        (tmp_path / path).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / path).write_text(path)
    monkeypatch.setattr(func_cache, "_topsrcdir", tmp_path)
    return tmp_path


@pytest.fixture
def opened(monkeypatch):
    calls = []
    real_open = open

    def recording_open(path, *args, **kwargs):
        calls.append((str(path), func_cache.time.time_ns()))
        return real_open(path, *args, **kwargs)

    monkeypatch.setattr("builtins.open", recording_open)
    return calls


def test_resolve_inputs_shared_walk(topsrcdir):
    patterns = [
        "a/**/*.gradle",
        "a/**/*.gradle.kts",
        "a/**/gradle.properties",
        "e/**/*.gradle",
        "a/*.txt",
    ]
    expected = set()
    for pattern in patterns:
        expected.update(
            p
            for p in topsrcdir.glob(pattern)
            if p.is_file() and not p.name.endswith((".pyc", ".pyd", ".pyo"))
        )
    assert func_cache._resolve_inputs(patterns) == sorted(expected)


def test_hash_inputs_unchanged(topsrcdir, tmp_path, opened):
    hashes_file = tmp_path / "cache" / "_file_hashes.json"
    old = 1_000_000_000_000_000_000
    for path in topsrcdir.rglob("*"):
        if path.is_file():
            os.utime(path, ns=(old, old))

    first = func_cache._hash_inputs(["a", "e"], hashes_file=hashes_file)
    assert first == func_cache._hash_inputs(["a", "e"])

    opened.clear()
    assert func_cache._hash_inputs(["a", "e"], hashes_file=hashes_file) == first
    assert [p for p, _ in opened if not p.startswith(str(hashes_file))] == []

    changed = topsrcdir / "a" / "x.gradle"
    changed.write_text("changed")
    os.utime(changed, ns=(old + 1, old + 1))
    opened.clear()
    second = func_cache._hash_inputs(["a", "e"], hashes_file=hashes_file)
    assert second != first
    assert [p for p, _ in opened if not p.startswith(str(hashes_file))] == [
        str(changed)
    ]


def test_hash_inputs_deleted(topsrcdir, tmp_path):
    hashes_file = tmp_path / "cache" / "_file_hashes.json"
    deleted = os.path.join("e", "w.gradle")
    first = func_cache._hash_inputs(["a", "e"], hashes_file=hashes_file)
    with open(hashes_file) as f:
        assert deleted in json.load(f)["files"]

    (topsrcdir / deleted).unlink()
    assert func_cache._hash_inputs(["a", "e"], hashes_file=hashes_file) != first
    with open(hashes_file) as f:
        assert deleted not in json.load(f)["files"]


def test_hash_inputs_recent(topsrcdir, tmp_path, opened):
    hashes_file = tmp_path / "cache" / "_file_hashes.json"
    func_cache._hash_inputs(["e"], hashes_file=hashes_file)

    opened.clear()
    func_cache._hash_inputs(["e"], hashes_file=hashes_file)
    assert str(topsrcdir / "e" / "w.gradle") in [p for p, _ in opened]


def test_hash_inputs_written_ns(topsrcdir, tmp_path, monkeypatch, opened):
    """The scan time is taken before any file is read."""
    hashes_file = tmp_path / "cache" / "_file_hashes.json"
    clock = itertools.count()
    monkeypatch.setattr(func_cache.time, "time_ns", lambda: next(clock))
    func_cache._hash_inputs(["e"], hashes_file=hashes_file)
    monkeypatch.undo()

    with open(hashes_file) as f:
        assert json.load(f)["written_ns"] < min(t for _, t in opened)


def test_hash_inputs_replace_fails(topsrcdir, tmp_path, monkeypatch):
    hashes_file = tmp_path / "cache" / "_file_hashes.json"

    def failing_replace(src, dst):
        raise PermissionError(dst)

    monkeypatch.setattr(func_cache.os, "replace", failing_replace)
    func_cache._hash_inputs(["e"], hashes_file=hashes_file)
    assert list(hashes_file.parent.iterdir()) == []


if __name__ == "__main__":
    main()
