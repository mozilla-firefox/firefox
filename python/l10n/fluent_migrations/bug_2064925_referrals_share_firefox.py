# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.transforms import COPY_PATTERN, TransformPattern


class SWAP_BRAND_TERM(TransformPattern):
    """Reuse the existing translation, rewriting the brand term reference to
    { -brand-product-name } so the string reads "Firefox" on every channel."""

    def visit_Placeable(self, node):
        if isinstance(node.expression, FTL.TermReference) and node.expression.id.name in (
            "brand-shorter-name",
            "brand-short-name",
        ):
            node.expression.id = FTL.Identifier("brand-product-name")
        return super().visit_Placeable(node)


def migrate(ctx):
    """Bug 2064925 - Referral entry points say "Share Firefox" on all channels, part {index}."""

    menubar = "browser/browser/menubar.ftl"
    ctx.add_transforms(
        menubar,
        menubar,
        [
            FTL.Message(
                id=FTL.Identifier("menu-application-referrals2"),
                attributes=[
                    FTL.Attribute(
                        FTL.Identifier("label"),
                        SWAP_BRAND_TERM(menubar, "menu-application-referrals.label"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("menu-referrals2"),
                attributes=[
                    FTL.Attribute(
                        FTL.Identifier("label"),
                        SWAP_BRAND_TERM(menubar, "menu-referrals.label"),
                    ),
                ],
            ),
        ],
    )

    appmenu = "browser/browser/appmenu.ftl"
    ctx.add_transforms(
        appmenu,
        appmenu,
        [
            FTL.Message(
                id=FTL.Identifier("appmenu-referrals2"),
                attributes=[
                    FTL.Attribute(
                        FTL.Identifier("label"),
                        SWAP_BRAND_TERM(appmenu, "appmenu-referrals.label"),
                    ),
                    FTL.Attribute(
                        FTL.Identifier("accesskey"),
                        COPY_PATTERN(appmenu, "appmenu-referrals.accesskey"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("appmenuitem-share-firefox-title2"),
                value=SWAP_BRAND_TERM(appmenu, "appmenuitem-share-firefox-title"),
            ),
        ],
    )

    preferences = "browser/browser/preferences/preferences.ftl"
    ctx.add_transforms(
        preferences,
        preferences,
        [
            FTL.Message(
                id=FTL.Identifier("referrals-section-header2"),
                attributes=[
                    FTL.Attribute(
                        FTL.Identifier("label"),
                        SWAP_BRAND_TERM(preferences, "referrals-section-header.label"),
                    ),
                    FTL.Attribute(
                        FTL.Identifier("description"),
                        COPY_PATTERN(preferences, "referrals-section-header.description"),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("referrals-link2"),
                attributes=[
                    FTL.Attribute(
                        FTL.Identifier("label"),
                        SWAP_BRAND_TERM(preferences, "referrals-link.label"),
                    ),
                ],
            ),
        ],
    )

    about_dialog = "browser/browser/aboutDialog.ftl"
    ctx.add_transforms(
        about_dialog,
        about_dialog,
        [
            FTL.Message(
                id=FTL.Identifier("helpus-referrals2"),
                value=SWAP_BRAND_TERM(about_dialog, "helpus-referrals"),
            ),
        ],
    )
