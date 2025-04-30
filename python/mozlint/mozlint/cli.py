# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import atexit
import os
import sys
import tempfile
from argparse import REMAINDER, SUPPRESS, ArgumentParser
from pathlib import Path

from mozlint.errors import NoValidLinter
from mozlint.formatters import all_formatters


class MozlintParser(ArgumentParser):
    arguments = [
        [
            ["paths"],
            {
                "nargs": "*",
                "default": None,
                "help": "Paths to file or directories to lint, like "
                "'browser/components/loop' or 'mobile/android'. "
                "If not provided, defaults to the files changed according "
                "to --outgoing and --workdir.",
            },
        ],
        [
            ["-l", "--linter"],
            {
                "dest": "linters",
                "default": [],
                "action": "append",
                "help": "Linters to run, e.g 'eslint'. By default all linters "
                "are run for all the appropriate files.",
            },
        ],
        [
            ["--list"],
            {
                "dest": "list_linters",
                "default": False,
                "action": "store_true",
                "help": "List all available linters and exit.",
            },
        ],
        [
            ["-W", "--warnings"],
            {
                "const": True,
                "nargs": "?",
                "choices": ["soft"],
                "dest": "show_warnings",
                "help": "Display and fail on warnings in addition to errors. "
                "--warnings=soft can be used to report warnings but only fail "
                "on errors.",
            },
        ],
        [
            ["-v", "--verbose"],
            {
                "dest": "show_verbose",
                "default": False,
                "action": "store_true",
                "help": "Enable verbose logging.",
            },
        ],
        [
            ["-f", "--format"],
            {
                "dest": "formats",
                "action": "append",
                "help": "Formatter to use. Defaults to 'stylish' on stdout. "
                "You can specify an optional path as --format formatter:path "
                "that will be used instead of stdout. "
                "You can also use multiple formatters at the same time. "
                "Formatters available: {}.".format(", ".join(all_formatters.keys())),
            },
        ],
        [
            ["-n", "--no-filter"],
            {
                "dest": "use_filters",
                "default": True,
                "action": "store_false",
                "help": "Ignore all filtering. This is useful for quickly "
                "testing a directory that otherwise wouldn't be run, "
                "without needing to modify the config file.",
            },
        ],
        [
            ["--include-third-party"],
            {
                "dest": "include_third-party",
                "default": False,
                "action": "store_true",
                "help": "Also run the linter(s) on third-party code",
            },
        ],
        [
            ["-o", "--outgoing"],
            {
                "const": True,
                "nargs": "?",
                "help": "Lint files touched by commits that are not on the remote repository. "
                "Without arguments, finds the default remote that would be pushed to. "
                "The remote branch can also be specified manually. Works with "
                "mercurial or git.",
            },
        ],
        [
            ["-w", "--workdir"],
            {
                "const": "all",
                "nargs": "?",
                "choices": ["staged", "all"],
                "help": "Lint files touched by changes in the working directory "
                "(i.e haven't been committed yet). On git, --workdir=staged "
                "can be used to only consider staged files. Works with "
                "mercurial or git.",
            },
        ],
        [
            ["-r", "--rev"],
            {
                "default": None,
                "type": str,
                "help": "Lint files touched by changes in revisions described by REV. "
                "For mercurial, it may be any revset. For git, it is a single tree-ish.",
            },
        ],
        [
            ["--stdin-filename"],
            {
                "default": None,
                "type": str,
                "help": "Lint a file passed in via stdin. The value is the "
                "relative path of the file from the repo root. This is useful "
                "for some editor or vcs integrations.",
            },
        ],
        [
            ["--dump-stdin-file"],
            {
                "default": None,
                "nargs": "?",
                "const": True,
                "help": "If a file was passed in via --stdin-filename, this flag will "
                "cause the resulting file (after applying any fixes) to be dumped to "
                "the specified file path. If no value is provided, stdout will be used.",
            },
        ],
        [
            ["--fix"],
            {
                "action": "store_true",
                "default": False,
                "help": "Fix lint errors if possible. Any errors that could not be fixed "
                "will be printed as normal.",
            },
        ],
        [
            ["--edit"],
            {
                "action": "store_true",
                "default": False,
                "help": "Each file containing lint errors will be opened in $EDITOR one after "
                "the other.",
            },
        ],
        [
            ["--setup"],
            {
                "action": "store_true",
                "default": False,
                "help": "Bootstrap linter dependencies without running any of the linters.",
            },
        ],
        [
            ["-j", "--jobs"],
            {
                "default": None,
                "dest": "num_procs",
                "type": int,
                "help": "Number of worker processes to spawn when running linters. "
                "Defaults to the number of cores in your CPU.",
            },
        ],
        # Paths to check for linter configurations.
        # Default: tools/lint set in tools/lint/mach_commands.py
        [
            ["--config-path"],
            {
                "action": "append",
                "default": [],
                "dest": "config_paths",
                "help": SUPPRESS,
            },
        ],
        [
            ["--check-exclude-list"],
            {
                "dest": "check_exclude_list",
                "default": False,
                "action": "store_true",
                "help": "Run linters for all the paths in the exclude list.",
            },
        ],
        [
            ["extra_args"],
            {
                "nargs": REMAINDER,
                "help": "Extra arguments that will be forwarded to the underlying linter.",
            },
        ],
    ]

    def __init__(self, **kwargs):
        ArgumentParser.__init__(self, usage=self.__doc__, **kwargs)

        for cli, args in self.arguments:
            self.add_argument(*cli, **args)

    def parse_known_args(self, *args, **kwargs):
        # Allow '-wo' or '-ow' as shorthand for both --workdir and --outgoing.
        for token in ("-wo", "-ow"):
            if token in args[0]:
                i = args[0].index(token)
                args[0].pop(i)
                args[0][i:i] = [token[:2], "-" + token[2]]

        # This is here so the eslint mach command doesn't lose 'extra_args'
        # when using mach's dispatch functionality.
        args, extra = ArgumentParser.parse_known_args(self, *args, **kwargs)
        args.extra_args = extra

        self.validate(args)
        return args, extra

    def validate(self, args):
        if args.edit and not os.environ.get("EDITOR"):
            self.error("must set the $EDITOR environment variable to use --edit")

        if args.stdin_filename and (
            args.paths or args.workdir or args.outgoing or args.rev
        ):
            self.error("can't read from both stdin and file system at the same time")

        invalid = None
        if args.paths:
            invalid = [p for p in args.paths if not os.path.exists(p)]

        if args.stdin_filename and not os.path.exists(args.stdin_filename):
            invalid = [args.stdin_filename]

        if invalid:
            s_do = " does" if len(invalid) == 1 else "s do"
            invalid = "\n".join(invalid)
            self.error(f"the following path{s_do} not exist:\n{invalid}")

        if args.dump_stdin_file and not args.stdin_filename:
            self.error("must specify --stdin-filename alongside --dump-stdin-file")

        if args.formats:
            formats = []
            for fmt in args.formats:
                if isinstance(fmt, tuple):  # format is already processed
                    formats.append(fmt)
                    continue

                path = None
                if ":" in fmt:
                    # Detect optional formatter path
                    pos = fmt.index(":")
                    fmt, path = fmt[:pos], os.path.realpath(fmt[pos + 1 :])

                    # Check path is writable
                    fmt_dir = os.path.dirname(path)
                    if not os.access(fmt_dir, os.W_OK | os.X_OK):
                        self.error(
                            f"the following directory is not writable: {fmt_dir}"
                        )

                if fmt not in all_formatters.keys():
                    self.error(f"the following formatter is not available: {fmt}")

                formats.append((fmt, path))
            args.formats = formats
        else:
            # Can't use argparse default or this choice will be always present
            args.formats = [("stylish", None)]


