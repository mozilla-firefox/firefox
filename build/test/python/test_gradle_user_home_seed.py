# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import tarfile

import mozunit
import pytest

sys.path.insert(
    0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "mobile", "android")
)
from gradle import seed_gradle_user_home


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def make_seed(tmp_path, files, name="seed"):
    root = tmp_path / name / "gradle-user-home-seed"
    for path, text in files.items():
        write(root / "caches" / path, text)
    seed = tmp_path / f"{name}.tar"
    with tarfile.open(seed, "w") as tar:
        tar.add(root, arcname="gradle-user-home-seed")
    return str(seed)


def make_home(tmp_path, files={}):
    home = tmp_path / "home"
    home.mkdir()
    for path, text in files.items():
        write(home / "caches" / path, text)
    return home


def test_seed_gradle_user_home_empty(tmp_path):
    home = make_home(tmp_path)
    seed_gradle_user_home(make_seed(tmp_path, {"jars-9/a.jar": "seed"}), str(home))
    assert read(home / "caches" / "jars-9" / "a.jar") == "seed"
    assert len(read(home / ".seeds").split()) == 1
    assert not (home / "seed.tmp").exists()


def test_seed_gradle_user_home_keeps_existing(tmp_path):
    home = make_home(tmp_path, {"jars-9/a.jar": "mine"})
    seed = make_seed(tmp_path, {"jars-9/a.jar": "seed", "jars-9/b.jar": "seed"})
    seed_gradle_user_home(seed, str(home))
    assert read(home / "caches" / "jars-9" / "a.jar") == "mine"
    assert read(home / "caches" / "jars-9" / "b.jar") == "seed"


def test_seed_gradle_user_home_applied_once(tmp_path):
    home = make_home(tmp_path)
    seed = make_seed(tmp_path, {"jars-9/a.jar": "seed"})
    seed_gradle_user_home(seed, str(home))
    os.remove(home / "caches" / "jars-9" / "a.jar")
    seed_gradle_user_home(seed, str(home))
    assert not (home / "caches" / "jars-9" / "a.jar").exists()
    assert len(read(home / ".seeds").split()) == 1


def test_seed_gradle_user_home_changed_seed(tmp_path):
    home = make_home(tmp_path)
    seed_gradle_user_home(make_seed(tmp_path, {"jars-9/a.jar": "seed"}), str(home))
    seed_gradle_user_home(
        make_seed(tmp_path, {"jars-9/b.jar": "seed"}, name="seed2"), str(home)
    )
    assert sorted(p.name for p in (home / "caches" / "jars-9").iterdir()) == [
        "a.jar",
        "b.jar",
    ]
    assert len(read(home / ".seeds").split()) == 2


def test_seed_gradle_user_home_leftover_staging(tmp_path):
    home = make_home(tmp_path)
    write(home / "seed.tmp" / "partial", "")
    seed_gradle_user_home(make_seed(tmp_path, {"jars-9/a.jar": "seed"}), str(home))
    assert not (home / "seed.tmp").exists()
    assert read(home / "caches" / "jars-9" / "a.jar") == "seed"


def test_seed_gradle_user_home_no_seed(tmp_path):
    home = make_home(tmp_path)
    seed_gradle_user_home(None, str(home))
    assert list(home.iterdir()) == []


def test_seed_gradle_user_home_no_home(tmp_path):
    seed = make_seed(tmp_path, {"jars-9/a.jar": "seed"})
    with pytest.raises(Exception, match="GRADLE_USER_HOME is not"):
        seed_gradle_user_home(seed, None)


def test_seed_gradle_user_home_missing_seed(tmp_path):
    home = make_home(tmp_path)
    with pytest.raises(Exception, match="which is not a file"):
        seed_gradle_user_home(str(tmp_path / "missing.tar"), str(home))
    assert list(home.iterdir()) == []


if __name__ == "__main__":
    mozunit.main()
