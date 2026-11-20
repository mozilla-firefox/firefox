# Any copyright is dedicated to the public domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import pytest
from mozunit import main

from gecko_taskgraph.test.conftest import FakeParameters
from gecko_taskgraph.transforms.source_test import (
    MAX_ARG_STRLEN,
    set_changed_files_env,
)

LONGEST_PATH = "d/" + "x" * (MAX_ARG_STRLEN - len("CHANGED_FILES=") - 1 - 6) + ".cpp"


@pytest.mark.parametrize(
    "files_changed,expected",
    [
        pytest.param(
            ["dom/b.cpp", "dom/a.cpp", "dom/a.h", "README.md"],
            "dom/a.cpp dom/b.cpp",
            id="matching",
        ),
        pytest.param(["README.md"], "", id="none-matching"),
        pytest.param(
            [LONGEST_PATH],
            LONGEST_PATH,
            id="at-limit",
        ),
        pytest.param(
            ["d" + LONGEST_PATH],
            "",
            id="over-limit",
        ),
    ],
)
def test_set_changed_files_env(run_transform, files_changed, expected):
    job = {
        "name": "clang",
        "changed-files-env": "CHANGED_FILES",
        "when": {"files-changed": ["**/*.cpp"]},
        "worker": {},
    }
    params = FakeParameters({"files_changed": files_changed})
    (result,) = run_transform(set_changed_files_env, job, params=params)
    assert result["worker"]["env"] == {"CHANGED_FILES": expected}
    assert "changed-files-env" not in result


def test_set_changed_files_env_unset(run_transform):
    job = {"name": "other", "worker": {}}
    params = FakeParameters({"files_changed": ["a.cpp"]})
    (result,) = run_transform(set_changed_files_env, job, params=params)
    assert "env" not in result["worker"]
    assert "changed-files-env" not in result


if __name__ == "__main__":
    main()