def _remove_file(path):
    try:
        os.remove(path)
    except Exception:
        pass


def find_linters(config_paths, linters=None):
    lints = {}
    for search_path in config_paths:
        if not os.path.isdir(search_path):
            continue

        sys.path.insert(0, search_path)
        files = os.listdir(search_path)
        for f in files:
            name = os.path.basename(f)

            if not name.endswith(".yml"):
                continue

            name = name.rsplit(".", 1)[0]

            if linters and name not in linters:
                continue

            lints[name] = os.path.join(search_path, f)

    linters_not_found = list(set(linters).difference(set(lints.keys())))
    return {"lint_paths": lints.values(), "linters_not_found": linters_not_found}


def get_exclude_list_output(result, paths):
    # Store the paths of all the subdirectories leading to the error files
    error_file_paths = set()
    for issues in result.issues.values():
        error_file = issues[0].relpath
        error_file_paths.add(error_file)
        parent_dir = os.path.dirname(error_file)
        while parent_dir:
            error_file_paths.add(parent_dir)
            parent_dir = os.path.dirname(parent_dir)

    paths = [os.path.dirname(path) if path[-1] == "/" else path for path in paths]
    # Remove all the error paths to get the list of green paths
    green_paths = sorted(set(paths).difference(error_file_paths))

    if green_paths:
        out = (
            "The following list of paths are now green "
            "and can be removed from the exclude list:\n\n"
        )
        out += "\n".join(green_paths)

    else:
        out = "No path in the exclude list is green."

    return out


