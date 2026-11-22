# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import ast
from pathlib import Path

from buildconfig import topsrcdir
from mozunit import main

from mach.command_util import MACH_COMMANDS


def literal_str(node):
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value
    return None


def parse_module(module):
    path = Path(topsrcdir) / module
    if not path.exists():
        return None
    return ast.parse(path.read_text(encoding="utf-8"), filename=str(path))


def declared_subcommands(tree):
    """Returns the (command, subcommand) pairs declared with @SubCommand or
    @decorators.SubCommand."""
    found = set()
    for node in ast.walk(tree):
        if (
            isinstance(node, ast.Call)
            and "SubCommand"
            in (getattr(node.func, "id", None), getattr(node.func, "attr", None))
            and len(node.args) >= 2
        ):
            command, subcommand = map(literal_str, node.args[:2])
            if command and subcommand:
                found.add((command, subcommand))
    return found


def dispatched_subcommands(tree):
    """Yields (lineno, command, subcommand) for each dispatch call that names a
    subcommand with string literals."""
    for node in ast.walk(tree):
        if not (
            isinstance(node, ast.Call)
            and getattr(node.func, "attr", None) == "dispatch"
            and node.args
        ):
            continue
        command = literal_str(node.args[0])
        for keyword in node.keywords:
            if keyword.arg == "subcommand":
                subcommand = literal_str(keyword.value)
                if command and subcommand:
                    yield node.lineno, command, subcommand


def test_dispatched_subcommands_exist():
    trees = {}
    for module in {ref.module for ref in MACH_COMMANDS.values()}:
        tree = parse_module(module)
        if tree:
            trees[module] = tree

    checked = 0
    for module, tree in sorted(trees.items()):
        for lineno, command, subcommand in dispatched_subcommands(tree):
            checked += 1
            ref = MACH_COMMANDS.get(command)
            assert ref, f"{module}:{lineno} dispatches unknown command {command!r}"
            declared = declared_subcommands(trees[ref.module])
            assert (command, subcommand) in declared, (
                f"{module}:{lineno} dispatches `mach {command} {subcommand}`, "
                f"but {ref.module} does not declare that subcommand"
            )

    assert checked, "Expected to find dispatch calls that name a subcommand"


if __name__ == "__main__":
    main()
