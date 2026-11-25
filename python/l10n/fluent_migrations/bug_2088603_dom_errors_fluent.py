# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.helpers import VARIABLE_REFERENCE
from fluent.migrate.transforms import COPY, REPLACE


def url_replacement(url):
    """Replace url, or the same url without en-US, with the $url variable."""
    return {
        variant: VARIABLE_REFERENCE("url")
        for variant in dict.fromkeys([url, url.replace("/en-US/", "/")])
    }


def migrate(ctx):
    """Bug 2088603 - Migrate DOM console messages from dom.properties to Fluent, part {index}."""

    source = "dom/chrome/dom/dom.properties"
    target = "dom/dom/dom.ftl"
    ctx.add_transforms(
        target,
        target,
        [
            FTL.Message(
                id=FTL.Identifier("dom-get-element-by-id-empty-param"),
                value=COPY(source, "EmptyGetElementByIdParam"),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-document-write-ignored"),
                value=COPY(source, "DocumentWriteIgnored"),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-parser-speculation-failed"),
                value=REPLACE(
                    source,
                    "SpeculationFailed2",
                    url_replacement(
                        "https://developer.mozilla.org/en-US/docs/Glossary/speculative_parsing"
                    ),
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-edit-context-character-bounds-warning"),
                value=REPLACE(
                    source,
                    "EditContextCharacterBoundsWarning",
                    url_replacement(
                        "https://developer.mozilla.org/docs/Web/API/EditContext/updateCharacterBounds"
                    ),
                ),
            ),
        ],
    )
