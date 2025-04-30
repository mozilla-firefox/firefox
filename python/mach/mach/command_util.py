# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import ast
import difflib
import errno
import importlib.metadata
import shlex
import sys
import types
import uuid
from collections.abc import Iterable
from pathlib import Path
from typing import Dict, Optional, Union

from mozfile import load_source

from .base import MissingFileError, UnknownCommandError

INVALID_ENTRY_POINT = r"""
Entry points should return a list of command providers or directories
containing command providers. The following entry point is invalid:

    %s

You are seeing this because there is an error in an external module attempting
to implement a mach command. Please fix the error, or uninstall the module from
your system.
""".lstrip()


class MachCommandReference:
    """A reference to a mach command.

    Holds the metadata for a mach command.
    """

    module: Path

    def __init__(
        self,
        module: Union[str, Path],
        command_dependencies: Optional[list] = None,
    ):
        self.module = Path(module)
        self.command_dependencies = command_dependencies or []


MACH_COMMANDS = {
    "adb": MachCommandReference("mobile/android/mach_commands.py"),
    "addstory": MachCommandReference("toolkit/content/widgets/mach_commands.py"),
    "addtest": MachCommandReference("testing/mach_commands.py"),
    "addwidget": MachCommandReference("toolkit/content/widgets/mach_commands.py"),
    "android": MachCommandReference("mobile/android/mach_commands.py"),
    "android-emulator": MachCommandReference("mobile/android/mach_commands.py"),
    "android-test": MachCommandReference("testing/android-test/mach_commands.py"),
    "artifact": MachCommandReference(
        "python/mozbuild/mozbuild/artifact_commands.py",
    ),
    "awsy-test": MachCommandReference("testing/awsy/mach_commands.py"),
    "bootstrap": MachCommandReference(
        "python/mozboot/mozboot/mach_commands.py",
    ),
    "browsertime": MachCommandReference("tools/browsertime/mach_commands.py"),
    "build": MachCommandReference(
        "python/mozbuild/mozbuild/build_commands.py",
    ),
    "build-backend": MachCommandReference(
        "python/mozbuild/mozbuild/build_commands.py",
    ),
    "buildsymbols": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "buildtokens": MachCommandReference("toolkit/content/widgets/mach_commands.py"),
    "busted": MachCommandReference("tools/mach_commands.py"),
    "cargo": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "clang-format": MachCommandReference(
        "python/mozbuild/mozbuild/code_analysis/mach_commands.py"
    ),
    "clang-tidy": MachCommandReference(
        "python/mozbuild/mozbuild/code_analysis/mach_commands.py"
    ),
    "clobber": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "compare-locales": MachCommandReference("tools/compare-locales/mach_commands.py"),
    "compileflags": MachCommandReference(
        "python/mozbuild/mozbuild/compilation/codecomplete.py"
    ),
    "configure": MachCommandReference("python/mozbuild/mozbuild/build_commands.py"),
    "cppunittest": MachCommandReference("testing/mach_commands.py"),
    "crashtest": MachCommandReference("layout/tools/reftest/mach_commands.py"),
    "data-review": MachCommandReference(
        "toolkit/components/glean/build_scripts/mach_commands.py"
    ),
    "doc": MachCommandReference("tools/moztreedocs/mach_commands.py"),
    "doctor": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "environment": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "eslint": MachCommandReference("tools/lint/mach_commands.py"),
    "event-into-legacy": MachCommandReference(
        "toolkit/components/glean/build_scripts/mach_commands.py"
    ),
    "fetch-condprofile": MachCommandReference("testing/condprofile/mach_commands.py"),
    "file-info": MachCommandReference(
        "python/mozbuild/mozbuild/frontend/mach_commands.py"
    ),
    "firefox-ui-functional": MachCommandReference(
        "testing/firefox-ui/mach_commands.py"
    ),
    "fluent-migration-test": MachCommandReference("testing/mach_commands.py"),
    "format": MachCommandReference("tools/lint/mach_commands.py"),
    "geckodriver": MachCommandReference("testing/geckodriver/mach_commands.py"),
    "geckoview-junit": MachCommandReference(
        "testing/mochitest/mach_commands.py", ["test"]
    ),
    "gen-uuid": MachCommandReference("dom/base/mach_commands.py"),
    "gen-use-counter-metrics": MachCommandReference("dom/base/mach_commands.py"),
    "generate-test-certs": MachCommandReference(
        "security/manager/tools/mach_commands.py"
    ),
    "gifft": MachCommandReference(
        "toolkit/components/telemetry/build_scripts/mach_commands.py"
    ),
    "glean": MachCommandReference(
        "toolkit/components/glean/build_scripts/mach_commands.py"
    ),
    "gradle": MachCommandReference("mobile/android/mach_commands.py"),
    "gradle-install": MachCommandReference("mobile/android/mach_commands.py"),
    "gtest": MachCommandReference(
        "python/mozbuild/mozbuild/mach_commands.py", ["test"]
    ),
    "hazards": MachCommandReference("js/src/devtools/rootAnalysis/mach_commands.py"),
    "ide": MachCommandReference("python/mozbuild/mozbuild/backend/mach_commands.py"),
    "import-pr": MachCommandReference("tools/vcs/mach_commands.py"),
    "install": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "install-moz-phab": MachCommandReference("tools/phabricator/mach_commands.py"),
    "jit-test": MachCommandReference("testing/mach_commands.py"),
    "jsapi-tests": MachCommandReference("testing/mach_commands.py"),
    "jsshell-bench": MachCommandReference("testing/mach_commands.py"),
    "jstestbrowser": MachCommandReference("layout/tools/reftest/mach_commands.py"),
    "jstests": MachCommandReference("testing/mach_commands.py"),
    "lint": MachCommandReference("tools/lint/mach_commands.py"),
    "logspam": MachCommandReference("tools/mach_commands.py"),
    "mach-commands": MachCommandReference("python/mach/mach/commands/commandinfo.py"),
    "mach-completion": MachCommandReference("python/mach/mach/commands/commandinfo.py"),
    "mach-debug-commands": MachCommandReference(
        "python/mach/mach/commands/commandinfo.py"
    ),
    "macos-sign": MachCommandReference("tools/signing/macos/mach_commands.py"),
    "manifest": MachCommandReference("testing/mach_commands.py"),
    "marionette-test": MachCommandReference("testing/marionette/mach_commands.py"),
    "mochitest": MachCommandReference("testing/mochitest/mach_commands.py", ["test"]),
    "mots": MachCommandReference("tools/mach_commands.py"),
    "mozbuild-reference": MachCommandReference(
        "python/mozbuild/mozbuild/frontend/mach_commands.py",
    ),
    "mozharness": MachCommandReference("testing/mozharness/mach_commands.py"),
    "mozregression": MachCommandReference("tools/mach_commands.py"),
    "newtab": MachCommandReference("browser/extensions/newtab/mach_commands.py"),
    "node": MachCommandReference("tools/mach_commands.py"),
    "npm": MachCommandReference("tools/mach_commands.py"),
    "package": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "package-multi-locale": MachCommandReference(
        "python/mozbuild/mozbuild/mach_commands.py"
    ),
    "pastebin": MachCommandReference("tools/mach_commands.py"),
    "perf-data-review": MachCommandReference(
        "toolkit/components/glean/build_scripts/mach_commands.py"
    ),
    "perftest": MachCommandReference("python/mozperftest/mozperftest/mach_commands.py"),
    "perftest-test": MachCommandReference(
        "python/mozperftest/mozperftest/mach_commands.py",
    ),
    "perftest-tools": MachCommandReference(
        "python/mozperftest/mozperftest/mach_commands.py"
    ),
    "power": MachCommandReference("tools/power/mach_commands.py"),
    "prettier": MachCommandReference("tools/lint/mach_commands.py"),
    "puppeteer-test": MachCommandReference("remote/mach_commands.py"),
    "python": MachCommandReference("python/mach_commands.py"),
    "python-test": MachCommandReference("python/mach_commands.py"),
    "raptor": MachCommandReference("testing/raptor/mach_commands.py"),
    "raptor-test": MachCommandReference("testing/raptor/mach_commands.py"),
    "reftest": MachCommandReference("layout/tools/reftest/mach_commands.py"),
    "release": MachCommandReference("python/mozrelease/mozrelease/mach_commands.py"),
    "release-history": MachCommandReference("taskcluster/mach_commands.py"),
    "remote": MachCommandReference("remote/mach_commands.py"),
    "repackage": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "resource-usage": MachCommandReference(
        "python/mozbuild/mozbuild/build_commands.py",
    ),
    "run": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "run-condprofile": MachCommandReference("testing/condprofile/mach_commands.py"),
    "rusttests": MachCommandReference("testing/mach_commands.py"),
    "settings": MachCommandReference("python/mach/mach/commands/settings.py"),
    "show-log": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "static-analysis": MachCommandReference(
        "python/mozbuild/mozbuild/code_analysis/mach_commands.py"
    ),
    "storybook": MachCommandReference(
        "browser/components/storybook/mach_commands.py", ["run"]
    ),
    "talos-test": MachCommandReference("testing/talos/mach_commands.py"),
    "taskcluster-build-image": MachCommandReference("taskcluster/mach_commands.py"),
    "taskcluster-image-digest": MachCommandReference("taskcluster/mach_commands.py"),
    "taskcluster-load-image": MachCommandReference("taskcluster/mach_commands.py"),
    "taskgraph": MachCommandReference("taskcluster/mach_commands.py"),
    "telemetry-tests-client": MachCommandReference(
        "toolkit/components/telemetry/tests/marionette/mach_commands.py"
    ),
    "test": MachCommandReference("testing/mach_commands.py"),
    "test-info": MachCommandReference("testing/mach_commands.py"),
    "test-interventions": MachCommandReference(
        "testing/webcompat/mach_commands.py",
    ),
    "tps-build": MachCommandReference("testing/tps/mach_commands.py"),
    "try": MachCommandReference("tools/tryselect/mach_commands.py"),
    "ts": MachCommandReference("tools/ts/mach_commands.py"),
    "uniffi": MachCommandReference(
        "toolkit/components/uniffi-bindgen-gecko-js/mach_commands.py"
    ),
    "update-glean": MachCommandReference(
        "toolkit/components/glean/build_scripts/mach_commands.py"
    ),
    "update-glean-tags": MachCommandReference(
        "toolkit/components/glean/build_scripts/mach_commands.py"
    ),
    "update-test": MachCommandReference("testing/update/mach_commands.py"),
    "use-moz-src": MachCommandReference("tools/use-moz-src/mach_commands.py"),
    "valgrind-test": MachCommandReference("build/valgrind/mach_commands.py"),
    "vcs-setup": MachCommandReference(
        "python/mozboot/mozboot/mach_commands.py",
    ),
    "vendor": MachCommandReference(
        "python/mozbuild/mozbuild/vendor/mach_commands.py",
    ),
    "warnings-list": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
    "warnings-summary": MachCommandReference(
        "python/mozbuild/mozbuild/mach_commands.py"
    ),
    "watch": MachCommandReference(
        "python/mozbuild/mozbuild/mach_commands.py",
    ),
    "web-platform-tests": MachCommandReference(
        "testing/web-platform/mach_commands.py",
    ),
    "web-platform-tests-update": MachCommandReference(
        "testing/web-platform/mach_commands.py",
    ),
    "webidl-example": MachCommandReference("dom/bindings/mach_commands.py"),
    "webidl-parser-test": MachCommandReference("dom/bindings/mach_commands.py"),
    "wpt": MachCommandReference("testing/web-platform/mach_commands.py"),
    "wpt-fetch-logs": MachCommandReference("testing/web-platform/mach_commands.py"),
    "wpt-interop-score": MachCommandReference("testing/web-platform/mach_commands.py"),
    "wpt-manifest-update": MachCommandReference(
        "testing/web-platform/mach_commands.py"
    ),
    "wpt-metadata-merge": MachCommandReference("testing/web-platform/mach_commands.py"),
    "wpt-metadata-summary": MachCommandReference(
        "testing/web-platform/mach_commands.py"
    ),
    "wpt-serve": MachCommandReference("testing/web-platform/mach_commands.py"),
    "wpt-test-paths": MachCommandReference("testing/web-platform/mach_commands.py"),
    "wpt-unittest": MachCommandReference("testing/web-platform/mach_commands.py"),
    "wpt-update": MachCommandReference("testing/web-platform/mach_commands.py"),
    "xpcshell": MachCommandReference("js/xpconnect/mach_commands.py"),
    "xpcshell-test": MachCommandReference(
        "testing/xpcshell/mach_commands.py", ["test"]
    ),
}


