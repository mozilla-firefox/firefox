import json
import os

from mozharness.mozilla.testing import raptor_ab
from mozunit import main


def test_firefox_vs_custom_car(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)
    fetches = tmp_path / "fetches"
    fetches.mkdir()
    symbols = fetches / raptor_ab.SYMBOLS_ARTIFACT
    symbols.write_bytes(b"firefox symbols")
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(fetches))
    monkeypatch.setenv(
        "RAPTOR_AB_CONFIG",
        json.dumps({
            "order": ["new", "base"],
            "runs": {
                "base": {"browser": "custom-car", "description": "Chromium"},
                "new": {
                    "browser": "firefox",
                    "description": "Firefox",
                    "arguments": ["--app=firefox"],
                    "task_label": "firefox-task",
                },
            },
        }),
    )
    monkeypatch.setenv(
        "EXTRA_MOZHARNESS_CONFIG", json.dumps({"installer_url": "firefox.zip"})
    )
    calls = []

    def run_raptor(run, side):
        calls.append((run["browser"], side))
        upload = tmp_path / "build" / "blobber_upload_dir"
        upload.mkdir(parents=True)
        (upload / "profile.json").write_text(side)
        return 0

    monkeypatch.setattr(raptor_ab, "run_raptor", run_raptor)

    assert raptor_ab.run_ab_profiles() == 0
    assert calls == [("firefox", "new"), ("custom-car", "base")]
    assert symbols.read_bytes() == b"firefox symbols"
    upload = tmp_path / "build" / "blobber_upload_dir"
    assert (upload / "new" / "profile.json").read_text() == "new"
    assert (upload / "base" / "profile.json").read_text() == "base"
    summary = json.loads((upload / "ab-summary.json").read_text())
    assert summary["completed"] == ["new", "base"]
    assert summary["runs"]["base"]["installer_url"] is None


def test_two_firefox_builds(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)
    fetches = tmp_path / "fetches"
    fetches.mkdir()
    symbols = fetches / raptor_ab.SYMBOLS_ARTIFACT
    symbols.write_bytes(b"new symbols")
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(fetches))
    monkeypatch.setenv(
        "EXTRA_MOZHARNESS_CONFIG", json.dumps({"installer_url": "new.zip"})
    )
    monkeypatch.setenv(
        "RAPTOR_AB_CONFIG",
        json.dumps({
            "order": ["base", "new"],
            "runs": {
                "base": {
                    "browser": "firefox",
                    "description": "base",
                    "installer_url": "base.zip",
                    "symbols_url": "base-symbols.zip",
                },
                "new": {"browser": "firefox", "description": "new"},
            },
        }),
    )
    monkeypatch.setattr(
        raptor_ab, "download", lambda url, dest: dest.write_bytes(b"base symbols")
    )
    calls = []

    def run_raptor(run, side):
        calls.append((side, symbols.read_bytes()))
        upload = tmp_path / "build" / "blobber_upload_dir"
        upload.mkdir(parents=True)
        (upload / "profile.json").write_text(side)
        return 0

    monkeypatch.setattr(raptor_ab, "run_raptor", run_raptor)
    assert raptor_ab.run_ab_profiles() == 0
    assert calls == [("base", b"base symbols"), ("new", b"new symbols")]
    assert symbols.read_bytes() == b"new symbols"
    upload = tmp_path / "build" / "blobber_upload_dir"
    assert (upload / "base" / "profile.json").read_text() == "base"
    assert (upload / "new" / "profile.json").read_text() == "new"


def test_run_arguments(monkeypatch):
    monkeypatch.setenv("MOZ_FETCHES_DIR", "C:/fetches")
    monkeypatch.setenv("EXTRA_MOZHARNESS_CONFIG", "{}")
    monkeypatch.setattr(
        raptor_ab.sys,
        "argv",
        [os.path.join("scripts", "raptor_ab_script.py"), "--app=custom-car"],
    )
    recorded = []

    def call(command, env):
        recorded.append((command, env))
        return 0

    monkeypatch.setattr(raptor_ab.subprocess, "call", call)
    assert (
        raptor_ab.run_raptor(
            {
                "installer_url": "firefox.zip",
                "arguments": ["--app=firefox", "$MOZ_FETCHES_DIR/node/node.exe"],
                "task_label": "firefox-task",
            },
            "new",
        )
        == 0
    )
    command, env = recorded[0]
    assert command[2:] == [
        os.path.join("scripts", "raptor_script.py"),
        "--app=firefox",
        "C:/fetches/node/node.exe",
        "--perfherder-extra-option=ab-new",
    ]
    assert env["RAPTOR_TASK_LABEL"] == "firefox-task"
    extra_config = json.loads(env["EXTRA_MOZHARNESS_CONFIG"])
    assert extra_config["installer_url"] == "firefox.zip"
    assert extra_config["log_dir"] == os.path.join("logs", "new")


def test_run_without_installer_has_no_extra_option(monkeypatch):
    monkeypatch.setenv("EXTRA_MOZHARNESS_CONFIG", "{}")
    monkeypatch.setattr(
        raptor_ab.sys,
        "argv",
        [os.path.join("scripts", "raptor_ab_script.py"), "--app=firefox"],
    )
    recorded = []

    def call(command, env):
        recorded.append(command)
        return 0

    monkeypatch.setattr(raptor_ab.subprocess, "call", call)
    assert raptor_ab.run_raptor({}, "new") == 0
    assert recorded[0][2:] == [
        os.path.join("scripts", "raptor_script.py"),
        "--app=firefox",
    ]


if __name__ == "__main__":
    main()
