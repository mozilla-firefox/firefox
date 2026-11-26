# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main

from gecko_taskgraph.task_context import source_url


@pytest.mark.parametrize(
    "params,expected",
    [
        pytest.param(
            {
                "repository_type": "hg",
                "head_repository": "https://hg.mozilla.org/integration/autoland",
                "head_rev": "abcdef",
            },
            "https://hg.mozilla.org/integration/autoland/rev/abcdef",
            id="hg",
        ),
        pytest.param(
            {
                "repository_type": "git",
                "head_repository": "https://github.com/mozilla-firefox/firefox/",
                "head_rev": "abcdef",
            },
            "https://github.com/mozilla-firefox/firefox/commit/abcdef",
            id="git",
        ),
    ],
)
def test_source_url(params, expected):
    assert source_url(params) == expected


if __name__ == "__main__":
    main()
