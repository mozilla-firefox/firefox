# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate import REPLACE
from fluent.migrate.helpers import VARIABLE_REFERENCE


def migrate(ctx):
    """Bug 1667795 - Migrate autocomplete.properties to Fluent, part {index}."""

    source = "toolkit/chrome/global/autocomplete.properties"
    target = "browser/browser/browser.ftl"

    ctx.add_transforms(
        target,
        target,
        [
            FTL.Message(
                id=FTL.Identifier("urlbar-result-bookmark-keyword-search"),
                value=REPLACE(
                    source,
                    "bookmarkKeywordSearch",
                    {
                        "%1$S": VARIABLE_REFERENCE("domain"),
                        "%2$S": VARIABLE_REFERENCE("searchTerms"),
                    },
                ),
            ),
        ],
    )
