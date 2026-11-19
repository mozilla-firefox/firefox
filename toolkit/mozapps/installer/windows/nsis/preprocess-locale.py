# preprocess-locale.py
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

# preprocess-locale.py provides two functions depending on the arguments passed
# to it when invoked.
#
# Preprocesses installer locale Fluent files and creates a basic NSIS nlf
# file when invoked with --preprocess-locale.
#
# Converts a UTF-8 file to a new UTF-16LE file when invoked with
# --convert-utf8-utf16le.

from codecs import BOM_UTF16_LE
import io
from os.path import join, isfile
import re
import sys
from optparse import OptionParser

from fluent.syntax import FluentParser
from fluent.syntax import ast as FTL


def open_utf16le_file(path):
    """
    Returns an opened file object with a a UTF-16LE byte order mark.
    """
    fp = io.open(path, "w+b")
    fp.write(BOM_UTF16_LE)
    return fp


# Maps the Fluent variables available to installer strings to the NSIS
# variables and defines used to replace them.
NSIS_VARIABLES = {
    "BrandShortName": "$BrandShortName",
    "BrandFullName": "$BrandFullName",
    "BrandFullNameDA": "$BrandFullNameDA",
    "BrandProductName": "$BrandProductName",
    "ClickInstructions": "$_CLICK",
    "FilePath": "$0",
    "MinSupportedVer": "${MinSupportedVer}",
    "MinSupportedCPU": "${MinSupportedCPU}",
}

# Locales that don't add a space before the accesskey in parentheses (e.g.
# "オプション(&O)"). This mirrors the historical installer translations.
ACCESSKEY_NO_SPACE_LOCALES = {"brx", "ja", "ja-JP-mac", "ko", "pa-IN"}

# Fluent IDs in override.ftl, mapped to the NSIS built-in strings they
# override. The order matches the order in the file.
OVERRIDE_IDS = {
    "nsis-setup-caption": "SetupCaption",
    "nsis-uninstall-caption": "UninstallCaption",
    "nsis-back-btn": "BackBtn",
    "nsis-next-btn": "NextBtn",
    "nsis-accept-btn": "AcceptBtn",
    "nsis-dont-accept-btn": "DontAcceptBtn",
    "nsis-install-btn": "InstallBtn",
    "nsis-uninstall-btn": "UninstallBtn",
    "nsis-cancel-btn": "CancelBtn",
    "nsis-close-btn": "CloseBtn",
    "nsis-browse-btn": "BrowseBtn",
    "nsis-show-details-btn": "ShowDetailsBtn",
    "nsis-click-next": "ClickNext",
    "nsis-click-install": "ClickInstall",
    "nsis-click-uninstall": "ClickUninstall",
    "nsis-completed": "Completed",
    "nsis-license-text-rb": "LicenseTextRB",
    "nsis-components-text": "ComponentsText",
    "nsis-components-sub-text2-no-inst-types": "ComponentsSubText2_NoInstTypes",
    "nsis-dir-text": "DirText",
    "nsis-dir-sub-text": "DirSubText",
    "nsis-dir-browse-text": "DirBrowseText",
    "nsis-space-available": "SpaceAvailable",
    "nsis-space-required": "SpaceRequired",
    "nsis-uninstalling-text": "UninstallingText",
    "nsis-uninstalling-sub-text": "UninstallingSubText",
    "nsis-file-error": "FileError",
    "nsis-file-error-no-ignore": "FileError_NoIgnore",
    "nsis-cant-write": "CantWrite",
    "nsis-copy-failed": "CopyFailed",
    "nsis-copy-to": "CopyTo",
    "nsis-registering": "Registering",
    "nsis-unregistering": "Unregistering",
    "nsis-symbol-not-found": "SymbolNotFound",
    "nsis-could-not-load": "CouldNotLoad",
    "nsis-create-folder": "CreateFolder",
    "nsis-create-shortcut": "CreateShortcut",
    "nsis-created-uninstaller": "CreatedUninstaller",
    "nsis-delete": "Delete",
    "nsis-delete-on-reboot": "DeleteOnReboot",
    "nsis-error-creating-shortcut": "ErrorCreatingShortcut",
    "nsis-error-creating": "ErrorCreating",
    "nsis-error-decompressing": "ErrorDecompressing",
    "nsis-error-registering": "ErrorRegistering",
    "nsis-exec-shell": "ExecShell",
    "nsis-exec": "Exec",
    "nsis-extract": "Extract",
    "nsis-error-writing": "ErrorWriting",
    "nsis-invalid-opcode": "InvalidOpcode",
    "nsis-no-ole": "NoOLE",
    "nsis-output-folder": "OutputFolder",
    "nsis-remove-folder": "RemoveFolder",
    "nsis-rename-on-reboot": "RenameOnReboot",
    "nsis-rename": "Rename",
    "nsis-skipped": "Skipped",
    "nsis-copy-details": "CopyDetails",
    "nsis-log-install": "LogInstall",
    "nsis-byte": "Byte",
    "nsis-kilo": "Kilo",
    "nsis-mega": "Mega",
    "nsis-giga": "Giga",
}

