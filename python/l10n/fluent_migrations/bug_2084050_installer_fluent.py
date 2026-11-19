# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import os
import re

import fluent.syntax.ast as FTL
from fluent.migrate.helpers import VARIABLE_REFERENCE
from fluent.migrate.transforms import REPLACE, LegacySource, Transform

# "$BrandFullNameDA" must come after "$BrandFullName", so that it wins when
# both match at the same position.
REPLACEMENTS = {
    "$BrandShortName": VARIABLE_REFERENCE("BrandShortName"),
    "$BrandFullName": VARIABLE_REFERENCE("BrandFullName"),
    "$BrandFullNameDA": VARIABLE_REFERENCE("BrandFullNameDA"),
    "$BrandProductName": VARIABLE_REFERENCE("BrandProductName"),
    "$_CLICK": VARIABLE_REFERENCE("ClickInstructions"),
    "$0": VARIABLE_REFERENCE("FilePath"),
    "${MinSupportedVer}": VARIABLE_REFERENCE("MinSupportedVer"),
    "${MinSupportedCPU}": VARIABLE_REFERENCE("MinSupportedCPU"),
}

PARENTHESES_ACCESSKEY_RE = re.compile(r"\s*[(（]&(.)[)）]")
ACCESSKEY_RE = re.compile(r"&(.)")
# Characters that can't start a line in a multiline Fluent pattern need to be
# wrapped in string literals. This is needed, for example, for strings like
# WARN_DISK_SPACE in Japanese, where line starts with `[OK]`.
SPECIAL_LINE_START = (".", "[", "*")

# Strings with an accesskey in the label for en-US.
ACCESSKEYS = {
    "AcceptBtn",
    "BackBtn",
    "BrowseBtn",
    "CloseBtn",
    "DontAcceptBtn",
    "InstallBtn",
    "NextBtn",
    "ShowDetailsBtn",
    "UninstallBtn",
    "MUI_BUTTONTEXT_FINISH",
    "CONTEXT_OPTIONS",
    "CONTEXT_SAFE_MODE",
    "ICONS_DESKTOP",
    "ICONS_STARTMENU",
    "ICONS_TASKBAR",
    "LAUNCH_TEXT",
    "MAINTENANCE_SERVICE_CHECKBOX_DESC",
    "OPTION_CUSTOM_RADIO",
    "OPTION_STANDARD_RADIO",
    "UN_REFRESH_BUTTON",
    "UN_REFRESH_LEARN_MORE",
    "UPGRADE_BUTTON",
}

OVERRIDE = [
    "SetupCaption",
    "UninstallCaption",
    "BackBtn",
    "NextBtn",
    "AcceptBtn",
    "DontAcceptBtn",
    "InstallBtn",
    "UninstallBtn",
    "CancelBtn",
    "CloseBtn",
    "BrowseBtn",
    "ShowDetailsBtn",
    "ClickNext",
    "ClickInstall",
    "ClickUninstall",
    "Completed",
    "LicenseTextRB",
    "ComponentsText",
    "ComponentsSubText2_NoInstTypes",
    "DirText",
    "DirSubText",
    "DirBrowseText",
    "SpaceAvailable",
    "SpaceRequired",
    "UninstallingText",
    "UninstallingSubText",
    "FileError",
    "FileError_NoIgnore",
    "CantWrite",
    "CopyFailed",
    "CopyTo",
    "Registering",
    "Unregistering",
    "SymbolNotFound",
    "CouldNotLoad",
    "CreateFolder",
    "CreateShortcut",
    "CreatedUninstaller",
    "Delete",
    "DeleteOnReboot",
    "ErrorCreatingShortcut",
    "ErrorCreating",
    "ErrorDecompressing",
    "ErrorRegistering",
    "ExecShell",
    "Exec",
    "Extract",
    "ErrorWriting",
    "InvalidOpcode",
    "NoOLE",
    "OutputFolder",
    "RemoveFolder",
    "RenameOnReboot",
    "Rename",
    "Skipped",
    "CopyDetails",
    "LogInstall",
    "Byte",
    "Kilo",
    "Mega",
    "Giga",
]

