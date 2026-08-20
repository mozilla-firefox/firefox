# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2052179 - Keep moz-box-link description out of accessible name, part {index}."""

    source = "toolkit/toolkit/global/mozBoxBase.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
moz-box-link-opens-in-new-tab = {COPY_PATTERN(from_path, "moz-box-link-anchor.title")}
""",
            from_path=source,
        ),
    )