# Used to detect trailing punctuation in a string, so that the accesskey
# can be inserted before it.
TRAILING_PUNCTUATION_RE = re.compile(r"\s*(?:…|\.{2,}|>)$")


class UnsupportedMessage(Exception):
    pass


class InvalidReference(Exception):
    pass


def override_name(message_id):
    if message_id not in OVERRIDE_IDS:
        raise ValueError(f"Unknown NSIS override string: {message_id}")
    return OVERRIDE_IDS[message_id]


def default_name(message_id):
    return message_id.upper().replace("-", "_")


def load_messages(path, reference=False):
    """
    Returns a dict of Fluent messages by ID.

    Parameters:
    path      - the path to the Fluent file
    reference - whether this is the en-US file, which must exist and be valid
    """
    if not isfile(path):
        if reference:
            raise InvalidReference(f"Missing en-US file: {path}")
        return {}
    with open(path, encoding="utf-8") as fp:
        resource = FluentParser(with_spans=False).parse(fp.read())
    if reference:
        for entry in resource.body:
            if isinstance(entry, FTL.Junk):
                error = entry.annotations[0].message if entry.annotations else ""
                content = entry.content.strip().splitlines()[0]
                raise InvalidReference(f"Invalid syntax in {path}: {error} ({content})")
    return {
        entry.id.name: entry
        for entry in resource.body
        if isinstance(entry, FTL.Message)
    }


def get_accesskey(message):
    for attribute in message.attributes:
        if attribute.id.name != "accesskey":
            continue
        text = ""
        for element in attribute.value.elements:
            if isinstance(element, FTL.TextElement):
                text += element.value
            elif isinstance(element.expression, FTL.StringLiteral):
                text += element.expression.parse()["value"]
            else:
                raise UnsupportedMessage(
                    f"{message.id.name}.accesskey uses unsupported syntax: "
                    f"{type(element.expression).__name__}"
                )
        return text.strip()[:1] or None
    return None


def escape_text(text, has_accesskey):
    text = text.replace("$", "$$")
    text = text.replace('"', '$\\"')
    if has_accesskey:
        text = text.replace("&", "&&")
    text = text.replace("\r", "").replace("\n", "$\\r$\\n")
    return text.replace("\t", "$\\t")


def find_accesskey(segments, accesskey):
    """
    Returns a (segment index, character index) tuple for the accesskey in the
    literal text segments, preferring a case-sensitive match, or None.
    """
    literals = [
        (segment, text)
        for segment, (text, is_literal) in enumerate(segments)
        if is_literal
    ]
    for segment, text in literals:
        index = text.find(accesskey)
        if index != -1:
            return segment, index
    for segment, text in literals:
        for index, char in enumerate(text):
            if char.lower() == accesskey.lower():
                return segment, index
    return None