MUI = [
    "MUI_TEXT_WELCOME_INFO_TITLE",
    "MUI_TEXT_WELCOME_INFO_TEXT",
    "MUI_TEXT_COMPONENTS_TITLE",
    "MUI_TEXT_COMPONENTS_SUBTITLE",
    "MUI_INNERTEXT_COMPONENTS_DESCRIPTION_TITLE",
    "MUI_INNERTEXT_COMPONENTS_DESCRIPTION_INFO",
    "MUI_TEXT_DIRECTORY_TITLE",
    "MUI_TEXT_DIRECTORY_SUBTITLE",
    "MUI_TEXT_INSTALLING_TITLE",
    "MUI_TEXT_INSTALLING_SUBTITLE",
    "MUI_TEXT_FINISH_TITLE",
    "MUI_TEXT_FINISH_SUBTITLE",
    "MUI_TEXT_ABORT_TITLE",
    "MUI_TEXT_ABORT_SUBTITLE",
    "MUI_BUTTONTEXT_FINISH",
    "MUI_TEXT_FINISH_INFO_TITLE",
    "MUI_TEXT_FINISH_INFO_TEXT",
    "MUI_TEXT_FINISH_INFO_REBOOT",
    "MUI_TEXT_FINISH_REBOOTNOW",
    "MUI_TEXT_FINISH_REBOOTLATER",
    "MUI_TEXT_STARTMENU_TITLE",
    "MUI_TEXT_STARTMENU_SUBTITLE",
    "MUI_INNERTEXT_STARTMENU_TOP",
    "MUI_TEXT_ABORTWARNING",
    "MUI_UNTEXT_WELCOME_INFO_TITLE",
    "MUI_UNTEXT_WELCOME_INFO_TEXT",
    "MUI_UNTEXT_CONFIRM_TITLE",
    "MUI_UNTEXT_CONFIRM_SUBTITLE",
    "MUI_UNTEXT_UNINSTALLING_TITLE",
    "MUI_UNTEXT_UNINSTALLING_SUBTITLE",
    "MUI_UNTEXT_FINISH_TITLE",
    "MUI_UNTEXT_FINISH_SUBTITLE",
    "MUI_UNTEXT_ABORT_TITLE",
    "MUI_UNTEXT_ABORT_SUBTITLE",
    "MUI_UNTEXT_FINISH_INFO_TITLE",
    "MUI_UNTEXT_FINISH_INFO_TEXT",
    "MUI_UNTEXT_FINISH_INFO_REBOOT",
    "MUI_UNTEXT_ABORTWARNING",
]

CUSTOM = [
    "REG_APP_DESC",
    "BRIEF_APP_DESC",
    "PRIVATE_BROWSING_SHORTCUT_TITLE",
    "CONTEXT_OPTIONS",
    "CONTEXT_SAFE_MODE",
    "OPTIONS_PAGE_TITLE",
    "OPTIONS_PAGE_SUBTITLE",
    "SHORTCUTS_PAGE_TITLE",
    "SHORTCUTS_PAGE_SUBTITLE",
    "COMPONENTS_PAGE_TITLE",
    "COMPONENTS_PAGE_SUBTITLE",
    "OPTIONAL_COMPONENTS_DESC",
    "MAINTENANCE_SERVICE_CHECKBOX_DESC",
    "SUMMARY_PAGE_TITLE",
    "SUMMARY_PAGE_SUBTITLE",
    "SUMMARY_INSTALLED_TO",
    "SUMMARY_REBOOT_REQUIRED_INSTALL",
    "SUMMARY_REBOOT_REQUIRED_UNINSTALL",
    "SUMMARY_INSTALL_CLICK",
    "SUMMARY_UPGRADE_CLICK",
    "LAUNCH_TEXT",
    "CREATE_ICONS_DESC",
    "ICONS_DESKTOP",
    "ICONS_STARTMENU",
    "ICONS_TASKBAR",
    "WARN_MANUALLY_CLOSE_APP_INSTALL",
    "WARN_MANUALLY_CLOSE_APP_UNINSTALL",
    "WARN_MANUALLY_CLOSE_APP_REFRESH",
    "WARN_WRITE_ACCESS",
    "WARN_DISK_SPACE",
    "WARN_MIN_SUPPORTED_OSVER_MSG",
    "WARN_MIN_SUPPORTED_CPU_MSG",
    "WARN_MIN_SUPPORTED_OSVER_CPU_MSG",
    "WARN_RESTART_REQUIRED_UNINSTALL",
    "WARN_RESTART_REQUIRED_UPGRADE",
    "ERROR_CREATE_DIRECTORY_PREFIX",
    "ERROR_CREATE_DIRECTORY_SUFFIX",
    "UN_CONFIRM_PAGE_TITLE",
    "UN_CONFIRM_UNINSTALLED_FROM",
    "UN_REFRESH_PAGE_TITLE",
    "UN_REFRESH_PAGE_EXPLANATION",
    "UN_REFRESH_LEARN_MORE",
    "UN_REFRESH_BUTTON",
    "BANNER_CHECK_EXISTING",
    "STATUS_INSTALL_APP",
    "STATUS_UNINSTALL_MAIN",
    "STATUS_CLEANUP",
    "UN_SURVEY_CHECKBOX_LABEL",
    "OPTIONS_SUMMARY",
    "OPTION_STANDARD_DESC",
    "OPTION_STANDARD_RADIO",
    "OPTION_CUSTOM_DESC",
    "OPTION_CUSTOM_RADIO",
    "UPGRADE_BUTTON",
]

