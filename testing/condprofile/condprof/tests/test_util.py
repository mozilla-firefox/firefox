# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import re
import sys
import types
from unittest import mock

import mozunit
import pytest
import requests
import responses
import yaml

from condprof import util


@pytest.fixture(autouse=True)
def clean_env(monkeypatch):
    for key in ("CONDPROF_RUNNER", "FXA_USERNAME", "FXA_PASSWORD", "MOZ_FETCHES_DIR"):
        monkeypatch.delenv(key, raising=False)
    monkeypatch.setattr(util, "_CACHED", {})
    monkeypatch.setattr(util, "TASK_CLUSTER", False)


class RecordingLogger:
    def __init__(self):
        self.calls = []

    def __getattr__(self, name):
        def _log(message, *args, **kw):
            self.calls.append((name, message))

        return _log


def test_bridge_logger_routes_messages():
    recorder = RecordingLogger()
    bridge = util.BridgeLogger(recorder)
    bridge.info("request" + " " * util.STRUCTLOG_PAD_SIZE + "url=x")
    bridge.info("response" + " " * util.STRUCTLOG_PAD_SIZE)
    bridge.info("plain message")
    bridge.info(42)
    bridge.error("bad")
    bridge.warning("careful")
    assert [name for name, _ in recorder.calls] == [
        "debug",
        "debug",
        "info",
        "info",
        "error",
        "warning",
    ]
    assert recorder.calls[3] == ("info", "42")


def test_bridge_logger_obfuscates(monkeypatch):
    monkeypatch.setenv("CONDPROF_RUNNER", "1")
    monkeypatch.setenv("FXA_USERNAME", "bob")
    monkeypatch.setenv("FXA_PASSWORD", "s3cret")
    recorder = RecordingLogger()
    util.BridgeLogger(recorder).info("bob logs in with s3cret")
    assert recorder.calls == [("info", "<USERNAME> logs in with <PASSWORD>")]


def test_get_logger_is_cached():
    assert util.get_logger() is util.get_logger()


@pytest.mark.parametrize("default_logger", [None, "structured"])
def test_get_logger_bridges_arsenic(monkeypatch, default_logger):
    monkeypatch.setattr(util, "logger", None)
    fallback = RecordingLogger()
    structured = RecordingLogger()
    monkeypatch.setattr(
        util.mozlog,
        "get_default_logger",
        lambda name: structured if default_logger else None,
    )
    monkeypatch.setattr(util.mozlog.unstructured, "getLogger", lambda name: fallback)
    connection = types.ModuleType("arsenic.connection")
    arsenic = types.ModuleType("arsenic")
    arsenic.connection = connection
    structlog = types.ModuleType("structlog")
    structlog.wrap_logger = lambda logger: ("wrapped", logger)
    monkeypatch.setitem(sys.modules, "arsenic", arsenic)
    monkeypatch.setitem(sys.modules, "arsenic.connection", connection)
    monkeypatch.setitem(sys.modules, "structlog", structlog)

    new_logger = util.get_logger()

    assert isinstance(new_logger, util.BridgeLogger)
    expected = structured if default_logger else fallback
    assert new_logger.logger is expected
    assert connection.log == ("wrapped", new_logger)
    assert util.logger is new_logger


def test_get_logger_without_arsenic(monkeypatch):
    monkeypatch.setattr(util, "logger", None)
    monkeypatch.setitem(sys.modules, "arsenic", None)
    assert isinstance(util.get_logger(), util.BridgeLogger)


def test_fresh_profile_sets_prefs_and_installs_addons(tmp_path, monkeypatch):
    source = tmp_path / "source"
    source.mkdir()
    (source / "prefs.js").write_text("")
    fake_profile = mock.Mock(profile=str(source))
    monkeypatch.setattr("mozprofile.create_profile", lambda app: fake_profile)
    downloads = []

    def fake_download(url, mozfetches_subdir=None):
        downloads.append((url, mozfetches_subdir))
        return "/fetches/" + url.split("/")[-1]

    monkeypatch.setattr(util, "download_file", fake_download)
    target = str(tmp_path / "profile")
    data = {"prefs": {"some.pref": 1}, "addons": {"ublock": "https://x/ublock.xpi"}}

    assert util.fresh_profile(target, data) == target

    prefs = fake_profile.set_preferences.call_args[0][0]
    assert prefs["some.pref"] == 1
    for key, value in util.DEFAULT_PREFS.items():
        assert prefs[key] == value
    assert downloads == [("https://x/ublock.xpi", "firefox-addons")]
    fake_profile.addons.install.assert_called_once_with(["/fetches/ublock.xpi"])
    assert (tmp_path / "profile" / "prefs.js").exists()


