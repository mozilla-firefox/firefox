# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import sys

import buildconfig
from mozbuild.preprocessor import Preprocessor


def main(output, input_file, *defines):
    pp = Preprocessor()
    pp.context.update({
        "FFI_EXEC_TRAMPOLINE_TABLE": "0",
        "FFI_VERSION_NUMBER": "30800",
        "FFI_VERSION_STRING": "3.8.0",
        "HAVE_LONG_DOUBLE": "0",
        "TARGET": buildconfig.substs["FFI_TARGET"],
        "VERSION": "",
    })
    for d in defines:
        pp.context.update({d: "1"})
    pp.do_filter("substitution")
    pp.setMarker(None)
    pp.out = output
    pp.do_include(input_file)


if __name__ == "__main__":
    main(*sys.argv[1:])
