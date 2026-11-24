# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2095460 - Migrate devtools Changes panel strings to Fluent, part {index}."""

    ctx.add_transforms(
        "devtools/client/changes.ftl",
        "devtools/client/changes.ftl",
        transforms_from(
            """
changes-no-changes = { COPY(from_path, "changes.noChanges") }
changes-no-changes-description = { COPY(from_path, "changes.noChangesDescription") }
changes-inline-stylesheet-label = { COPY(from_path, "changes.inlineStyleSheetLabel2") }
changes-element-style-label = { COPY(from_path, "changes.elementStyleLabel") }
changes-iframe-label = { COPY(from_path, "changes.iframeLabel") }
changes-copy-all-changes-button = { COPY(from_path, "changes.contextmenu.copyAllChanges") }
changes-copy-all-changes-button-title = { COPY(from_path, "changes.contextmenu.copyAllChangesDescription") }
changes-copy-rule-button = { COPY(from_path, "changes.contextmenu.copyRule") }
changes-copy-rule-button-title = { COPY(from_path, "changes.contextmenu.copyRuleDescription") }
changes-context-menu-copy =
    .label = { COPY(from_path, "changes.contextmenu.copy") }
    .accesskey = { COPY(from_path, "changes.contextmenu.copy.accessKey") }
changes-context-menu-copy-rule =
    .label = { COPY(from_path, "changes.contextmenu.copyRule") }
changes-context-menu-copy-declaration =
    .label = { COPY(from_path, "changes.contextmenu.copyDeclaration") }
changes-context-menu-select-all =
    .label = { COPY(from_path, "changes.contextmenu.selectAll") }
    .accesskey = { COPY(from_path, "changes.contextmenu.selectAll.accessKey") }
""",
            from_path="devtools/client/changes.properties",
        ),
    )
