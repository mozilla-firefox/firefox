# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2095460 - Migrate devtools DOM panel strings to Fluent, part {index}."""

    ctx.add_transforms(
        "devtools/client/dom.ftl",
        "devtools/client/dom.ftl",
        transforms_from(
            """
dom-panel-filter-placeholder = { COPY(from_path, "dom.filterDOMPanel") }
dom-panel-refresh-button-title = { COPY(from_path, "dom.refresh") }
""",
            from_path="devtools/client/dom.properties",
        ),
    )
