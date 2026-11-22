# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Report which source directories the PGO training run executes code from,
to find what the training corpus misses, by matching the functions of a
profile with the Breakpad symbols of the instrumented build.

Usage: pgo_coverage.py [--upload-dir DIR] <llvm-profdata> <merged.profdata>
                       <symbols directory>
"""

import argparse
import glob
import os
import re
import subprocess
from collections import Counter

from pgo_metrics import covered_functions, suite, write_perfherder

# Source paths as toolkit/crashreporter/tools/symbolstore.py writes them, with
# the repository of the push, e.g. hg.mozilla.org/try.
REPO_FILE = re.compile(r"^(?:hg|git):([^:]+):([^:]+):")
GENERATED_FILE = re.compile(r"^s3:gecko-generated-sources[^:]*:[0-9a-f]+/([^:]+):")
RUST_REPO = "github.com/rust-lang/rust"
RUST_HASH = re.compile(r"::h[0-9a-f]{16}$")
RUST_ESCAPES = {
    "LT": "<",
    "GT": ">",
    "LP": "(",
    "RP": ")",
    "RF": "&",
    "BP": "*",
    "C": ",",
    "SP": "@",
}
RUST_ESCAPE = re.compile(r"\$(LT|GT|LP|RP|RF|BP|C|SP|u[0-9a-f]+)\$")

# Source directories with fewer functions are only in the report, not in
# Perfherder.
PERFHERDER_MIN_FUNCTIONS = 500

UNMATCHED = "(unmatched)"
OUTSIDE_TREE = "(outside tree)"


def source_dir(path):
    if (m := REPO_FILE.match(path)) and m.group(1) != RUST_REPO:
        return os.path.dirname(m.group(2))
    if m := GENERATED_FILE.match(path):
        return "(generated)/" + os.path.dirname(m.group(1))
    return OUTSIDE_TREE


def strip_trailing_group(name, open, close):
    if not name.endswith(close):
        return name
    depth = 0
    for i in range(len(name) - 1, -1, -1):
        if name[i] == close:
            depth += 1
        elif name[i] == open:
            depth -= 1
            if depth == 0:
                return name[:i]
    return name


def function_key(name):
    """Normalize a demangled name for the differences between llvm-cxxfilt
    and dump_syms: drop the parameters, the qualifiers and the template
    arguments of the function itself."""
    # The ref-qualifier comes after the cv-qualifiers.
    for qualifier in (" &&", " &", " volatile", " const"):
        name = name.removesuffix(qualifier)
    if not name.endswith("operator()"):
        name = strip_trailing_group(name, "(", ")")
    if not name.endswith(("operator>", "operator->", "operator>>")):
        name = strip_trailing_group(name, "<", ">")
    return name


def unescape_rust(name):
    """Finish demangling the Rust legacy symbols that llvm-cxxfilt demangles
    as C++, the way dump_syms does."""
    if "$" not in name and ".." not in name:
        return name
    name = RUST_ESCAPE.sub(
        lambda m: RUST_ESCAPES.get(m.group(1)) or chr(int(m.group(1)[1:], 16)),
        name.replace("..", "::"),
    )
    return name.removeprefix("_").replace("::_<", "::<")


def parse_symbols(path, function_dirs):
    """Add the source directory of each function of a .sym file, including
    the functions that only exist inlined, to function_dirs."""
    files = {}
    origins = {}
    name = file = None
    # Inlined functions by address, to get the file of the innermost one from
    # the line record at that address.
    inlined = {}

    def add(name, file):
        function_dirs.setdefault(function_key(name), files.get(file, OUTSIDE_TREE))

    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("FILE "):
                _, file_id, file_path = line.rstrip("\n").split(" ", 2)
                files[file_id] = source_dir(file_path)
            elif line.startswith("FUNC "):
                if name is not None and file is not None:
                    add(name, file)
                fields = line.rstrip("\n").split(" ", 5)
                name = fields[5] if fields[1] == "m" else " ".join(fields[4:])
                file = None
                inlined = {}
            elif line.startswith("INLINE_ORIGIN "):
                _, origin_id, origin = line.rstrip("\n").split(" ", 2)
                origins[origin_id] = origin
            elif line.startswith("INLINE "):
                _, depth, _, call_file, origin, address, _ = line.split(" ", 6)
                if file is None and depth == "0":
                    file = call_file
                if inlined.get(address, (-1,))[0] < int(depth):
                    inlined[address] = (int(depth), origin)
            elif name is not None and line[0] in "0123456789abcdef":
                address, _, _, line_file = line.rstrip("\n").split(" ", 3)
                if file is None:
                    file = line_file
                if inlined and (origin := inlined.pop(address, None)):
                    add(origins[origin[1]], line_file)
    if name is not None and file is not None:
        add(name, file)


def profile_functions(llvm_profdata, profdata, covered=False):
    """Return the mangled names of the functions in the profile, only those
    the training run executed if covered is true."""
    if covered:
        names = covered_functions(llvm_profdata, profdata)
    else:
        output = subprocess.check_output(
            [llvm_profdata, "show", "--all-functions", profdata],
            text=True,
            timeout=600,
        )
        names = [
            line[2:-1]
            for line in output.splitlines()
            if line.startswith("  ") and line[2] != " " and line.endswith(":")
        ]
    # Local functions are prefixed with their file name.
    return [name.rpartition(";")[2] for name in names if name]


def run_demangler(command, names, check=True):
    return subprocess.run(
        command,
        input="".join(name + "\n" for name in names),
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        check=check,
        timeout=600,
    ).stdout


def parse_undname(output):
    """Return the names demangled by llvm-undname by mangled name. It prints
    each mangled name and, when it can demangle it, the demangled name,
    followed by an empty line, and fails if it can't demangle one of them."""
    names = {}
    for block in output.split("\n\n"):
        mangled, _, name = block.strip("\n").partition("\n")
        if name:
            names[mangled] = name
    return names


