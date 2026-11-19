# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

import mozunit
import pytest

sys.path.insert(
    0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "mobile", "android")
)
from gradle import jvmargs_with_max_heap


def write_properties(tmp_path, text):
    with open(tmp_path / "gradle.properties", "w", encoding="utf-8") as fh:
        fh.write(text)
    return str(tmp_path)


def test_jvmargs_with_max_heap(tmp_path):
    topsrcdir = write_properties(
        tmp_path,
        "org.gradle.parallel=true\n"
        "org.gradle.jvmargs=-Xmx7g -Xms2g -XX:+UseParallelGC\n",
    )
    assert (
        jvmargs_with_max_heap(topsrcdir, "16g") == "-Xmx16g -Xms2g -XX:+UseParallelGC"
    )


def test_jvmargs_with_max_heap_spaced_separator(tmp_path):
    topsrcdir = write_properties(tmp_path, "  org.gradle.jvmargs : -Xmx7g -Xms2g\n")
    assert jvmargs_with_max_heap(topsrcdir, "16g") == "-Xmx16g -Xms2g"


def test_jvmargs_with_max_heap_last_definition(tmp_path):
    topsrcdir = write_properties(
        tmp_path,
        "org.gradle.jvmargs=-Xmx4g -Xms1g\norg.gradle.jvmargs=-Xmx7g -Xms2g\n",
    )
    assert jvmargs_with_max_heap(topsrcdir, "16g") == "-Xmx16g -Xms2g"


def test_jvmargs_with_max_heap_backslash(tmp_path):
    topsrcdir = write_properties(tmp_path, "org.gradle.jvmargs=-Xmx7g -Xms2g\n")
    assert jvmargs_with_max_heap(topsrcdir, "\\1") == "-Xmx\\1 -Xms2g"


def test_jvmargs_with_max_heap_no_jvmargs(tmp_path):
    topsrcdir = write_properties(tmp_path, "org.gradle.parallel=true\n")
    with pytest.raises(Exception, match="has no single line org.gradle.jvmargs"):
        jvmargs_with_max_heap(topsrcdir, "16g")


def test_jvmargs_with_max_heap_no_xmx(tmp_path):
    topsrcdir = write_properties(tmp_path, "org.gradle.jvmargs=-Xms2g\n")
    with pytest.raises(Exception, match="has no single line org.gradle.jvmargs"):
        jvmargs_with_max_heap(topsrcdir, "16g")


def test_jvmargs_with_max_heap_continuation(tmp_path):
    topsrcdir = write_properties(tmp_path, "org.gradle.jvmargs=-Xmx7g \\\n    -Xms2g\n")
    with pytest.raises(Exception, match="has no single line org.gradle.jvmargs"):
        jvmargs_with_max_heap(topsrcdir, "16g")


if __name__ == "__main__":
    mozunit.main()
