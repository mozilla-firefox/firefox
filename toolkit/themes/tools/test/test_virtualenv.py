# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import ast
from pathlib import Path

import buildconfig
import mozunit
from mach.requirements import MachEnvRequirements

MACH_COMMANDS = Path(__file__).parent.parent / "mach_commands.py"


def subcommand_virtualenvs():
    tree = ast.parse(MACH_COMMANDS.read_text())
    for node in ast.walk(tree):
        if not isinstance(node, ast.FunctionDef):
            continue
        for decorator in node.decorator_list:
            if not (
                isinstance(decorator, ast.Call)
                and getattr(decorator.func, "id", None) == "SubCommand"
            ):
                continue
            for keyword in decorator.keywords:
                if keyword.arg == "virtualenv_name":
                    yield decorator.args[1].value, keyword.value.value


def test_subcommand_virtualenvs_have_pillow():
    virtualenvs = list(subcommand_virtualenvs())
    assert virtualenvs
    for subcommand, name in virtualenvs:
        requirements = MachEnvRequirements.from_requirements_definition(
            buildconfig.topsrcdir,
            False,
            False,
            Path(buildconfig.topsrcdir) / "python" / "sites" / f"{name}.txt",
        )
        packages = {r.requirement.name.lower() for r in requirements.pypi_requirements}
        assert "pillow" in packages, f"{subcommand}: {name} has no Pillow"


if __name__ == "__main__":
    mozunit.main()
