# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import re

# ruff linter deprecates Dict, List, Tuple required for Python 3.8 compatibility
from typing import Callable, Dict, List, Optional, cast  # noqa UP035

from manifestparser import TestManifest
from manifestparser.token import ManifestTokens
from manifestparser.toml import DEFAULT_SECTION, alphabetize_toml_str, sort_paths
from mozlint import result
from mozlint.pathutils import expand_exclusions
from mozpack import path as mozpath
from tomlkit.items import Array, Table
from tomlkit.toml_document import TOMLDocument

SECTION_REGEX = r"^\[.*\]$"
DISABLE_REGEX = r"^[ \t]*#[ \t]*\[.*\]"

ListStr = List[str]  # noqa UP006
OptManifest = Optional[TOMLDocument]  # noqa UP035
OptRegex = Optional[re.Pattern]


class State:
    """Global state helper to help find TOML line numbers"""

    def __init__(self, fix: bool, config, topsrcdir: str):
        self.fix = fix
        self.config = config
        self.topsrcdir: str = topsrcdir
        self._results: List = []  # noqa UP035
        self._path: str = ""  # current manifest path
        self._manifest_str: str = ""  # contents of the current manifest
        self._manifest_fixed: int = 0  # warnings fixed in this manifest
        self._fixed: int = 0  # number of warnings fixed total
        self.section: str = ""  # current section (test file)
        self.condition: str = ""  # current condition (skip-if expression)
        self.section_char: int = 0  # first char of this section
        self.section_line: int = 0  # first line of this section
        self.manifest_tokens: ManifestTokens = ManifestTokens()

    def results(self) -> List:  # noqa UP035
        return self._results

    def manifest_str(self) -> str:  # noqa UP035
        return self._manifest_str

    def manifest_fixed(self) -> int:  # noqa UP035
        return self._manifest_fixed

    def fixed(self) -> int:  # noqa UP035
        return self._fixed

    def set_path(self, path: str) -> None:
        self.section = ""
        self.condition = ""
        self.section_char = 0
        self.section_line = 1
        self._path = path
        self._manifest_fixed = 0

    def set_section(self, section) -> None:
        self.section = str(section)
        self.condition = ""
        self.section_char = self._manifest_str.find(self.section)
        self.section_line = self._manifest_str.count("\n", 0, self.section_char) + 1

    def set_condition(self, condition) -> None:
        self.condition = condition

    def parse_manifest(self, file_name: str) -> OptManifest:
        self.set_path(mozpath.relpath(file_name, self.topsrcdir))
        parser: TestManifest = TestManifest(use_toml=True, document=True)
        try:
            parser.read(file_name)
        except Exception as e:
            self.error(f"The manifest is not valid TOML: {str(e)}")
            return None
        manifest: TOMLDocument = parser.source_documents[file_name]
        self._manifest_str = open(file_name, encoding="utf-8").read()
        return manifest

    def make_result(self, message: str, is_error: bool = False) -> Dict:  # noqa UP035
        lineno: int = self.section_line
        if self.condition:
            condition_char: int = self._manifest_str.find(
                self.condition, self.section_char
            )
            lineno += self._manifest_str.count("\n", self.section_char, condition_char)
        if is_error:
            level = "error"
        else:
            level = "warning"
        result = {
            "path": self._path,
            "lineno": lineno,  # NOTE: tomlkit does not report lineno/column
            "column": 0,
            "message": message,
            "level": level,
        }
        return result

    def add_result(self, r: Dict) -> None:  # noqa UP035
        self._results.append(result.from_config(self.config, **r))

    def error(self, message) -> None:
        self.add_result(self.make_result(message, True))

    def warning(self, message) -> None:
        self.add_result(self.make_result(message))
        if self.fix:  # warnings are fixable
            self._manifest_fixed += 1
            self._fixed += 1

    def check_condition(self, kind_if: str) -> None:
        """
        Checks the condition for warnings or errors and updates results
        Returns the number of fixable warnings
        """

        error_msg: str = self.manifest_tokens.canonical_condition(self.condition)
        if error_msg:
            self.error(f"non canonical condition: {error_msg}")
        if "verify" in self.condition:
            return  # do not warn with verify or verify-standalone
        if self.condition.find("bits == ") >= 0:
            self.warning("using 'bits' is not idiomatic, use 'arch' instead")
        if self.condition.find("processor == ") >= 0:
            self.warning("using 'processor' is not idiomatic, use 'arch' instead")
        if self.condition.find("android_version == ") >= 0:
            self.warning(
                "using 'android_version' is not idiomatic, use 'os_version' instead (see testing/mozbase/mozinfo/mozinfo/platforminfo.py)"
            )
        if self.condition.find("os == 'linux'") >= 0:
            if self.condition.find("os_version == '22.04'") >= 0:
                if self.condition.find("asan") >= 0:
                    self.error("asan build-type is not tested on Linux 22.04")
                if self.condition.find("tsan") >= 0:
                    self.error("tsan build-type is not tested on Linux 22.04")
                if self.condition.find("display == 'x11'") >= 0:
                    self.warning(
                        "linux os_version == '22.04' is only supported on display == 'wayland'"
                    )
            elif (
                self.condition.find("os_version == '24.04'") >= 0
                and self.condition.find("display == 'wayland'") >= 0
            ):
                self.warning(
                    "linux os_version == '24.04' is only supported on display == 'x11'"
                )
            if kind_if == "skip-if" and self.condition.find("display == '") < 0:
                self.warning("linux condition requires display == 'x11' or 'wayland'")
        if self.condition.find("os == 'mac'") >= 0:
            if self.condition.find("os_version == '11.20'") >= 0:
                self.warning("mac os_version == '11.20' is no longer used")
        if self.condition.find("os == 'win'") >= 0:
            if self.condition.find("os_version == '11.2009'") >= 0:
                self.warning("win os_version == '11.2009' is no longer used")
            elif self.condition.find("tsan") >= 0:
                self.error("tsan build-type is not tested on Windows")
        if self.condition.find("apple_catalina") >= 0:
            self.warning(
                "instead of 'apple_catalina' please use os == 'mac' && os_version == '10.15' && arch == 'x86_64'"
            )
        if self.condition.find("apple_silicon") >= 0:
            self.warning(
                "instead of 'apple_silicon' please use os == 'mac' && os_version == '15.30' && arch == 'aarch64'"
            )
        if self.condition.find("win10_2009") >= 0:
            self.warning(
                "instead of win10_2009 please use os == 'win' && os_version == '10.2009' && arch == 'x86_64'"
            )
        if self.condition.find("win11_2009") >= 0:
            self.warning("win11_2009 is no longer used")
        if self.condition.find("!debug") >= 0:
            self.warning(
                'instead of "!debug" use three conditions: "asan", "opt", "tsan"'
            )
        if self.condition.find("== true") >= 0 or self.condition.find("== false") >= 0:
            self.warning(
                "use boolean variables directly instead of testing for literal values"
            )


