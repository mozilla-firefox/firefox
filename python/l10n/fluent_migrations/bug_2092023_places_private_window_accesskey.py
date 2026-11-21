# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.transforms import COPY_PATTERN, FluentSource

# Locales that use the en-US accesskey letters across Firefox rather than
# localizing them. For these, the migration sets the en-US accesskey; for every
# other locale, it keeps the locale's current accesskey.
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


class EN_OR_LOCALE_ACCESSKEY(FluentSource):
    """Pick the en-US accesskey for locales that use the English accesskey,
    otherwise copy the existing accesskey currently used by the locale.
    """

    def __init__(self, path, key, reference_path, reference_key):
        super().__init__(path, key)
        self.reference_path = reference_path
        self.reference_key = reference_key

    def __call__(self, ctx):
        if ctx.locale not in EN_ACCESSKEY_LOCALES:
            return super().__call__(ctx)
        msg_key, _, attr_key = self.reference_key.partition(".")
        for entry in ctx.reference_resources[self.reference_path].body:
            if isinstance(entry, FTL.Message) and entry.id.name == msg_key:
                for attribute in entry.attributes:
                    if attribute.id.name == attr_key:
                        return attribute.value.clone()
        raise KeyError(f"{self.reference_key} not found in {self.reference_path}")


def migrate(ctx):
    """Bug 2092023 - Restore the Places context menu's Open in New Private Window accesskey, part {index}."""

    path = "browser/browser/places.ftl"

    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("places-open-in-private-window2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(path, "places-open-in-private-window.label"),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=EN_OR_LOCALE_ACCESSKEY(
                            path,
                            "places-open-in-private-window.accesskey",
                            path,
                            "places-open-in-private-window2.accesskey",
                        ),
                    ),
                ],
            ),
        ],
    )
