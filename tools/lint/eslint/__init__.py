# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), "eslint"))
from mozbuild.nodeutil import check_node_executables_valid, find_node_executable
from mozlint import result

from eslint import prettier_utils, setup_helper

ESLINT_ERROR_MESSAGE = """
An error occurred running eslint. Please check the following error messages:

{}
""".strip()

ESLINT_NOT_FOUND_MESSAGE = """
Could not find eslint!  We looked at the --binary option, at the ESLINT
environment variable, and then at your local node_modules path. Please Install
eslint and needed plugins with:

mach eslint --setup

and try again.
""".strip()


def setup(root, **lintargs):
    setup_helper.set_project_root(root)

    if not check_node_executables_valid():
        return 1

    return setup_helper.eslint_maybe_setup()


def lint(paths, config, binary=None, fix=None, rules=[], setup=None, **lintargs):
    """Run eslint."""
    log = lintargs["log"]
    setup_helper.set_project_root(lintargs["root"])
    module_path = setup_helper.get_project_root()

    # Valid binaries are:
    #  - Any provided by the binary argument.
    #  - Any pointed at by the ESLINT environmental variable.
    #  - Those provided by |mach lint --setup|.

    if not binary:
        binary, _ = find_node_executable()

    if not binary:
        log.error(ESLINT_NOT_FOUND_MESSAGE)
        return 1

    extra_args = lintargs.get("extra_args") or []

    result = {"results": [], "fixed": 0}
    eslint_proc = None

    # If something passes formatonly, avoid running eslint and only run prettier.
    # This allows ./mach format and similar mechanisms to work correctly.
    if not lintargs.get("formatonly", False):
        exclude_args = []
        for path in config.get("global_exclude", []):
            exclude_args.extend(["--ignore-pattern", path])

        for rule in rules:
            extra_args.extend(["--rule", rule])

        i = 0
        extra_args_len = len(extra_args)
        while i < extra_args_len:
            if extra_args[i] == "--rule":
                i += 1
                # ESLint requires that we specify any plugins. These are typically
                # named by the prefix before the first `/`. There are some cases where
                # this doesn't work, but ESLint also can't handle those cases with
                # the `--plugin` argument anyway.
                if i < extra_args_len and "/" in extra_args[i]:
                    extra_args.extend(["--plugin", extra_args[i].split("/", 1)[0]])

            i += 1

        # First run ESLint
        cmd_args = (
            [
                binary,
                os.path.join(module_path, "node_modules", "eslint", "bin", "eslint.js"),
                # This keeps ext as a single argument.
                "--format",
                "json",
                "--no-error-on-unmatched-pattern",
                "--no-warn-ignored",
            ]
            + rules
            # Flat configuration doesn't understand --ignore-path, though Prettier does.
            + list(filter(lambda x: not x.startswith("--ignore-path"), extra_args))
            + exclude_args
            + paths
        )

        if fix:
            # eslint requires that --fix be set before the --ext argument.
            cmd_args.insert(2, "--fix")

        log.debug("ESLint command: {}".format(" ".join(cmd_args)))

        eslint_proc = prettier_utils.start_process(cmd_args)
        if fix:
            result = finish_eslint(eslint_proc, config)
            eslint_proc = None
            if result == 1:
                return result

    # Then run Prettier
    ignore_args = (
        []
        if any(a.startswith("--ignore-path") for a in extra_args)
        else ["--ignore-path=.prettierignore", "--ignore-path=.prettierignore-css"]
    )

    # Don't pass the configuration, plugin or rule information to Prettier,
    # as it doesn't understand those arguments.
    def bypass(arg):
        bypass_list = ["--config", "--plugin", "--rule"]
        return any(not arg.startswith(flag) for flag in bypass_list)

    cmd_args = (
        [
            binary,
            os.path.join(
                module_path, "node_modules", "prettier", "bin", "prettier.cjs"
            ),
            "--list-different",
            "--no-error-on-unmatched-pattern",
            "--ignore-unknown",
        ]
        # Don't pass the configuration, plugin or rule information to Prettier,
        # as it doesn't understand those arguments.
        + list(
            filter(
                lambda x: (
                    not x.startswith("--config")
                    and not x.startswith("--plugin")
                    and not x.startswith("--rule")
                ),
                [arg for arg in extra_args if bypass(arg)],
            )
        )
        # Prettier does not support exclude arguments.
        # + exclude_args
        + ignore_args
        + paths
    )
    log.debug("Prettier command: {}".format(" ".join(cmd_args)))

    if fix:
        cmd_args.append("--write")

    prettier_proc = prettier_utils.start_process(cmd_args)
    if eslint_proc is not None:
        result = None
        try:
            result = finish_eslint(eslint_proc, config)
        finally:
            if result in (None, 1):
                prettier_proc.kill()
                prettier_proc.communicate()
        if result == 1:
            return result

    prettier_result = prettier_utils.finish_prettier(prettier_proc, config, fix)
    if prettier_result == 1:
        return prettier_result

    result["results"].extend(prettier_result["results"])
    result["fixed"] = result["fixed"] + prettier_result["fixed"]
    return result


def finish_eslint(proc, config):
    encoding = "utf-8"

    try:
        output, errors = proc.communicate()
    except KeyboardInterrupt:
        proc.kill()
        raise

    if errors:
        errors = errors.decode(encoding, "replace")
        print(ESLINT_ERROR_MESSAGE.format(errors))

    if proc.returncode >= 2:
        return 1

    if not output:
        return {"results": [], "fixed": 0}  # no output means success
    output = output.decode(encoding, "replace")
    try:
        jsonresult = json.loads(output)
    except ValueError:
        print(ESLINT_ERROR_MESSAGE.format(output))
        return 1

    results = []
    fixed = 0
    for obj in jsonresult:
        errors = obj["messages"]
        # This will return a count of files fixed, rather than issues fixed, as
        # that is the only count we have.
        if "output" in obj:
            fixed = fixed + 1

        for err in errors:
            err.update({
                "hint": (
                    "This issue can be fixed automatically with --fix."
                    if err.get("fix")
                    else None
                ),
                "level": "error" if err["severity"] == 2 else "warning",
                "lineno": err.get("line") or 0,
                "path": obj["filePath"],
                "rule": err.get("ruleId"),
            })
            results.append(result.from_config(config, **err))

    return {"results": results, "fixed": fixed}