def format_message(message, reference, ab_cd):
    """
    Returns the value of a Fluent message in the format used by NSIS locale
    files, with the accesskey inlined in the label.

    Parameters:
    message   - the localized Fluent message to format
    reference - the en-US Fluent message, used for the accesskey fallback
    ab_cd     - the locale code
    """
    if message.value is None:
        raise UnsupportedMessage(f"{message.id.name} has no value")

    accesskey = None
    reference_accesskey = get_accesskey(reference)
    if reference_accesskey:
        accesskey = get_accesskey(message) or reference_accesskey

    # List of (text, is_literal) tuples. Accesskeys are only searched in
    # literal text, never in variable names.
    segments = []
    for element in message.value.elements:
        if isinstance(element, FTL.TextElement):
            segments.append((element.value, True))
            continue
        expression = element.expression
        if isinstance(expression, FTL.StringLiteral):
            segments.append((expression.parse()["value"], True))
        elif (
            isinstance(expression, FTL.VariableReference)
            and expression.id.name in NSIS_VARIABLES
        ):
            segments.append((NSIS_VARIABLES[expression.id.name], False))
        else:
            raise UnsupportedMessage(
                f"{message.id.name} uses unsupported syntax: {type(expression).__name__}"
            )

    has_accesskey = accesskey is not None
    position = find_accesskey(segments, accesskey) if has_accesskey else None
    value = ""
    for segment, (text, is_literal) in enumerate(segments):
        if not is_literal:
            value += text
        elif position and position[0] == segment:
            index = position[1]
            value += (
                escape_text(text[:index], True) + "&" + escape_text(text[index:], True)
            )
        else:
            value += escape_text(text, has_accesskey)

    if has_accesskey and position is None:
        separator = "" if ab_cd in ACCESSKEY_NO_SPACE_LOCALES else " "
        label = f"{separator}(&{escape_text(accesskey, True)})"
        match = TRAILING_PUNCTUATION_RE.search(value)
        position = match.start() if match else len(value)
        value = value[:position] + label + value[position:]

    return value


def get_locale_strings(filename, l10ndirs, ab_cd, prefix, middle, nsis_name):
    """
    Returns a string created by converting an installer Fluent file into the
    format required by NSIS locale files.

    Parameters:
    filename  - the name of the Fluent file to convert
    l10ndirs  - list of paths to search for the Fluent file, in order of
                preference. The last one must contain the en-US file.
    ab_cd     - the locale code
    prefix    - a string to prefix each line with
    middle    - a string to insert between the name and value for each line
    nsis_name - a function returning the NSIS name for a Fluent message ID
    """
    reference = load_messages(join(l10ndirs[-1], filename), reference=True)
    localizations = [load_messages(join(d, filename)) for d in l10ndirs[:-1]]

    output = ""
    for message_id, reference_message in reference.items():
        reference_value = format_message(reference_message, reference_message, "en-US")
        value = None
        for messages in localizations:
            if message_id not in messages:
                continue
            try:
                value = format_message(messages[message_id], reference_message, ab_cd)
                break
            except UnsupportedMessage as e:
                print(f"Warning: {filename}: {e}", file=sys.stderr)
        if value is None:
            value = reference_value
        output += f'{prefix}{nsis_name(message_id)}{middle} "{value}"\n'
    return output


def preprocess_locale_files(config_dir, l10ndirs, ab_cd):
    """
    Preprocesses the installer localized Fluent files into the format
    required by NSIS and creates a basic NSIS nlf file.

    Parameters:
    config_dir - the path to the destination directory
    l10ndirs   - list of paths to search for installer locale files
    ab_cd      - the locale code
    """

    # Create the main NSIS language file
    fp = open_utf16le_file(join(config_dir, "overrideLocale.nsh"))
    locale_strings = get_locale_strings(
        "override.ftl", l10ndirs, ab_cd, "LangString ^", " 0 ", override_name
    )
    fp.write(locale_strings.encode("utf-16-le"))
    fp.close()

    # Create the Modern User Interface language file
    fp = open_utf16le_file(join(config_dir, "baseLocale.nsh"))
    fp.write(
        (
            """;NSIS Modern User Interface - Language File
;Compatible with Modern UI 1.68
;Language: baseLocale (0)
!insertmacro MOZ_MUI_LANGUAGEFILE_BEGIN \"baseLocale\"
!define MUI_LANGNAME \"baseLocale\"
"""
        ).encode("utf-16-le")
    )
    locale_strings = get_locale_strings(
        "mui.ftl", l10ndirs, ab_cd, "!define ", " ", default_name
    )
    fp.write(locale_strings.encode("utf-16-le"))
    fp.write("!insertmacro MOZ_MUI_LANGUAGEFILE_END\n".encode("utf-16-le"))
    fp.close()

    # Create the custom language file for our custom strings
    fp = open_utf16le_file(join(config_dir, "customLocale.nsh"))
    locale_strings = get_locale_strings(
        "custom.ftl", l10ndirs, ab_cd, "LangString ", " 0 ", default_name
    )
    fp.write(locale_strings.encode("utf-16-le"))
    fp.close()