class DecoratorVisitor(ast.NodeVisitor):
    def __init__(self):
        self.results = {}

    def visit_FunctionDef(self, node):
        # We only care about `Command` and `SubCommand` decorators, since
        # they are the only ones that can specify virtualenv_name
        decorators = [
            decorator
            for decorator in node.decorator_list
            if isinstance(decorator, ast.Call)
            and isinstance(decorator.func, ast.Name)
            and decorator.func.id in ["SubCommand", "Command"]
        ]

        relevant_kwargs = ["command", "subcommand", "virtualenv_name"]

        for decorator in decorators:
            kwarg_dict = {}

            for name, arg in zip(["command", "subcommand"], decorator.args):
                kwarg_dict[name] = arg.s

            for keyword in decorator.keywords:
                if keyword.arg not in relevant_kwargs:
                    # We only care about these 3 kwargs, so we can safely skip the rest
                    continue

                kwarg_dict[keyword.arg] = getattr(keyword.value, "s", "")

            command = kwarg_dict.pop("command")
            self.results.setdefault(command, {})

            sub_command = kwarg_dict.pop("subcommand", None)
            virtualenv_name = kwarg_dict.pop("virtualenv_name", None)

            if sub_command:
                self.results[command].setdefault("subcommands", {})
                sub_command_dict = self.results[command]["subcommands"].setdefault(
                    sub_command, {}
                )

                if virtualenv_name:
                    sub_command_dict["virtualenv_name"] = virtualenv_name
            elif virtualenv_name:
                # If there is no `subcommand` we are in the `@Command`
                # decorator, and need to store the virtualenv_name for
                # the 'command'.
                self.results[command]["virtualenv_name"] = virtualenv_name

        self.generic_visit(node)