def demangle(llvm_bin, names):
    """Demangle the Itanium and Rust names with llvm-cxxfilt, and the MSVC
    ones, on Windows, with llvm-undname."""
    msvc = [name for name in names if name.startswith("?")]
    other = [name for name in names if not name.startswith("?")]
    demangled = {}
    if msvc:
        demangled = parse_undname(
            run_demangler(
                [
                    llvm_bin("llvm-undname"),
                    "--no-access-specifier",
                    "--no-calling-convention",
                    "--no-member-type",
                    "--no-return-type",
                ],
                msvc,
                check=False,
            )
        )
    if other:
        output = run_demangler([llvm_bin("llvm-cxxfilt"), "--no-params"], other)
        demangled.update(zip(other, output.splitlines()))
    return [
        function_key(unescape_rust(RUST_HASH.sub("", demangled.get(name, name))))
        for name in names
    ]


def function_coverage(llvm_profdata, profdata, symbols_dir):
    """Return the source directory of each function in the profile, and
    whether the training run executed it."""

    def llvm_bin(tool):
        return os.path.join(
            os.path.dirname(llvm_profdata),
            tool + (".exe" if llvm_profdata.endswith(".exe") else ""),
        )

    function_dirs = {}
    sym_files = glob.glob(os.path.join(symbols_dir, "**", "*.sym"), recursive=True)
    # Functions are matched by name, so start with the largest library.
    for sym_file in sorted(sym_files, key=os.path.getsize, reverse=True):
        # The JS shell is not part of the package.
        if os.path.basename(sym_file) != "js.sym":
            parse_symbols(sym_file, function_dirs)

    names = set(demangle(llvm_bin, profile_functions(llvm_profdata, profdata)))
    covered = set(demangle(llvm_bin, profile_functions(llvm_profdata, profdata, True)))
    return [
        (function_dirs.get(name, UNMATCHED), name in covered)
        for name in names
        if not name.startswith(("non-virtual thunk to ", "virtual thunk to "))
    ]


def directory_coverage(functions, depth):
    """Return Counters of the functions, and of those the training run
    executed, by source directory truncated to depth."""
    total = Counter()
    executed = Counter()
    for path, covered in functions:
        directory = "/".join(path.split("/")[:depth])
        total[directory] += 1
        executed[directory] += covered
    return total, executed


def format_coverage(total, executed):
    lines = [f"{'directory':50} {'executed':>9} {'functions':>9} {'%':>6}"]
    for directory, functions in total.most_common():
        lines.append(
            f"{directory:50} {executed[directory]:9} {functions:9} "
            f"{100 * executed[directory] / functions:6.1f}"
        )
    return "\n".join(lines)


def perfherder_suite(total, executed):
    return suite(
        "pgo coverage",
        100 * sum(executed.values()) / sum(total.values()),
        [
            (directory, 100 * executed[directory] / functions)
            for directory, functions in sorted(total.items())
            if functions >= PERFHERDER_MIN_FUNCTIONS
        ],
        unit="%",
        lowerIsBetter=False,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--upload-dir",
        help="Write pgo-coverage.txt and the Perfherder data to this directory",
    )
    parser.add_argument("llvm_profdata", help="Path to llvm-profdata")
    parser.add_argument("profdata", help="Merged profile of the training run")
    parser.add_argument(
        "symbols_dir", help="Breakpad symbols directory of the instrumented build"
    )
    args = parser.parse_args()

    functions = function_coverage(args.llvm_profdata, args.profdata, args.symbols_dir)
    coverage = {depth: directory_coverage(functions, depth) for depth in (2, 3)}
    report = "\n\n".join(format_coverage(*coverage[depth]) for depth in (2, 3))
    if not args.upload_dir:
        print(report)
        return

    os.makedirs(args.upload_dir, exist_ok=True)
    with open(os.path.join(args.upload_dir, "pgo-coverage.txt"), "w") as f:
        f.write("Functions the PGO training run executed, by source directory.\n\n")
        f.write(report + "\n")
    write_perfherder(
        [perfherder_suite(*coverage[2])],
        os.path.join(args.upload_dir, "perfherder-data-pgo-coverage.json"),
    )


if __name__ == "__main__":
    main()
