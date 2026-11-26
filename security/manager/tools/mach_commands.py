# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

from mach.decorators import Command, CommandArgument
from mach.util import UserError
from mozpack.files import FileFinder


def run_module_main_on(module, input_filename, output_is_binary):
    """Run the given module (pycert, pykey, etc.) on the given
    file."""
    # By convention, the specification files have names of the form
    # "name.ext.*spec", where "ext" is some extension, and the "*" in
    # "*spec" identifies what kind of specification it represents
    # (certspec, keyspec, etc.). Taking off the ".*spec" part results in
    # the desired filename for this file.
    output_filename = os.path.splitext(input_filename)[0]
    mode = "w"
    encoding = "utf-8"
    newline = "\n"
    if output_is_binary:
        mode = "wb"
        encoding = None
        newline = None
    with open(output_filename, mode=mode, encoding=encoding, newline=newline) as output:
        module.main(output, input_filename)


def is_certspec_file(filename):
    """Returns True if the given filename is a certificate
    specification file (.certspec) and False otherwise."""
    return filename.endswith(".certspec")


def is_keyspec_file(filename):
    """Returns True if the given filename is a key specification
    file (.keyspec) and False otherwise."""
    return filename.endswith(".keyspec")


def is_pkcs12spec_file(filename):
    """Returns True if the given filename is a pkcs12
    specification file (.pkcs12spec) and False otherwise."""
    return filename.endswith(".pkcs12spec")


def is_sctspec_file(filename):
    """Returns True if the given filename is an SCT
    specification file (.sctspec) and False otherwise."""
    return filename.endswith(".sctspec")


def is_bindingspec_file(filename):
    """Returns True if the given filename is a TLS certificate
    binding specification file (.bindingspec) and False
    otherwise."""
    return filename.endswith(".bindingspec")


def is_specification_file(filename):
    """Returns True if the given filename is a specification
    file supported by this script, and False otherewise."""
    return (
        is_certspec_file(filename)
        or is_keyspec_file(filename)
        or is_pkcs12spec_file(filename)
        or is_sctspec_file(filename)
        or is_bindingspec_file(filename)
    )


@Command(
    "generate-test-certs",
    category="devenv",
    description="Generate test certificates and keys from specifications.",
    virtualenv_name="generate-test-certs",
)
@CommandArgument(
    "specifications",
    nargs="*",
    help="Specification files for test certs. If omitted, all certs are regenerated.",
)
def generate_test_certs(command_context, specifications):
    """Generate test certificates and keys from specifications."""
    import pycert
    import pyct
    import pykey
    import pypkcs12
    import pytlsbinding

    if not specifications:
        specifications = find_all_specifications(command_context)

    for specification in specifications:
        output_is_binary = False
        if is_certspec_file(specification):
            module = pycert
        elif is_keyspec_file(specification):
            module = pykey
        elif is_pkcs12spec_file(specification):
            module = pypkcs12
            output_is_binary = True
        elif is_sctspec_file(specification):
            module = pyct
            output_is_binary = True
        elif is_bindingspec_file(specification):
            module = pytlsbinding
        else:
            raise UserError(
                f"'{specification}' is not a .certspec, .keyspec, .pkcs12spec, or .bindingspec file"
            )
        run_module_main_on(module, os.path.abspath(specification), output_is_binary)
    return 0


def find_all_specifications(command_context):
    """Searches the source tree for all specification files
    and returns them as a list."""
    specifications = []
    inclusions = [
        "browser/base/content/test/browser-siteIdentity/",
        "browser/components/backup/tests/marionette",
        "netwerk/test/marionette",
        "netwerk/test/unit",
        "security/manager/ssl",
        "services/settings/test/unit/test_remote_settings_signatures",
        "testing/mochitest/DoHServer",
        "testing/raptor/browsertime/utils",
        "testing/xpcshell/moz-http2",
        "toolkit/mozapps/extensions/test/xpcshell/data/productaddons",
    ]
    finder = FileFinder(command_context.topsrcdir)
    for inclusion_path in inclusions:
        for f, _ in finder.find(inclusion_path):
            if is_specification_file(f):
                specifications.append(os.path.join(command_context.topsrcdir, f))
    return specifications