def run(
    paths,
    linters,
    formats,
    outgoing,
    workdir,
    rev,
    edit,
    check_exclude_list,
    setup=False,
    list_linters=False,
    num_procs=None,
    virtualenv_manager=None,
    setupargs=None,
    dump_stdin_file=None,
    **lintargs,
):
    from mozlint import LintRoller, formatters
    from mozlint.editor import edit_issues

    lintargs["config_paths"] = [
        os.path.join(lintargs["root"], p) for p in lintargs["config_paths"]
    ]

    # Always perform exhaustive linting for exclude list paths
    lintargs["use_filters"] = lintargs["use_filters"] and not check_exclude_list

    if list_linters:
        lint_paths = find_linters(lintargs["config_paths"], linters)
        linters = [
            os.path.splitext(os.path.basename(l))[0] for l in lint_paths["lint_paths"]
        ]
        print("\n".join(sorted(linters)))
        print(
            "\nNote that clang-tidy checks are not run as part of this "
            "command, but using the static-analysis command."
        )
        return 0

    lint = LintRoller(setupargs=setupargs or {}, **lintargs)
    linters_info = find_linters(lintargs["config_paths"], linters)

    result = None

    stdin_tempfile = None
    if fpath := (lintargs.get("stdin_filename")):
        if lintargs.get("fix") and not dump_stdin_file:
            # If `--fix` is specified alongside `--stdin-filename`, that
            # implies we want to dump the result.
            dump_stdin_file = True

        # We set dir to the same directory as the file it is shadowing because
        # some linters (e.g eslint) walk backwards from the file being linted
        # in order to discover configuration.
        stdin_tempfile = tempfile.NamedTemporaryFile(
            mode="wb",
            delete=False,
            dir=os.path.dirname(fpath),
            suffix=os.path.splitext(fpath)[1],
        )

        # Read directly from stdins byte buffer so that we treat the bytes
        # as-is. Otherwise Python will convert LF to CRLF on Windows.
        stdin_tempfile.write(b"".join(sys.stdin.buffer))
        stdin_tempfile.close()
        paths = [stdin_tempfile.name]
        atexit.register(_remove_file, stdin_tempfile.name)

    old_stdout = None
    if dump_stdin_file:
        # External integrations expect the contents of this filename to be
        # dumped to stdout. If any log output accidentally ends up going to
        # stdout, then that could actually end up being written to the file.
        # Temporarily redirect stdout to stderr to guard against this.
        old_stdout = sys.stdout
        sys.stdout = sys.stderr

    try:
        lint.read(linters_info["lint_paths"])

        if check_exclude_list:
            if len(lint.linters) > 1:
                print("error: specify a single linter to check with `-l/--linter`")
                return 1
            paths = lint.linters[0]["local_exclude"]

        if (
            not paths
            and Path.cwd() == Path(lint.root)
            and not (outgoing or workdir or rev)
        ):
            print(
                "warning: linting the entire repo takes a long time, using --outgoing and "
                "--workdir instead. If you want to lint the entire repo, run `./mach lint .`"
            )
            # Setting the default values
            outgoing = True
            workdir = "all"

        # Always run bootstrapping, but return early if --setup was passed in.
        ret = lint.setup(virtualenv_manager=virtualenv_manager)
        if setup:
            return ret

        if linters_info["linters_not_found"] != []:
            raise NoValidLinter

        # run all linters
        result = lint.roll(
            paths, outgoing=outgoing, workdir=workdir, rev=rev, num_procs=num_procs
        )
    except NoValidLinter as e:
        result = lint.result
        print(str(e))

    # If --dump-stdin-file is passed, simply output the file and return 0
    # regardless of whether there were any problems or not. This is what some
    # external integrations will expect.
    if dump_stdin_file:
        assert stdin_tempfile

        if dump_stdin_file is True:
            sys.stdout = old_stdout
            dump_stdin_file = sys.stdout.buffer
        else:
            dump_stdin_file = open(dump_stdin_file, "wb")

        try:
            with open(stdin_tempfile.name, "rb") as fp:
                dump_stdin_file.write(fp.read())
        finally:
            _remove_file(stdin_tempfile.name)
        return 0

    if edit and result.issues:
        edit_issues(result)
        result = lint.roll(result.issues.keys(), num_procs=num_procs)

    for every in linters_info["linters_not_found"]:
        result.failed_setup.add(every)

    if check_exclude_list:
        # Get and display all those paths in the exclude list which are
        # now green and can be safely removed from the list
        out = get_exclude_list_output(result, paths)
        print(out, file=sys.stdout)
        return result.returncode

    for formatter_name, path in formats:
        formatter = formatters.get(formatter_name)

        out = formatter(result)
        # We do this only for `json` that is mostly used in automation
        if not out and formatter_name == "json":
            out = "{}"

        if out:
            fh = open(path, "w") if path else sys.stdout

            if not path and fh.encoding == "ascii":
                # If sys.stdout.encoding is ascii, printing output will fail
                # due to the stylish formatter's use of unicode characters.
                # Ideally the user should fix their environment by setting
                # `LC_ALL=C.UTF-8` or similar. But this is a common enough
                # problem that we help them out a little here by manually
                # encoding and writing to the stdout buffer directly.
                out += "\n"
                fh.buffer.write(out.encode("utf-8", errors="replace"))
                fh.buffer.flush()
            else:
                print(out, file=fh)

            if path:
                fh.close()

    return result.returncode


def main() -> int:
    parser = MozlintParser()
    args = vars(parser.parse_args())
    return run(**args)


if __name__ == "__main__":
    sys.exit(main())
