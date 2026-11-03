# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os.path
import re
import sys
import yaml

with open(os.path.join(os.path.dirname(__file__), "diskarzhan.yaml")) as fd:
    _database = yaml.safe_load(fd)
    cxx_api = _database["stdcxx"]
    c_api = _database["stdc"]

_include_re = re.compile(r"#\s*include\s*<([^>\n]+)>")


def _find_includes(raw_content, include_re=_include_re):
    includes = {}
    for match in include_re.finditer(raw_content):
        includes.setdefault(match.group(1), match)
    return includes


def lint_headers(path, raw_content, api, *, include_re, literals=None):
    includes = _find_includes(raw_content, include_re)
    included = [(header, includes[header]) for header in api if header in includes]
    if not included:
        return []

    content = include_re.sub("", raw_content)
    file_symbols = set(re.findall(r"\w+", content))
    literals = literals or {}

    changes = []
    for header, match in included:
        if not file_symbols.isdisjoint(api[header]):
            continue
        if any(
            re.search(rf"""[0-9."']{suffix}\b""", content)
            for suffix in literals.get(header, ())
        ):
            continue

        msg = f"{path} includes {header} but does not reference any of its API"
        lineno = 1 + raw_content.count("\n", 0, match.start())
        changes.append((lineno, msg))

    return changes


def fix_includes(path, raw_content, changes):
    lines_to_delete = {lineno for lineno, _ in changes}
    prev_content = raw_content.split("\n")
    new_content = [
        raw_line
        for lineno, raw_line in enumerate(prev_content, start=1)
        if lineno not in lines_to_delete
    ]
    with open(path, "w", newline="\n") as outfd:
        outfd.write("\n".join(new_content))


def lint_std_headers(path, raw_content):
    includes = _find_includes(raw_content)
    included = [(header, includes[header]) for header in cxx_api if header in includes]
    if not included:
        return []

    # If there a:
    #   namespace std {...}
    # or a:
    #   using namespace std
    # or even:
    #   namespace std = ...
    # then we just look for symbol tokens.
    # Otherwise we look for the very common std::{token} pattern,
    # which avoids same false negative.
    if re.search(r"\bnamespace\s+std\b", raw_content):
        symbol_pattern = r"\b({})\b"
    else:
        symbol_pattern = r"\bstd::({})\b"

    file_symbols = set(
        re.findall(symbol_pattern.format(r"[a-zA-Z_][a-zA-Z_0-9]*"), raw_content)
    )

    changes = []
    for header, match in included:
        if not file_symbols.isdisjoint(cxx_api[header]):
            continue

        msg = f"{path} includes <{header}> but does not reference any of its API"
        lineno = 1 + raw_content.count("\n", 0, match.start())
        changes.append((lineno, msg))

    return changes


def lint_cstd_headers(path, raw_content):
    includes = _find_includes(raw_content)
    included = []
    for header in c_api:
        matches = [
            match
            for match in (includes.get(header), includes.get(f"c{header[:-2]}"))
            if match
        ]
        if matches:
            included.append((header, min(matches, key=lambda match: match.start())))
    if not included:
        return []

    symbol_pattern = r"\b(?:(?:std)?::)?({})\b"

    file_symbols = set(
        re.findall(symbol_pattern.format(r"[a-zA-Z_][a-zA-Z_0-9]*"), raw_content)
    )

    changes = []
    for header, match in included:
        if not file_symbols.isdisjoint(c_api[header]):
            continue

        msg = (
            f"{path} includes <{match.group(1)}> but does not reference any of its API"
        )
        lineno = 1 + raw_content.count("\n", 0, match.start())
        changes.append((lineno, msg))

    return changes


def lint(paths, *, fix=False):

    change_count = 0
    for path in paths:
        try:
            with open(path) as fd:
                raw_content = fd.read()
        except UnicodeDecodeError:
            continue

        changes = lint_std_headers(path, raw_content)
        changes += lint_cstd_headers(path, raw_content)

        if fix:
            if changes:
                fix_includes(path, raw_content, changes)
        else:
            for lineno, msg in changes:
                print(f"{path}:{lineno}: {msg}")

        change_count += len(changes)

    return change_count


def run():
    import argparse

    parser = argparse.ArgumentParser(description="cleanup standard includes")
    parser.add_argument("sources", nargs="+", type=str, help="files to cleanup")
    parser.add_argument(
        "--fix", action="store_true", help="apply modification in-place"
    )
    args = parser.parse_args()
    changed = lint(args.sources, fix=args.fix)
    print(
        f"{'Applied' if args.fix else 'Found'} {changed} change{'s' if changed else ''}"
    )
    if not args.fix and changed:
        sys.exit(1)
