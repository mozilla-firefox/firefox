# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2031472 - Convert Send Tab menupopup String Attributes, part {index}."""

    target = "browser/browser/browserContext.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
"""
main-context-menu-send-to-mobile-enable-sync2 = {COPY_PATTERN(from_path, "main-context-menu-send-to-mobile-enable-sync.label")}
main-context-menu-send-to-mobile-connect-phone2 = {COPY_PATTERN(from_path, "main-context-menu-send-to-mobile-connect-phone.label")}
main-context-menu-send-to-mobile-device-missing2 = {COPY_PATTERN(from_path, "main-context-menu-send-to-mobile-device-missing.label")}
""",
            from_path=target,
        ),
    )
