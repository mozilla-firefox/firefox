# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1953335 - Expose collapsed state and grouping of tab groups in the all tabs menu, part {index}."""

    path = "browser/browser/tabbrowser.ftl"

    ctx.add_transforms(
        path,
        path,
        transforms_from(
            """
tabbrowser-manager-current-window-tab-group-expanded =
    .label = {COPY_PATTERN(from_path, "tabbrowser-manager-current-window-tab-group.label")}
    .tooltiptext = {COPY_PATTERN(from_path, "tabbrowser-manager-current-window-tab-group.tooltiptext")}
    .aria-description = {COPY_PATTERN(from_path, "tab-group-description")}
""",
            from_path=path,
        ),
    )
