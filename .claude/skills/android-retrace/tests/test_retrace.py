# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Run with: ./mach python --virtualenv python-test .agents/skills/android-retrace/tests/test_retrace.py"""

import importlib.util
import io
import sys
import urllib.error
import zipfile
from pathlib import Path

import mozunit
import pytest

spec = importlib.util.spec_from_file_location(
    "retrace", Path(__file__).resolve().parent.parent / "retrace.py"
)
retrace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retrace)

QUEUE = f"{retrace.ROOT}/queue/v1"
INDEX = f"{retrace.ROOT}/index/v1/task/gecko.v2"


def not_found(url):
    return urllib.error.HTTPError(url, 404, "Not Found", {}, None)


@pytest.fixture
def responses(monkeypatch):
    """Serve get_json from a dict of URL to response; a missing URL is a 404."""
    served = {}

    def fake_get_json(url):
        if url not in served:
            raise not_found(url)
        return served[url]

    monkeypatch.setattr(retrace, "get_json", fake_get_json)
    return served


def test_find_signing_task_builds_index_path(responses):
    responses[
        f"{INDEX}.mozilla-central.pushdate.2026.11.11.20261111091117"
        ".mobile.fenix-nightly"
    ] = {"taskId": "signing-apk"}

    assert retrace.find_signing_task("20261111091117", "fenix", ["nightly"]) == (
        "nightly",
        "signing-apk",
    )


def test_find_signing_task_tries_each_channel(responses):
    responses[
        f"{INDEX}.mozilla-beta.pushdate.2026.11.11.20261111090329.mobile.focus-beta"
    ] = {"taskId": "signing-apk"}

    assert retrace.find_signing_task(
        "20261111090329", "focus", list(retrace.CHANNELS)
    ) == ("beta", "signing-apk")


def test_find_signing_task_returns_none_when_unknown(responses):
    assert retrace.find_signing_task(
        "20990101000000", "fenix", list(retrace.CHANNELS)
    ) == (None, None)


def test_find_task_in_group_follows_continuation(responses):
    def task(task_id, name):
        return {"task": {"metadata": {"name": name}}, "status": {"taskId": task_id}}

    responses[f"{QUEUE}/task-group/group/list"] = {
        "tasks": [task("a", "build-apk-fenix-nightly")],
        "continuationToken": "next",
    }
    responses[f"{QUEUE}/task-group/group/list?continuationToken=next"] = {
        "tasks": [task("b", "signing-bundle-fenix-nightly")],
    }

    assert retrace.find_task_in_group("group", "signing-bundle-fenix-nightly") == "b"
    assert retrace.find_task_in_group("group", "signing-bundle-focus-nightly") is None


def add_shipping_graph(responses, signing_bundle_deps):
    responses[
        f"{INDEX}.mozilla-central.pushdate.2026.11.11.20261111091117"
        ".mobile.fenix-nightly"
    ] = {"taskId": "signing-apk"}
    responses[f"{QUEUE}/task/signing-apk"] = {"taskGroupId": "group"}
    responses[f"{QUEUE}/task-group/group/list"] = {
        "tasks": [
            {
                "task": {"metadata": {"name": "signing-bundle-fenix-nightly"}},
                "status": {"taskId": "signing-bundle"},
            }
        ]
    }
    responses[f"{QUEUE}/task/signing-bundle"] = {
        "dependencies": list(signing_bundle_deps)
    }
    for task_id, name in signing_bundle_deps.items():
        responses[f"{QUEUE}/task/{task_id}"] = {"metadata": {"name": name}}


def test_find_bundle_build_follows_shipping_graph(responses):
    add_shipping_graph(
        responses,
        {"decision": "Gecko Decision Task", "bundle": "build-bundle-fenix-nightly"},
    )

    assert retrace.find_bundle_build("20261111091117", "fenix", ["nightly"]) == (
        "nightly",
        "bundle",
    )


def test_find_bundle_build_without_build_bundle_exits(responses):
    add_shipping_graph(responses, {"decision": "Gecko Decision Task"})

    with pytest.raises(SystemExit, match="No build-bundle task"):
        retrace.find_bundle_build("20261111091117", "fenix", ["nightly"])


def test_find_bundle_build_unknown_build_exits(responses):
    with pytest.raises(SystemExit, match="--aab"):
        retrace.find_bundle_build("20990101000000", "fenix", ["nightly"])


