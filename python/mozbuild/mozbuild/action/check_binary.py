# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import os
import re
import subprocess
import sys

import buildconfig
from mozpack.executables import ELF, UNKNOWN, get_type
from packaging.version import Version

from mozbuild.util import memoize

STDCXX_MAX_VERSION = Version("3.4.19")
CXXABI_MAX_VERSION = Version("1.3.7")
GLIBC_MAX_VERSION = Version("2.17")
LIBGCC_MAX_VERSION = Version("4.8")

PLATFORM = buildconfig.substs["OS_TARGET"]
READELF = buildconfig.substs.get("READELF", "readelf")

ADDR_RE = re.compile(r"[0-9a-f]{8,16}")

if buildconfig.substs.get("HAVE_64BIT_BUILD"):
    GUESSED_NSMODULE_SIZE = 8
else:
    GUESSED_NSMODULE_SIZE = 4


get_type = memoize(get_type)


@memoize
def get_output(*cmd):
    env = dict(os.environ)
    env["LC_ALL"] = "C"
    return subprocess.check_output(cmd, env=env, universal_newlines=True).splitlines()


class Skip(RuntimeError):
    pass


class Empty(RuntimeError):
    pass


def at_least_one(iter):
    saw_one = False
    for item in iter:
        saw_one = True
        yield item
    if not saw_one:
        raise Empty()


# Iterates the symbol table on ELF binaries.
def iter_elf_symbols(binary, all=False):
    ty = get_type(binary)
    # Static libraries are ar archives. Assume they are ELF.
    if ty == UNKNOWN and open(binary, "rb").read(8) == b"!<arch>\n":
        ty = ELF
    assert ty == ELF

    def looks_like_readelf_data(data):
        return len(data) >= 8 and data[0].endswith(":") and data[0][:-1].isdigit()

    for line in get_output(
        READELF, "--wide", "--syms" if all else "--dyn-syms", binary
    ):
        data = line.split()
        if not looks_like_readelf_data(data):
            # Older versions of llvm-readelf would use .hash for --dyn-syms,
            # which would add an extra column at the beginning.
            data = data[1:]
            if not looks_like_readelf_data(data):
                continue
        n, addr, size, type, bind, vis, index, name = data[:8]

        if "@" in name:
            name, ver = name.rsplit("@", 1)
            while name.endswith("@"):
                name = name[:-1]
        else:
            ver = None
        yield {
            "addr": int(addr, 16),
            # readelf output may contain decimal values or hexadecimal
            # values prefixed with 0x for the size. Let python autodetect.
            "size": int(size, 0),
            "name": name,
            "version": ver,
        }


def iter_readelf_dynamic(binary):
    for line in get_output(READELF, "-d", binary):
        data = line.split(None, 2)
        if data and len(data) == 3 and data[0].startswith("0x"):
            yield data[1].rstrip(")").lstrip("("), data[2]


def check_binary_compat(binary):
    if get_type(binary) != ELF:
        raise Skip()
    checks = (
        ("libstdc++", "GLIBCXX_", STDCXX_MAX_VERSION),
        ("libstdc++", "CXXABI_", CXXABI_MAX_VERSION),
        ("libgcc", "GCC_", LIBGCC_MAX_VERSION),
        ("libc", "GLIBC_", GLIBC_MAX_VERSION),
    )

    unwanted = {}
    try:
        for sym in at_least_one(iter_elf_symbols(binary)):
            # Only check versions on undefined symbols
            if sym["addr"] != 0:
                continue

            # No version to check
            if not sym["version"]:
                continue

            for _, prefix, max_version in checks:
                if sym["version"].startswith(prefix):
                    version = Version(sym["version"][len(prefix) :])
                    if version > max_version:
                        unwanted.setdefault(prefix, []).append(sym)
    except Empty:
        raise RuntimeError("Could not parse readelf output?")
    if unwanted:
        error = []
        for lib, prefix, _ in checks:
            if prefix in unwanted:
                error.append(f"We do not want these {lib} symbol versions to be used:")
                error.extend(
                    " {} ({})".format(s["name"], s["version"]) for s in unwanted[prefix]
                )
        raise RuntimeError("\n".join(error))


def check_textrel(binary):
    if get_type(binary) != ELF:
        raise Skip()
    try:
        for tag, value in at_least_one(iter_readelf_dynamic(binary)):
            if tag == "TEXTREL" or (tag == "FLAGS" and "TEXTREL" in value):
                raise RuntimeError(
                    "We do not want text relocations in libraries and programs"
                )
    except Empty:
        raise RuntimeError("Could not parse readelf output?")


def ishex(s):
    try:
        int(s, 16)
        return True
    except ValueError:
        return False


