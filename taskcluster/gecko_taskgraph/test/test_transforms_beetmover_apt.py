# Any copyright is dedicated to the Public Domain.
# https://creativecommons.org/publicdomain/zero/1.0/

import pytest
from mozunit import main

from gecko_taskgraph.transforms.beetmover_apt import skip_project_branch_nightlies


@pytest.mark.parametrize(
    "release_type,expected_labels",
    (
        pytest.param("nightly", ["beetmover-apt"], id="nightly"),
        pytest.param("beta", ["beetmover-apt"], id="beta"),
        pytest.param("release", ["beetmover-apt"], id="release"),
        pytest.param("esr153", ["beetmover-apt"], id="esr153"),
        pytest.param("nightly-pine", [], id="nightly-pine"),
        pytest.param("nightly-larch", [], id="nightly-larch"),
    ),
)
def test_skip_project_branch_nightlies(run_transform, release_type, expected_labels):
    tasks = run_transform(
        skip_project_branch_nightlies,
        {"label": "beetmover-apt"},
        params={"release_type": release_type},
    )
    assert [t["label"] for t in tasks] == expected_labels


if __name__ == "__main__":
    main()
