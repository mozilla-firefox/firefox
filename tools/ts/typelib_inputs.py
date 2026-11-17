# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import mozpack.path as mozpath

IDL_EXCLUDED_DIRS = (
    "accessible/interfaces/ia2",
    "accessible/interfaces/msaa",
    "other-licenses",
    "python/mozbuild/mozbuild/test",
    "testing/web-platform",
    "third_party",
    "toolkit/components/translations/cld2",
    "tools/ts/test",
)

IDL_PATTERNS = ("**/*.idl",)


def select_source_files(paths, patterns, excluded_dirs):
    """Return the sorted ``paths`` that match one of the ``patterns`` and that
    are not under one of the ``excluded_dirs``."""
    return sorted(
        path
        for path in paths
        if any(mozpath.match(path, pattern) for pattern in patterns)
        and not any(path.startswith(f"{d}/") for d in excluded_dirs)
    )


def idl_files(paths):
    return select_source_files(paths, IDL_PATTERNS, IDL_EXCLUDED_DIRS)