def command_virtualenv_info_for_module(module_path):
    with module_path.open("r") as file:
        content = file.read()

    tree = ast.parse(content)
    visitor = DecoratorVisitor()
    visitor.visit(tree)

    return visitor.results


class DetermineCommandVenvAction(argparse.Action):
    def __init__(
        self,
        option_strings,
        dest,
        topsrcdir,
        required=True,
    ):
        self.topsrcdir = topsrcdir
        argparse.Action.__init__(
            self,
            option_strings,
            dest,
            required=required,
            help=argparse.SUPPRESS,
            nargs=argparse.REMAINDER,
        )

    def __call__(self, parser, namespace, values, option_string=None):
        if len(values) == 0:
            return

        command = values[0]

        aliases = namespace.mach_command_aliases

        if command in aliases:
            alias = aliases[command]
            arg_string = shlex.split(alias)
            command = arg_string.pop(0)

        # the "help" command does not have a module file, it's handled
        # a bit later and should be skipped here.
        if command == "help":
            return

        command_reference = MACH_COMMANDS.get(command)

        if not command_reference:
            # Try to find similarly named commands, may raise UnknownCommandError.
            suggested_command = suggest_command(command)

            sys.stderr.write(
                f"We're assuming the '{command}' command is '{suggested_command}' and we're executing it for you.\n\n"
            )

            command = suggested_command
            command_reference = MACH_COMMANDS.get(command)

        setattr(namespace, "command_name", command)

        if len(values) > 1:
            potential_sub_command_name = values[1]
        else:
            potential_sub_command_name = None

        module_path = Path(self.topsrcdir) / command_reference.module
        module_dict = command_virtualenv_info_for_module(module_path)
        command_dict = module_dict.get(command, {})

        if not command_dict:
            return

        site = command_dict.get("virtualenv_name", "common")

        if potential_sub_command_name and not potential_sub_command_name.startswith(
            "-"
        ):
            all_sub_commands_dict = command_dict.get("subcommands", {})

            if all_sub_commands_dict:
                sub_command_dict = all_sub_commands_dict.get(
                    potential_sub_command_name, {}
                )

                if sub_command_dict:
                    site = sub_command_dict.get("virtualenv_name", "common")

        setattr(namespace, "site_name", site)