def is_libxul(binary):
    basename = os.path.basename(binary).lower()
    return "xul" in basename


def check_pt_load(binary):
    if get_type(binary) != ELF or not is_libxul(binary):
        raise Skip()
    count = 0
    for line in get_output(READELF, "-l", binary):
        data = line.split()
        if data and data[0] == "LOAD":
            count += 1
    if count <= 1:
        raise RuntimeError("Expected more than one PT_LOAD segment")


def check_mozglue_order(binary):
    if PLATFORM != "Android":
        raise Skip()
    # While this is very unlikely (libc being added by the compiler at the end
    # of the linker command line), if libmozglue.so ends up after libc.so, all
    # hell breaks loose, so better safe than sorry, and check it's actually the
    # case.
    try:
        mozglue = libc = None
        for n, (tag, value) in enumerate(at_least_one(iter_readelf_dynamic(binary))):
            if tag == "NEEDED":
                if "[libmozglue.so]" in value:
                    mozglue = n
                elif "[libc.so]" in value:
                    libc = n
        if libc is None:
            raise RuntimeError("libc.so is not linked?")
        if mozglue is not None and libc < mozglue:
            raise RuntimeError("libmozglue.so must be linked before libc.so")
    except Empty:
        raise RuntimeError("Could not parse readelf output?")


def check_networking(binary):
    retcode = 0
    networking_functions = set(
        [
            # socketpair is not concerning; it is restricted to AF_UNIX
            "recv",
            "send",
            # We would be concerned by recvmsg and sendmsg; but we believe
            # they are okay as documented in 1376621#c23
            "gethostbyname",
            "gethostbyaddr",
            "gethostent",
            "sethostent",
            "endhostent",
            "gethostent_r",
            "gethostbyname2",
            "gethostbyaddr_r",
            "gethostbyname_r",
            "gethostbyname2_r",
            "getservent",
            "getservbyname",
            "getservbyport",
            "setservent",
            "getprotoent",
            "getprotobyname",
            "getprotobynumber",
            "setprotoent",
            "endprotoent",
        ]
    )
    # These are used by the crash monitor & crash monitor client to talk with
    # the main process on Linux and macOS.
    socket_functions = set(
        [
            "connect",
            "accept",
            "listen",
        ]
    )

    if PLATFORM == "WINNT":
        networking_functions |= socket_functions

    bad_occurences_names = set()

    try:
        for sym in at_least_one(iter_elf_symbols(binary, all=True)):
            if sym["addr"] == 0 and sym["name"] in networking_functions:
                bad_occurences_names.add(sym["name"])
    except Empty:
        raise RuntimeError("Could not parse readelf output?")

    basename = os.path.basename(binary)
    if bad_occurences_names:
        s = (
            "TEST-UNEXPECTED-FAIL | check_networking | {} | Identified {} "
            + "networking function(s) being imported in the rust static library ({})"
        )
        print(
            s.format(
                basename,
                len(bad_occurences_names),
                ",".join(sorted(bad_occurences_names)),
            ),
            file=sys.stderr,
        )
        retcode = 1
    elif buildconfig.substs.get("MOZ_AUTOMATION"):
        print(f"TEST-PASS | check_networking | {basename}")
    return retcode


def checks(binary):
    # The clang-plugin is built as target but is really a host binary.
    # Cheat and pretend we weren't called.
    if "clang-plugin" in binary:
        return 0
    checks = []
    if buildconfig.substs.get("MOZ_STDCXX_COMPAT") and PLATFORM == "Linux":
        checks.append(check_binary_compat)

    # Disabled for local builds because of readelf performance: See bug 1472496
    if not buildconfig.substs.get("DEVELOPER_OPTIONS"):
        checks.append(check_textrel)
        checks.append(check_pt_load)
        checks.append(check_mozglue_order)

    retcode = 0
    basename = os.path.basename(binary)
    for c in checks:
        try:
            name = c.__name__
            c(binary)
            if buildconfig.substs.get("MOZ_AUTOMATION"):
                print(f"TEST-PASS | {name} | {basename}")
        except Skip:
            pass
        except RuntimeError as e:
            print(
                f"TEST-UNEXPECTED-FAIL | {name} | {basename} | {str(e)}",
                file=sys.stderr,
            )
            retcode = 1
    return retcode


def main(args):
    parser = argparse.ArgumentParser(description="Check built binaries")

    parser.add_argument(
        "--networking",
        action="store_true",
        help="Perform checks for networking functions",
    )

    parser.add_argument(
        "binary", metavar="PATH", help="Location of the binary to check"
    )

    options = parser.parse_args(args)

    if options.networking:
        return check_networking(options.binary)
    return checks(options.binary)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
