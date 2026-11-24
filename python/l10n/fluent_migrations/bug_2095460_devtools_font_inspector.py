# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2095460 - Migrate devtools font inspector strings to Fluent, part {index}."""

    ctx.add_transforms(
        "devtools/client/font-inspector.ftl",
        "devtools/client/font-inspector.ftl",
        transforms_from(
            """
font-inspector-system = { COPY(from_path, "fontinspector.system") }
font-inspector-no-fonts-used = { COPY(from_path, "fontinspector.noFontsUsedOnCurrentElement") }
font-inspector-copy-url-title = { COPY(from_path, "fontinspector.copyURL") }
font-inspector-custom-instance-name = { COPY(from_path, "fontinspector.customInstanceName") }
font-inspector-font-instance-label = { COPY(from_path, "fontinspector.fontInstanceLabel") }
font-inspector-font-size-label = { COPY(from_path, "fontinspector.fontSizeLabel") }
font-inspector-font-weight-label = { COPY(from_path, "fontinspector.fontWeightLabel") }
font-inspector-font-italic-label = { COPY(from_path, "fontinspector.fontItalicLabel") }
font-inspector-letter-spacing-label = { COPY(from_path, "fontinspector.letterSpacingLabel") }
font-inspector-line-height-label = { COPY(from_path, "fontinspector.lineHeightLabelCapitalized") }
font-inspector-show-more = { COPY(from_path, "fontinspector.showMore") }
font-inspector-show-less = { COPY(from_path, "fontinspector.showLess") }
font-inspector-all-fonts-on-page-header = { COPY(from_path, "fontinspector.allFontsOnPageHeader") }
font-inspector-fonts-used-label = { COPY(from_path, "fontinspector.fontsUsedLabel") }
font-inspector-preview-text-placeholder = { COPY(from_path, "fontinspector.previewTextPlaceholder") }
font-inspector-font-version = { COPY(from_path, "fontinspector.fontVersion") }
font-inspector-font-designer = { COPY(from_path, "fontinspector.fontDesigner") }
font-inspector-font-manufacturer = { COPY(from_path, "fontinspector.fontManufacturer") }
font-inspector-font-vendor = { COPY(from_path, "fontinspector.fontVendor") }
font-inspector-font-description = { COPY(from_path, "fontinspector.fontDescription") }
font-inspector-font-license = { COPY(from_path, "fontinspector.fontLicense") }
font-inspector-font-license-info-url = { COPY(from_path, "fontinspector.fontLicenseInfoUrl") }
font-inspector-show-full-text = { COPY(from_path, "fontinspector.showFullText") }
""",
            from_path="devtools/client/font-inspector.properties",
        ),
    )