def suggest_command(command):
    names = MACH_COMMANDS.keys()
    # We first try to look for a valid command that is very similar to the given command.
    suggested_commands = difflib.get_close_matches(command, names, cutoff=0.8)
    # If we find more than one matching command, or no command at all,
    # we give command suggestions instead (with a lower matching threshold).
    # All commands that start with the given command (for instance:
    # 'mochitest-plain', 'mochitest-chrome', etc. for 'mochitest-')
    # are also included.
    if len(suggested_commands) != 1:
        suggested_commands = set(difflib.get_close_matches(command, names, cutoff=0.5))
        suggested_commands |= {cmd for cmd in names if cmd.startswith(command)}
        raise UnknownCommandError(command, "run", suggested_commands)

    return suggested_commands[0]


def load_commands_from_directory(path: Path):
    """Scan for mach commands from modules in a directory.

    This takes a path to a directory, loads the .py files in it, and
    registers and found mach command providers with this mach instance.
    """
    for f in sorted(path.iterdir()):
        if not f.suffix == ".py" or f.name == "__init__.py":
            continue

        full_path = path / f
        module_name = f"mach.commands.{str(f)[0:-3]}"

        load_commands_from_file(full_path, module_name=module_name)


def load_commands_from_file(path: Union[str, Path], module_name=None):
    """Scan for mach commands from a file.

    This takes a path to a file and loads it as a Python module under the
    module name specified. If no name is specified, a random one will be
    chosen.
    """
    if module_name is None:
        # Ensure parent module is present otherwise we'll (likely) get
        # an error due to unknown parent.
        if "mach.commands" not in sys.modules:
            mod = types.ModuleType("mach.commands")
            sys.modules["mach.commands"] = mod

        module_name = f"mach.commands.{uuid.uuid4().hex}"

    try:
        load_source(module_name, str(path))
    except OSError as e:
        if e.errno != errno.ENOENT:
            raise

        raise MissingFileError(f"{path} does not exist")


