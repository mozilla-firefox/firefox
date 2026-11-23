# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.helpers import VARIABLE_REFERENCE
from fluent.migrate.transforms import COPY, REPLACE


def addon_details():
    return FTL.Pattern([
        FTL.TextElement('<img data-l10n-name="addon-icon"/> '),
        FTL.Placeable(VARIABLE_REFERENCE("addonName")),
    ])


def migrate(ctx):
    """Bug 2093214 - Migrate extensions.properties to Fluent, part {index}."""

    source = "toolkit/chrome/global/extensions.properties"

    target = "toolkit/toolkit/global/extensions.ftl"
    ctx.add_transforms(
        target,
        target,
        [
            FTL.Message(
                id=FTL.Identifier("webext-uninstall-confirmation-title"),
                value=REPLACE(
                    source,
                    "uninstall.confirmation.title",
                    {"%1$S": VARIABLE_REFERENCE("extension")},
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("webext-uninstall-confirmation-message"),
                value=REPLACE(
                    source,
                    "uninstall.confirmation.message",
                    {"%1$S": VARIABLE_REFERENCE("extension")},
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("webext-uninstall-confirmation-button-uninstall"),
                value=COPY(source, "uninstall.confirmation.button-0.label"),
            ),
            FTL.Message(
                id=FTL.Identifier("webext-uninstall-confirmation-button-keep"),
                value=COPY(source, "uninstall.confirmation.button-1.label"),
            ),
            FTL.Message(
                id=FTL.Identifier("webext-save-as-pdf-dialog-title"),
                value=COPY(source, "saveaspdf.saveasdialog.title"),
            ),
        ],
    )

    target = "browser/browser/appMenuNotifications.ftl"
    ctx.add_transforms(
        target,
        target,
        [
            FTL.Message(
                id=FTL.Identifier("appmenu-new-tab-controlled-message"),
                value=REPLACE(
                    source,
                    "newTabControlled.message2",
                    {"%1$S": addon_details()},
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("appmenu-homepage-controlled-message"),
                value=REPLACE(
                    source,
                    "homepageControlled.message",
                    {"%1$S": addon_details()},
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("appmenu-tab-hide-controlled-message"),
                value=REPLACE(
                    source,
                    "tabHideControlled.message",
                    {
                        "%1$S": addon_details(),
                        "%2$S": FTL.TextElement(
                            '<img data-l10n-name="all-tabs-icon"/>'
                        ),
                    },
                ),
            ),
        ],
    )
