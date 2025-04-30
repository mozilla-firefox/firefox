#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import sys

import yaml


def has_pkg_section(p, section, arch):
    has_section = section in p.keys()
    if has_section:
        for pkg in p[section]:
            if type(pkg) is str:
                yield pkg
            else:
                next_section = next(iter(pkg.keys()))
                if "on " in next_section or f"to {arch}" in next_section:
                    yield from has_pkg_section(pkg, next(iter(pkg.keys())), arch)


def iter_pkgs(part, all_pkgs, arch):
    for section in ["build-packages", "stage-packages"]:
        for pkg in has_pkg_section(part, section, arch):
            if pkg not in all_pkgs:
                if ":" in pkg and pkg.split(":")[1] != arch:
                    continue
                all_pkgs.append(pkg)


def parse(yaml_file, arch):
    all_pkgs = []
    with open(yaml_file) as inp:
        snap = yaml.safe_load(inp)
        parts = snap["parts"]
        for p in parts:
            iter_pkgs(parts[p], all_pkgs, arch)
    return " ".join(all_pkgs)


if __name__ == "__main__":
    print(parse(sys.argv[1], sys.argv[2]))
