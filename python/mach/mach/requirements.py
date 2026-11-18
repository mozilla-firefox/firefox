# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import os
import re
from pathlib import Path

from packaging.requirements import Requirement

THUNDERBIRD_PYPI_ERROR = """
Thunderbird requirements definitions cannot include PyPI packages.
""".strip()


class PthSpecifier:
    def __init__(self, path: str):
        self.path = path


class PypiSpecifier:
    def __init__(self, requirement):
        self.requirement = requirement


class PypiOptionalSpecifier(PypiSpecifier):
    def __init__(self, repercussion, requirement):
        super().__init__(requirement)
        self.repercussion = repercussion


class RequirementsTxtSpecifier:
    def __init__(self, path: str):
        self.path = path


class MachEnvRequirements:
    """Requirements associated with a "site dependency manifest", as
    defined in "python/sites/".

    Represents the dependencies of a site. The source files consist
    of colon-delimited fields. The first field
    specifies the action. The remaining fields are arguments to that
    action. The following actions are supported:

    requires-python -- Specifies the minimum and/or maximum Python
        versions supported by this site.

    pth -- Adds the path given as argument to "mach.pth" under
        the virtualenv's site packages directory.

    pypi -- Fetch the package, plus dependencies, from PyPI.

    pypi-optional -- Attempt to install the package and dependencies from PyPI.
        Continue using the site, even if the package could not be installed.

    requirements-txt -- Specifies a path to a 'requirements.txt' lockfile.
        All requirements from the lockfile will be installed with hash
        validation. All entries must include hashes.

    packages.txt -- Denotes that the specified path is a child manifest. It
        will be read and processed as if its contents were concatenated
        into the manifest being read.

    thunderbird-packages.txt -- Denotes a Thunderbird child manifest.
        Thunderbird child manifests are only activated when working on Thunderbird,
        and they can cannot have "pypi" or "pypi-optional" entries.
    """

    def __init__(self):
        self.requires_python = ""
        self.requirements_paths = []
        self.pth_requirements = []
        self.pypi_requirements = []
        self.pypi_optional_requirements = []
        self.vendored_requirements = []
        self.vendored_fallback_requirements = []
        self.requirements_txt_files = []

    def pths_as_absolute(self, topsrcdir: str):
        return [
            os.path.normcase(Path(topsrcdir) / pth.path)
            for pth in (self.pth_requirements + self.vendored_requirements)
        ]

    def pths_fallback_as_absolute(self, topsrcdir: str):
        return [
            os.path.normcase(Path(topsrcdir) / pth.path)
            for pth in self.vendored_fallback_requirements
        ]

    @classmethod
    def from_requirements_definition(
        cls,
        topsrcdir: str,
        is_thunderbird,
        only_strict_requirements,
        requirements_definition,
    ):
        requirements = cls()
        _parse_mach_env_requirements(
            requirements,
            Path(requirements_definition),
            Path(topsrcdir),
            is_thunderbird,
            only_strict_requirements,
        )
        return requirements