def create_nlf_file(moz_dir, ab_cd, config_dir):
    """
    Create a basic NSIS nlf file.

    Parameters:
    moz_dir    - the path to top source directory for the toolkit source
    ab_cd      - the locale code
    config_dir - the path to the destination directory
    """
    rtl = "-"

    # Check whether the locale is right to left from locales.nsi.
    fp = io.open(
        join(moz_dir, "toolkit/mozapps/installer/windows/nsis/locales.nsi"),
        "r",
        encoding="utf-8",
    )
    for line in fp:
        line = line.strip()
        if line == "!define " + ab_cd + "_rtl":
            rtl = "RTL"
            break

    fp.close()

    # Create the main NSIS language file with RTL for right to left locales
    # along with the default codepage, font name, and font size represented
    # by the '-' character.
    fp = open_utf16le_file(join(config_dir, "baseLocale.nlf"))
    fp.write(
        (
            """# Header, don't edit
NLF v6
# Start editing here
# Language ID
0
# Font and size - dash (-) means default
-
-
# Codepage - dash (-) means ANSI code page
-
# RTL - anything else than RTL means LTR
%s
"""
            % rtl
        ).encode("utf-16-le")
    )
    fp.close()


def preprocess_locale_file(config_dir, l10ndirs, ab_cd, ftl_filename, output_filename):
    """
    Preprocesses a single localized Fluent file into the format required by
    NSIS.

    Parameters:
    config_dir      - the path to the destination directory
    l10ndirs        - list of paths to search for installer locale files
    ab_cd           - the locale code
    ftl_filename    - the name of the Fluent file to search for
    output_filename - the output filename to write
    """

    # Create the custom language file for our custom strings
    fp = open_utf16le_file(join(config_dir, output_filename))
    locale_strings = get_locale_strings(
        ftl_filename, l10ndirs, ab_cd, "LangString ", " 0 ", default_name
    )
    fp.write(locale_strings.encode("utf-16-le"))
    fp.close()


def convert_utf8_utf16le(in_file_path, out_file_path):
    """
    Converts a UTF-8 file to a new UTF-16LE file

    Arguments:
    in_file_path  - the path to the UTF-8 source file to convert
    out_file_path - the path to the UTF-16LE destination file to create
    """
    in_fp = open(in_file_path, "r", encoding="utf-8")
    out_fp = open_utf16le_file(out_file_path)
    out_fp.write(in_fp.read().encode("utf-16-le"))
    in_fp.close()
    out_fp.close()