def lint(paths, config, fix=None, **lintargs):
    topsrcdir: str = lintargs["root"]
    file_names = list(expand_exclusions(paths, config, topsrcdir))
    file_names = [os.path.normpath(f) for f in file_names]
    section_rx: OptRegex = re.compile(SECTION_REGEX, flags=re.M)
    disable_rx: OptRegex = re.compile(DISABLE_REGEX, flags=re.M)
    state: State = State(fix, config, topsrcdir)
    manifest: OptManifest = None

    for file_name in file_names:
        if file_name.endswith(".cargo/audit.toml"):
            continue  # special case that cannot be excluded in yml
        manifest = state.parse_manifest(file_name)
        if manifest is None:  # error parsing manifest
            continue

        if not DEFAULT_SECTION in manifest:
            state.warning(
                f"The manifest does not start with a [{DEFAULT_SECTION}] section."
            )
        sections: ListStr = [k for k in manifest.keys() if k != DEFAULT_SECTION]
        sorted_sections: ListStr = sort_paths(sections)
        if sections != sorted_sections:
            state.warning("The manifest sections are not in alphabetical order.")
        m = section_rx.findall(state.manifest_str())
        if len(m) > 0:
            for section_match in m:
                section: str = section_match[1:-1]
                if section == DEFAULT_SECTION:
                    continue
                state.set_section(section)
                if not section.startswith('"'):
                    state.warning(
                        f"The section name must be double quoted: [{section}]"
                    )
        m = disable_rx.findall(state.manifest_str())
        if len(m) > 0:
            for disabled_section in m:
                state.error(
                    f"Use 'disabled = \"<reason>\"' to disable a test instead of a comment: {disabled_section}"
                )

        is_xpcshell = os.path.basename(file_name).startswith("xpcshell")
        for section, keyvals in manifest.body:
            if section is None:
                continue
            state.set_section(section)
            if not isinstance(keyvals, Table):
                state.error(f"Bad assignment in preamble: {section} = {keyvals}")
            else:
                for k, v in keyvals.items():
                    if (
                        k == "prefs"
                        and str(section) != DEFAULT_SECTION
                        and not is_xpcshell
                    ):
                        state.error(
                            f"'prefs' is only supported in the [{DEFAULT_SECTION}] section for non-xpcshell manifests"
                        )
                    if k.endswith("-if"):
                        if not isinstance(v, Array):
                            state.error(
                                f'Value for conditional must be an array: {k} = "{v}"'
                            )
                        else:
                            for e in v:
                                state.set_condition(e)
                                if e.find("||") > 0 and e.find("&&") < 0:
                                    state.error(
                                        f'Value for conditional must not include explicit ||, instead put on multiple lines: {k} = [ ... "{e}" ... ]'
                                    )
                                else:
                                    state.check_condition(k)

        if state.manifest_fixed() > 0:
            manifest_str: str = alphabetize_toml_str(manifest, True)  # does fixes
            with open(file_name, "w", encoding="utf-8", newline="\n") as fp:
                fp.write(manifest_str)

    return {"results": state.results(), "fixed": state.fixed()}