def test_download_mapping_reuses_cache(tmp_path, monkeypatch):
    cached = tmp_path / "mapping.txt"
    cached.write_text("cached")

    def fail(*args, **kwargs):
        raise AssertionError("should not download")

    monkeypatch.setattr(retrace.urllib.request, "urlopen", fail)

    assert retrace.download_mapping("task", cached) == cached


def test_download_mapping_writes_file(tmp_path, monkeypatch):
    monkeypatch.setattr(
        retrace.urllib.request,
        "urlopen",
        lambda request: io.BytesIO(b"# compiler: R8\n"),
    )
    dest = tmp_path / "out" / "mapping.txt"

    assert retrace.download_mapping("task", dest) == dest
    assert dest.read_text() == "# compiler: R8\n"


def test_download_mapping_missing_artifact_exits(tmp_path, monkeypatch):
    def raise_not_found(request):
        raise not_found(request.full_url)

    monkeypatch.setattr(retrace.urllib.request, "urlopen", raise_not_found)
    dest = tmp_path / "mapping.txt"

    with pytest.raises(SystemExit, match="Google Play Console"):
        retrace.download_mapping("task", dest)
    assert list(tmp_path.iterdir()) == []


def test_extract_from_aab(tmp_path):
    aab = tmp_path / "app.aab"
    with zipfile.ZipFile(aab, "w") as bundle:
        bundle.writestr(retrace.AAB_MAPPING, "a.b.C -> x:\n")
    dest = tmp_path / "mapping.txt"

    assert retrace.extract_from_aab(aab, dest) == dest
    assert dest.read_text() == "a.b.C -> x:\n"


def test_extract_from_aab_without_mapping_exits(tmp_path):
    aab = tmp_path / "app.aab"
    with zipfile.ZipFile(aab, "w") as bundle:
        bundle.writestr("base/manifest/AndroidManifest.xml", "")

    with pytest.raises(SystemExit, match="not R8-optimised"):
        retrace.extract_from_aab(aab, tmp_path / "mapping.txt")


def make_cmdline_tools(sdk, versions):
    for version in versions:
        bin_dir = sdk / "cmdline-tools" / version / "bin"
        bin_dir.mkdir(parents=True)
        (bin_dir / "retrace").touch()


@pytest.fixture
def no_path_retrace(monkeypatch, tmp_path):
    monkeypatch.setattr(retrace.shutil, "which", lambda name: None)
    monkeypatch.setattr(retrace.platform, "system", lambda: "Darwin")
    monkeypatch.setattr(retrace.Path, "home", lambda: tmp_path / "home")
    monkeypatch.delenv("ANDROID_HOME", raising=False)
    monkeypatch.delenv("ANDROID_SDK_ROOT", raising=False)


def test_find_retrace_prefers_highest_version(tmp_path, monkeypatch, no_path_retrace):
    sdk = tmp_path / "sdk"
    make_cmdline_tools(sdk, ["17.0", "9.0", "23.0"])
    (sdk / "cmdline-tools" / "mozboot" / "bin").mkdir(parents=True)
    (sdk / "cmdline-tools" / "mozboot" / "bin" / "retrace").touch()
    monkeypatch.setenv("ANDROID_HOME", str(sdk))

    assert retrace.find_retrace() == str(sdk / "cmdline-tools/23.0/bin/retrace")


def test_find_retrace_prefers_latest(tmp_path, monkeypatch, no_path_retrace):
    sdk = tmp_path / "sdk"
    make_cmdline_tools(sdk, ["23.0", "latest"])
    monkeypatch.setenv("ANDROID_HOME", str(sdk))

    assert retrace.find_retrace() == str(sdk / "cmdline-tools/latest/bin/retrace")


def test_find_retrace_falls_back_to_mozbuild(tmp_path, no_path_retrace):
    sdk = tmp_path / "home" / ".mozbuild" / "android-sdk-macosx"
    make_cmdline_tools(sdk, ["21.0"])

    assert retrace.find_retrace() == str(sdk / "cmdline-tools/21.0/bin/retrace")


def test_find_retrace_missing_exits(no_path_retrace):
    with pytest.raises(SystemExit, match="mach bootstrap"):
        retrace.find_retrace()


@pytest.mark.parametrize("build_id", ["123", "2026111109111x", "202611110911170"])
def test_main_rejects_bad_build_id(monkeypatch, build_id):
    monkeypatch.setattr(sys, "argv", ["retrace.py", "--build-id", build_id])

    with pytest.raises(SystemExit, match="14-digit"):
        retrace.main()


if __name__ == "__main__":
    mozunit.main()