FTP_LINKS = [
    "/pub/firefox/nightly/latest-mozilla-central/firefox-150.0a1.en-US.mac.dmg",
    "/pub/firefox/nightly/latest-mozilla-central/firefox-150.0a1.en-US.linux-x86_64.tar.xz",
    "/pub/firefox/nightly/latest-mozilla-central/firefox-150.0a1.en-US.win64.zip",
]


def _fake_bs4(links):
    class Soup:
        def __init__(self, page, parser):
            assert parser == "html.parser"

        def find_all(self, tag, href):
            return [{"href": link} for link in links]

    module = types.ModuleType("bs4")
    module.BeautifulSoup = Soup
    return module


@pytest.mark.parametrize(
    "system,machine,expected",
    [
        ("Darwin", "arm64", FTP_LINKS[0]),
        ("Linux", "x86_64", FTP_LINKS[1]),
    ],
)
@responses.activate
def test_get_firefox_download_link(monkeypatch, system, machine, expected):
    responses.add(responses.GET, util.link, body="<html></html>", status=200)
    monkeypatch.setitem(sys.modules, "bs4", _fake_bs4(FTP_LINKS))
    monkeypatch.setattr(util.platform, "system", lambda: system)
    monkeypatch.setattr(util.platform, "machine", lambda: machine)
    assert util.get_firefox_download_link() == "https://ftp.mozilla.org" + expected


@responses.activate
def test_get_firefox_download_link_no_match(monkeypatch):
    responses.add(responses.GET, util.link, body="<html></html>", status=200)
    monkeypatch.setitem(sys.modules, "bs4", _fake_bs4(FTP_LINKS[2:]))
    monkeypatch.setattr(util.platform, "system", lambda: "Darwin")
    with pytest.raises(Exception):
        util.get_firefox_download_link()


def test_get_firefox_download_link_unsupported_platform(monkeypatch):
    monkeypatch.setitem(sys.modules, "bs4", _fake_bs4([]))
    monkeypatch.setattr(util.platform, "system", lambda: "Windows")
    with pytest.raises(NotImplementedError):
        util.get_firefox_download_link()


def test_get_firefox_download_link_needs_bs4(monkeypatch):
    monkeypatch.setitem(sys.modules, "bs4", None)
    with pytest.raises(ImportError, match="beautifulsoup4"):
        util.get_firefox_download_link()


URL = "https://example.com/file.tgz"


@responses.activate
def test_check_exists_found():
    responses.add(
        responses.HEAD, URL, headers={"Content-Type": "application/x-gzip"}, status=200
    )
    exists, headers = util.check_exists("file.tgz", server="https://example.com")
    assert exists == 200
    assert headers["Content-Type"] == "application/x-gzip"


@responses.activate
def test_check_exists_html_page():
    responses.add(responses.HEAD, URL, headers={"Content-Type": "text/html"})
    assert util.check_exists(URL)[0] is False
    assert util.check_exists(URL, all_types=True)[0] == 200


@responses.activate
def test_check_exists_not_found():
    responses.add(responses.HEAD, URL, status=404)
    assert util.check_exists(URL)[0] == 404


@responses.activate
def test_check_exists_follows_redirect():
    other = "https://example.com/other.tgz"
    responses.add(responses.HEAD, URL, status=302, headers={"Location": other})
    responses.add(responses.HEAD, other, headers={"Content-Type": "application/gzip"})
    assert util.check_exists(URL)[0] == 200


def test_check_exists_connection_error(monkeypatch):
    def fail(*args, **kw):
        raise requests.exceptions.ConnectionError()

    monkeypatch.setattr(util.requests, "head", fail)
    assert util.check_exists(URL) == (False, {})


