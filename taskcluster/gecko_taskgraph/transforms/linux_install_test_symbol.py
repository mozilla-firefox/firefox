# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from taskgraph.transforms.base import TransformSequence

_CHANNEL_ABBREV = {
    "release": "rel",
    "beta": "beta",
    "devedition": "deved",
    "nightly": "nightly",
    "esr": "esr",
}

_METHOD_GROUP = {
    "apt": "inst-apt",
    "rpm": "inst-rpm",
    "flatpak": "inst-flatpak",
    "tarball": "inst-tarball",
    "snap": "inst-snap",
}

transforms = TransformSequence()


def _get_channel_abbrev(channel):
    abbrev = _CHANNEL_ABBREV.get(channel)
    if abbrev is None:
        raise ValueError(f"Unknown channel {channel!r}: add it to _CHANNEL_ABBREV")
    return abbrev


def _get_arch_abbrev(platform):
    return "a64" if "aarch64" in platform else "x64"


def _get_method_group(install_method):
    group = _METHOD_GROUP.get(install_method)
    if group is None:
        raise ValueError(
            f"Unknown install method {install_method!r}: add it to _METHOD_GROUP"
        )
    return group


def _extract_matrix_values(task):
    # taskgraph.transforms.matrix stores resolved values at
    # task["attributes"]["matrix"] after expansion (see matrix.py:_resolve_matrix).
    label = task.get("label", task.get("name", "?"))
    matrix = task.get("attributes", {}).get("matrix", {})
    channel = matrix.get("channel")
    if channel is None:
        raise ValueError(f"Task '{label}' has distro-symbol but no matrix channel")
    locale = matrix.get("locale")
    if locale is None:
        raise ValueError(f"Task '{label}' has distro-symbol but no matrix locale")
    return channel, locale


@transforms.add
def set_treeherder_symbol(config, tasks):
    for task in tasks:
        attrs = task.get("attributes", {})
        distro_symbol = attrs.pop("distro-symbol", None)
        install_method = attrs.pop("install-method", None)
        if distro_symbol is None:
            yield task
            continue

        channel, locale = _extract_matrix_values(task)
        channel_abbrev = _get_channel_abbrev(channel)
        locale_abbrev = locale.split("-")[0]
        arch_abbrev = _get_arch_abbrev(task.get("treeherder", {}).get("platform", ""))
        method_group = _get_method_group(install_method)

        task.setdefault("treeherder", {})["symbol"] = (
            f"{method_group}({distro_symbol}-{arch_abbrev}-{channel_abbrev}-{locale_abbrev})"
        )
        yield task
