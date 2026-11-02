# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Parsing of jar manifests into a model no backend owns.

A jar manifest names the same installs `moz.build` already expresses. Parsing
it produces this model, and a separate step turns the model into build objects,
so every reader gets one description of what a jar manifest declares.
"""

from __future__ import annotations

import dataclasses
from typing import Optional

from mozbuild.jar import DeprecatedJarManifest, JarManifestParser
from mozbuild.preprocessor import Preprocessor


@dataclasses.dataclass(frozen=True)
class JarEntry:
    """One `destination (source)` line."""

    source: str
    output: str
    preprocess: bool
    is_locale: bool


@dataclasses.dataclass(frozen=True)
class JarSection:
    """One `name.jar:` section and the entries below it."""

    name: str
    base: str
    relativesrcdir: Optional[str]
    entries: tuple[JarEntry, ...]
    chrome_manifests: tuple[str, ...]


def jar_manifest_defines(context):
    """The defines a jar manifest is preprocessed with.

    A configured define takes precedence over one that the declaring
    `moz.build` sets. Bug 2080035 is about deciding whether the `moz.build`
    define should take precedence instead.
    """
    defines = {**(context.get("DEFINES") or {}), **context.config.defines}
    return {name: value for name, value in defines.items() if value is not False}


def parse_jar_manifest(path, defines, ab_cd):
    """Read a jar manifest and return its sections with every file that was read.

    The caller records the read paths so that editing the manifest or any of
    its includes regenerates whatever consumed the sections.
    """
    pp = Preprocessor()
    pp.context.update(defines)
    pp.context.update(AB_CD=ab_cd)
    pp.out = JarManifestParser()
    try:
        pp.do_include(path)
    except DeprecatedJarManifest as e:
        raise DeprecatedJarManifest(
            f"Parsing error while processing {path}: {e}"
        ) from e

    sections = tuple(
        JarSection(
            name=jarinfo.name,
            base=jarinfo.base,
            relativesrcdir=jarinfo.relativesrcdir,
            entries=tuple(
                JarEntry(
                    source=entry.source,
                    output=entry.output,
                    preprocess=entry.preprocess,
                    is_locale=entry.is_locale,
                )
                for entry in jarinfo.entries
            ),
            chrome_manifests=tuple(jarinfo.chrome_manifests),
        )
        for jarinfo in pp.out
    )

    return sections, pp.includes
