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
import re
from typing import Optional

from mozbuild.jar import DeprecatedJarManifest, JarManifestParser
from mozbuild.preprocessor import Preprocessor

AB_CD_TOKEN = "@AB_CD@"


class JarManifestError(Exception):
    """A jar manifest declares something the model cannot represent."""


@dataclasses.dataclass(frozen=True)
class LocaleTemplate:
    """A string holding the `@AB_CD@` placeholder until a locale resolves it.

    A jar manifest names most of its paths once for every locale, so the model
    keeps the placeholder and each reader resolves it for the locale it is
    building.
    """

    text: str

    def resolve(self, ab_cd: str) -> str:
        return self.text.replace(AB_CD_TOKEN, ab_cd)


@dataclasses.dataclass(frozen=True)
class JarEntry:
    """One `destination (source)` line."""

    source: LocaleTemplate
    output: LocaleTemplate
    preprocess: bool
    is_locale: bool

    def to_dict(self):
        """The entry as the l10n manifest carries it."""
        return {
            "source": self.source.text,
            "output": self.output.text,
            "preprocess": self.preprocess,
            "is_locale": self.is_locale,
        }

    @classmethod
    def from_dict(cls, raw):
        return cls(
            source=LocaleTemplate(raw["source"]),
            output=LocaleTemplate(raw["output"]),
            preprocess=raw["preprocess"],
            is_locale=raw["is_locale"],
        )


@dataclasses.dataclass(frozen=True)
class JarSection:
    """One `name.jar:` section and the entries below it."""

    name: LocaleTemplate
    base: str
    relativesrcdir: Optional[str]
    entries: tuple[JarEntry, ...]
    chrome_manifests: tuple[LocaleTemplate, ...]

    def to_dict(self):
        """The section as the l10n manifest carries it."""
        return {
            "name": self.name.text,
            "base": self.base,
            "relativesrcdir": self.relativesrcdir,
            "chrome_manifests": [m.text for m in self.chrome_manifests],
            "entries": [entry.to_dict() for entry in self.entries],
        }

    @classmethod
    def from_dict(cls, raw):
        return cls(
            name=LocaleTemplate(raw["name"]),
            base=raw["base"],
            relativesrcdir=raw["relativesrcdir"],
            entries=tuple(JarEntry.from_dict(e) for e in raw["entries"]),
            chrome_manifests=tuple(LocaleTemplate(m) for m in raw["chrome_manifests"]),
        )


def _reject_locale_placeholder(what, text, path):
    """A base or a relativesrcdir is read as written, so it may not hold the
    locale placeholder. Only a section name, an entry and a chrome manifest
    line are resolved for a locale.
    """
    if text and AB_CD_TOKEN in text:
        raise JarManifestError(
            f"{path}: {what} {text} names {AB_CD_TOKEN}, which is only resolved "
            f"in a section name, an entry or a chrome manifest line"
        )


def jar_manifest_defines(context):
    """The defines a jar manifest is preprocessed with.

    A configured define takes precedence over one that the declaring
    `moz.build` sets. Bug 2080035 is about deciding whether the `moz.build`
    define should take precedence instead.
    """
    defines = {**(context.get("DEFINES") or {}), **context.config.defines}
    return {name: value for name, value in defines.items() if value is not False}


_LOCALE_CONDITION = re.compile(
    r"^\s*#\s*(if|elif|ifdef|ifndef|elifdef|elifndef)\b.*\bAB_CD\b"
)


def _reject_locale_condition(paths):
    """A preprocessor condition on the locale is refused.

    The manifest is parsed once for every locale, so the condition cannot be
    decided per locale. Evaluating it against the placeholder would pick one
    branch for all of them.
    """
    for read in sorted(paths):
        with open(read, encoding="utf-8") as fh:
            for number, line in enumerate(fh, 1):
                if _LOCALE_CONDITION.match(line):
                    raise JarManifestError(
                        f"{read}:{number}: a preprocessor condition names AB_CD, "
                        f"which a jar manifest leaves open for every locale"
                    )


def parse_jar_manifest(path, defines):
    """Read a jar manifest and return its sections with every file that was read.

    The caller records the read paths so that editing the manifest or any of
    its includes regenerates whatever consumed the sections.
    """
    pp = Preprocessor()
    pp.context.update(defines)
    # The preprocessor fails on an unknown variable, so AB_CD is defined as
    # its own placeholder and comes out of substitution unchanged.
    pp.context.update(AB_CD=AB_CD_TOKEN)
    pp.out = JarManifestParser()
    try:
        pp.do_include(path)
    except DeprecatedJarManifest as e:
        raise DeprecatedJarManifest(
            f"Parsing error while processing {path}: {e}"
        ) from e

    _reject_locale_condition(pp.includes)
    for jarinfo in pp.out:
        _reject_locale_placeholder("base", jarinfo.base, path)
        _reject_locale_placeholder("relativesrcdir", jarinfo.relativesrcdir, path)

    sections = tuple(
        JarSection(
            name=LocaleTemplate(jarinfo.name),
            base=jarinfo.base,
            relativesrcdir=jarinfo.relativesrcdir,
            entries=tuple(
                JarEntry(
                    source=LocaleTemplate(entry.source),
                    output=LocaleTemplate(entry.output),
                    preprocess=entry.preprocess,
                    is_locale=entry.is_locale,
                )
                for entry in jarinfo.entries
            ),
            chrome_manifests=tuple(
                LocaleTemplate(line) for line in jarinfo.chrome_manifests
            ),
        )
        for jarinfo in pp.out
    )

    return sections, pp.includes
