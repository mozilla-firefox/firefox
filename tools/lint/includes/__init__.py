# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import difflib
import os
import re

import yaml
from diskarzhan import diskarzhan
from mozlint import result
from mozlint.pathutils import expand_exclusions

here = os.path.dirname(__file__)
with open(os.path.join(here, "..", "..", "..", "mfbt", "api.yml")) as fd:
    description = yaml.safe_load(fd)

supported_keys = "variables", "functions", "macros", "types", "literals"
for categories in description.values():
    assert set(categories.keys()).issubset(supported_keys)


def generate_diff(path, raw_content, line_to_delete):
    prev_content = raw_content.split("\n")
    new_content = [
        raw_line
        for lineno, raw_line in enumerate(prev_content, start=1)
        if lineno != line_to_delete
    ]
    diff = "\n".join(
        difflib.unified_diff(prev_content, new_content, fromfile=path, tofile=path)
    )
    return diff


MFBT_INCLUDE_RE = re.compile(r'#\s*include\s*"mozilla/([^"\n]+)"')
mfbt_api = {
    header: frozenset(
        symbol
        for category, symbols in categories.items()
        if category != "literals"
        for symbol in symbols
    )
    for header, categories in description.items()
}
mfbt_literals = {
    header: categories["literals"]
    for header, categories in description.items()
    if "literals" in categories
}


def check_mfbt_headers(path, raw_content, config):
    """Return list of (lineno, msg) for unused mfbt headers."""
    api = mfbt_api
    if "mfbt/" in path:
        api = {
            header: symbols
            for header, symbols in mfbt_api.items()
            if not path.endswith(f"mfbt/{header}")
            and not path.endswith(f"mfbt/{header[:-2]}.cpp")
        }
    return diskarzhan.lint_headers(
        path, raw_content, api, include_re=MFBT_INCLUDE_RE, literals=mfbt_literals
    )


def lint(paths, config, **lintargs):
    results = {"results": [], "fixed": 0}
    paths = list(expand_exclusions(paths, config, lintargs["root"]))
    fix = lintargs.get("fix")

    for path in paths:
        try:
            with open(path) as fd:
                raw_content = fd.read()
        except UnicodeDecodeError:
            continue

        mfbt_results = check_mfbt_headers(path, raw_content, config)
        diskarzhan_results = diskarzhan.lint_std_headers(path, raw_content)
        diskarzhan_results += diskarzhan.lint_cstd_headers(path, raw_content)

        all_results = mfbt_results + diskarzhan_results

        if fix:
            if all_results:
                diskarzhan.fix_includes(path, raw_content, all_results)
                results["fixed"] += len(all_results)
        else:
            for lineno, msg in all_results:
                results["results"].append(
                    result.from_config(
                        config,
                        path=path,
                        message=msg,
                        level="error",
                        lineno=lineno,
                        diff=generate_diff(path, raw_content, lineno),
                    )
                )

    return results