def test_check_mozfetches_dir(tmp_path, monkeypatch):
    assert util.check_mozfetches_dir("a.xpi", "addons") is None
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(tmp_path))
    assert util.check_mozfetches_dir("a.xpi", "addons") is None
    (tmp_path / "addons").mkdir()
    (tmp_path / "addons" / "a.xpi").write_text("")
    assert util.check_mozfetches_dir("a.xpi", "addons") == str(
        tmp_path / "addons" / "a.xpi"
    )


def test_download_file_from_mozfetches(tmp_path, monkeypatch):
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(tmp_path))
    (tmp_path / "addons").mkdir()
    (tmp_path / "addons" / "file.tgz").write_text("")
    assert util.download_file(URL, mozfetches_subdir="addons") == str(
        tmp_path / "addons" / "file.tgz"
    )


@responses.activate
def test_download_file_missing(tmp_path):
    responses.add(responses.HEAD, URL, headers={"Content-Type": "text/html"})
    with pytest.raises(util.ArchiveNotFound):
        util.download_file(URL, target=str(tmp_path / "file.tgz"))


def _add_file(etag=None, body=b"x" * 3000):
    headers = {"Content-Type": "application/gzip", "content-length": str(len(body))}
    if etag is not None:
        headers["ETag"] = etag
    responses.add(responses.HEAD, URL, headers=headers)
    responses.add(responses.GET, URL, body=body, headers=headers)


@pytest.mark.parametrize("task_cluster", [False, True])
@responses.activate
def test_download_file_new(tmp_path, monkeypatch, task_cluster):
    monkeypatch.setattr(util, "TASK_CLUSTER", task_cluster)
    _add_file(etag="abc")
    target = tmp_path / "sub" / "file.tgz"
    assert util.download_file(URL, target=str(target)) == str(target)
    assert target.read_bytes() == b"x" * 3000
    assert (tmp_path / "sub" / "file.tgz.etag").read_text() == "abc"


