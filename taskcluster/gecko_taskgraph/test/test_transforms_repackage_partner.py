# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main
from taskgraph.task import Task

from gecko_taskgraph.test.conftest import FakeParameters
from gecko_taskgraph.transforms.repackage_partner import make_job_description


@pytest.mark.parametrize("kind", ["release-partner-repack", "release-eme-free-repack"])
@pytest.mark.parametrize(
    "platform,upstream_suffix,stub_installer",
    [
        pytest.param("macosx64-shippable", "-mac-notarization", False, id="production"),
        pytest.param("macosx64-shippable", "-mac-signing", False, id="staging"),
        pytest.param("win32-shippable", "", False, id="win32"),
        pytest.param("win32-shippable", "", True, id="win32-stub"),
        pytest.param("win64-shippable", "", False, id="win64"),
        pytest.param("win64-aarch64-shippable", "", False, id="win64-aarch64"),
    ],
)
def test_repackage_fetch_source(
    run_transform, monkeypatch, kind, platform, upstream_suffix, stub_installer
):
    upstream_kind = kind + upstream_suffix
    dependencies = (
        {kind: "partner-repack"}
        if upstream_suffix
        else {"build-signing": "original-build-signing"}
    )
    if upstream_suffix == "-mac-notarization":
        dependencies[f"{kind}-mac-signing"] = "partner-mac-signing"
    upstream = Task(
        kind=upstream_kind,
        label="selected-upstream",
        attributes={
            "kind": upstream_kind,
            "build_platform": platform,
            "build_type": "opt",
            "artifact_prefix": "releng/partner",
        },
        task={},
        dependencies=dependencies,
    )
    repack_id = "acme/one/en-US"
    job = {
        "label": "partner-repackage",
        "attributes": {"primary-dependency-label": upstream.label},
        "build-platform": platform,
        "extra": {"repack_id": repack_id},
        "package-formats": ["dmg" if upstream_suffix else "installer"],
    }
    monkeypatch.setattr(
        "gecko_taskgraph.transforms.repackage_partner.worker_type_implementation",
        lambda *args: ("docker-worker", "linux"),
    )
    (result,) = run_transform(
        make_job_description,
        job,
        kind=f"{kind}-repackage",
        kind_dependencies_tasks={upstream.label: upstream},
        params=FakeParameters({
            "project": "mozilla-release"
            if upstream_suffix == "-mac-notarization"
            else "try",
            "release_partners": [],
            "release_partner_config": {
                kind: {"acme": {"one": {"repack_stub_installer": stub_installer}}}
            },
        }),
    )
    if upstream_suffix:
        expected = [{"artifact": f"{repack_id}/target.tar.gz", "extract": False}]
    else:
        expected = [
            {"artifact": f"{repack_id}/target.zip", "extract": False},
            f"{repack_id}/setup.exe",
        ]
        if stub_installer:
            expected.extend([
                {"artifact": f"{repack_id}/target-stub.zip", "extract": False},
                f"{repack_id}/setup-stub.exe",
            ])
    fetches = {
        key: value for key, value in result["fetches"].items() if key != "toolchain"
    }
    assert fetches == {upstream_kind: expected}
    assert result["dependencies"][upstream_kind] == upstream.label
    assert all(
        result["dependencies"][key] == value for key, value in dependencies.items()
    )


if __name__ == "__main__":
    main()
