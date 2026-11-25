# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.helpers import TERM_REFERENCE, VARIABLE_REFERENCE
from fluent.migrate.transforms import COPY, REPLACE


def url_replacement(*urls):
    """Replace any of urls, or the same urls without en-US, with the $url variable."""
    return {
        variant: VARIABLE_REFERENCE("url")
        for url in urls
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
            FTL.Message(
                id=FTL.Identifier("dom-performance-observer-unsupported-entry-types"),
                value=REPLACE(
                    source,
                    "UnsupportedEntryTypesIgnored",
                    {
                        "%1$S": VARIABLE_REFERENCE("entryTypes"),
                    },
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-performance-observer-no-valid-entry-types"),
                value=COPY(source, "AllEntryTypesIgnored"),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-svg-reference-loop"),
                value=REPLACE(
                    source,
                    "SVGRefLoopWarning",
                    {
                        "%1$S": VARIABLE_REFERENCE("tagName"),
                        "%2$S": VARIABLE_REFERENCE("id"),
                    },
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-svg-reference-chain-length-exceeded"),
                value=REPLACE(
                    source,
                    "SVGRefChainLengthExceededWarning",
                    {
                        "%1$S": VARIABLE_REFERENCE("tagName"),
                        "%2$S": VARIABLE_REFERENCE("id"),
                    },
                ),
            ),
        ],
    )

    target = "dom/dom/deprecations.ftl"
    ctx.add_transforms(
        target,
        target,
        [
            FTL.Message(
                id=FTL.Identifier("dom-deprecated-capture-events"),
                value=REPLACE(
                    source,
                    "UseOfCaptureEventsWarning",
                    url_replacement(
                        "https://developer.mozilla.org/docs/Web/API/EventTarget/addEventListener",
                        "http://developer.mozilla.org/en/docs/DOM:element.addEventListener",
                    ),
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-deprecated-release-events"),
                value=REPLACE(
                    source,
                    "UseOfReleaseEventsWarning",
                    url_replacement(
                        "https://developer.mozilla.org/docs/Web/API/EventTarget/removeEventListener",
                        "http://developer.mozilla.org/en/docs/DOM:element.removeEventListener",
                    ),
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-deprecated-idb-object-store-moz-get-all"),
                value=COPY(source, "IDBObjectStoreMozGetAllWarning"),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-deprecated-mathml-mo-explicit-accent"),
                value=REPLACE(
                    source,
                    "MathML_DeprecatedMoExplicitAccentWarning",
                    {
                        "%1$S": VARIABLE_REFERENCE("attribute"),
                        "%2$S": VARIABLE_REFERENCE("element"),
                    },
                ),
            ),
        ],
    )

    target = "dom/dom/dialogs.ftl"
    ctx.add_transforms(
        target,
        target,
        [
            FTL.Message(
                id=FTL.Identifier("dom-slow-script-dialog-title"),
                value=COPY(source, "KillScriptTitle"),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-slow-script-dialog-message"),
                value=COPY(source, "KillScriptMessage"),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-slow-script-dialog-location"),
                value=REPLACE(
                    source,
                    "KillScriptLocation",
                    {
                        "%1$S": VARIABLE_REFERENCE("location"),
                    },
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-slow-script-dialog-dont-ask-again"),
                value=COPY(source, "DontAskAgain"),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-slow-script-dialog-stop-button"),
                value=COPY(source, "StopScriptButton"),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-slow-script-dialog-continue-button"),
                value=COPY(source, "WaitForScriptButton"),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-slow-addon-script-dialog-title"),
                value=COPY(source, "KillAddonScriptTitle"),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-slow-addon-script-dialog-message"),
                value=REPLACE(
                    source,
                    "KillAddonScriptMessage",
                    {
                        "%1$S": VARIABLE_REFERENCE("extensionName"),
                        "%2$S": TERM_REFERENCE("brand-short-name"),
                    },
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("dom-slow-addon-script-dialog-checkbox"),
                value=COPY(source, "KillAddonScriptGlobalMessage"),
            ),
        ],
    )
