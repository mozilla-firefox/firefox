# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
from pathlib import Path
from subprocess import CompletedProcess
from unittest import mock

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

from raptor_profiling import RaptorProfiling


@pytest.fixture
def toolchain_dir(tmp_path, monkeypatch):
    toolchain_dir = tmp_path / "toolchains"
    for tool in [
        Path("node", "node.exe"),
        Path("node", "bin", "node"),
        Path("profiler-node-tools", "profiler-edit.js"),
    ]:
        (toolchain_dir / tool).parent.mkdir(parents=True, exist_ok=True)
        (toolchain_dir / tool).touch()
    monkeypatch.setenv("MOZBUILD_STATE_PATH", str(toolchain_dir))
    return toolchain_dir


def test_source_path_mappings_are_passed_to_profiler_edit(tmp_path, toolchain_dir):
    source_path_mappings = [
        {"urlPrefix": "http://127.0.0.1:62763/", "localPath": str(tmp_path)}
    ]
    upload_dir = tmp_path / "upload"
    upload_dir.mkdir()
    profiler = RaptorProfiling(
        upload_dir,
        {"run_local": True},
        {"name": "speedometer3"},
        source_path_mappings,
    )
    profiler.profile = upload_dir / "profile_unprocessed.jslb.gz"
    profiler.profile.touch()

    profiler_edit_runs = []

    def run_profiler_edit(cmd, **kwargs):
        mapping_path = Path(cmd[cmd.index("--map-source-paths") + 1])
        profiler_edit_runs.append(json.loads(mapping_path.read_text()))
        Path(cmd[cmd.index("-o") + 1]).touch()
        return CompletedProcess(cmd, 0, stdout="", stderr="")

    with mock.patch("raptor_profiling.subprocess.run", side_effect=run_profiler_edit):
        profiles = profiler.post_process_profiles()
    profiler.clean()

    assert profiles
    assert profiler_edit_runs == [{"mappings": source_path_mappings}] * len(profiles)


if __name__ == "__main__":
    mozunit.main()
