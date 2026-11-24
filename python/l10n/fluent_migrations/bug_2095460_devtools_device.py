# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2095460 - Migrate devtools device type strings to Fluent, part {index}."""

    ctx.add_transforms(
        "devtools/client/device.ftl",
        "devtools/client/device.ftl",
        transforms_from(
            """
device-type-phones = { COPY(from_path, "device.phones") }
device-type-tablets = { COPY(from_path, "device.tablets") }
device-type-laptops = { COPY(from_path, "device.laptops") }
device-type-televisions = { COPY(from_path, "device.televisions") }
device-type-consoles = { COPY(from_path, "device.consoles") }
device-type-watches = { COPY(from_path, "device.watches") }
device-type-custom = { COPY(from_path, "device.custom") }
""",
            from_path="devtools/client/device.properties",
        ),
    )
