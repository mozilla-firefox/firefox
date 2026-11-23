# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.transforms import TransformPattern


class WRAP_NAME(TransformPattern):
    """Wrap the { $name } placeable in <span data-l10n-name="name"></span>,
    keeping the rest of each translation."""

    def visit_Placeable(self, node):
        if (
            isinstance(node.expression, FTL.VariableReference)
            and node.expression.id.name == "name"
        ):
            return FTL.Pattern(
                elements=[
                    FTL.TextElement('<span data-l10n-name="name">'),
                    FTL.Placeable(
                        expression=FTL.VariableReference(id=FTL.Identifier("name"))
                    ),
                    FTL.TextElement("</span>"),
                ]
            )
        return super().visit_Placeable(node)


def migrate(ctx):
    """Bug 2094063 - Truncate long cardholder names in the payment method doorhanger, part {index}."""

    path = "toolkit/toolkit/formautofill/formAutofill.ftl"
    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier(f"{prefix}-2"),
                value=WRAP_NAME(path, prefix),
            )
            for prefix in (
                "credit-card-doorhanger-card-details-name",
                "credit-card-doorhanger-card-details-name-expiration",
                "credit-card-doorhanger-card-details-name-security-code",
                "credit-card-doorhanger-card-details-name-expiration-security-code",
            )
        ],
    )