if __name__ == "__main__":
    usage = """usage: %prog command <args>

Commands:
 --convert-utf8-utf16le     - Preprocesses installer locale properties files
 --preprocess-locale        - Preprocesses the installer localized Fluent
                              files into the format required by NSIS and
                              creates a basic NSIS nlf file.
 --preprocess-single-file   - Preprocesses a single Fluent file into the
                              format required by NSIS
 --create-nlf-file          - Creates a basic NSIS nlf file

preprocess-locale.py --preprocess-locale <src> <locale> <code> <dest>

Arguments:
 <src>   \tthe path to top source directory for the toolkit source
 <locale>\tthe path to the installer's locale files
 <code>  \tthe locale code
 <dest>  \tthe path to the destination directory


preprocess-locale.py --preprocess-single-file <src>
                                              <locale>
                                              <dest>
                                              <infile>
                                              <outfile>

Arguments:
 <src>    \tthe path to top source directory for the toolkit source
 <locale> \tthe path to the installer's locale files
 <dest>   \tthe path to the destination directory
 <infile> \tthe Fluent file to process
 <outfile>\tthe nsh file to write


preprocess-locale.py --create-nlf-file <src>
                                       <code>
                                       <dest>

Arguments:
 <src>    \tthe path to top source directory for the toolkit source
 <code>   \tthe locale code
 <dest>   \tthe path to the destination directory


preprocess-locale.py --convert-utf8-utf16le <src> <dest>

Arguments:
 <src> \tthe path to the UTF-8 source file to convert
 <dest>\tthe path to the UTF-16LE destination file to create
"""

    preprocess_locale_args_help_string = """\
Arguments to --preprocess-locale should be:
   <src> <locale> <code> <dest>
or
   <src> <code> <dest> --l10n-dir <dir> [--l10n-dir <dir> ...]"""

    preprocess_single_file_args_help_string = """\
Arguments to --preprocess-single_file should be:
   <src> <locale> <code> <dest> <infile> <outfile>
or
   <src> <locale> <code> <dest> <infile> <outfile>
   --l10n-dir <dir> [--l10n-dir <dir>...]"""

    create_nlf_args_help_string = """\
Arguments to --create-nlf-file should be:
   <src> <code> <dest>"""

    p = OptionParser(usage=usage)
    p.add_option(
        "--preprocess-locale", action="store_true", default=False, dest="preprocess"
    )
    p.add_option(
        "--preprocess-single-file",
        action="store_true",
        default=False,
        dest="preprocessSingle",
    )
    p.add_option(
        "--create-nlf-file", action="store_true", default=False, dest="createNlf"
    )
    p.add_option(
        "--l10n-dir",
        action="append",
        default=[],
        dest="l10n_dirs",
        help="Add directory to lookup for locale files",
    )
    p.add_option(
        "--ab-cd",
        default="en-US",
        dest="ab_cd",
        help="The locale code, used by --preprocess-single-file",
    )
    p.add_option(
        "--convert-utf8-utf16le", action="store_true", default=False, dest="convert"
    )

    options, args = p.parse_args()

    foundOne = False
    if options.preprocess:
        foundOne = True
    if options.convert:
        if foundOne:
            p.error("More than one command specified")
        else:
            foundOne = True
    if options.preprocessSingle:
        if foundOne:
            p.error("More than one command specified")
        else:
            foundOne = True
    if options.createNlf:
        if foundOne:
            p.error("More than one command specified")
        else:
            foundOne = True

    if not foundOne:
        p.error("No command specified")

    try:
        if options.preprocess:
            if len(args) not in (3, 4):
                p.error(preprocess_locale_args_help_string)

            # Parse args
            pargs = args[:]
            moz_dir = pargs[0]
            if len(pargs) == 4:
                l10n_dirs = [pargs[1]]
                del pargs[1]
            else:
                if not options.l10n_dirs:
                    p.error(preprocess_locale_args_help_string)
                l10n_dirs = options.l10n_dirs
            ab_cd = pargs[1]
            config_dir = pargs[2]

            # Create the output files
            create_nlf_file(moz_dir, ab_cd, config_dir)
            preprocess_locale_files(config_dir, l10n_dirs, ab_cd)
        elif options.preprocessSingle:
            if len(args) not in (4, 5):
                p.error(preprocess_single_file_args_help_string)

            # Parse args
            pargs = args[:]
            moz_dir = pargs[0]
            if len(pargs) == 5:
                l10n_dirs = [pargs[1]]
                del pargs[1]
            else:
                if not options.l10n_dirs:
                    p.error(preprocess_single_file_args_help_string)
                l10n_dirs = options.l10n_dirs
            config_dir = pargs[1]
            in_file = pargs[2]
            out_file = pargs[3]

            # Create the output files
            preprocess_locale_file(
                config_dir, l10n_dirs, options.ab_cd, in_file, out_file
            )
        elif options.createNlf:
            if len(args) != 3:
                p.error(create_nlf_args_help_string)

            # Parse args
            pargs = args[:]
            moz_dir = pargs[0]
            ab_cd = pargs[1]
            config_dir = pargs[2]

            # Create the output files
            create_nlf_file(moz_dir, ab_cd, config_dir)
        elif options.convert:
            if len(args) != 2:
                p.error("--convert-utf8-utf16le needs both of <src> <dest>")
            convert_utf8_utf16le(*args)
    except InvalidReference as e:
        sys.exit(f"Error: {e}")
