# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import re

import fluent.syntax.ast as FTL
from fluent.migrate.transforms import TransformPattern


class STRIP_TRAILING_STOP(TransformPattern):
    """Drop a trailing full stop (Latin, ideographic or danda) or exclamation mark from each pattern."""

    def visit_Pattern(self, node):
        node = super().visit_Pattern(node)
        if node.elements and isinstance(node.elements[-1], FTL.TextElement):
            last = node.elements[-1]
            last.value = re.sub(r"[.。।!]$", "", last.value)
        return node


def migrate(ctx):
    """Bug 2093755 - Remove trailing period from the HNT mobile card button label, part {index}."""

    path = "browser/browser/newtab/onboarding.ftl"
    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("hnt-card-mobile-primary-button2"),
                value=STRIP_TRAILING_STOP(path, "hnt-card-mobile-primary-button"),
            ),
        ],
    )
