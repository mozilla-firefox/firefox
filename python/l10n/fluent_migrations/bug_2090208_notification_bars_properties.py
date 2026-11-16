# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate import COPY, REPLACE
from fluent.migrate.helpers import VARIABLE_REFERENCE


def migrate(ctx):
    """Bug 2090208 - Migrate Safe Browsing, captive portal and keyword fixup notification bar strings from browser.properties to Fluent, part {index}."""

    source = "browser/chrome/browser/browser.properties"
    target = "browser/browser/browser.ftl"

    def message(id, key):
        return FTL.Message(
            id=FTL.Identifier(id),
            value=COPY(source, key),
        )

    def button(id, label_key, accesskey_key):
        return FTL.Message(
            id=FTL.Identifier(id),
            attributes=[
                FTL.Attribute(
                    id=FTL.Identifier("label"),
                    value=COPY(source, label_key),
                ),
                FTL.Attribute(
                    id=FTL.Identifier("accesskey"),
                    value=COPY(source, accesskey_key),
                ),
            ],
        )

    host = {"%1$S": VARIABLE_REFERENCE("hostName")}

    ctx.add_transforms(
        target,
        target,
        [
            message(
                "safebrowsing-deceptive-site-message",
                "safebrowsing.deceptiveSite",
            ),
            message(
                "safebrowsing-reported-attack-site-message",
                "safebrowsing.reportedAttackSite",
            ),
            message(
                "safebrowsing-reported-unwanted-site-message",
                "safebrowsing.reportedUnwantedSite",
            ),
            message(
                "safebrowsing-reported-harmful-site-message",
                "safebrowsing.reportedHarmfulSite",
            ),
            button(
                "safebrowsing-get-me-out-of-here-button",
                "safebrowsing.getMeOutOfHereButton.label",
                "safebrowsing.getMeOutOfHereButton.accessKey",
            ),
            button(
                "safebrowsing-not-a-deceptive-site-button",
                "safebrowsing.notADeceptiveSiteButton.label",
                "safebrowsing.notADeceptiveSiteButton.accessKey",
            ),
            button(
                "safebrowsing-not-an-attack-site-button",
                "safebrowsing.notAnAttackButton.label",
                "safebrowsing.notAnAttackButton.accessKey",
            ),
            message(
                "captive-portal-info-message",
                "captivePortal.infoMessage3",
            ),
            FTL.Message(
                id=FTL.Identifier("captive-portal-show-login-page-button"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY(source, "captivePortal.showLoginPage2"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("keyword-uri-fixup-message"),
                value=REPLACE(source, "keywordURIFixup.message", host),
            ),
            FTL.Message(
                id=FTL.Identifier("keyword-uri-fixup-go-to-button"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=REPLACE(source, "keywordURIFixup.goTo", host),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=COPY(source, "keywordURIFixup.goTo.accesskey"),
                    ),
                ],
            ),
        ],
    )