def load_commands_from_spec(
    spec: Dict[str, MachCommandReference], topsrcdir: str, missing_ok=False
):
    """Load mach commands based on the given spec.

    Takes a dictionary mapping command names to their metadata.
    """
    modules = {spec[command].module for command in spec}

    for path in modules:
        try:
            load_commands_from_file(Path(topsrcdir) / path)
        except MissingFileError:
            if not missing_ok:
                raise


def load_commands_from_entry_point(group="mach.providers"):
    """Scan installed packages for mach command provider entry points. An
    entry point is a function that returns a list of paths to files or
    directories containing command providers.

    This takes an optional group argument which specifies the entry point
    group to use. If not specified, it defaults to 'mach.providers'.
    """
    for entry in importlib.metadata.entry_points(group=group):
        paths = [Path(path) for path in entry.load()()]
        if not isinstance(paths, Iterable):
            print(INVALID_ENTRY_POINT % entry)
            sys.exit(1)

        for path in paths:
            if path.is_file():
                load_commands_from_file(path)
            elif path.is_dir():
                load_commands_from_directory(path)
            else:
                print(f"command provider '{path}' does not exist")


def load_command_module_from_command_name(command_name: str, topsrcdir: str):
    command_reference = MACH_COMMANDS.get(command_name)
    load_commands_from_spec(
        {command_name: command_reference}, topsrcdir, missing_ok=False
    )