@responses.activate
def test_download_file_default_target(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    _add_file()
    assert util.download_file(URL) == "file.tgz"
    assert (tmp_path / "file.tgz").exists()
    assert not (tmp_path / "file.tgz.etag").exists()


@pytest.mark.parametrize("etag,stored", [(None, "abc"), ("abc", None), ("abc", "abc")])
@responses.activate
def test_download_file_reuses_existing(tmp_path, etag, stored):
    _add_file(etag=etag)
    target = tmp_path / "file.tgz"
    target.write_bytes(b"old")
    if stored is not None:
        (tmp_path / "file.tgz.etag").write_text(stored)
    assert util.download_file(URL, target=str(target)) == str(target)
    assert target.read_bytes() == b"old"
    assert [call.request.method for call in responses.calls] == ["HEAD"]


@responses.activate
def test_download_file_etag_changed(tmp_path):
    _add_file(etag="new")
    target = tmp_path / "file.tgz"
    target.write_bytes(b"old")
    (tmp_path / "file.tgz.etag").write_text("old")
    util.download_file(URL, target=str(target))
    assert target.read_bytes() == b"x" * 3000
    assert (tmp_path / "file.tgz.etag").read_text() == "new"


@responses.activate
def test_download_file_cannot_list_cache(tmp_path, monkeypatch):
    _add_file()
    real_listdir = util.os.listdir

    def listdir(path):
        if path == str(tmp_path):
            raise OSError("nope")
        return real_listdir(path)

    monkeypatch.setattr(util.os, "listdir", listdir)
    target = tmp_path / "file.tgz"
    util.download_file(URL, target=str(target))
    assert target.exists()


@pytest.fixture
def fake_system(monkeypatch):
    commands = []
    monkeypatch.setattr(util.os, "system", commands.append)
    return commands


def test_extract_from_dmg(tmp_path, monkeypatch, fake_system):
    mount = tmp_path / "mount"
    (mount / "README").mkdir(parents=True)
    (mount / "Firefox.app" / "Contents").mkdir(parents=True)
    (mount / "Firefox.app" / "Contents" / "Info.plist").write_text("")
    monkeypatch.setattr(util.tempfile, "mkdtemp", lambda: str(mount))
    target = tmp_path / "firefox.app"

    util.extract_from_dmg("nightly.dmg", str(target))

    assert (target / "Contents" / "Info.plist").exists()
    assert not mount.exists()
    assert fake_system[0].startswith("hdiutil attach -nobrowse")
    assert fake_system[0].endswith("nightly.dmg")
    assert fake_system[1] == "hdiutil detach " + str(mount)


def test_extract_from_dmg_without_app(tmp_path, monkeypatch, fake_system):
    mount = tmp_path / "mount"
    mount.mkdir()
    monkeypatch.setattr(util.tempfile, "mkdtemp", lambda: str(mount))
    with pytest.raises(OSError, match="No app file"):
        util.extract_from_dmg("nightly.dmg", str(tmp_path / "firefox.app"))
    assert not mount.exists()


def test_latest_nightly_with_binary(fake_system):
    with util.latest_nightly("/path/to/firefox") as binary:
        assert binary == "/path/to/firefox"
    assert fake_system == []


@pytest.mark.parametrize(
    "system,binary,commands",
    [
        (
            "Darwin",
            "/Volumes/Nightly/Firefox Nightly.app/Contents/MacOS/firefox",
            [
                "hdiutil attach -mountpoint /Volumes/Nightly nightly.archive",
                "hdiutil detach /Volumes/Nightly",
            ],
        ),
        ("Linux", "firefox/firefox", ["tar -xvf nightly.archive"]),
    ],
)
def test_latest_nightly_downloads(monkeypatch, fake_system, system, binary, commands):
    monkeypatch.setattr(util.platform, "system", lambda: system)
    monkeypatch.setattr(util, "get_firefox_download_link", lambda: "https://x/n")
    monkeypatch.setattr(util, "download_file", lambda url: "nightly.archive")
    monkeypatch.setattr(util.time, "sleep", lambda s: None)
    removed = []
    monkeypatch.setattr(util.shutil, "rmtree", removed.append)

    with util.latest_nightly() as found:
        assert found == binary

    assert fake_system == commands
    assert removed == (["firefox"] if system == "Linux" else [])


def test_write_yml_file(tmp_path):
    target = tmp_path / "config.yml"
    util.write_yml_file(str(target), {"args": ["-marionette"], "env": {"A": 1}})
    assert yaml.safe_load(target.read_text()) == {
        "args": ["-marionette"],
        "env": {"A": 1},
    }


def test_write_yml_file_error_is_logged(tmp_path):
    util.write_yml_file(str(tmp_path / "missing" / "config.yml"), {})
    assert not (tmp_path / "missing").exists()


def test_get_version_parses_firefox_version_output(monkeypatch):
    calls = []

    class FakePopen:
        def __init__(self, cmd, **kw):
            calls.append(cmd)

        def communicate(self):
            return b"Mozilla Firefox 150.0a1\nsecond line\n", b""

    monkeypatch.setattr(util, "Popen", FakePopen)
    assert util.get_version("/bin/firefox") == "150.0a1"
    assert calls == [["/bin/firefox", "--version"]]


@pytest.mark.parametrize(
    "system,maxsize,expected",
    [
        ("Windows", 2**63 - 1, "win64"),
        ("Darwin", 2**63 - 1, "macosx64"),
        ("Linux", 2**31 - 1, "linux32"),
    ],
)
def test_get_current_platform_per_os_and_pointer_size(
    monkeypatch, system, maxsize, expected
):
    monkeypatch.setattr(util.platform, "system", lambda: system)
    monkeypatch.setattr(util.sys, "maxsize", maxsize)
    assert util.get_current_platform() == expected


def test_base_env_stores_args_and_abstract_methods_raise(tmp_path):
    env = util.BaseEnv(str(tmp_path), "firefox", "geckodriver", "archive", "dev")
    assert env.profile == str(tmp_path)
    assert (env.firefox, env.geckodriver) == ("firefox", "geckodriver")
    assert (env.archive, env.device_name) == ("archive", "dev")
    for name, args in (
        ("get_target_platform", ()),
        ("get_device", ()),
        ("get_browser", (None,)),
        ("get_browser_args", (True,)),
        ("get_browser_version", ()),
        ("get_geckodriver", (None,)),
    ):
        with pytest.raises(NotImplementedError):
            result = getattr(env, name)(*args)
            if name == "get_browser":
                result.__enter__()
    with pytest.raises(NotImplementedError):
        env.target_platform
    assert env.prepare("log") is None
    assert env.check_session(None) is None
    assert env.dump_logs() is None
    assert env.collect_profile() is None
    assert env.stop_browser() is None


def test_base_env_temporary_profile():
    env = util.BaseEnv(None, None, None, None, None)
    assert util.os.path.basename(env.profile) == "profile"
    util.shutil.rmtree(util.os.path.dirname(env.profile))


def test_get_tc_secret_outside_taskcluster():
    with pytest.raises(OSError):
        util.get_tc_secret()


@pytest.mark.parametrize("proxy", [None, "http://taskcluster"])
@responses.activate
def test_get_tc_secret(monkeypatch, proxy):
    monkeypatch.setattr(util, "TASK_CLUSTER", True)
    monkeypatch.setenv("MOZ_SCM_LEVEL", "3")
    if proxy:
        monkeypatch.setenv("TASKCLUSTER_PROXY_URL", proxy)
    else:
        monkeypatch.delenv("TASKCLUSTER_PROXY_URL", raising=False)
    url = (proxy or util._DEFAULT_SERVER) + (
        "/secrets/v1/secret/project%2Freleng%2Fgecko%2Fbuild%2Flevel-3"
        "%2Fconditioned-profiles"
    )
    responses.add(
        responses.GET, url, body=json.dumps({"secret": {"username": "u"}}), status=200
    )
    assert util.get_tc_secret() == {"username": "u"}


@responses.activate
def test_get_tc_secret_error(monkeypatch):
    monkeypatch.setattr(util, "TASK_CLUSTER", True)
    monkeypatch.delenv("TASKCLUSTER_PROXY_URL", raising=False)
    responses.add(
        responses.GET, re.compile(util._DEFAULT_SERVER + "/secrets/.*"), status=403
    )
    with pytest.raises(requests.exceptions.HTTPError):
        util.get_tc_secret()


def test_obfuscate_without_runner():
    assert util.obfuscate("bob s3cret") == (True, "bob s3cret")


def test_obfuscate_without_credentials(monkeypatch):
    monkeypatch.setenv("CONDPROF_RUNNER", "1")
    assert util.obfuscate("bob s3cret") == (False, "bob s3cret")


def test_obfuscate_replaces_credentials_when_runner_set(monkeypatch):
    monkeypatch.setenv("CONDPROF_RUNNER", "1")
    monkeypatch.setenv("FXA_USERNAME", "bob")
    monkeypatch.setenv("FXA_PASSWORD", "s3cret")
    assert util.obfuscate("nothing here") == (False, "nothing here")
    assert util.obfuscate("bob:s3cret") == (True, "<USERNAME>:<PASSWORD>")


def test_obfuscate_file_rewrites_only_when_credentials_match(tmp_path, monkeypatch):
    log = tmp_path / "log.txt"
    log.write_text("user bob")
    util.obfuscate_file(str(log))
    assert log.read_text() == "user bob"

    monkeypatch.setenv("CONDPROF_RUNNER", "1")
    monkeypatch.setenv("FXA_USERNAME", "bob")
    monkeypatch.setenv("FXA_PASSWORD", "s3cret")
    other = tmp_path / "other.txt"
    other.write_text("nothing")
    util.obfuscate_file(str(other))
    assert other.read_text() == "nothing"
    util.obfuscate_file(str(log))
    assert log.read_text() == "user <USERNAME>"


def test_get_credentials_from_env(monkeypatch):
    monkeypatch.setenv("FXA_USERNAME", "bob")
    monkeypatch.setenv("FXA_PASSWORD", "s3cret")
    assert util.get_credentials() == ("bob", "s3cret")
    monkeypatch.setenv("FXA_USERNAME", "alice")
    assert util.get_credentials() == ("bob", "s3cret")


def test_get_credentials_from_taskcluster(monkeypatch):
    monkeypatch.setattr(util, "TASK_CLUSTER", True)
    monkeypatch.setattr(
        util, "get_tc_secret", lambda: {"username": "tc-user", "password": "tc-pass"}
    )
    assert util.get_credentials() == ("tc-user", "tc-pass")


if __name__ == "__main__":
    mozunit.main()
