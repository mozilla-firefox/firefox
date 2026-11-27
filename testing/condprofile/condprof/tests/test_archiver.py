# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import os
import tarfile

import mozunit
import pytest

from condprof import archiver
from condprof.archiver import Archiver


@pytest.fixture
def profile(tmp_path):
    profile = tmp_path / "profile"
    (profile / "minidumps").mkdir(parents=True)
    (profile / "prefs.js").write_text("prefs")
    (profile / "minidumps" / "crash.dmp").write_text("dump")
    (profile / "minidumps" / "crash.extra").write_text("extra")
    (profile / "minidumps" / "notes.txt").write_text("notes")
    return profile


@pytest.mark.parametrize("task_cluster", [False, True])
def test_create_archive_packs_profile_files(
    tmp_path, profile, monkeypatch, task_cluster
):
    monkeypatch.setattr(archiver, "TASK_CLUSTER", task_cluster)
    target = str(tmp_path / "out.tgz")
    assert (
        Archiver("settled", str(profile), str(tmp_path)).create_archive(target)
        == target
    )
    with tarfile.open(target) as tar:
        names = sorted(tar.getnames())
    assert names == ["minidumps", "minidumps/notes.txt", "prefs.js"]


def test_create_archive_named_after_date(tmp_path, profile):
    arch = Archiver("full", str(profile), str(tmp_path))
    when = datetime.date(2026, 11, 23)
    archive = arch.create_archive(when)
    assert archive == os.path.join(str(tmp_path), "full-2026-11-23-hp.tar.gz")
    assert os.path.exists(archive)


def test_create_archive_skips_vanished_files(tmp_path, profile, monkeypatch):
    real_add = tarfile.TarFile.add

    def add(self, name, *args, **kw):
        if name.endswith("prefs.js"):
            raise FileNotFoundError(name)
        return real_add(self, name, *args, **kw)

    monkeypatch.setattr(tarfile.TarFile, "add", add)
    target = str(tmp_path / "out.tgz")
    Archiver("settled", str(profile), str(tmp_path)).create_archive(target)
    with tarfile.open(target) as tar:
        assert "prefs.js" not in tar.getnames()


def test_create_archive_custom_iterator(tmp_path):
    seen = []

    def iterator(tar):
        yield 2
        for name in ("a", "b"):
            seen.append(name)
            yield name

    target = str(tmp_path / "out.tgz")
    Archiver("settled", str(tmp_path), str(tmp_path)).create_archive(
        target, iterator=iterator
    )
    assert seen == ["a", "b"]


def test_read_tar_maps_names_to_metadata_and_data(tmp_path, profile):
    target = str(tmp_path / "out.tgz")
    arch = Archiver("settled", str(profile), str(tmp_path))
    arch.create_archive(target)
    files = arch._read_tar(target)
    metadata, data = files["prefs.js"]
    assert metadata.name == "prefs.js"
    assert data == b"prefs"
    assert files["minidumps"][1] is None


def test_tarinfo2mem_extract_error():
    class BrokenTar:
        def extractfile(self, info):
            raise OSError()

    info = tarfile.TarInfo("x")
    metadata, data = archiver._tarinfo2mem(BrokenTar(), info)
    assert metadata.name == "x"
    assert metadata is not info
    assert data is None


if __name__ == "__main__":
    mozunit.main()
