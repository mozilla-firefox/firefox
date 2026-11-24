# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2095460 - Migrate devtools box model strings to Fluent, part {index}."""

    ctx.add_transforms(
        "devtools/client/boxmodel.ftl",
        "devtools/client/boxmodel.ftl",
        transforms_from(
            """
boxmodel-title = { COPY(from_path, "boxmodel.title") }
boxmodel-geometry-button-title = { COPY(from_path, "boxmodel.geometryButton.tooltip") }
boxmodel-properties-label = { COPY(from_path, "boxmodel.propertiesLabel") }
boxmodel-properties-hide-label = { COPY(from_path, "boxmodel.propertiesHideLabel") }
boxmodel-properties-show-label = { COPY(from_path, "boxmodel.propertiesShowLabel") }
boxmodel-offset-parent = { COPY(from_path, "boxmodel.offsetParent") }
boxmodel-offset-parent-title = { COPY(from_path, "boxmodel.offsetParent.title") }
""",
            from_path="devtools/client/boxmodel.properties",
        ),
    )
