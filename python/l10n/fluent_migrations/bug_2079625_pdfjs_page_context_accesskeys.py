# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.transforms import COPY_PATTERN, FluentSource, Transform

# Locales that use the en-US accesskey letters across Firefox rather than
# localizing them. For these, the migration sets the en-US accesskey; for every
# other locale, ACCESSKEY_FROM_LABEL picks a letter from the translated label.
EN_ACCESSKEY_LOCALES = {
    "as",
    "bn",
    "bn-BD",
    "bn-IN",
    "bo",
    "bqi",
    "brx",
    "en-CA",
    "en-GB",
    "en-US",
    "en-ZA",
    "gn",
    "gu-IN",
    "hi-IN",
    "ja",
    "ja-JP-mac",
    "km",
    "kn",
    "ko",
    "ks",
    "lb",
    "lo",
    "mai",
    "mr",
    "my",
    "ne-NP",
    "or",
    "pa-IN",
    "sat",
    "sco",
    "si",
    "skr",
    "ta",
    "ta-LK",
    "te",
    "tl",
    "trs",
    "ur",
    "vi",
    "zh-CN",
    "zh-TW",
}
DESCENDERS = "gjpqy"
NARROW = "ilI"


class ACCESSKEY_FROM_LABEL(FluentSource):
    """Pick an accesskey from the text of a Fluent label, preferring
    characters that are neither narrow nor have descenders.

    The English accesskey is read from `reference_key` in the reference FTL
    file at `reference_path`.
    """

    def __init__(self, path, key, reference_path, reference_key):
        super().__init__(path, key)
        self.reference_path = reference_path
        self.reference_key = reference_key

    def english_accesskey(self, ctx):
        msg_key, _, attr_key = self.reference_key.partition(".")
        for entry in ctx.reference_resources[self.reference_path].body:
            if isinstance(entry, FTL.Message) and entry.id.name == msg_key:
                for attribute in entry.attributes:
                    if attribute.id.name == attr_key:
                        return attribute.value.clone()
        raise KeyError(f"{self.reference_key} not found in {self.reference_path}")

    def __call__(self, ctx):
        if ctx.locale in EN_ACCESSKEY_LOCALES:
            return self.english_accesskey(ctx)
        pattern = super().__call__(ctx)
        label = "".join(
            element.value
            for element in pattern.elements
            if isinstance(element, FTL.TextElement)
        )
        candidates = [c for c in label if c.isalpha()]
        for allowed in (
            lambda c: c not in NARROW and c not in DESCENDERS,
            lambda c: c not in DESCENDERS,
            lambda c: True,
        ):
            for c in candidates:
                if allowed(c):
                    return Transform.pattern_of(FTL.TextElement(c))
        return self.english_accesskey(ctx)


def migrate(ctx):
    """Bug 2079625 - Give the PDF viewer's page items in the content context menu accesskeys, part {index}."""

    path = "browser/browser/browserContext.ftl"

    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("main-context-menu-pdfjs-copy-page2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(
                            path, "main-context-menu-pdfjs-copy-page.label"
                        ),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=ACCESSKEY_FROM_LABEL(
                            path,
                            "main-context-menu-pdfjs-copy-page.label",
                            path,
                            "main-context-menu-pdfjs-copy-page2.accesskey",
                        ),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("main-context-menu-pdfjs-cut-page2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(
                            path, "main-context-menu-pdfjs-cut-page.label"
                        ),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=ACCESSKEY_FROM_LABEL(
                            path,
                            "main-context-menu-pdfjs-cut-page.label",
                            path,
                            "main-context-menu-pdfjs-cut-page2.accesskey",
                        ),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("main-context-menu-pdfjs-delete-page2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(
                            path, "main-context-menu-pdfjs-delete-page.label"
                        ),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=ACCESSKEY_FROM_LABEL(
                            path,
                            "main-context-menu-pdfjs-delete-page.label",
                            path,
                            "main-context-menu-pdfjs-delete-page2.accesskey",
                        ),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("main-context-menu-pdfjs-save-page2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(
                            path, "main-context-menu-pdfjs-save-page.label"
                        ),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=ACCESSKEY_FROM_LABEL(
                            path,
                            "main-context-menu-pdfjs-save-page.label",
                            path,
                            "main-context-menu-pdfjs-save-page2.accesskey",
                        ),
                    ),
                ],
            ),
        ],
    )
