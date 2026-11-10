# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from mozunit import main
from taskgraph.task import Task

from gecko_taskgraph.test.conftest import FakeParameters
from gecko_taskgraph.transforms.copy_cached_dep import transforms


def test_copy_cached_dep_adds_eager_index(run_transform):
    dep = Task(
        kind="toolchain",
        label="toolchain-foo",
        attributes={
            "cached_task": {"type": "toolchain.v3", "name": "foo", "digest": "abc"}
        },
        task={},
    )
    task = {
        "name": "foo",
        "attributes": {"primary-dependency-label": dep.label},
    }

    (result,) = run_transform(
        transforms,
        task,
        kind_dependencies_tasks={dep.label: dep},
        params=FakeParameters({"level": "3", "tasks_for": "hg-push", "build_date": 0}),
    )

    assert result["attributes"]["cached_task"]["digest"] == "abc"
    assert result["attributes"]["eager_indexes"] == [
        "test-domain.cache.level-3.fake-kind.v1.foo.hash.abc"
    ]


if __name__ == "__main__":
    main()
