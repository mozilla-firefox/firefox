# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import mozpack.path as mozpath
from mozversioncontrol import get_repository_object

CONF_EXCLUDED_DIRS = (
    "mobile",
    "testing/web-platform",
    "third_party",
    "tools/ts/test",
    "widget/android",
    "widget/uikit",
)

CONF_PATTERNS = ("**/components.conf",)

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

JS_EXCLUDED_DIRS = ("testing/web-platform",)

JS_PATTERNS = (
    "**/*.html",
    "**/*.js",
    "**/*.mjs",
    "**/*.xhtml",
)

WEBIDL_PATTERNS = (
    "dom/chrome-webidl/*.webidl",
    "dom/webidl/*.webidl",
)


def listed_files(topsrcdir):
    return get_repository_object(topsrcdir).get_tracked_files(
        CONF_PATTERNS + IDL_PATTERNS + JS_PATTERNS + WEBIDL_PATTERNS
    )


def select_source_files(paths, patterns, excluded_dirs):
    """Return the sorted ``paths`` that match one of the ``patterns`` and that
    are not under one of the ``excluded_dirs``."""
    return sorted(
        path
        for path in paths
        if any(mozpath.match(path, pattern) for pattern in patterns)
        and not any(path.startswith(f"{d}/") for d in excluded_dirs)
    )


def conf_files(paths):
    return select_source_files(paths, CONF_PATTERNS, CONF_EXCLUDED_DIRS)


def idl_files(paths):
    return select_source_files(paths, IDL_PATTERNS, IDL_EXCLUDED_DIRS)


def js_files(paths):
    return select_source_files(paths, JS_PATTERNS, JS_EXCLUDED_DIRS)


def webidl_files(paths):
    return select_source_files(paths, WEBIDL_PATTERNS, ())