NSISSTRINGS = [
    "INSTALLER_WIN_CAPTION",
    "STUB_CLEANUP_PAVEOVER_HEADER3",
    "STUB_CLEANUP_REINSTALL_HEADER3",
    "STUB_CLEANUP_PAVEOVER_BUTTON2",
    "STUB_CLEANUP_REINSTALL_BUTTON2",
    "STUB_CLEANUP_CHECKBOX_LABEL3",
    "STUB_INSTALLING_LABEL2",
    "STUB_BLURB_FIRST3",
    "STUB_BLURB_SECOND2",
    "STUB_BLURB_THIRD2",
    "STUB_BLURB_FIRST2_DEVEDITION",
    "STUB_BLURB_SECOND2_DEVEDITION",
    "STUB_BLURB_THIRD2_DEVEDITION",
    "WARN_MIN_SUPPORTED_OSVER_MSG2",
    "WARN_MIN_SUPPORTED_CPU_MSG2",
    "WARN_MIN_SUPPORTED_OSVER_CPU_MSG2",
    "WARN_WRITE_ACCESS_QUIT2",
    "WARN_DISK_SPACE_QUIT2",
    "ERROR_DOWNLOAD_CONT2",
    "STUB_CANCEL_PROMPT_HEADING",
    "STUB_CANCEL_PROMPT_MESSAGE",
    "STUB_CANCEL_PROMPT_BUTTON_CONTINUE",
    "STUB_CANCEL_PROMPT_BUTTON_EXIT",
]


# Legacy keys whose Fluent ID drops the version number.
RENAMED_IDS = {
    "STUB_CLEANUP_PAVEOVER_HEADER3": "stub-cleanup-paveover-header",
    "STUB_CLEANUP_REINSTALL_HEADER3": "stub-cleanup-reinstall-header",
    "STUB_CLEANUP_PAVEOVER_BUTTON2": "stub-cleanup-paveover-button",
    "STUB_CLEANUP_REINSTALL_BUTTON2": "stub-cleanup-reinstall-button",
    "STUB_CLEANUP_CHECKBOX_LABEL3": "stub-cleanup-checkbox-label",
    "STUB_INSTALLING_LABEL2": "stub-installing-label",
    "STUB_BLURB_FIRST3": "stub-blurb-first",
    "STUB_BLURB_SECOND2": "stub-blurb-second",
    "STUB_BLURB_THIRD2": "stub-blurb-third",
    "STUB_BLURB_FIRST2_DEVEDITION": "stub-blurb-first-devedition",
    "STUB_BLURB_SECOND2_DEVEDITION": "stub-blurb-second-devedition",
    "STUB_BLURB_THIRD2_DEVEDITION": "stub-blurb-third-devedition",
    "WARN_WRITE_ACCESS_QUIT2": "warn-write-access-quit",
    "WARN_DISK_SPACE_QUIT2": "warn-disk-space-quit",
    "ERROR_DOWNLOAD_CONT2": "error-download-cont",
}


def kebab_case(name):
    # Keys in override.properties are camelCase (BackBtn -> back-btn), the
    # others are UPPER_SNAKE (MUI_TEXT_FINISH_TITLE -> mui-text-finish-title).
    return re.sub(r"([a-z0-9])([A-Z])", r"\1-\2", name).replace("_", "-").lower()


def clean(text):
    """Strip quotes and normalize newlines in a legacy installer string."""
    text = text.strip()
    # Strip quotes from strings that are quoted to include
    # trailing whitespaces, e.g. "foo ".
    if len(text) > 1 and text[0] == text[-1] == '"':
        text = text[1:-1]
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return re.sub(r"[ \t]+\n", "\n", text)


def split_accesskey(text):
    """Return the label without the accesskey, and the accesskey (or None)."""
    match = PARENTHESES_ACCESSKEY_RE.search(text)
    if match:
        return text[: match.start()] + text[match.end() :], match.group(1)
    match = ACCESSKEY_RE.search(text)
    if match:
        return text[: match.start()] + text[match.start() + 1 :], match.group(1)
    return text, None