def _parse_mach_env_requirements(
    requirements_output,
    root_requirements_path: Path,
    topsrcdir: Path,
    is_thunderbird,
    only_strict_requirements,
):
    def _parse_requirements_line(
        current_requirements_path: Path, line, line_number, is_thunderbird_packages_txt
    ):
        line = line.strip()
        if not line or line.startswith("#"):
            return

        action, params = line.rstrip().split(":", maxsplit=1)
        if action == "requires-python":
            if requirements_output.requires_python == "":
                requirements_output.requires_python = params
            else:
                raise Exception(
                    f"'requires-python' is already set to "
                    f"'{requirements_output.requires_python}'. "
                    f"Attempted to set it again to '{params}'. "
                    f"Please ensure the file '{root_requirements_path}' "
                    f"contains only one 'requires-python' entry."
                )
        elif action == "pth":
            path = topsrcdir / params
            if not path.exists():
                # In sparse checkouts, not all paths will be populated.
                return

            requirements_output.pth_requirements.append(PthSpecifier(params))
        elif action == "vendored":
            requirements_output.vendored_requirements.append(PthSpecifier(params))
        elif action == "vendored-fallback":
            if is_thunderbird_packages_txt:
                raise Exception(THUNDERBIRD_PYPI_ERROR)

            pypi_pkg, vendored_path, repercussion = params.split(":")
            requirements = topsrcdir / "third_party" / "python" / "requirements.txt"
            with open(requirements) as req:
                content = req.read()
                pattern = re.compile(rf"^({pypi_pkg}==.*) \\$", re.MULTILINE)
                version_matches = pattern.findall(content, re.MULTILINE)
                if len(version_matches) != 1:
                    raise Exception(
                        f"vendored-fallback package {pypi_pkg} is not referenced in {requirements}"
                    )
                (raw_requirement,) = version_matches

            requirements_output.pypi_optional_requirements.append(
                PypiOptionalSpecifier(
                    repercussion,
                    _parse_package_specifier(raw_requirement, only_strict_requirements),
                )
            )

            requirements_output.vendored_fallback_requirements.append(
                PthSpecifier(vendored_path)
            )
        elif action == "packages.txt":
            _parse_requirements_definition_file(
                topsrcdir / params,
                is_thunderbird_packages_txt,
                True,
            )
        elif action == "pypi":
            if is_thunderbird_packages_txt:
                raise Exception(THUNDERBIRD_PYPI_ERROR)

            requirements_output.pypi_requirements.append(
                PypiSpecifier(
                    _parse_package_specifier(params, only_strict_requirements)
                )
            )
        elif action == "pypi-optional":
            if is_thunderbird_packages_txt:
                raise Exception(THUNDERBIRD_PYPI_ERROR)

            if len(params.split(":", maxsplit=1)) != 2:
                raise Exception(
                    "Expected pypi-optional package to have a repercussion "
                    'description in the format "package:fallback explanation", '
                    f'found "{params}"'
                )
            raw_requirement, repercussion = params.split(":")
            requirements_output.pypi_optional_requirements.append(
                PypiOptionalSpecifier(
                    repercussion,
                    _parse_package_specifier(raw_requirement, only_strict_requirements),
                )
            )
        elif action == "requirements-txt":
            if is_thunderbird_packages_txt:
                raise Exception(THUNDERBIRD_PYPI_ERROR)

            requirements_txt_path = topsrcdir / params
            if not requirements_txt_path.exists():
                raise Exception(
                    f"requirements.txt file not found: {requirements_txt_path}"
                )

            req_txt_specifier = RequirementsTxtSpecifier(str(requirements_txt_path))
            requirements_output.requirements_txt_files.append(req_txt_specifier)
        elif action == "thunderbird-packages.txt":
            if is_thunderbird:
                _parse_requirements_definition_file(
                    topsrcdir / params, is_thunderbird_packages_txt=True
                )
        else:
            raise Exception(f"Unknown requirements definition action: {action}")

    def _parse_requirements_definition_file(
        requirements_path: Path, is_thunderbird_packages_txt, recursive=False
    ):
        """Parse requirements file into list of requirements"""
        if not requirements_path.is_file():
            raise Exception(f'Missing requirements file at "{requirements_path}"')

        requirements_output.requirements_paths.append(str(requirements_path))

        with open(requirements_path) as requirements_file:
            lines = [line for line in requirements_file]

        start_line = 1
        first_line = lines[0].strip()
        if not first_line.startswith("requires-python:"):
            if not recursive:
                raise ValueError(
                    f"{requirements_path}:1: Please ensure the first line specifies a Python requirement in the form "
                    f"'requires-python:<specifier>' (for example: 'requires-python:>=3.8').\n"
                    f"See https://packaging.python.org/en/latest/specifications/version-specifiers/#id5 for details."
                )
        elif recursive:
            raise ValueError(
                f"{requirements_path}:1 Files included using 'packages.txt:' actions must not start with a "
                f"Python requirement specifier (i.e. a 'requires-python:<specifier>' line)"
            )

        for number, line in enumerate(lines, start=start_line):
            _parse_requirements_line(
                requirements_path, line, number, is_thunderbird_packages_txt
            )

    _parse_requirements_definition_file(root_requirements_path, False)


class UnexpectedFlexibleRequirementException(Exception):
    def __init__(self, raw_requirement):
        self.raw_requirement = raw_requirement


def _parse_package_specifier(raw_requirement, only_strict_requirements):
    requirement = Requirement(raw_requirement)

    if only_strict_requirements and [
        s for s in requirement.specifier if s.operator != "=="
    ]:
        raise UnexpectedFlexibleRequirementException(raw_requirement)
    return requirement
