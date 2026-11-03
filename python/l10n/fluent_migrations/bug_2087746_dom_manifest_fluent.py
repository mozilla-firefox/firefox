# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate import REPLACE
from fluent.migrate.helpers import VARIABLE_REFERENCE, transforms_from


def migrate(ctx):
    """Bug 2087746 - Migrate Web App Manifest warnings from dom.properties to Fluent, part {index}."""

    source = "dom/chrome/dom/dom.properties"
    target = "dom/dom/manifest.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
dom-manifest-id-is-invalid = {COPY(from_path, "ManifestIdIsInvalid")}
dom-manifest-id-not-same-origin = {COPY(from_path, "ManifestIdNotSameOrigin")}
dom-manifest-should-be-object = {COPY(from_path, "ManifestShouldBeObject")}
dom-manifest-scope-url-invalid = {COPY(from_path, "ManifestScopeURLInvalid")}
dom-manifest-scope-not-same-origin = {COPY(from_path, "ManifestScopeNotSameOrigin")}
dom-manifest-start-url-outside-scope = {COPY(from_path, "ManifestStartURLOutsideScope")}
dom-manifest-start-url-invalid = {COPY(from_path, "ManifestStartURLInvalid")}
dom-manifest-start-url-should-be-same-origin = {COPY(from_path, "ManifestStartURLShouldBeSameOrigin")}
""",
            from_path=source,
        ),
    )

    def replace(new_id, old_id, variables):
        return FTL.Message(
            id=FTL.Identifier(new_id),
            value=REPLACE(
                source,
                old_id,
                {
                    f"%{i}$S": VARIABLE_REFERENCE(name)
                    for i, name in enumerate(variables, start=1)
                },
            ),
        )

    ctx.add_transforms(
        target,
        target,
        [
            replace(
                "dom-manifest-invalid-type",
                "ManifestInvalidType",
                ["objectName", "property", "expectedType"],
            ),
            replace(
                "dom-manifest-invalid-css-color",
                "ManifestInvalidCSSColor",
                ["property", "value"],
            ),
            replace(
                "dom-manifest-lang-is-invalid",
                "ManifestLangIsInvalid",
                ["property", "value"],
            ),
            replace(
                "dom-manifest-image-url-is-invalid",
                "ManifestImageURLIsInvalid",
                ["memberName", "index", "member", "value"],
            ),
            replace(
                "dom-manifest-image-unusable",
                "ManifestImageUnusable",
                ["memberName", "index"],
            ),
            replace(
                "dom-manifest-image-repeated-purposes",
                "ManifestImageRepeatedPurposes",
                ["memberName", "index", "purposes"],
            ),
        ],
    )