def get_reference_message(ctx, target, message_id):
    reference = ctx.reference_resources[target]
    return next(
        entry
        for entry in reference.body
        if isinstance(entry, FTL.Message) and entry.id.name == message_id
    )


def ends_with_whitespace(pattern):
    last = pattern.elements[-1]
    if isinstance(last, FTL.TextElement):
        text = last.value
    elif isinstance(last.expression, FTL.StringLiteral):
        text = last.expression.value
    else:
        return False
    return text[-1:].isspace()


class INSTALLER_STRING(REPLACE):
    """
    Migrate an installer string, removing the accesskey from labels, and adding
    the trailing space if en-US has one and the translation doesn't.
    """

    def __init__(self, path, key, target, message_id, label=False):
        super().__init__(path, key, REPLACEMENTS, normalize_printf=False, trim=False)
        self.target = target
        self.message_id = message_id
        self.label = label

    def get_text(self, ctx):
        text = clean(super().get_text(ctx))
        return split_accesskey(text)[0] if self.label else text

    def __call__(self, ctx):
        elements = []
        for element in super().__call__(ctx).elements:
            if not isinstance(element, FTL.TextElement):
                elements.append(element)
                continue
            lines = element.value.split("\n")
            elements.append(FTL.TextElement(lines[0]))
            for line in lines[1:]:
                elements.append(FTL.TextElement("\n"))
                if line.startswith(SPECIAL_LINE_START):
                    elements.append(FTL.Placeable(FTL.StringLiteral(line[0])))
                    line = line[1:]
                elements.append(FTL.TextElement(line))
        pattern = Transform.pattern_of(*elements)
        # These strings are prefixes for a file name, e.g. "Delete file: ".
        reference = get_reference_message(ctx, self.target, self.message_id)
        if ends_with_whitespace(reference.value) and not ends_with_whitespace(pattern):
            pattern = Transform.pattern_of(*pattern.elements, FTL.TextElement(" "))
        return pattern


class INSTALLER_ACCESSKEY(LegacySource):
    """Extract the accesskey from a label, falling back to the en-US one."""

    def __init__(self, path, key, target, message_id):
        super().__init__(path, key)
        self.target = target
        self.message_id = message_id

    def __call__(self, ctx):
        key = split_accesskey(clean(self.get_text(ctx)))[1]
        if not key:
            message = get_reference_message(ctx, self.target, self.message_id)
            attribute = next(a for a in message.attributes if a.id.name == "accesskey")
            key = attribute.value.elements[0].value
        return Transform.pattern_of(FTL.TextElement(key))


def installer_transforms(source, target, keys, prefix=""):
    transforms = []
    for key in keys:
        message_id = RENAMED_IDS.get(key, prefix + kebab_case(key))
        has_accesskey = key in ACCESSKEYS
        attributes = []
        if has_accesskey:
            attributes.append(
                FTL.Attribute(
                    FTL.Identifier("accesskey"),
                    INSTALLER_ACCESSKEY(source, key, target, message_id),
                )
            )
        transforms.append(
            FTL.Message(
                id=FTL.Identifier(message_id),
                value=INSTALLER_STRING(
                    source, key, target, message_id, label=has_accesskey
                ),
                attributes=attributes,
            )
        )
    return transforms


def has_source(ctx, path):
    # The context used by fluent-migration-test to inspect the recipe has no
    # localization directory.
    localization_dir = getattr(ctx, "localization_dir", None)
    return localization_dir is None or os.path.isfile(
        os.path.join(localization_dir, path)
    )


def migrate(ctx):
    """Bug 2084050 - Migrate the Windows installer strings to Fluent, part {index}."""

    # add_transforms() raises if none of the files read so far exist, which
    # would stop the whole migration for locales missing the first file (e.g.
    # hto has no override.properties). Only migrate files that exist.
    source = "browser/installer/override.properties"
    target = "browser/installer/override.ftl"
    if has_source(ctx, source):
        ctx.add_transforms(
            target, target, installer_transforms(source, target, OVERRIDE, "nsis-")
        )

    source = "browser/installer/mui.properties"
    target = "browser/installer/mui.ftl"
    if has_source(ctx, source):
        ctx.add_transforms(target, target, installer_transforms(source, target, MUI))

    source = "browser/installer/custom.properties"
    target = "browser/installer/custom.ftl"
    if has_source(ctx, source):
        ctx.add_transforms(target, target, installer_transforms(source, target, CUSTOM))

    source = "browser/installer/nsisstrings.properties"
    target = "browser/installer/nsisstrings.ftl"
    if has_source(ctx, source):
        ctx.add_transforms(
            target, target, installer_transforms(source, target, NSISSTRINGS)
        )
