# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import io
import os
import sys
import tarfile
import time
import types
from unittest import mock

import mozunit
import pytest

sys.modules.setdefault(
    "xdg", types.SimpleNamespace(xdg_config_home=lambda: os.path.expanduser("~"))
)

from talos import heavy  # noqa: E402


@pytest.fixture(autouse=True)
def log():
    with mock.patch.object(heavy, "LOG", new=mock.MagicMock()) as log:
        yield log


def test_progress_bar_logs_each_ten_percent(log):
    with heavy.ProgressBar(size=20, template="%d") as bar:
        for _ in range(25):
            bar.incr()
    assert bar.current == 20
    assert [c.args[0] for c in log.info.call_args_list] == [
        str(i) for i in range(10, 100, 10)
    ]


def response(status, headers=None):
    return mock.Mock(status_code=status, headers=headers or {})


def test_follow_redirects():
    pages = [
        response(303, {"Location": "http://b"}),
        response(200, {"Last-Modified": "Wed, 21 Oct 2015 07:28:00 GMT"}),
    ]
    with mock.patch.object(heavy.requests, "head", side_effect=pages) as head:
        location, last_modified = heavy.follow_redirects("http://a")
    assert location == "http://b"
    assert last_modified == datetime.datetime(2015, 10, 21, 7, 28)
    assert [c.args[0] for c in head.call_args_list] == ["http://a", "http://b"]


def test_follow_redirects_without_last_modified():
    with mock.patch.object(heavy.requests, "head", return_value=response(200)):
        assert heavy.follow_redirects("http://a") == ("http://a", None)


def test_follow_redirects_too_many():
    page = response(303, {"Location": "http://loop"})
    with mock.patch.object(heavy.requests, "head", return_value=page):
        with pytest.raises(ValueError, match="Max redirects"):
            heavy.follow_redirects("http://a", max=2)


def set_mtime(path, when):
    os.utime(path, (when, when))


def test_profile_age(tmp_path):
    now = time.time()
    profile = tmp_path / "profile"
    (profile / "sub").mkdir(parents=True)
    (profile / "sub" / "f").write_text("x")
    for p in (profile / "sub" / "f", profile / "sub", profile):
        set_mtime(p, now - 10 * 86400)
    set_mtime(profile / "sub" / "f", now - 3.5 * 86400)
    assert heavy._recursive_mtime(str(profile)) == pytest.approx(now - 3.5 * 86400)
    assert heavy.profile_age(str(profile)) == 3
    later = datetime.datetime.fromtimestamp(now + 2 * 86400)
    assert heavy.profile_age(str(profile), later) == 5


def make_archive():
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w:gz") as tar:
        content = b"user_pref('a', 1);"
        info = tarfile.TarInfo("prefs.js")
        info.size = len(content)
        tar.addfile(info, io.BytesIO(content))
    return data.getvalue()


@pytest.fixture
def download():
    archive = make_archive()
    resp = mock.Mock(headers={"content-length": str(len(archive))})
    resp.iter_content.return_value = [archive[:10], b"", archive[10:]]
    session = mock.Mock()
    session.get.return_value = resp
    with mock.patch.object(
        heavy, "follow_redirects", return_value=("https://real/url", None)
    ) as follow, mock.patch.object(heavy.requests, "Session", return_value=session):
        yield mock.Mock(follow=follow, session=session, response=resp)


def test_download_profile(tmp_path, download, log):
    target = heavy.download_profile("simple", profiles_dir=str(tmp_path / "p"))
    assert target == os.path.join(str(tmp_path / "p"), "simple")
    with open(os.path.join(target, "prefs.js"), "rb") as f:
        assert f.read() == b"user_pref('a', 1);"
    assert os.path.exists(
        os.path.join(str(tmp_path / "p"), ".cache", "today-simple.tgz")
    )
    download.follow.assert_called_once_with(heavy.TC_LINK % "simple")
    download.session.get.assert_called_once_with(
        "https://real/url", stream=True, timeout=20
    )
    download.response.raise_for_status.assert_called_once_with()
    assert any(
        c.args[0].startswith("Download progress") for c in log.info.call_args_list
    )


def test_download_profile_fresh_local_copy(tmp_path, download):
    target = tmp_path / "simple"
    target.mkdir()
    assert heavy.download_profile("simple", profiles_dir=str(tmp_path)) == str(target)
    download.session.get.assert_not_called()


def test_download_profile_stale_local_copy(tmp_path, download):
    target = tmp_path / "simple"
    target.mkdir()
    set_mtime(target, time.time() - 8 * 86400)
    heavy.download_profile("simple", profiles_dir=str(tmp_path))
    download.session.get.assert_called_once()


def test_download_profile_default_dir_xdg(tmp_path, download, monkeypatch):
    monkeypatch.delenv("MOZ_LEGACY_HOME", raising=False)
    with mock.patch.object(heavy, "xdg_config_home", return_value=str(tmp_path)):
        target = heavy.download_profile("simple")
    assert target == os.path.join(str(tmp_path), "mozilla", "profiles", "simple")


def test_download_profile_default_dir_legacy(tmp_path, download, monkeypatch):
    monkeypatch.setenv("MOZ_LEGACY_HOME", "1")
    with mock.patch.object(heavy.os.path, "expanduser", return_value=str(tmp_path)):
        target = heavy.download_profile("simple")
    assert target == os.path.join(str(tmp_path), ".mozilla", "profiles", "simple")


if __name__ == "__main__":
    mozunit.main()
